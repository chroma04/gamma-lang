#define GM_RT_SEPARATE 1
#define GM_RT_PROGRAM 1
/* The runtime of programs compiled by the Gamma C back end.
 *
 * The runtime is linked into a program in one of two ways. Normally the compiler
 * emits one C translation unit: this header, gm_rt.c, then the generated code, and
 * everything here is `static` so the unit is self-contained (and the optimizer sees
 * the whole program). With GM_RT_SEPARATE defined, this header and gm_rt.c are
 * compiled on their own into an object that is reused across programs, and the
 * generated code includes only this header: the declarations marked GM_API and
 * GM_API_VAR then have external linkage. The generated code also defines GM_RT_PROGRAM,
 * which leaves out the system headers that only gm_rt.c needs.
 * GNU C11 is required (statement expressions, zero-size arrays, __int128,
 * __builtin_*_overflow).
 */
#ifndef GM_RT_H
#define GM_RT_H

#ifdef GM_FREESTANDING
/* A freestanding target (spec 3.7.1): no operating system and no C library beyond the
 * functions every freestanding toolchain provides (memcpy, memmove, memset, memcmp). The
 * parts of the runtime that need an operating system are left out, and there is one thread
 * of execution. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
#define strlen gm_strlen
static inline size_t gm_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
#define snprintf gm_snprintf
static int gm_snprintf(char *dst, size_t cap, const char *fmt, ...);
#define GM_TLS
/* The context switch is private to the object: only the program's exports are symbols. */
#define GM_ASM_GLOBL ".local "
#else
#define GM_ASM_GLOBL ".globl "
#if defined(__wasi__)
/* WASI (wasm32-wasi): a hosted target with files, clocks, arguments and the environment,
 * but no threads, child processes, sockets or signals; the runtime's functions for those
 * report IoError.Unsupported. There is one thread of execution, and fibers switch stacks
 * through Binaryen's Asyncify transformation, which the compiler applies to the module
 * (see "context switching" in gm_rt.c). */
#define GM_WASI 1
#define GM_TLS
#else
#define GM_TLS _Thread_local
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* pthread_getattr_np */
#endif

#if defined(_WIN32)
/* The Windows API level of the runtime (WaitOnAddress, GetCurrentThreadStackLimits), set
 * before any system header fixes a lower one. */
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif

#if defined(GM_RT_PROGRAM) && defined(GM_RT_SEPARATE)
/* The generated code of a program that is linked with the separately compiled runtime
 * needs only the declarations below, which use nothing from the operating system's headers;
 * parsing those would take the C compiler about as long as a small program (on Windows,
 * where every unit of a split build would parse <windows.h> and Winsock, much longer). */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#ifndef GM_WASI
#include <signal.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#if defined(_WIN32)
/* Windows (MinGW-w64): the Win32 API and Winsock 2 stand in for the POSIX headers. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <bcrypt.h>
#elif defined(GM_WASI)
#include <unistd.h>
#include <dirent.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sched.h>
#include <poll.h>
#include <sys/wait.h>
#if defined(__linux__) && !defined(__FILC__)
#include <sys/syscall.h>
#endif
/* glibc's posix_spawn starts a child without copying the parent's address space (clone with
 * CLONE_VM | CLONE_VFORK), reports a failed exec, and can change the child's directory
 * (2.29): Proc.spawn uses it instead of fork, which costs time in proportion to the
 * parent's memory. */
#if defined(__GLIBC__) && !defined(__FILC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
#define GM_POSIX_SPAWN 1
#include <spawn.h>
#endif
#endif
#endif /* GM_RT_PROGRAM */
#endif /* GM_FREESTANDING */

/* GM_API: the linkage of the runtime's functions (and of its variables' definitions);
 * GM_API_VAR: of the declarations of its variables here. */
#ifdef GM_RT_SEPARATE
#define GM_API
#define GM_API_VAR extern
#else
#define GM_API static
#define GM_API_VAR static
#endif

/* GM_HUGE_FN: marks a generated function with an enormous body (a very long chain of
 * operations). GCC's points-to analysis and jump threading take time that grows much
 * faster than such a function, so they are switched off for it. */
#if defined(__GNUC__) && !defined(__clang__)
#define GM_HUGE_FN __attribute__((optimize("no-tree-pta", "no-thread-jumps")))
#else
#define GM_HUGE_FN
#endif

/* GM_LONG_FN: marks a generated function with a very long body (thousands of operations,
 * such as a long chain). From LLVM 19 on, Clang's early common-subexpression pass asks for
 * the known bits of values through the selects and checks of such a body at a cost that
 * grows with its length, so the whole function takes time that grows about with the cube
 * of its length (minutes for ten thousand links, hours for fifty thousand). Such a function
 * is compiled without optimization there. Apple numbers its Clang differently: Apple clang
 * 17 is the first based on LLVM 19. */
#if defined(__clang__) && ((defined(__apple_build_version__) && __clang_major__ >= 17) || \
                           (!defined(__apple_build_version__) && __clang_major__ >= 19))
#define GM_LONG_FN __attribute__((optnone, noinline))
#else
#define GM_LONG_FN
#endif

#define GM_XSTR(x) GM_STR(x)
#define GM_STR(x) #x
/* The assembler name of C symbol `s` (macOS prefixes an underscore). */
#define GM_SYM(s) GM_XSTR(__USER_LABEL_PREFIX__) s

/* ---- byte order ------------------------------------------------------------
 *
 * Integers (of up to 128 bits) are stored as one number in the target's byte order. Code
 * that addresses the bytes of an integer goes through gm_ibp, which names the i-th least
 * significant byte, so it does not depend on the order. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define GM_BIG_ENDIAN 1
#else
#define GM_BIG_ENDIAN 0
#endif

/* The i-th least significant byte of the integer of `size` bytes at p. */
static inline uint8_t *gm_ibp(const void *p, size_t size, size_t i) {
    uint8_t *b = (uint8_t *)p;
    if (!GM_BIG_ENDIAN) return b + i;
    return b + (size - 1 - i);
}

/* The unsigned integer of `size` (at most 16) bytes at p. */
static inline unsigned __int128 gm_load_u128(const void *p, size_t size) {
    unsigned __int128 v = 0;
    for (size_t i = size; i > 0; i--) v = (v << 8) | *gm_ibp(p, size, i - 1);
    return v;
}

/* Stores the low `size` (at most 16) bytes of v at p. */
static inline void gm_store_u128(void *p, size_t size, unsigned __int128 v) {
    for (size_t i = 0; i < size; i++) *gm_ibp(p, size, i) = (uint8_t)(v >> (8 * i));
}

/* ---- memory helpers ------------------------------------------------------ */

/* GM_LD loads a value of C type T from address p (any alignment); GM_ST stores the value
 * v of C type T at address p. Under Fil-C a memcpy is a call into its runtime, and one
 * on a local makes the local escape to the heap, so they are typed accesses through a
 * packed struct (alignment 1, any type may alias) instead. */
#ifdef __FILC__
#define GM_UNALIGNED(T, p) (((struct __attribute__((packed, may_alias)) { T v; } *)(p))->v)
#define GM_LD(T, p) ({ T gm_ld_v_ = GM_UNALIGNED(T, p); gm_ld_v_; })
#define GM_ST(T, p, v) do { T gm_st_v_ = (v); GM_UNALIGNED(T, p) = gm_st_v_; } while (0)
#else
#define GM_LD(T, p) ({ T gm_ld_v_; memcpy(&gm_ld_v_, (const void *)(p), sizeof(T)); gm_ld_v_; })
#define GM_ST(T, p, v) do { T gm_st_v_ = (v); memcpy((void *)(p), &gm_st_v_, sizeof(T)); } while (0)
#endif
/* The value v as C type T of the same size: a blob as its mirror or back. */
#define GM_CONV(T, v) ({ union { __typeof__(v) s; T d; } gm_cv_u_; gm_cv_u_.s = (v); gm_cv_u_.d; })
/* Round half to even. The MinGW C library lacks roundeven; rint does it in the default
 * rounding mode, which programs never change. */
#ifdef _WIN32
#define GM_ROUNDEVEN(x) __builtin_rint(x)
#else
#define GM_ROUNDEVEN(x) __builtin_roundeven(x)
#endif
/* `#fast_math` functions: float contraction and reassociation are permitted (§5.3). */
#if defined(__clang__)
#define GM_FAST_MATH_ATTR
#define GM_FAST_MATH_BODY _Pragma("clang fp contract(fast) reassociate(on)")
#else
#define GM_FAST_MATH_ATTR __attribute__((optimize("associative-math", "no-signed-zeros", "no-trapping-math", "fp-contract=fast")))
#define GM_FAST_MATH_BODY
#endif
/* Pointer arithmetic and tagging. Under Fil-C a pointer keeps its capability only while
 * it is computed as a pointer (an integer turned back into a pointer has none), so the
 * address is changed with zmkptr, which keeps the capability; elsewhere through
 * uintptr_t, whose arithmetic wraps. GM_PTR_ADD adds a byte offset (any value: a
 * negative offset wraps around). GM_PTR_TAG and GM_PTR_UNTAG set and clear a mark on a
 * function's address, and GM_PTR_TAGGED tells whether it is set: bit 0, as the generated
 * functions are aligned; on WebAssembly, where a function's "address" is its index in the
 * function table and alignment means nothing, the top bit, as no table is that large. */
#ifdef __FILC__
#include <stdfil.h>
#define GM_PTR_ADD(p, n) zmkptr((void *)(p), (unsigned long)(p) + (unsigned long)(n))
#define GM_PTR_TAG(p) zorptr((void *)(p), 1)
#define GM_PTR_UNTAG(p) zandptr((void *)(p), ~1ul)
/* An address as a pointer (`to_ptr`): it grants no access. The empty asm hides where
 * the address came from: the optimizer could fold a plain cast back into the pointer it
 * was taken from, which keeps its capability. (zmkptr(NULL, n) is no better: filcc 0.686
 * at -O1 and above drops an access through it instead of trapping.) */
#define GM_INT_TO_PTR(n) ({ uintptr_t gm_n_ = (n); __asm__("" : "+r"(gm_n_)); (void *)gm_n_; })
#else
#define GM_INT_TO_PTR(n) ((void *)(uintptr_t)(n))
#define GM_PTR_ADD(p, n) ((void *)((uintptr_t)(p) + (uintptr_t)(n)))
#ifdef __wasm__
#define GM_PTR_TAG_BIT ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1))
#else
#define GM_PTR_TAG_BIT ((uintptr_t)1)
#endif
#define GM_PTR_TAG(p) ((void *)((uintptr_t)(p) | GM_PTR_TAG_BIT))
#define GM_PTR_UNTAG(p) ((void *)((uintptr_t)(p) & ~GM_PTR_TAG_BIT))
#endif
#ifdef __FILC__
#define GM_PTR_TAGGED(p) (((uintptr_t)(p) & 1) != 0)
#else
#define GM_PTR_TAGGED(p) (((uintptr_t)(p) & GM_PTR_TAG_BIT) != 0)
#endif
/* Rounds a double to the nearest binary16 (ties to even); returns its bits. */
static inline uint16_t gm_f64_to_f16(double x) {
    uint64_t bits;
    memcpy(&bits, &x, 8);
    uint16_t sign = (uint16_t)((bits >> 63) << 15);
    int exp = (int)((bits >> 52) & 0x7ff);
    uint64_t man = bits & ((1ull << 52) - 1);
    if (exp == 0x7ff) return man == 0 ? (uint16_t)(sign | 0x7c00) : (uint16_t)(sign | 0x7e00);
    if (exp == 0) return sign;
    int e = exp - 1023;
    int he = e + 15;
    if (he >= 1) {
        if (he >= 0x1f) return (uint16_t)(sign | 0x7c00);
        uint32_t keep = (uint32_t)(man >> 42);
        uint64_t rem = man & ((1ull << 42) - 1);
        uint64_t half = 1ull << 41;
        uint32_t r = ((uint32_t)he << 10) | keep;
        if (rem > half || (rem == half && (keep & 1) == 1)) r += 1;
        return (uint16_t)(sign | r);
    }
    unsigned __int128 m = ((unsigned __int128)1 << 52) | man;
    int shift = 28 - e;
    if (shift >= 120) return sign;
    unsigned __int128 q = m >> shift;
    unsigned __int128 rem = m & (((unsigned __int128)1 << shift) - 1);
    unsigned __int128 half = (unsigned __int128)1 << (shift - 1);
    uint32_t r = (uint32_t)q;
    if (rem > half || (rem == half && (r & 1) == 1)) r += 1;
    return (uint16_t)(sign | r);
}

/* The number x converted to f16. Fil-C (as of 0.686) miscompiles the compiler's own
 * routines for converting to _Float16 (__truncdfhf2 and the like), so Fil-C builds round
 * with gm_f64_to_f16 instead (every source value converts to double exactly or, if an
 * integer, beyond f16's range either way); on x86-64 they also have the F16C
 * instructions (-mf16c) do float <-> f16 for arithmetic. */
#ifdef __FILC__
static inline _Float16 gm_f16_from(double d) {
    uint16_t h = gm_f64_to_f16(d);
    _Float16 f;
    memcpy(&f, &h, sizeof f);
    return f;
}
#define GM_F16(x) gm_f16_from((double)(x))
#else
#define GM_F16(x) ((_Float16)(x))
#endif
/* A zero value of C type T (scalar or blob). */
#define GM_ZERO(T) ((T){})

typedef struct { uint8_t b[0]; } gm_unit;

/* ---- traps ---------------------------------------------------------------- */

/* Debug builds keep a chain of the running Gamma functions: each pushes a gm_bt on entry,
 * pops it on exit, and records in it the location of each call it makes. Functions of the
 * core library have no name; backtraces leave them out. Each thread and each fiber has a
 * chain of its own; a fiber's continues into the chain of the `with` that runs it. */
typedef struct gm_bt {
    struct gm_bt *prev;
    const char *fn;
    const char *file;
    uint32_t line, col;
} gm_bt;
/* Records in the gm_bt `b` the location of the call about to be made. */
#define GM_BT_AT(b, f, l, c) ((b).file = (f), (b).line = (l), (b).col = (c))
GM_API_VAR GM_TLS gm_bt *gm_bt_top;

/* Stops the program: flushes stdout, writes "trap: <msg>\n  at <file>:<line>:<col>\n" to
 * stderr (the second line only if file is not NULL), followed in Debug builds by the calls
 * leading there, and exits with status 134. */
GM_API _Noreturn void gm_trap(const char *msg, const char *file, uint32_t line, uint32_t col);
/* As gm_trap, with a message given as bytes (not NUL-terminated). */
GM_API _Noreturn void gm_trap_bytes(const uint8_t *msg, size_t len, const char *file, uint32_t line, uint32_t col);
/* The trap of a pointer cast to a type of alignment `align` that `addr` lacks. */
GM_API _Noreturn void gm_trap_misaligned(uintptr_t addr, unsigned align, const char *file, uint32_t line, uint32_t col);
/* The traps of failed checks, which format their messages here rather than in the
 * generated code: index `i` out of bounds for length `len`; slice bounds lo..hi invalid for
 * length `len`; a shift amount `n` too large for the integer type named `ty`; a vector load
 * or store (`what`) of `lanes` lanes with a slice of `len` elements. */
GM_API _Noreturn void gm_trap_index(uint64_t i, uint64_t len, const char *file, uint32_t line, uint32_t col);
GM_API _Noreturn void gm_trap_slice(uint64_t lo, uint64_t hi, uint64_t len, const char *file, uint32_t line, uint32_t col);
GM_API _Noreturn void gm_trap_shift(uint64_t n, const char *ty, const char *file, uint32_t line, uint32_t col);
GM_API _Noreturn void gm_trap_lanes(const char *what, uint32_t lanes, uint64_t len, const char *file, uint32_t line,
                                    uint32_t col);

/* ---- growable byte buffer --------------------------------------------------- */

typedef struct { uint8_t *data; size_t len, cap; } gm_buf;
GM_API void gm_buf_push(gm_buf *b, const void *data, size_t len);
GM_API void gm_buf_free(gm_buf *b);

/* Traps: "integer overflow in `pow` (<base>^<n>)", for an integer `base` of `bits` bits
 * stored in the size of its type. */
GM_API _Noreturn void gm_trap_pow(const uint8_t *base, uint32_t bits, bool sgn, uint32_t n, const char *file,
                                  uint32_t line, uint32_t col);

/* ---- type descriptors (for formatting, spec §21.6.1) -------------------------- */

typedef enum {
    GM_K_INT,      /* bits, is_signed; storage `size` bytes, target byte order */
    GM_K_FLOAT,    /* bits: 16, 32 or 64 */
    GM_K_BOOL,
    GM_K_PTR,      /* any pointer or function pointer: printed as 0x<hex> */
    GM_K_SLICE,    /* elem; {ptr at 0, len (uintptr_t) at sizeof(void*)} */
    GM_K_ARRAY,    /* elem, len */
    GM_K_TUPLE,    /* fields (named == true: print "name: "); 1 unnamed elem prints "(x,)" */
    GM_K_OPTIONAL, /* elem = payload at offset 0; if flag_offset == GM_NICHE the payload is a
                      pointer and null is address 0, else present iff byte at flag_offset != 0 */
    GM_K_RESULT,   /* elem = ok, err; is_err iff byte at tag_offset != 0; payload at payload_offset */
    GM_K_STRUCT,   /* name, fields; fields named "_" are skipped */
    GM_K_UNION,    /* name, variants; tag (tag_size bytes, tag_signed) at tag_offset;
                      payload at payload_offset */
    GM_K_OPAQUE,   /* printed as `name` verbatim (e.g. "<handler>") */
    GM_K_BITS      /* a #bits struct: name, fields with *bit* offsets, packed into an unsigned
                      integer of `size` bytes. A field's width is 1 for GM_K_BOOL and `bits` for
                      GM_K_INT, GM_K_BITS and GM_K_UNION (the tag's width); fields named "_" are
                      skipped like a struct's */
} gm_kind;

#define GM_NICHE 0xffffffffu

typedef struct gm_type gm_type;

typedef struct {
    const char *name;     /* field / element name, or NULL for unnamed tuple elements */
    uint32_t offset;
    const gm_type *type;
} gm_field;

typedef struct {
    const char *name;
    int64_t tag;          /* the tag value (as a signed 64-bit number) */
    const gm_type *payload; /* NULL if the variant has no payload */
} gm_variant;

struct gm_type {
    gm_kind kind;
    uint32_t size, align;
    const char *name;
    uint16_t bits;
    bool is_signed;
    const gm_type *elem;
    const gm_type *err;
    uint64_t len;
    uint32_t nfields;
    const gm_field *fields;
    bool named;
    uint32_t flag_offset;
    uint32_t tag_offset, payload_offset;
    uint8_t tag_size;
    bool tag_signed;
    uint32_t nvariants;
    const gm_variant *variants;
};

/* Appends the display form of the value of type t at address v (§21.6.1). `top` is true for
 * the argument itself: a []u8 / []const u8 slice is then written raw, otherwise quoted. */
GM_API void gm_format(gm_buf *out, const gm_type *t, const void *v, bool top);
/* The display form of the value of type t at address v, as `gm_format` with `top` gives
 * it, where it already is or can cheaply be put: a string's own bytes, the digits of an
 * integer in `scratch` (GM_VIEW_SCRATCH bytes); any other value is formatted into `b`
 * (empty on entry, to be freed if it has data). Sets *data and returns the length. */
#define GM_VIEW_SCRATCH 48
GM_API size_t gm_format_view(gm_buf *b, uint8_t *scratch, const gm_type *t, const void *v, const void **data);
/* Traps: "unwrap of error value <v>", the error `v` of type `t`. */
GM_API _Noreturn void gm_trap_unwrap(const gm_type *t, const void *v, const char *file, uint32_t line, uint32_t col);

/* A placeholder's format spec (§21.6.1). align: 0 auto, 1 left, 2 right, 3 center; kind:
 * 0 display, 1 x, 2 X, 3 o, 4 b, 5 e; precision -1 when absent. */
typedef struct {
    uint8_t fill, align, plus, alt, zero, kind;
    uint32_t width;
    int32_t precision;
} gm_spec;

/* Appends the value of type t at address v as a placeholder with the spec s. */
GM_API void gm_format_spec(gm_buf *out, const gm_type *t, const void *v, const gm_spec *s);

/* A template known when the write runs (the one body that the instances of a generic
 * function share whatever their templates, §21.6.1): its n placeholders, the n + 1 pieces
 * of text around them, and for each placeholder the argument it writes and its spec (fmt,
 * NULL for the default). The tables are static data of the generated code. The field names
 * are words the runtime uses already: an exported Gamma function may not take a name that
 * the runtime uses (§19.4). */
typedef struct {
    const uint8_t *ptr;
    size_t len;
} gm_tmpl_piece;
typedef struct {
    uint32_t n;
    const gm_tmpl_piece *text;
    const uint32_t *arg;
    const gm_spec *const *fmt;
} gm_tmpl;

/* ---- ambient effects ---------------------------------------------------------- */

GM_API void gm_write_out(const uint8_t *p, size_t len);  /* buffered stdout */
GM_API void gm_write_err(const uint8_t *p, size_t len);  /* stderr, flushed */
GM_API void gm_flush(void);                              /* flushes stdout */

/* Statement coverage: `n` counters, written out at exit to the file `path` (if not NULL). */
GM_API void gm_cover_init(uint32_t n, const char *path);
GM_API_VAR uint32_t *gm_cover_counts;
#define GM_COVER(i) (gm_cover_counts != NULL ? (void)__atomic_fetch_add(&gm_cover_counts[i], 1, __ATOMIC_RELAXED) : (void)0)

/* Process arguments as a Gamma `[]const string`: returns a pointer to `argc` slices
 * ({ptr, len} pairs, each 2 * sizeof(void*) bytes). Set by gm_set_args at startup. */
GM_API void gm_set_args(int argc, char **argv);
/* The index of the test that a test binary is to run: its first argument. Without one,
 * prints its usage and returns -1. */
GM_API long gm_test_index(int argc, char **argv);
GM_API const void *gm_args(size_t *count);
/* Looks up an environment variable; returns false if unset. */
GM_API bool gm_env_var(const uint8_t *name, size_t len, const uint8_t **value, size_t *value_len);

/* ---- system services (behind the core library's default handlers, §12.9) --------- */

/* Operations that can fail return a count, or an error code: -1 - i for the i-th
 * variant of IoError other than Other, and -1000 - errno for Other(errno). */
GM_API int64_t gm_io_error(int err);
GM_API int64_t gm_sys_read(void *p, size_t n);      /* standard input */
GM_API uint64_t gm_clock_now(void);                 /* monotonic nanoseconds */
GM_API int64_t gm_clock_wall(void);                 /* Unix time in nanoseconds */
GM_API void gm_clock_sleep(uint64_t ns);
GM_API void gm_rand_fill(void *p, size_t n);
/* Files and directories: the flags of gm_fs_open, and handles that are file descriptors
 * (files; C runtime descriptors on Windows) or indexes into a table of open directories. */
enum { GM_FS_READ = 1, GM_FS_WRITE = 2, GM_FS_APPEND = 4, GM_FS_CREATE = 8, GM_FS_TRUNCATE = 16, GM_FS_EXCLUSIVE = 32 };
GM_API int64_t gm_fs_open(const void *path, size_t len, uint32_t flags);
GM_API int64_t gm_fs_close(int64_t f);
GM_API int64_t gm_fs_read(int64_t f, void *p, size_t n);
GM_API int64_t gm_fs_write(int64_t f, const void *p, size_t n);
GM_API int64_t gm_fs_seek(int64_t f, int64_t offset, uint32_t whence);
GM_API int64_t gm_fs_stat(const void *path, size_t len, void *out, size_t n);   /* size, kind (0 file, 1 directory, 2 symlink, 3 other), mtime, permission bits */
GM_API int64_t gm_fs_lstat(const void *path, size_t len, void *out, size_t n);   /* as stat; no final symlink followed */
GM_API int64_t gm_fs_remove(const void *path, size_t len);
GM_API int64_t gm_fs_rename(const void *from, size_t from_len, const void *to, size_t to_len);
GM_API int64_t gm_fs_make_dir(const void *path, size_t len);
GM_API int64_t gm_fs_remove_dir(const void *path, size_t len);
GM_API int64_t gm_fs_open_dir(const void *path, size_t len);
GM_API int64_t gm_fs_next_entry(int64_t d, void *p, size_t n);   /* length + kind * 2^32; 0 after the last */
GM_API int64_t gm_fs_close_dir(int64_t d);
GM_API int64_t gm_fs_truncate(int64_t f, uint64_t size);
GM_API int64_t gm_fs_sync(int64_t f);
GM_API int64_t gm_fs_cwd(void *p, size_t n);          /* length; EINVAL if it does not fit */
GM_API int64_t gm_fs_canonical(const void *path, size_t len, void *p, size_t n);   /* length; EINVAL if it does not fit */
GM_API int64_t gm_fs_read_link(const void *path, size_t len, void *p, size_t n);   /* length; EINVAL if it does not fit */
GM_API int64_t gm_fs_symlink(const void *target, size_t tlen, const void *path, size_t len);
GM_API int64_t gm_fs_set_mode(const void *path, size_t len, uint32_t mode);
GM_API int64_t gm_fs_set_modified(const void *path, size_t len, int64_t ns);   /* Unix nanoseconds */
GM_API void gm_env_exit(uint8_t code);                /* ends the process after flushing output */
/* TCP sockets; handles are file descriptors (SOCKET values on Windows). Host names are resolved with getaddrinfo;
 * a failed lookup is Other(0), as in the interpreter. */
GM_API int64_t gm_net_connect(const void *host, size_t len, uint16_t port);
GM_API int64_t gm_net_listen(const void *host, size_t len, uint16_t port);
GM_API int64_t gm_net_local_port(int64_t h);
GM_API int64_t gm_net_accept(int64_t h);
GM_API int64_t gm_net_send(int64_t h, const void *p, size_t n);
GM_API int64_t gm_net_recv(int64_t h, void *p, size_t n);
GM_API int64_t gm_net_close(int64_t h);
GM_API int64_t gm_net_shutdown(int64_t h, uint32_t how);   /* 0 read, 1 write, 2 both */
GM_API int64_t gm_net_set_timeouts(int64_t h, uint64_t recv_ns, uint64_t send_ns);   /* 0: none */
GM_API int64_t gm_net_peer(int64_t h, void *p, size_t n);   /* "host:port"; EINVAL if it does not fit */

/* Child processes. gm_proc_launch takes the argument vector, changes to the environment
 * ("NAME=value" sets a variable, "NAME" removes it; applied to ours, or to an empty environment
 * when flags has GM_PROC_CLEAR_ENV) and the working directory (empty: ours); the flags hold two
 * bits per standard stream (stdin from bit 0, stdout from bit 2, stderr from bit 4: 0 inherit,
 * 1 the null device, 2 a pipe), and GM_PROC_GROUP to start the child in a process group of its
 * own (POSIX; a job object on Windows), which gm_proc_kill then ends as a whole. It returns a
 * handle. gm_proc_read_out waits until a piped output
 * stream has data and reads from it: the length plus 2^32 for stderr, or 0 once both streams
 * have ended. gm_proc_wait returns the exit code, or the signal number plus 2^32. With
 * GM_PROC_PROGRAM the file to run is `program` (searched for in PATH if it has no separator)
 * instead of argv[0], which is then only the name the child sees. gm_proc_try_wait is
 * gm_proc_wait that does not block: 2^33 while the child runs; once it returns a status the
 * child is released as by gm_proc_wait. */
enum { GM_PROC_CLEAR_ENV = 64, GM_PROC_GROUP = 128, GM_PROC_PROGRAM = 256 };
GM_API int64_t gm_proc_launch(const void *argv, size_t argc, const void *env, size_t envc, const void *dir,
                              size_t dir_len, const void *program, size_t program_len, uint32_t flags);
GM_API int64_t gm_proc_write_in(int64_t h, const void *p, size_t n);
GM_API int64_t gm_proc_close_in(int64_t h);
GM_API int64_t gm_proc_read_out(int64_t h, void *p, size_t n);
GM_API int64_t gm_proc_wait(int64_t h);
GM_API int64_t gm_proc_try_wait(int64_t h);
GM_API int64_t gm_proc_kill(int64_t h);

/* ---- threads (§19.6) ----------------------------------------------------------------
 *
 * A thread runs a plain function `void f(void *arg)`. Fibers, handler contexts and the exit in
 * progress are per thread; the runtime's own shared tables are guarded by gm_rt_lock. */
/* The lock guarding the runtime's shared tables. */
GM_API void gm_rt_acquire(void);
GM_API void gm_rt_release(void);
GM_API int64_t gm_thread_spawn(void *f, void *arg);   /* a handle, or an error code */
GM_API int64_t gm_thread_join(int64_t h);
GM_API int64_t gm_thread_detach(int64_t h);
GM_API void gm_thread_yield(void);
/* Blocks while *addr == expected, until woken or the timeout (ns; negative: none) passes;
 * 0 or an error code. May return early. */
GM_API int64_t gm_futex_wait(uint32_t *addr, uint32_t expected, int64_t timeout_ns);
GM_API int64_t gm_futex_wake(uint32_t *addr, uint32_t n);   /* UINT32_MAX: all */
GM_API uint32_t gm_cpu_count(void);
/* Writes to standard output (fd 1, buffered like Out) or standard error (fd 2). */
GM_API void gm_sys_write(int32_t fd, const void *p, size_t n);

/* ---- heap (behind the core library's @heap_alloc / @heap_free) ------------------ */

/* Returns NULL when out of memory. In debug builds (gm_debug != 0) the block is filled with
 * 0xAA, and gm_heap_free traps on double and invalid frees (it keeps track of live blocks). */
GM_API void *gm_heap_alloc(size_t size, size_t align);
GM_API void gm_heap_free(void *p, size_t size, size_t align, const char *file, uint32_t line, uint32_t col);
/* Debug builds: records the allocator (owner, vt) and site of the block at p (§18.3). */
GM_API void gm_alloc_tag(void *p, void *owner, void *vt, const uint8_t *file, size_t flen, uint32_t line, uint32_t col);
/* Debug builds: traps if the block at p was allocated by another allocator, and forgets it. */
GM_API void gm_alloc_check(void *p, void *owner, void *vt, const uint8_t *file, size_t flen, uint32_t line, uint32_t col,
                           const char *tfile, uint32_t tline, uint32_t tcol);
/* Nonzero in Debug builds (initially 1; set by the generated code). */
GM_API_VAR int gm_debug;
/* memset, called through a volatile pointer so that filling a frame that is about to die
 * (Debug builds) is not removed as a dead store. */
GM_API_VAR void *(*volatile gm_poison)(void *, int, size_t);
/* Nonzero in Debug, Checked and Safe builds; zero in Fast ones (set by the generated code). */
GM_API_VAR int gm_checks;

/* ---- frames, handler contexts and non-local exits ----------------------------------
 *
 * The calling convention of generated code is described in docs/architecture.md. Each
 * function's locals live in a frame struct starting with a gm_frame; handler contexts are
 * linked lists of gm_node; `break`, `continue`, `return`, `Env.exit`, `resume` and fiber
 * cancellation travel as the exit in progress, gm_unw.
 */

typedef struct gm_frame { struct gm_frame *up; uint8_t clause_lit; } gm_frame;
typedef struct gm_with gm_with;
typedef struct gm_node {
    uint32_t eff;
    uint8_t kind;               /* GM_LIT, GM_DECL or GM_NATIVE */
    void *const *clauses;       /* clause functions, by operation */
    void *env;                  /* literal: its frame; declaration: the handler value */
    struct gm_node *hctx;       /* the context clauses run in */
    gm_frame *with_frame;       /* the installing `with`, for clause exits */
    uint32_t with_label;
    void *result;               /* where a `break` from a clause stores the `with`'s value */
    gm_with *fw;                /* the installing `with … on`, or NULL (§14) */
    struct gm_node *next;
} gm_node;
enum { GM_LIT, GM_DECL, GM_NATIVE };
typedef struct { void *code; void *env; gm_node *cx; } gm_closure;
typedef struct { void *env; gm_node *cx; } gm_hval;
/* Exit kinds. GM_RESUME ends a `ctl` clause, after which its driver switches to the fiber
 * named by `resume`; GM_CANCEL unwinds a discarded fiber to its base. */
enum { GM_NONE, GM_BREAK, GM_CONTINUE, GM_RETURN, GM_EXIT, GM_RESUME, GM_CANCEL };
/* C11 memory orders, indexed by the tag of `MemoryOrder` (§19.6). */
static const int gm_ord[] = {__ATOMIC_RELAXED, __ATOMIC_ACQUIRE, __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST};
typedef struct { int kind; gm_frame *frame; uint32_t label; uint8_t is_err; uint8_t code; } gm_unwind;
/* Each thread has its own exit in progress (§19.6). */
GM_API_VAR GM_TLS gm_unwind gm_unw;

/* Runs entry `f` of the program under context `cx` on a thread with a large stack and
 * returns its exit status. */
GM_API int gm_run_main(int (*f)(gm_node *), gm_node *cx);
/* The same for a program that switches fibers: on WASI, that takes the fiber driver at the
 * bottom of the stack (which brings in the code of the switch, see gm_rt.c). */
GM_API int gm_run_main_fibers(int (*f)(gm_node *), gm_node *cx);
/* On WASI, run an exported function (called by `f`, which ignores its argument): the
 * _fibers variant under the fiber driver, unless one is already running below. */
GM_API void gm_run_export(int (*f)(gm_node *));
GM_API void gm_run_export_fibers(int (*f)(gm_node *));

/* The innermost handler of effect `eff` in context `cx`, or NULL. The innermost node, most
 * often the one, is looked at inline: a call is dear, under Fil-C especially. */
GM_API gm_node *gm_find_outer(gm_node *cx, uint32_t eff);
static inline gm_node *gm_find(gm_node *cx, uint32_t eff) {
    return cx == NULL || cx->eff == eff ? cx : gm_find_outer(cx->next, eff);
}
/* Whether errdefers run as the current exit leaves a block of frame `fr` (§8.8). */
GM_API bool gm_errdefer_runs(gm_frame *fr);

/* ---- fibers (§14) ------------------------------------------------------------------------
 *
 * A `with … on` runs its body on a fiber: a gm_fiber with its own stack. The code that runs
 * the `with` is the driver; it loops: switch to a fiber (gm_with_go), and when control comes
 * back, look at why (w->event). A fiber that performs a `ctl` operation suspends in
 * gm_ctl_perform; the driver then calls the clause (gm_with_clause) on its own stack. The
 * clause ends with `resume`, which sets w->next and exits with GM_RESUME, or with another
 * exit, which completes the `with`. A finished fiber switches to its driver for good
 * (gm_fiber_finish).
 *
 * Fiber structs are never freed: a pool reuses them, and each suspension increments the
 * fiber's epoch, so a continuation (fiber, epoch) that was already consumed is detected
 * even after the fiber is reused.
 */

#define GM_MIN_STACK 16384
/* Bytes at the bottom of a fiber stack kept for reporting an overflow. */
#define GM_STACK_RESERVE 8192

typedef struct gm_fiber gm_fiber;
/* A continuation: the suspended fiber and its epoch. The layout of `cont(R)`. */
typedef struct { gm_fiber *fiber; uintptr_t epoch; } gm_cont;
/* Calls a `ctl` clause for a pending perform: `req` holds the node and the arguments. */
typedef void (*gm_clause_thunk)(void *req, gm_cont k);

enum { GM_EV_PERFORM = 1, GM_EV_DONE };

struct gm_with {
    void *driver_sp;            /* the driver's saved context while a fiber runs */
    gm_fiber *suspended;        /* fibers suspended on this `with`'s operations, newest first */
    uint32_t nsuspended;
    gm_fiber *owned;            /* fibers created for this `with` */
    gm_node *body_cx;           /* the body's handler context (for @fiber) */
    uint8_t event;              /* why the driver runs again: GM_EV_PERFORM or GM_EV_DONE */
    gm_fiber *event_fiber;
    gm_fiber *next;             /* the fiber chosen by `resume` */
};

struct gm_fiber {
    void *sp;                   /* saved context while not running */
    uintptr_t limit;            /* the lowest address its frames may use (checked builds) */
    gm_with *owner;             /* the `with` it was created for */
    gm_with *susp_with;         /* while suspended: the `with` whose operation it performed */
    uintptr_t epoch;
    uint8_t state;              /* GM_FB_NEW, GM_FB_RUNNING, GM_FB_SUSPENDED or GM_FB_DONE */
    uint8_t cancel;             /* set by `cancel` before switching to it */
    uint8_t started;
    gm_fiber *susp_next, *susp_prev;
    gm_fiber *own_next;
    gm_fiber *pool_next;
    void (*start)(gm_fiber *);  /* the code it starts with */
    /* the pending perform */
    gm_clause_thunk thunk;
    gm_bt *bt;                  /* its backtrace chain while it is not running */
    void *req;
    void *res;                  /* where `resume` stores the operation's result */
    /* data for `start` */
    void *frame, *env, *fn, *arg;
    gm_node *cx, *inst;
};

enum { GM_FB_NEW, GM_FB_RUNNING, GM_FB_SUSPENDED, GM_FB_DONE };

/* Creates a fiber of `w` on the memory [stack, stack + len), which will run start(fiber).
 * `arg_size` bytes at the top of the stack are reserved for fiber->arg. Traps if the stack
 * is smaller than GM_MIN_STACK (checked). */
GM_API gm_fiber *gm_fiber_new(gm_with *w, uint8_t *stack, size_t len, void (*start)(gm_fiber *), size_t arg_size,
                              const char *file, uint32_t line, uint32_t col);
/* Registers a new fiber as suspended on `w` (made by @fiber) and returns its continuation. */
GM_API gm_cont gm_fiber_cont(gm_with *w, gm_fiber *fb);
/* Checked builds compare each frame's address with the lowest address the running stack
   may use: the fiber's limit, or the thread's (§14.1, §18). */
#if defined(__SANITIZE_ADDRESS__)
#define GM_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GM_ASAN 1
#endif
#endif
#if defined(GM_ASAN) || defined(__FILC__)
/* AddressSanitizer may place frames on a separate "fake stack", and Fil-C allocates frames
   whose address is taken on its heap and runs fibers on stacks of its own, so frame
   addresses say nothing about the stack's depth; both report overflows themselves. */
#define GM_NO_FRAME_PROBES 1
#define GM_STACK_CHECK(p, n) ((void)(p))
#else
#if defined(GM_FREESTANDING) || defined(GM_WASI)
/* No signal handler or exception port to report an overflow: the probes in each function
   (against a fiber's limit, and on WASI the module's own stack) are all there is. */
#define GM_NO_FRAME_PROBES 1
#endif
/* `n` bounds the bytes of the function's other locals, which may lie below its frame `p`. */
#if defined(GM_FREESTANDING) || defined(GM_WASI)
#define GM_STACK_CHECK(p, n) do { if ((uintptr_t)(p) < gm_stack_limit + (uintptr_t)(n)) gm_stack_overflow(); } while (0)
#else
/* The frame address is above the function's locals where the C compiler keeps the frame
   record at the top of the frame (x86-64, s390x, Clang on AArch64): the frame and the
   temporaries must fit below it. GCC on AArch64 keeps the record at the bottom of the
   frame unless the function has a stack protector: without one, the frame is above the
   frame address already. */
#if defined(__aarch64__) && defined(__GNUC__) && !defined(__clang__) && !defined(__SSP__) && !defined(__SSP_STRONG__) && !defined(__SSP_ALL__)
#define GM_STACK_CHECK(p, n) do { (void)(p); if ((uintptr_t)__builtin_frame_address(0) < gm_stack_limit) gm_stack_overflow(); } while (0)
#else
#define GM_STACK_CHECK(p, n) do { if ((uintptr_t)__builtin_frame_address(0) < gm_stack_limit + (uintptr_t)(n) + sizeof(*(p))) gm_stack_overflow(); } while (0)
#endif
#endif
#endif
/* The probe of a function whose calls nest at most three deep, all of them direct calls of
   functions of the same kind (the code generator decides which are): none if its frame is
   small. At most four such frames, 1 KiB, then lie below the frame of the function that
   probed last, in the space kept below the limit (GM_STACK_RESERVE on a fiber). */
#define GM_SHALLOW_FRAME 256
#define GM_STACK_CHECK_SHALLOW(p, n) do { if (sizeof(*(p)) + (n) > GM_SHALLOW_FRAME) GM_STACK_CHECK(p, n); } while (0)
GM_API_VAR GM_TLS gm_fiber *gm_cur_fiber;   /* the running fiber, or NULL on the thread's stack */
GM_API_VAR GM_TLS uintptr_t gm_stack_limit;        /* of the running stack; 0 if unknown */
GM_API_VAR GM_TLS uintptr_t gm_thread_stack_limit; /* of the thread's own stack */
/* Records the stack limit of the calling thread (at the start of `main` or of a thread). */
GM_API void gm_thread_stack(void);
GM_API _Noreturn void gm_stack_overflow(void);
/* The trap of a call of function `name`, whose frame of `bytes` no stack can hold. */
GM_API _Noreturn void gm_frame_overflow(const char *name, uint64_t bytes);
/* Driver: switches to `fb` until something happens on `w` (an event). */
GM_API void gm_with_go(gm_with *w, gm_fiber *fb);
/* Driver: calls the clause for the pending perform of w->event_fiber. */
GM_API void gm_with_clause(gm_with *w);
/* Fiber: suspends on `w` until resumed (or cancelled: gm_unw.kind is then GM_CANCEL). */
GM_API void gm_ctl_perform(gm_with *w, gm_clause_thunk thunk, void *req, void *res);
/* Fiber: ends the running fiber and switches to its driver. */
GM_API _Noreturn void gm_fiber_finish(gm_fiber *fb);
/* Clause of `w`: consumes continuation *k for `resume`; returns where to store the value. */
GM_API void *gm_resume(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col);
/* Clause of `w`: consumes continuation *k and cancels its fiber (§14.5). */
GM_API void gm_cancel_cont(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col);
/* Driver: completes `w`. `crossing`: an exit from a fiber is leaving the `with`, so the
 * suspended fibers are discarded; otherwise any suspended fiber is a checked error (§14.4).
 * Then the fibers return to the pool. */
GM_API void gm_with_end(gm_with *w, bool crossing, const char *file, uint32_t line, uint32_t col);

#endif


#define GM_NOINLINE __attribute__((noinline))
/* The step of the structural hash: the state and the word, multiplied by an odd constant
   to 128 bits, whose halves are folded together, so that every bit of the result depends
   on every bit of both. */
static inline uint64_t gm_mix64(uint64_t h, uint64_t x) {
    unsigned __int128 m = (unsigned __int128)(h ^ x) * 0x9E3779B97F4A7C15ULL;
    return (uint64_t)m ^ (uint64_t)(m >> 64);
}
static inline uint64_t gm_mix128(uint64_t h, unsigned __int128 x) {
    return gm_mix64(gm_mix64(h, (uint64_t)x), (uint64_t)(x >> 64));
}
static inline uint64_t gm_mix_float(uint64_t h, double x) {
    uint64_t bits;
    if (x == 0) x = 0;
    memcpy(&bits, &x, 8);
    return gm_mix64(h, bits);
}
/* Mixes n bytes as little-endian words of eight, the last one of the bytes that are left. */
static uint64_t gm_mix_bytes(uint64_t h, const uint8_t *p, uint64_t n) {
    for (; n >= 8; p += 8, n -= 8) {
        uint64_t w;
        memcpy(&w, p, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        w = __builtin_bswap64(w);
#endif
        h = gm_mix64(h, w);
    }
    if (n > 0) {
        uint64_t w = 0;
        for (uint64_t i = 0; i < n; i++) w |= (uint64_t)p[i] << (8 * i);
        h = gm_mix64(h, w);
    }
    return h;
}
static double gm_fmin(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? x : y;
    return x < y ? x : y;
}
static double gm_fmax(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? y : x;
    return x > y ? x : y;
}
/* base ^ e by repeated squaring, wrapping at the width of T; *o is set when a product that
 * contributes to the result overflowed T (so the true result does not fit T). */
#define GM_POW_FN(name, T)                                                      \
    static T name(T b, uint64_t e, bool *o) {                                   \
        T r = 1;                                                                \
        for (;;) {                                                              \
            if ((e & 1) && __builtin_mul_overflow(r, b, &r)) *o = true;        \
            e >>= 1;                                                            \
            if (!e) return r;                                                   \
            if (__builtin_mul_overflow(b, b, &b)) *o = true;                    \
        }                                                                       \
    }
GM_POW_FN(gm_pow_u8, uint8_t)
GM_POW_FN(gm_pow_i8, int8_t)
GM_POW_FN(gm_pow_u16, uint16_t)
GM_POW_FN(gm_pow_i16, int16_t)
GM_POW_FN(gm_pow_u32, uint32_t)
GM_POW_FN(gm_pow_i32, int32_t)
GM_POW_FN(gm_pow_u64, uint64_t)
GM_POW_FN(gm_pow_i64, int64_t)
GM_POW_FN(gm_pow_u128, unsigned __int128)
GM_POW_FN(gm_pow_i128, __int128)
/* Bit counts of the low `bits` bits of x (the caller masks the rest). */
static unsigned __int128 gm_clz(unsigned __int128 x, int bits) {
    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;
    int n = hi ? __builtin_clzll(hi) : lo ? 64 + __builtin_clzll(lo) : 128;
    return n - (128 - bits);
}
static unsigned __int128 gm_ctz(unsigned __int128 x, int bits) {
    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;
    int n = lo ? __builtin_ctzll(lo) : hi ? 64 + __builtin_ctzll(hi) : 128;
    return n < bits ? n : bits;
}
static unsigned __int128 gm_popcount(unsigned __int128 x) {
    return __builtin_popcountll((uint64_t)x) + __builtin_popcountll((uint64_t)(x >> 64));
}
static unsigned __int128 gm_bswap(unsigned __int128 x, int bits) {
    unsigned __int128 r = ((unsigned __int128)__builtin_bswap64((uint64_t)x) << 64) | __builtin_bswap64((uint64_t)(x >> 64));
    return r >> (128 - bits);
}
static unsigned __int128 gm_bitreverse(unsigned __int128 x, int bits) {
    unsigned __int128 r = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t w = (uint32_t)(x >> (32 * i));
        w = ((w >> 1) & 0x55555555u) | ((w & 0x55555555u) << 1);
        w = ((w >> 2) & 0x33333333u) | ((w & 0x33333333u) << 2);
        w = ((w >> 4) & 0x0f0f0f0fu) | ((w & 0x0f0f0f0fu) << 4);
        w = __builtin_bswap32(w);
        r |= (unsigned __int128)w << (32 * (3 - i));
    }
    return r >> (128 - bits);
}
/* Rotates the low `bits` bits of x left by k (taken modulo bits). */
static unsigned __int128 gm_rotl(unsigned __int128 x, uint64_t k, int bits) {
    unsigned __int128 mask = bits == 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);
    x &= mask;
    k %= (uint64_t)bits;
    if (k == 0) return x;
    return ((x << k) | (x >> ((uint64_t)bits - k))) & mask;
}
typedef struct { _Alignas(1) uint8_t b[0]; } gm_b0_1;
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct gm_cs32e03j420uj1 { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_cs32e03j420uj1;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t a[40]; } gm_t6kvj62qe5fp2;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
_Static_assert(sizeof(gm_cs32e03j420uj1) == 24 && _Alignof(gm_cs32e03j420uj1) == 8 && offsetof(gm_cs32e03j420uj1, f0) == 0 && offsetof(gm_cs32e03j420uj1, f1) == 8 && offsetof(gm_cs32e03j420uj1, f2) == 16 && offsetof(gm_cs32e03j420uj1, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t6kvj62qe5fp2) == 40 && _Alignof(gm_t6kvj62qe5fp2) == 1 && offsetof(gm_t6kvj62qe5fp2, a) == 0, "layout of `[40]u8`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_tiilmfd16v3fc f1; } gm_t4f5fr60fo50r;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4f5fr60fo50r) == 32 && _Alignof(gm_t4f5fr60fo50r) == 8 && offsetof(gm_t4f5fr60fo50r, f0) == 0 && offsetof(gm_t4f5fr60fo50r, f1) == 8, "layout of `(i: usize, x: EffectRef)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_args_881ap90 {
    gm_frame h;
    gm_toa2vhe3l4hfr l6; /* a */
    gm_b0_1 ret;
};
struct gm_fr_components_dvsj2j9 {
    gm_frame h;
    gm_tddtjfggdat0d l25; /* sig */
    gm_tddtjfggdat0d l26; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_ref_6sjtehp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_effects_mg3vf2a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_gu67n99 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_empty_set_3vntabf {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* a */
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_fr74f0n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_parse_exponent_4uekfcv {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_effects_vieja9u {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sig_uguln8a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_ty_vka2r8h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_0f0dp07 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_2q6gmep {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8gmb252 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_debug_string_pns0400 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_e9824eu {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_float_7p0icl6 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* digits */
    gm_b0_1 ret;
};
struct gm_fr_write_g9nufph {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_int_lq1ktof {
    gm_frame h;
    gm_t6kvj62qe5fp2 l8; /* digits */
    gm_b0_1 ret;
};
struct gm_fr_write_kki8q0t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_m28h1lv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_n5e95le {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u50pgu2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_args_881ap90(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_components_dvsj2j9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_6dvr333(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gu67n99(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf_c(void * a0, gm_toa2vhe3l4hfr a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_key_fr74f0n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_len_op_symbol_2qoiciu(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) int64_t gm_f_parse_exponent_4uekfcv(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_cs32e03j420uj1 gm_f_printer_h7jigd7_c(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_uguln8a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0f0dp07(void * a0, gm_tu0pangbbaeec a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2jdd77g(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2q6gmep(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e9824eu(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_float_7p0icl6(void *env, gm_node *cx, gm_node *inst, double a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_g9nufph(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_int_lq1ktof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kfkm5rj(void * a0, gm_tu0pangbbaeec a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m28h1lv(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n5e95le(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u50pgu2(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqn5uk3(void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_b16_8 a3);
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s0k04b3r99uqd[10];
extern const uint8_t gm_s0lrfrtvffhu7[4];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s15tjb1oda5hu[6];
extern const uint8_t gm_s1rqgg2ibsis6[10];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s3ndsddjcvskd[3];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s4imhc4v1tf2p[10];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5j8t1cg2erp9[8];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s7mhpbh399e5s[4];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sb7t8ia5pj1l5[19];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_scp1b2sk2nlmr[8];
extern const uint8_t gm_sd66vi8qjocp9[5];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_sddmbtkcc2n83[11];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfif278eeflhm[4];
extern const uint8_t gm_sfqmvjqk1sdmt[6];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shsv7bl83q04d[9];
extern const uint8_t gm_si7vj1dcaqhfd[5];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_sj21lfdl9q0t4[6];
extern const uint8_t gm_sjq55q61p743j[3];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm337p11h7spc[8];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_snja7imrvdfb7[11];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_so31m6feg820p[4];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_sqm2dbl8kpvcq[4];
extern const uint8_t gm_squedv51820an[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcp2qdr7uqsmui[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_sut5kfu0n4de9[5];
extern const gm_tmpl gm_tpl9d1siq0c9pir;
extern const gm_tmpl gm_tplblsk2k5t46dn;
extern const gm_tmpl gm_tpld1ficc6ikf1e;
extern const gm_tmpl gm_tpldofkl5m8hqug;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplf3c2iulsht2e;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplngsd5kcuu6fv;
extern const gm_tmpl gm_tplolonhuuitsj2;
extern const gm_tmpl gm_tpls4d220l28nlk;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_name_h7mjik7 F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 136));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 136) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 268, 59);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 216));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 216) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint64_t r7; bool o8 = __builtin_sub_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
    uint64_t t9 = r7;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t11 = (t9 < t10);
    if (t11) {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_b16_8 t14 = gm_l1;
        gm_tftb1odc739t5 t15 = gm_f_regrow_3cp9o8d(t12, t13, t14);
        gm_l3 = t15;
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t16) {
        } else {
            gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t18;
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 1), t17);
            F.ret = t18;
            goto gm_fret;
        }
    } else {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        uint64_t t21 = gm_l2;
        uint64_t t22 = gm_l2;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t r24; bool o25 = __builtin_add_overflow(t22, t23, &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
        uint64_t t26 = r24;
        uint64_t t27 = ((uintptr_t)t26) - (uintptr_t)t21;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 1);
    }
    void * t30 = gm_l0;
    void * t31 = gm_l0;
    void * t32 = GM_LD(void *, ((uint8_t *)t31));
    uint64_t t33 = gm_l2;
    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r35; bool o36 = __builtin_add_overflow(t33, t34, &r35);
    if (o36) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
    uint64_t t37 = r35;
    uint64_t t38 = ((uintptr_t)t37) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
    GM_ST(gm_b16_8, ((uint8_t *)t30), t39);
    gm_tftb1odc739t5 t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    ((uint8_t *)&t40)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t40 + 1), GM_ZERO(gm_b0_1));
    F.ret = t40;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_gu67n99(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0) {
    struct gm_fr_format_gu67n99 F;
    gm_tidu2er0d7h95 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tidu2er0d7h95 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tidu2er0d7h95 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_6dvr333((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)12ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t8;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_jcnt4hm F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 151, 54);
    memmove(gm_out, (((uint8_t *)t3) + (uintptr_t)t6 * 56), 56);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uvfu1k1 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ijtp9lo();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 24), t4);
    memmove(gm_out, ((uint8_t *)&t5), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_name_rbnop5l F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 256));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 256) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 290, 61);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_printer_h7jigd7 F;
    void * gm_l0; /* t */
    void * gm_l1; /* out */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    gm_tmm67r2c94d5j t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    GM_ST(uint32_t, ((uint8_t *)&t4 + 16), ((uint32_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t4 + 20), ((uint32_t)0ULL));
    memmove(gm_out, ((uint8_t *)&t4), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint8_t t7 = ((t5 == t6));
    if (t7) {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_f_empty_slice_ijtp9lo();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_3cp9o8d(t8, ((uint64_t)1ULL), t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t12) {
        } else {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t14;
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            F.ret = t14;
            goto gm_fret;
        }
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = gm_l2;
    uint8_t t18 = gm_l1;
    GM_ST(uint8_t, (((uint8_t *)t16) + (uintptr_t)t17 * 1), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
    GM_ST(gm_b16_8, ((uint8_t *)t19), t27);
    gm_tftb1odc739t5 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
    ((uint8_t *)&t28)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t28 + 1), GM_ZERO(gm_b0_1));
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
    void * t6 = t5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_effects_vieja9u F;
    void * gm_l0; /* t */
    void * gm_l1; /* set */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)&F.l2);
    gm_tmm67r2c94d5j t5;
    gm_f_printer_h7jigd7((void *)&t5, t3, t4);
    gm_l3 = t5;
    void * t6 = (void *)((uint8_t *)&gm_l3);
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)&F.l2);
    gm_tmm67r2c94d5j t5;
    gm_f_printer_h7jigd7((void *)&t5, t3, t4);
    gm_l3 = t5;
    void * t6 = (void *)((uint8_t *)&gm_l3);
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_b0_1 t8 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)&F.l2);
    gm_tmm67r2c94d5j t5;
    gm_f_printer_h7jigd7((void *)&t5, t3, t4);
    gm_l3 = t5;
    void * t6 = (void *)((uint8_t *)&gm_l3);
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = (t2 > t3);
    if (t4) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t6) gm_trap_slice(t8, t9, t6, gm_srcc24pnihhgkug, 416, 12);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    gm_b16_8 t12 = gm_l1;
    uint8_t t13 = (gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0f0dp07(void * a0, gm_tu0pangbbaeec a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_0f0dp07 F;
    void * gm_l0; /* l */
    gm_tu0pangbbaeec gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tu0pangbbaeec gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tu0pangbbaeec t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_so31m6feg820p));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_kfkm5rj(t2, t3, ((void *)&gm_tpldofkl5m8hqug), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2q6gmep(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2q6gmep F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0k04b3r99uqd));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplblsk2k5t46dn), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8gmb252 F;
    void * gm_l0; /* l */
    gm_tvaas22dl64e0 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tvaas22dl64e0 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t4 = gm_f_write_2jdd77g(t2, &(*(gm_tvaas22dl64e0 *)((uint8_t *)a1)), ((void *)&gm_tplei8v65ors6jc), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e9824eu(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_e9824eu F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sqm2dbl8kpvcq));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplf3c2iulsht2e), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_g9nufph(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_g9nufph F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sd66vi8qjocp9));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplngsd5kcuu6fv), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_kki8q0t F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_snja7imrvdfb7));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_uqn5uk3(t2, t3, ((void *)&gm_tpl9d1siq0c9pir), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m28h1lv(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_m28h1lv F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sut5kfu0n4de9));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpld1ficc6ikf1e), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n5e95le(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_n5e95le F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sddmbtkcc2n83));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplolonhuuitsj2), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u50pgu2(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u50pgu2 F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_si7vj1dcaqhfd));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpls4d220l28nlk), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_args_881ap90(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_args_881ap90 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_t428ue2u3hf5g gm_l8; /* k */
    GM_STACK_CHECK(&F, 580u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l9;
    gm_tdr8bh2bm3fc8 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l2 = t13;
    {
        for (;;) {
            uint64_t t14 = gm_l2;
            uint8_t t15 = (t14 > ((uint64_t)0ULL));
            if (!t15) {
                goto gm_lb1;
            }
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t19 = gm_l2;
            uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
            if (o21) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 203, 41);
            uint64_t t22 = r20;
            if ((uintptr_t)t22 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t22, t18, gm_srcp2qdr7uqsmui, 203, 36);
            uint8_t t23 = gm_f_is_empty_set_3vntabf(t16, &(*(gm_toa2vhe3l4hfr *)(((uint8_t *)t17) + (uintptr_t)t22 * 40)));
            if (!t23) {
                goto gm_lb1;
            }
            {
                uint64_t t24 = gm_l2;
                uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                if (o26) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 203, 51);
                uint64_t t27 = r25;
                gm_l2 = t27;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t28 = gm_l2;
    uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
    if (t29) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t30 = gm_l0;
    void * t31 = GM_LD(void *, (((uint8_t *)t30) + 8));
    gm_tftb1odc739t5 t32 = gm_f_push_t83rg2m(t31, ((uint8_t)40ULL));
    gm_l9 = t32;
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcp2qdr7uqsmui, 205, 5);
    void * t34 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t36 = gm_l2;
    uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t38 = (uintptr_t)t36;
    if (t37 > t38 || t38 > (uintptr_t)t35) gm_trap_slice(t37, t38, t35, gm_srcp2qdr7uqsmui, 206, 27);
    uint64_t t39 = ((uintptr_t)t36) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t40;
    GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t34 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39));
    gm_l3 = t40;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t41 = gm_l4;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t43 = (t41 < t42);
            if (!t43) {
                goto gm_lb4;
            }
            {
                uint64_t t44 = gm_l4;
                void * t45 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t47 = gm_l4;
                gm_toa2vhe3l4hfr t48 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)t45) + (uintptr_t)t47 * 40));
                gm_tdr8bh2bm3fc8 t49;
                GM_ST(uint64_t, ((uint8_t *)&t49 + 0), t44);
                GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&t49 + 8), t48);
                gm_l10 = t49;
                {
                    {
                        uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                        gm_l5 = t50;
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l10) + 8), 40);
                        uint64_t t51 = gm_l5;
                        uint8_t t52 = (t51 > ((uint64_t)0ULL));
                        if (t52) {
                            void * t53 = gm_l0;
                            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 8));
                            gm_b16_8 t55;
                            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t54, t55);
                            gm_l11 = t56;
                            uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcp2qdr7uqsmui, 207, 20);
                        }
                        {
                            {
                                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t59 = (uint8_t)(t58 == ((uint8_t)0ULL));
                                if (!t59) {
                                    goto gm_lb10;
                                }
                                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                                gm_l7 = t60;
                                void * t61 = gm_l0;
                                gm_t88rcucg1b7uk t62 = gm_l7;
                                gm_b0_1 t63 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t61, t62);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t65 = (uint8_t)(t64 == ((uint8_t)1ULL));
                                if (!t65) {
                                    goto gm_lb11;
                                }
                                memmove(((uint8_t *)&gm_l8), (((uint8_t *)&F.l6) + 8), 32);
                                void * t66 = gm_l0;
                                void * t67 = (void *)((uint8_t *)&gm_l8);
                                gm_b0_1 t68 = gm_f_key_fr74f0n((void *)NULL, cx, NULL, t66, t67);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb9;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 208, 9);
                        }
gm_lb9:;
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 206, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t69 = gm_l4;
            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
            uint64_t t72 = r70;
            gm_l4 = t72;
        }
    }
gm_lb4:;
    void * t73 = gm_l0;
    void * t74 = GM_LD(void *, (((uint8_t *)t73) + 8));
    gm_tftb1odc739t5 t75 = gm_f_push_t83rg2m(t74, ((uint8_t)41ULL));
    gm_l12 = t75;
    uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcp2qdr7uqsmui, 213, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_components_dvsj2j9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_components_dvsj2j9 F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* out */
    uint8_t gm_l3; /* signed */
    uint16_t gm_l4; /* bits */
    uint16_t gm_l5; /* bits */
    uint8_t gm_l6; /* c */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint8_t gm_l8; /* c */
    gm_t88rcucg1b7uk gm_l9; /* t */
    uint8_t gm_l10; /* c */
    gm_t88rcucg1b7uk gm_l11; /* t */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_tnnjeerolos0a gm_l14; /* kind */
    gm_t88rcucg1b7uk gm_l15; /* e */
    gm_b16_8 gm_l16; /* elems */
    gm_b16_8 gm_l17; /* names */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* t */
    gm_t88rcucg1b7uk gm_l22; /* t */
    gm_t88rcucg1b7uk gm_l23; /* err */
    gm_t88rcucg1b7uk gm_l24; /* ok */
    uint32_t gm_l27; /* id */
    gm_b16_8 gm_l28; /* args */
    gm_b16_8 gm_l29; /* name */
    gm_t88rcucg1b7uk gm_l30; /* t */
    gm_t88rcucg1b7uk gm_l31; /* t */
    uint32_t gm_l32; /* id */
    gm_t88rcucg1b7uk gm_l33; /* len */
    gm_t88rcucg1b7uk gm_l34; /* elem */
    gm_t9qkh0rsooc8d gm_l35; /* op */
    gm_t88rcucg1b7uk gm_l36; /* lhs */
    gm_t88rcucg1b7uk gm_l37; /* rhs */
    gm_tqsk8dt6ib3qq gm_l38; /* <temporary array> */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_t88rcucg1b7uk gm_l42; /* t */
    gm_t428ue2u3hf5g gm_l43; /* k */
    gm_tnpifv20irnph gm_l44; /* set */
    GM_STACK_CHECK(&F, 2747u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l45;
    gm_b16_8 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_b16_8 gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_b16_8 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_b16_8 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_tlcm4lvu3g1a8 gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_tftb1odc739t5 gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_tftb1odc739t5 gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tftb1odc739t5 gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tftb1odc739t5 gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tlcm4lvu3g1a8 gm_l81;
    gm_tftb1odc739t5 gm_l82;
    uint8_t gm_l83;
    gm_t53at80pfdus3 gm_l84;
    gm_tftb1odc739t5 gm_l85;
    gm_tftb1odc739t5 gm_l86;
    gm_tftb1odc739t5 gm_l87;
    gm_tftb1odc739t5 gm_l88;
    gm_tftb1odc739t5 gm_l89;
    {
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, (((uint8_t *)t56) + 8));
        gm_l2 = t57;
        void * t58 = gm_l0;
        void * t59 = GM_LD(void *, ((uint8_t *)t58));
        gm_t88rcucg1b7uk t60 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l45, t59, GM_CONV(gm_csguankko4lmhd, t60));
        {
            {
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)0ULL));
                    if (!t62) {
                        goto gm_lb2;
                    }
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l3 = t63;
                    uint16_t t64 = GM_LD(uint16_t, ((((uint8_t *)&gm_l45) + 8) + 2));
                    gm_l4 = t64;
                    void * t65 = gm_l2;
                    uint8_t t66 = gm_l3;
                    if (t66) {
                        gm_b16_8 t67;
                        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sitlbs33jqqal));
                        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(1));
                        gm_l46 = t67;
                    } else {
                        gm_b16_8 t68;
                        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s0m9d22aen6f8));
                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
                        gm_l46 = t68;
                    }
                    gm_b16_8 t69 = gm_l46;
                    uint16_t t70 = gm_l4;
                    gm_tvaas22dl64e0 t71;
                    GM_ST(uint16_t, (uint8_t *)&t71 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t71 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t69);
                    GM_ST(uint16_t, ((uint8_t *)&t71 + 16), t70);
                    gm_tftb1odc739t5 t72 = gm_f_write_8gmb252(t65, &t71);
                    gm_l47 = t72;
                    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcp2qdr7uqsmui, 56, 31);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)1ULL));
                    if (!t75) {
                        goto gm_lb3;
                    }
                    void * t76 = gm_l2;
                    gm_b16_8 t77;
                    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s6groq37ram3h));
                    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t78 = gm_f_extend_0cs85gs(t76, t77);
                    gm_l48 = t78;
                    uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                    if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcp2qdr7uqsmui, 57, 19);
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)2ULL));
                    if (!t81) {
                        goto gm_lb4;
                    }
                    void * t82 = gm_l2;
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s0i3i86nphpsh));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t84 = gm_f_extend_0cs85gs(t82, t83);
                    gm_l49 = t84;
                    uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                    if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcp2qdr7uqsmui, 58, 19);
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t87 = (uint8_t)(t86 == ((uint8_t)3ULL));
                    if (!t87) {
                        goto gm_lb5;
                    }
                    uint16_t t88 = GM_LD(uint16_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l5 = t88;
                    void * t89 = gm_l2;
                    uint16_t t90 = gm_l5;
                    gm_tu0pangbbaeec t91;
                    GM_ST(uint16_t, ((uint8_t *)&t91 + 0), t90);
                    gm_tftb1odc739t5 t92 = gm_f_write_0f0dp07(t89, t91);
                    gm_l50 = t92;
                    uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                    if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcp2qdr7uqsmui, 59, 25);
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)4ULL));
                    if (!t95) {
                        goto gm_lb6;
                    }
                    void * t96 = gm_l2;
                    gm_b16_8 t97;
                    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s929lennfd3g6));
                    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(4));
                    gm_tftb1odc739t5 t98 = gm_f_extend_0cs85gs(t96, t97);
                    gm_l51 = t98;
                    uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                    if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcp2qdr7uqsmui, 60, 18);
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t101 = (uint8_t)(t100 == ((uint8_t)5ULL));
                    if (!t101) {
                        goto gm_lb7;
                    }
                    void * t102 = gm_l2;
                    gm_b16_8 t103;
                    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_s3cv29r83pgn4));
                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t104 = gm_f_extend_0cs85gs(t102, t103);
                    gm_l52 = t104;
                    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                    if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srcp2qdr7uqsmui, 61, 19);
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t107 = (uint8_t)(t106 == ((uint8_t)6ULL));
                    if (!t107) {
                        goto gm_lb8;
                    }
                    void * t108 = gm_l2;
                    gm_b16_8 t109;
                    GM_ST(void *, (uint8_t *)&t109, (void *)(gm_s2737i1iq57nf));
                    GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(4));
                    gm_tftb1odc739t5 t110 = gm_f_extend_0cs85gs(t108, t109);
                    gm_l53 = t110;
                    uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                    if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcp2qdr7uqsmui, 62, 18);
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)7ULL));
                    if (!t113) {
                        goto gm_lb9;
                    }
                    void * t114 = gm_l2;
                    gm_b16_8 t115;
                    GM_ST(void *, (uint8_t *)&t115, (void *)(gm_slolcr6ikvg33));
                    GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(6));
                    gm_tftb1odc739t5 t116 = gm_f_extend_0cs85gs(t114, t115);
                    gm_l54 = t116;
                    uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srcp2qdr7uqsmui, 63, 20);
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)8ULL));
                    if (!t119) {
                        goto gm_lb10;
                    }
                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l6 = t120;
                    gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 4));
                    gm_l7 = t121;
                    void * t122 = gm_l2;
                    uint8_t t123 = gm_l6;
                    if (t123) {
                        gm_b16_8 t124;
                        GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sm337p11h7spc));
                        GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(7));
                        gm_l55 = t124;
                    } else {
                        gm_b16_8 t125;
                        GM_ST(void *, (uint8_t *)&t125, (void *)(gm_srs1vrn2uaams));
                        GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(1));
                        gm_l55 = t125;
                    }
                    gm_b16_8 t126 = gm_l55;
                    gm_tftb1odc739t5 t127 = gm_f_extend_0cs85gs(t122, t126);
                    gm_l56 = t127;
                    uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                    if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcp2qdr7uqsmui, 65, 13);
                    void * t129 = gm_l0;
                    gm_t88rcucg1b7uk t130 = gm_l7;
                    gm_b0_1 t131 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t129, t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb10:;
                {
                    uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t133 = (uint8_t)(t132 == ((uint8_t)9ULL));
                    if (!t133) {
                        goto gm_lb11;
                    }
                    uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l8 = t134;
                    gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 4));
                    gm_l9 = t135;
                    void * t136 = gm_l2;
                    uint8_t t137 = gm_l8;
                    if (t137) {
                        gm_b16_8 t138;
                        GM_ST(void *, (uint8_t *)&t138, (void *)(gm_s1rqgg2ibsis6));
                        GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(9));
                        gm_l57 = t138;
                    } else {
                        gm_b16_8 t139;
                        GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sfif278eeflhm));
                        GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(3));
                        gm_l57 = t139;
                    }
                    gm_b16_8 t140 = gm_l57;
                    gm_tftb1odc739t5 t141 = gm_f_extend_0cs85gs(t136, t140);
                    gm_l58 = t141;
                    uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                    if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcp2qdr7uqsmui, 69, 13);
                    void * t143 = gm_l0;
                    gm_t88rcucg1b7uk t144 = gm_l9;
                    gm_b0_1 t145 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t143, t144);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t147 = (uint8_t)(t146 == ((uint8_t)10ULL));
                    if (!t147) {
                        goto gm_lb12;
                    }
                    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l10 = t148;
                    gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 4));
                    gm_l11 = t149;
                    {
                        {
                            uint8_t t150 = gm_l10;
                            if (!t150) {
                                goto gm_lb14;
                            }
                            void * t151 = gm_l0;
                            void * t152 = GM_LD(void *, ((uint8_t *)t151));
                            gm_t88rcucg1b7uk t153 = gm_l11;
                            gm_t53at80pfdus3 t154;
                            gm_f_kind_jcnt4hm((void *)&t154, t152, GM_CONV(gm_csguankko4lmhd, t153));
                            gm_tjcj4j2gbh6c8 t155;
                            GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)0));
                            GM_ST(uint16_t, ((uint8_t *)&t155 + 2), ((uint16_t)8ULL));
                            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&t154) + 0));
                            uint8_t t157 = (uint8_t)(t156 == ((uint8_t)0ULL));
                            if (t157) {
                                gm_tjcj4j2gbh6c8 t158 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t154) + 8));
                                t157 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t158), ((uint8_t *)&t155));
                            }
                            uint8_t t159 = (uint8_t)(t157);
                            if (!t159) {
                                goto gm_lb14;
                            }
                            void * t160 = gm_l2;
                            gm_b16_8 t161;
                            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_s0iqiubfm7e7e));
                            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(6));
                            gm_t53at80pfdus3 t162;
                            memset(&t162, 0, sizeof t162);
                            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t162 + 8), t155);
                            gm_tftb1odc739t5 t163 = gm_f_extend_0cs85gs(t160, t161);
                            gm_l59 = t163;
                            uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                            if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srcp2qdr7uqsmui, 74, 17);
                            goto gm_lb13;
                        }
gm_lb14:;
                        void * t165 = gm_l2;
                        uint8_t t166 = gm_l10;
                        if (t166) {
                            gm_b16_8 t167;
                            GM_ST(void *, (uint8_t *)&t167, (void *)(gm_shsv7bl83q04d));
                            GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(8));
                            gm_l60 = t167;
                        } else {
                            gm_b16_8 t168;
                            GM_ST(void *, (uint8_t *)&t168, (void *)(gm_s3ndsddjcvskd));
                            GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(2));
                            gm_l60 = t168;
                        }
                        gm_b16_8 t169 = gm_l60;
                        gm_tftb1odc739t5 t170 = gm_f_extend_0cs85gs(t165, t169);
                        gm_l61 = t170;
                        uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                        if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcp2qdr7uqsmui, 76, 17);
                        void * t172 = gm_l0;
                        gm_t88rcucg1b7uk t173 = gm_l11;
                        gm_b0_1 t174 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t172, t173);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
gm_lb13:;
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)11ULL));
                    if (!t176) {
                        goto gm_lb15;
                    }
                    uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l12 = t177;
                    gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 8));
                    gm_l13 = t178;
                    gm_tnnjeerolos0a t179 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l45) + 8) + 12));
                    gm_l14 = t179;
                    {
                        {
                            uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            uint8_t t181 = (uint8_t)(t180 == ((uint8_t)1ULL));
                            if (!t181) {
                                goto gm_lb17;
                            }
                            gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 4));
                            gm_l15 = t182;
                            void * t183 = gm_l2;
                            gm_tftb1odc739t5 t184 = gm_f_push_t83rg2m(t183, ((uint8_t)91ULL));
                            gm_l62 = t184;
                            uint8_t t185 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                            if (!t185) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srcp2qdr7uqsmui, 83, 21);
                            void * t186 = gm_l0;
                            gm_t88rcucg1b7uk t187 = gm_l15;
                            gm_b0_1 t188 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t186, t187);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t189 = gm_l2;
                            gm_tftb1odc739t5 t190 = gm_f_push_t83rg2m(t189, ((uint8_t)93ULL));
                            gm_l63 = t190;
                            uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                            if (!t191) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srcp2qdr7uqsmui, 85, 21);
                            goto gm_lb16;
                        }
gm_lb17:;
                        {
                            uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            uint8_t t193 = (uint8_t)(t192 == ((uint8_t)2ULL));
                            if (!t193) {
                                goto gm_lb18;
                            }
                            void * t194 = gm_l2;
                            uint64_t t195 = gm_l12;
                            gm_t4ce0jd3tdro4 t196;
                            GM_ST(uint64_t, ((uint8_t *)&t196 + 0), t195);
                            gm_tftb1odc739t5 t197 = gm_f_write_n5e95le(t194, t196);
                            gm_l64 = t197;
                            uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                            if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srcp2qdr7uqsmui, 87, 26);
                            goto gm_lb16;
                        }
gm_lb18:;
                        {
                            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            uint8_t t200 = (uint8_t)(t199 == ((uint8_t)3ULL));
                            if (!t200) {
                                goto gm_lb19;
                            }
                            void * t201 = gm_l2;
                            uint64_t t202 = gm_l12;
                            gm_t4ce0jd3tdro4 t203;
                            GM_ST(uint64_t, ((uint8_t *)&t203 + 0), t202);
                            gm_tftb1odc739t5 t204 = gm_f_write_2q6gmep(t201, t203);
                            gm_l65 = t204;
                            uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                            if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srcp2qdr7uqsmui, 88, 26);
                            goto gm_lb16;
                        }
gm_lb19:;
                        {
                            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            uint8_t t207 = (uint8_t)(t206 == ((uint8_t)0ULL));
                            if (!t207) {
                                goto gm_lb20;
                            }
                            void * t208 = gm_l2;
                            uint64_t t209 = gm_l12;
                            gm_t4ce0jd3tdro4 t210;
                            GM_ST(uint64_t, ((uint8_t *)&t210 + 0), t209);
                            gm_tftb1odc739t5 t211 = gm_f_write_u50pgu2(t208, t210);
                            gm_l66 = t211;
                            uint8_t t212 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (!t212) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srcp2qdr7uqsmui, 89, 27);
                            goto gm_lb16;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 81, 13);
                    }
gm_lb16:;
                    void * t213 = gm_l0;
                    gm_t88rcucg1b7uk t214 = gm_l13;
                    gm_b0_1 t215 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t213, t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t217 = (uint8_t)(t216 == ((uint8_t)12ULL));
                    if (!t217) {
                        goto gm_lb21;
                    }
                    gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                    gm_l16 = t218;
                    gm_b16_8 t219 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l45) + 8) + 16));
                    gm_l17 = t219;
                    void * t220 = gm_l2;
                    gm_tftb1odc739t5 t221 = gm_f_push_t83rg2m(t220, ((uint8_t)40ULL));
                    gm_l67 = t221;
                    uint8_t t222 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                    if (!t222) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srcp2qdr7uqsmui, 94, 13);
                    gm_b16_8 t223 = gm_l16;
                    gm_l18 = t223;
                    gm_l19 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t224 = gm_l19;
                            uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            uint8_t t226 = (t224 < t225);
                            if (!t226) {
                                goto gm_lb22;
                            }
                            {
                                uint64_t t227 = gm_l19;
                                void * t228 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                uint64_t t230 = gm_l19;
                                gm_t88rcucg1b7uk t231 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t228) + (uintptr_t)t230 * 4));
                                gm_tlcm4lvu3g1a8 t232;
                                GM_ST(uint32_t, (uint8_t *)&t232 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t232 + 0), t227);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t232 + 8), t231);
                                gm_l68 = t232;
                                {
                                    {
                                        uint64_t t233 = GM_LD(uint64_t, ((uint8_t *)&gm_l68));
                                        gm_l20 = t233;
                                        gm_t88rcucg1b7uk t234 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l68) + 8));
                                        gm_l21 = t234;
                                        uint64_t t235 = gm_l20;
                                        uint8_t t236 = (t235 > ((uint64_t)0ULL));
                                        if (t236) {
                                            void * t237 = gm_l2;
                                            gm_b16_8 t238;
                                            GM_ST(void *, (uint8_t *)&t238, (void *)(gm_sg5gdnrith2i6));
                                            GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(2));
                                            gm_tftb1odc739t5 t239 = gm_f_extend_0cs85gs(t237, t238);
                                            gm_l69 = t239;
                                            uint8_t t240 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                            if (!t240) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_srcp2qdr7uqsmui, 96, 28);
                                        }
                                        uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                        uint8_t t242 = (t241 > ((uint64_t)0ULL));
                                        if (t242) {
                                            void * t243 = gm_l2;
                                            void * t244 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                            uint64_t t245 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                            uint64_t t246 = gm_l20;
                                            if ((uintptr_t)t246 >= (uintptr_t)(t245)) gm_trap_index((uintptr_t)t246, t245, gm_srcp2qdr7uqsmui, 97, 54);
                                            gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)t244) + (uintptr_t)t246 * 16));
                                            gm_tnpifv20irnph t248;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t248 + 0), t247);
                                            gm_tftb1odc739t5 t249 = gm_f_write_g9nufph(t243, t248);
                                            gm_l70 = t249;
                                            uint8_t t250 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                            if (!t250) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srcp2qdr7uqsmui, 97, 36);
                                        }
                                        void * t251 = gm_l0;
                                        gm_t88rcucg1b7uk t252 = gm_l21;
                                        gm_b0_1 t253 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t251, t252);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb25;
                                    }
gm_lb26:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 95, 13);
                                }
gm_lb25:;
                            }
gm_lb24:;
                            uint64_t t254 = gm_l19;
                            uint64_t r255; bool o256 = __builtin_add_overflow(t254, ((uint64_t)1ULL), &r255);
                            uint64_t t257 = r255;
                            gm_l19 = t257;
                        }
                    }
gm_lb22:;
                    {
                        {
                            uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint8_t t259 = ((t258 == ((uint64_t)1ULL)));
                            if (!t259) {
                                goto gm_lb28;
                            }
                            uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                            uint8_t t261 = ((t260 == ((uint64_t)0ULL)));
                            if (!t261) {
                                goto gm_lb28;
                            }
                            void * t262 = gm_l2;
                            gm_tftb1odc739t5 t263 = gm_f_push_t83rg2m(t262, ((uint8_t)44ULL));
                            gm_l71 = t263;
                            uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                            if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srcp2qdr7uqsmui, 100, 52);
                            goto gm_lb27;
                        }
gm_lb28:;
                    }
gm_lb27:;
                    void * t265 = gm_l2;
                    gm_tftb1odc739t5 t266 = gm_f_push_t83rg2m(t265, ((uint8_t)41ULL));
                    gm_l72 = t266;
                    uint8_t t267 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                    if (!t267) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_srcp2qdr7uqsmui, 101, 13);
                    goto gm_lb1;
                }
gm_lb21:;
            }
            {
                {
                    uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t269 = (uint8_t)(t268 == ((uint8_t)13ULL));
                    if (!t269) {
                        goto gm_lb29;
                    }
                    gm_t88rcucg1b7uk t270 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l45) + 8));
                    gm_l22 = t270;
                    void * t271 = gm_l2;
                    gm_tftb1odc739t5 t272 = gm_f_push_t83rg2m(t271, ((uint8_t)63ULL));
                    gm_l73 = t272;
                    uint8_t t273 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                    if (!t273) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_srcp2qdr7uqsmui, 104, 13);
                    void * t274 = gm_l0;
                    gm_t88rcucg1b7uk t275 = gm_l22;
                    gm_b0_1 t276 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t274, t275);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb29:;
                {
                    uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t278 = (uint8_t)(t277 == ((uint8_t)14ULL));
                    if (!t278) {
                        goto gm_lb30;
                    }
                    gm_t88rcucg1b7uk t279 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l45) + 8));
                    gm_l23 = t279;
                    gm_t88rcucg1b7uk t280 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 4));
                    gm_l24 = t280;
                    void * t281 = gm_l0;
                    gm_t88rcucg1b7uk t282 = gm_l23;
                    gm_b0_1 t283 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t281, t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t284 = gm_l2;
                    gm_tftb1odc739t5 t285 = gm_f_push_t83rg2m(t284, ((uint8_t)33ULL));
                    gm_l74 = t285;
                    uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                    if (!t286) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srcp2qdr7uqsmui, 109, 13);
                    void * t287 = gm_l0;
                    gm_t88rcucg1b7uk t288 = gm_l24;
                    gm_b0_1 t289 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t287, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb30:;
                {
                    uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t291 = (uint8_t)(t290 == ((uint8_t)15ULL));
                    if (!t291) {
                        goto gm_lb31;
                    }
                    memmove(((uint8_t *)&F.l25), (((uint8_t *)&gm_l45) + 8), 48);
                    void * t292 = gm_l0;
                    gm_b16_8 t293;
                    GM_ST(void *, (uint8_t *)&t293, (void *)(gm_s579fb7l5l6df));
                    GM_ST(uintptr_t, (uint8_t *)&t293 + 8, (uintptr_t)(2));
                    void * t294 = (void *)((uint8_t *)&F.l25);
                    gm_b0_1 t295 = gm_f_sig_uguln8a((void *)NULL, cx, NULL, t292, t293, t294);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb31:;
                {
                    uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t297 = (uint8_t)(t296 == ((uint8_t)16ULL));
                    if (!t297) {
                        goto gm_lb32;
                    }
                    memmove(((uint8_t *)&F.l26), (((uint8_t *)&gm_l45) + 8), 48);
                    void * t298 = gm_l0;
                    gm_b16_8 t299;
                    GM_ST(void *, (uint8_t *)&t299, (void *)(gm_s7mhpbh399e5s));
                    GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(3));
                    void * t300 = (void *)((uint8_t *)&F.l26);
                    gm_b0_1 t301 = gm_f_sig_uguln8a((void *)NULL, cx, NULL, t298, t299, t300);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb32:;
                {
                    uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t303 = (uint8_t)(t302 == ((uint8_t)17ULL));
                    if (!t303) {
                        goto gm_lb33;
                    }
                    uint32_t t304 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l27 = t304;
                    gm_b16_8 t305 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l45) + 8) + 8));
                    gm_l28 = t305;
                    void * t306 = gm_l0;
                    void * t307 = GM_LD(void *, ((uint8_t *)t306));
                    uint32_t t308 = gm_l27;
                    gm_b16_8 t309 = gm_f_adt_name_h7mjik7(t307, t308);
                    gm_l29 = t309;
                    void * t310 = gm_l2;
                    gm_b16_8 t311 = gm_l29;
                    gm_tftb1odc739t5 t312 = gm_f_extend_0cs85gs(t310, t311);
                    gm_l75 = t312;
                    uint8_t t313 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                    if (!t313) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srcp2qdr7uqsmui, 116, 13);
                    {
                        {
                            uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                            uint8_t t315 = ((t314 == ((uint64_t)1ULL)));
                            if (!t315) {
                                goto gm_lb35;
                            }
                            gm_b16_8 t316 = gm_l29;
                            gm_b16_8 t317;
                            GM_ST(void *, (uint8_t *)&t317, (void *)(gm_sj21lfdl9q0t4));
                            GM_ST(uintptr_t, (uint8_t *)&t317 + 8, (uintptr_t)(5));
                            uint8_t t318 = gm_f_starts_with_6vvt01e(t316, t317);
                            if (!t318) {
                                goto gm_lb35;
                            }
                            {
                                {
                                    void * t319 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                    uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t320)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t320, gm_srcp2qdr7uqsmui, 120, 32);
                                    uint8_t t321 = GM_LD(uint8_t, ((((uint8_t *)t319) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                                    uint8_t t322 = (uint8_t)(t321 == ((uint8_t)0ULL));
                                    if (!t322) {
                                        goto gm_lb37;
                                    }
                                    gm_t88rcucg1b7uk t323 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t319) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                                    gm_l30 = t323;
                                    void * t324 = gm_l0;
                                    void * t325 = GM_LD(void *, ((uint8_t *)t324));
                                    uint32_t t326 = gm_l27;
                                    uint32_t t327 = gm_f_template_9o92cvv(t325, t326);
                                    uint32_t t328 = gm_l27;
                                    uint8_t t329 = ((t327 == t328));
                                    if (!t329) {
                                        goto gm_lb37;
                                    }
                                    void * t330 = gm_l0;
                                    gm_t88rcucg1b7uk t331 = gm_l30;
                                    gm_b0_1 t332 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t330, t331);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb36;
                                }
gm_lb37:;
                            }
gm_lb36:;
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
gm_lb35:;
                    }
gm_lb34:;
                    void * t333 = gm_l0;
                    gm_b16_8 t334 = gm_l28;
                    gm_b0_1 t335 = gm_f_args_881ap90((void *)NULL, cx, NULL, t333, t334);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb33:;
            }
            {
                {
                    uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t337 = (uint8_t)(t336 == ((uint8_t)18ULL));
                    if (!t337) {
                        goto gm_lb38;
                    }
                    gm_t88rcucg1b7uk t338 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l45) + 8));
                    gm_l31 = t338;
                    void * t339 = gm_l2;
                    gm_b16_8 t340;
                    GM_ST(void *, (uint8_t *)&t340, (void *)(gm_sfqmvjqk1sdmt));
                    GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t341 = gm_f_extend_0cs85gs(t339, t340);
                    gm_l76 = t341;
                    uint8_t t342 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                    if (!t342) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 1, gm_srcp2qdr7uqsmui, 126, 13);
                    void * t343 = gm_l0;
                    gm_t88rcucg1b7uk t344 = gm_l31;
                    gm_b0_1 t345 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t343, t344);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t346 = gm_l2;
                    gm_tftb1odc739t5 t347 = gm_f_push_t83rg2m(t346, ((uint8_t)41ULL));
                    gm_l77 = t347;
                    uint8_t t348 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                    if (!t348) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srcp2qdr7uqsmui, 128, 13);
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t350 = (uint8_t)(t349 == ((uint8_t)19ULL));
                    if (!t350) {
                        goto gm_lb39;
                    }
                    uint32_t t351 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l32 = t351;
                    void * t352 = gm_l2;
                    void * t353 = gm_l0;
                    void * t354 = GM_LD(void *, ((uint8_t *)t353));
                    uint32_t t355 = gm_l32;
                    gm_b16_8 t356 = gm_f_param_name_rbnop5l(t354, t355);
                    gm_tftb1odc739t5 t357 = gm_f_extend_0cs85gs(t352, t356);
                    gm_l78 = t357;
                    uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                    if (!t358) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_srcp2qdr7uqsmui, 130, 23);
                    goto gm_lb1;
                }
gm_lb39:;
                {
                    uint8_t t359 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t360 = (uint8_t)(t359 == ((uint8_t)20ULL));
                    if (!t360) {
                        goto gm_lb40;
                    }
                    gm_t88rcucg1b7uk t361 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l45) + 8));
                    gm_l33 = t361;
                    gm_t88rcucg1b7uk t362 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 4));
                    gm_l34 = t362;
                    void * t363 = gm_l2;
                    gm_tftb1odc739t5 t364 = gm_f_push_t83rg2m(t363, ((uint8_t)91ULL));
                    gm_l79 = t364;
                    uint8_t t365 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                    if (!t365) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srcp2qdr7uqsmui, 132, 13);
                    void * t366 = gm_l0;
                    gm_t88rcucg1b7uk t367 = gm_l33;
                    gm_b0_1 t368 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t366, t367);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t369 = gm_l2;
                    gm_tftb1odc739t5 t370 = gm_f_push_t83rg2m(t369, ((uint8_t)93ULL));
                    gm_l80 = t370;
                    uint8_t t371 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                    if (!t371) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srcp2qdr7uqsmui, 134, 13);
                    void * t372 = gm_l0;
                    gm_t88rcucg1b7uk t373 = gm_l34;
                    gm_b0_1 t374 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t372, t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t376 = (uint8_t)(t375 == ((uint8_t)22ULL));
                    if (!t376) {
                        goto gm_lb41;
                    }
                    gm_t9qkh0rsooc8d t377 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l45) + 8));
                    gm_l35 = t377;
                    gm_t88rcucg1b7uk t378 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 4));
                    gm_l36 = t378;
                    gm_t88rcucg1b7uk t379 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 8));
                    gm_l37 = t379;
                    gm_t88rcucg1b7uk t380 = gm_l36;
                    gm_t88rcucg1b7uk t381 = gm_l37;
                    gm_tqsk8dt6ib3qq t382;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t382 + 0), t380);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t382 + 4), t381);
                    gm_l38 = t382;
                    void * t383 = (void *)((uint8_t *)&gm_l38);
                    uint64_t t384 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t385 = (uintptr_t)((uint64_t)2ULL);
                    if (t384 > t385 || t385 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t384, t385, ((uint64_t)2ULL), gm_srcp2qdr7uqsmui, 138, 35);
                    uint64_t t386 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t387;
                    GM_ST(void *, (uint8_t *)&t387, (void *)(((uint8_t *)t383 + (uintptr_t)((uint64_t)0ULL) * 4)));
                    GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(t386));
                    gm_l39 = t387;
                    gm_l40 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t388 = gm_l40;
                            uint64_t t389 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                            uint8_t t390 = (t388 < t389);
                            if (!t390) {
                                goto gm_lb42;
                            }
                            {
                                uint64_t t391 = gm_l40;
                                void * t392 = GM_LD(void *, ((uint8_t *)&gm_l39));
                                uint64_t t393 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                uint64_t t394 = gm_l40;
                                gm_t88rcucg1b7uk t395 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t392) + (uintptr_t)t394 * 4));
                                gm_tlcm4lvu3g1a8 t396;
                                GM_ST(uint32_t, (uint8_t *)&t396 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t396 + 0), t391);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t396 + 8), t395);
                                gm_l81 = t396;
                                {
                                    {
                                        uint64_t t397 = GM_LD(uint64_t, ((uint8_t *)&gm_l81));
                                        gm_l41 = t397;
                                        gm_t88rcucg1b7uk t398 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l81) + 8));
                                        gm_l42 = t398;
                                        uint64_t t399 = gm_l41;
                                        uint8_t t400 = ((t399 == ((uint64_t)1ULL)));
                                        if (t400) {
                                            void * t401 = gm_l2;
                                            gm_t9qkh0rsooc8d t402 = gm_l35;
                                            gm_b16_8 t403 = gm_f_len_op_symbol_2qoiciu(t402);
                                            gm_tnpifv20irnph t404;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t404 + 0), t403);
                                            gm_tftb1odc739t5 t405 = gm_f_write_m28h1lv(t401, t404);
                                            gm_l82 = t405;
                                            uint8_t t406 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                                            if (!t406) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 1, gm_srcp2qdr7uqsmui, 139, 29);
                                        }
                                        void * t407 = gm_l0;
                                        void * t408 = GM_LD(void *, ((uint8_t *)t407));
                                        gm_t88rcucg1b7uk t409 = gm_l42;
                                        gm_f_kind_jcnt4hm((void *)&gm_l84, t408, GM_CONV(gm_csguankko4lmhd, t409));
                                        {
                                            {
                                                uint8_t t410 = GM_LD(uint8_t, (((uint8_t *)&gm_l84) + 0));
                                                uint8_t t411 = (uint8_t)(t410 == ((uint8_t)22ULL));
                                                if (!t411) {
                                                    goto gm_lb48;
                                                }
                                                gm_l83 = ((uint8_t)1);
                                                goto gm_lb47;
                                            }
gm_lb48:;
                                            {
                                                gm_l83 = ((uint8_t)0);
                                                goto gm_lb47;
                                            }
gm_lb49:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 140, 20);
                                        }
gm_lb47:;
                                        uint8_t t412 = gm_l83;
                                        if (t412) {
                                            void * t413 = gm_l2;
                                            gm_tftb1odc739t5 t414 = gm_f_push_t83rg2m(t413, ((uint8_t)40ULL));
                                            gm_l85 = t414;
                                            uint8_t t415 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                                            if (!t415) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_srcp2qdr7uqsmui, 141, 21);
                                            void * t416 = gm_l0;
                                            gm_t88rcucg1b7uk t417 = gm_l42;
                                            gm_b0_1 t418 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t416, t417);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t419 = gm_l2;
                                            gm_tftb1odc739t5 t420 = gm_f_push_t83rg2m(t419, ((uint8_t)41ULL));
                                            gm_l86 = t420;
                                            uint8_t t421 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                                            if (!t421) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 1, gm_srcp2qdr7uqsmui, 143, 21);
                                        } else {
                                            void * t422 = gm_l0;
                                            gm_t88rcucg1b7uk t423 = gm_l42;
                                            gm_b0_1 t424 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t422, t423);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        goto gm_lb45;
                                    }
gm_lb46:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 138, 13);
                                }
gm_lb45:;
                            }
gm_lb44:;
                            uint64_t t425 = gm_l40;
                            uint64_t r426; bool o427 = __builtin_add_overflow(t425, ((uint64_t)1ULL), &r426);
                            uint64_t t428 = r426;
                            gm_l40 = t428;
                        }
                    }
gm_lb42:;
                    goto gm_lb1;
                }
gm_lb41:;
            }
            {
                {
                    uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t430 = (uint8_t)(t429 == ((uint8_t)21ULL));
                    if (!t430) {
                        goto gm_lb50;
                    }
                    memmove(((uint8_t *)&gm_l43), (((uint8_t *)&gm_l45) + 8), 32);
                    void * t431 = gm_l0;
                    void * t432 = (void *)((uint8_t *)&gm_l43);
                    gm_b0_1 t433 = gm_f_key_fr74f0n((void *)NULL, cx, NULL, t431, t432);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb50:;
                {
                    uint8_t t434 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t435 = (uint8_t)(t434 == ((uint8_t)24ULL));
                    if (!t435) {
                        goto gm_lb51;
                    }
                    gm_tnpifv20irnph t436 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l45) + 8));
                    gm_l44 = t436;
                    void * t437 = gm_l0;
                    void * t438 = (void *)((uint8_t *)&gm_l44);
                    gm_b0_1 t439 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t437, t438);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb51:;
                {
                    uint8_t t440 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t441 = (uint8_t)(t440 == ((uint8_t)25ULL));
                    if (!t441) {
                        goto gm_lb52;
                    }
                    void * t442 = gm_l2;
                    gm_b16_8 t443;
                    GM_ST(void *, (uint8_t *)&t443, (void *)(gm_sic5vsema0r36));
                    GM_ST(uintptr_t, (uint8_t *)&t443 + 8, (uintptr_t)(7));
                    gm_tftb1odc739t5 t444 = gm_f_extend_0cs85gs(t442, t443);
                    gm_l87 = t444;
                    uint8_t t445 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                    if (!t445) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l87) + 1, gm_srcp2qdr7uqsmui, 151, 24);
                    goto gm_lb1;
                }
gm_lb52:;
                {
                    uint8_t t446 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t447 = (uint8_t)(t446 == ((uint8_t)23ULL));
                    if (!t447) {
                        goto gm_lb53;
                    }
                    void * t448 = gm_l2;
                    gm_b16_8 t449;
                    GM_ST(void *, (uint8_t *)&t449, (void *)(gm_s4imhc4v1tf2p));
                    GM_ST(uintptr_t, (uint8_t *)&t449 + 8, (uintptr_t)(9));
                    gm_tftb1odc739t5 t450 = gm_f_extend_0cs85gs(t448, t449);
                    gm_l88 = t450;
                    uint8_t t451 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                    if (!t451) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 1, gm_srcp2qdr7uqsmui, 152, 22);
                    goto gm_lb1;
                }
gm_lb53:;
                {
                    uint8_t t452 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t453 = (uint8_t)(t452 == ((uint8_t)26ULL));
                    if (!t453) {
                        goto gm_lb54;
                    }
                    void * t454 = gm_l2;
                    gm_b16_8 t455;
                    GM_ST(void *, (uint8_t *)&t455, (void *)(gm_scp1b2sk2nlmr));
                    GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(7));
                    gm_tftb1odc739t5 t456 = gm_f_extend_0cs85gs(t454, t455);
                    gm_l89 = t456;
                    uint8_t t457 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                    if (!t457) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 1, gm_srcp2qdr7uqsmui, 153, 19);
                    goto gm_lb1;
                }
gm_lb54:;
                gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 55, 5);
            }
        }
gm_lb1:;
    }
    {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effect_ref_6sjtehp F;
    void * gm_l0; /* p */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    {
        {
            void * t6 = gm_l1;
            uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)t6));
            uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = gm_l1;
            uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 8) + 8));
            uint8_t t11 = (t10 > ((uint64_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            {
                {
                    void * t12 = gm_l1;
                    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 8));
                    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 8) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t14, gm_srcp2qdr7uqsmui, 190, 24);
                    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t13) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
                    if (!t16) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t13) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                    gm_l2 = t17;
                    void * t18 = gm_l0;
                    gm_t88rcucg1b7uk t19 = gm_l2;
                    gm_b0_1 t20 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t18, t19);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 8));
    void * t23 = gm_l0;
    void * t24 = GM_LD(void *, ((uint8_t *)t23));
    void * t25 = gm_l1;
    uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)t25));
    gm_b16_8 t27 = gm_f_effect_name_prpp0ip(t24, t26);
    gm_tftb1odc739t5 t28 = gm_f_extend_0cs85gs(t22, t27);
    gm_l3 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcp2qdr7uqsmui, 195, 5);
    void * t30 = gm_l0;
    void * t31 = gm_l1;
    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 8));
    gm_b0_1 t33 = gm_f_args_881ap90((void *)NULL, cx, NULL, t30, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effects_mg3vf2a F;
    void * gm_l0; /* p */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_tiilmfd16v3fc gm_l5; /* e */
    GM_STACK_CHECK(&F, 373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_t4f5fr60fo50r gm_l7;
    gm_tftb1odc739t5 gm_l8;
    void * t7 = gm_l1;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint8_t t9 = ((t8 == ((uint64_t)0ULL)));
    if (t9) {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sbvutvpa8pe3d));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t13 = gm_f_extend_0cs85gs(t11, t12);
        gm_l6 = t13;
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcp2qdr7uqsmui, 179, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t15 = gm_l1;
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)t15));
    gm_l2 = t16;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l3;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                uint64_t t20 = gm_l3;
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t23 = gm_l3;
                gm_tiilmfd16v3fc t24 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t21) + (uintptr_t)t23 * 24));
                gm_t4f5fr60fo50r t25;
                GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t25 + 8), t24);
                gm_l7 = t25;
                {
                    {
                        uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l4 = t26;
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l7) + 8), 24);
                        uint64_t t27 = gm_l4;
                        uint8_t t28 = (t27 > ((uint64_t)0ULL));
                        if (t28) {
                            void * t29 = gm_l0;
                            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 8));
                            gm_b16_8 t31;
                            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s0m0qaokk51g0));
                            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(3));
                            gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
                            gm_l8 = t32;
                            uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                            if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcp2qdr7uqsmui, 183, 20);
                        }
                        void * t34 = gm_l0;
                        void * t35 = (void *)((uint8_t *)&gm_l5);
                        gm_b0_1 t36 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t34, t35);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 182, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t37 = gm_l3;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            uint64_t t40 = r38;
            gm_l3 = t40;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_set_3vntabf F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tnpifv20irnph gm_l3; /* set */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l4;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
            gm_l2 = t8;
            {
                {
                    void * t9 = gm_l0;
                    void * t10 = GM_LD(void *, ((uint8_t *)t9));
                    gm_t88rcucg1b7uk t11 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l4, t10, GM_CONV(gm_csguankko4lmhd, t11));
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)24ULL));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    gm_tnpifv20irnph t14 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l4) + 8));
                    gm_l3 = t14;
                    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
                    F.ret = t16;
                    goto gm_fret;
                }
gm_lb4:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf_c(void * a0, gm_toa2vhe3l4hfr a1) { return gm_f_is_empty_set_3vntabf(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_key_fr74f0n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_key_fr74f0n F;
    void * gm_l0; /* p */
    void * gm_l1; /* k */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* s */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* b */
    uint8_t gm_l7; /* signed */
    uint16_t gm_l8; /* bits */
    uint64_t gm_l9; /* raw */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint8_t gm_l13; /* c */
    GM_STACK_CHECK(&F, 894u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_t53at80pfdus3 gm_l16;
    gm_b16_8 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tf5srh4mp4b3i gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, (((uint8_t *)t25) + 8));
    gm_l2 = t26;
    void * t27 = gm_l1;
    {
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t27) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
            if (!t29) {
                goto gm_lb2;
            }
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)t27) + 8) + 8));
            gm_l3 = t30;
            void * t31 = gm_l2;
            gm_b16_8 t32 = gm_l3;
            gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
            gm_l14 = t33;
            uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcp2qdr7uqsmui, 225, 25);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t27) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)2ULL));
            if (!t36) {
                goto gm_lb3;
            }
            void * t37 = gm_l2;
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
            gm_l15 = t39;
            uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcp2qdr7uqsmui, 226, 18);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t27) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)3ULL));
            if (!t42) {
                goto gm_lb4;
            }
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 8));
            gm_l4 = t43;
            gm_b16_8 t44 = gm_l4;
            void * t45 = gm_l2;
            gm_b0_1 t46 = gm_f_write_debug_string_pns0400((void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t27) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)0ULL));
            if (!t48) {
                goto gm_lb5;
            }
            gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 8));
            gm_l5 = t49;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((((uint8_t *)t27) + 8) + 8));
            gm_l6 = t50;
            void * t51 = gm_l0;
            void * t52 = GM_LD(void *, ((uint8_t *)t51));
            gm_t88rcucg1b7uk t53 = gm_l5;
            gm_f_kind_jcnt4hm((void *)&gm_l16, t52, GM_CONV(gm_csguankko4lmhd, t53));
            {
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)0ULL));
                    if (!t55) {
                        goto gm_lb7;
                    }
                    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 8));
                    gm_l7 = t56;
                    gm_b16_8 t57 = gm_l6;
                    uint8_t t58 = gm_l7;
                    void * t59 = gm_l2;
                    gm_b0_1 t60 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t57, t58, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)1ULL));
                    if (!t62) {
                        goto gm_lb8;
                    }
                    gm_b16_8 t63 = gm_l6;
                    void * t64 = gm_l2;
                    gm_b0_1 t65 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t63, ((uint8_t)0), t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb6;
                }
gm_lb8:;
                {
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)2ULL));
                    if (!t67) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t68 = gm_l6;
                    void * t69 = gm_l2;
                    gm_b0_1 t70 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t68, ((uint8_t)1), t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb6;
                }
gm_lb9:;
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)4ULL));
                    if (!t72) {
                        goto gm_lb10;
                    }
                    void * t73 = gm_l2;
                    {
                        {
                            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t75 = (t74 > ((uint64_t)0ULL));
                            if (!t75) {
                                goto gm_lb12;
                            }
                            void * t76 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)t76) + (uintptr_t)((uint64_t)0ULL) * 1));
                            uint8_t t79 = ((t78 == ((uint8_t)1ULL)));
                            if (!t79) {
                                goto gm_lb12;
                            }
                            gm_b16_8 t80;
                            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sehko5bb96s6p));
                            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(4));
                            gm_l17 = t80;
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_b16_8 t81;
                        GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sogehh8t1m7tn));
                        GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(5));
                        gm_l17 = t81;
                    }
gm_lb11:;
                    gm_b16_8 t82 = gm_l17;
                    gm_tftb1odc739t5 t83 = gm_f_extend_0cs85gs(t73, t82);
                    gm_l18 = t83;
                    uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcp2qdr7uqsmui, 233, 26);
                    goto gm_lb6;
                }
gm_lb10:;
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)3ULL));
                    if (!t86) {
                        goto gm_lb13;
                    }
                    uint16_t t87 = GM_LD(uint16_t, (((uint8_t *)&gm_l16) + 8));
                    gm_l8 = t87;
                    gm_l9 = ((uint64_t)0ULL);
                    gm_b16_8 t88 = gm_l6;
                    gm_l10 = t88;
                    gm_l11 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t89 = gm_l11;
                            uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint8_t t91 = (t89 < t90);
                            if (!t91) {
                                goto gm_lb14;
                            }
                            {
                                uint64_t t92 = gm_l11;
                                void * t93 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t95 = gm_l11;
                                uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t93) + (uintptr_t)t95 * 1));
                                gm_tf5srh4mp4b3i t97;
                                GM_ST(uint8_t, (uint8_t *)&t97 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t97 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t97 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t97 + 0), t92);
                                GM_ST(uint8_t, ((uint8_t *)&t97 + 8), t96);
                                gm_l19 = t97;
                                {
                                    {
                                        uint64_t t98 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                                        gm_l12 = t98;
                                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 8));
                                        gm_l13 = t99;
                                        uint64_t t100 = gm_l9;
                                        uint8_t t101 = gm_l13;
                                        uint64_t t102 = (uint64_t)t101;
                                        uint64_t t103 = gm_l12;
                                        uint64_t r104; bool o105 = __builtin_mul_overflow(t103, ((uint64_t)8ULL), &r104);
                                        if (o105) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 236, 69);
                                        uint64_t t106 = r104;
                                        uint64_t t107 = (uint64_t)t106;
                                        unsigned __int128 n108 = (unsigned __int128)t107;
                                        if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcp2qdr7uqsmui, 236, 55);
                                        uint64_t r109 = (uint64_t)((uint64_t)t102 << (int)n108);
                                        bool o110 = (uint64_t)(r109 >> (int)n108) != t102;
                                        if (o110) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 236, 55);
                                        uint64_t t111 = r109;
                                        uint64_t t112 = (uint64_t)(t100 | t111);
                                        gm_l9 = t112;
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 236, 21);
                                }
gm_lb17:;
                            }
gm_lb16:;
                            uint64_t t113 = gm_l11;
                            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                            uint64_t t116 = r114;
                            gm_l11 = t116;
                        }
                    }
gm_lb14:;
                    {
                        {
                            uint16_t t117 = gm_l8;
                            uint8_t t118 = ((t117 == ((uint16_t)64ULL)));
                            if (!t118) {
                                goto gm_lb20;
                            }
                            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t120 = ((t119 == ((uint64_t)8ULL)));
                            if (!t120) {
                                goto gm_lb20;
                            }
                            uint64_t t121 = gm_l9;
                            double t122;
                            memset(&t122, 0, sizeof t122); memcpy(&t122, ((uint8_t *)&t121), sizeof t122 < 8 ? sizeof t122 : 8);
                            void * t123 = gm_l2;
                            gm_b0_1 t124 = gm_f_write_float_7p0icl6((void *)NULL, cx, NULL, t122, t123);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                        {
                            {
                                uint16_t t125 = gm_l8;
                                uint8_t t126 = ((t125 == ((uint16_t)32ULL)));
                                if (!t126) {
                                    goto gm_lb22;
                                }
                                uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint8_t t128 = ((t127 == ((uint64_t)4ULL)));
                                if (!t128) {
                                    goto gm_lb22;
                                }
                                uint64_t t129 = gm_l9;
                                uint32_t t130 = (uint32_t)t129;
                                float t131;
                                memset(&t131, 0, sizeof t131); memcpy(&t131, ((uint8_t *)&t130), sizeof t131 < 4 ? sizeof t131 : 4);
                                double t132 = (double)t131;
                                void * t133 = gm_l2;
                                gm_b0_1 t134 = gm_f_write_float_7p0icl6((void *)NULL, cx, NULL, t132, t133);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb21;
                            }
gm_lb22:;
                            void * t135 = gm_l0;
                            gm_t88rcucg1b7uk t136 = gm_l5;
                            gm_b0_1 t137 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t135, t136);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t138 = gm_l2;
                            gm_b16_8 t139;
                            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_s15tjb1oda5hu));
                            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(5));
                            gm_tftb1odc739t5 t140 = gm_f_extend_0cs85gs(t138, t139);
                            gm_l20 = t140;
                            uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcp2qdr7uqsmui, 243, 25);
                        }
gm_lb21:;
                    }
gm_lb19:;
                    goto gm_lb6;
                }
gm_lb13:;
                {
                    void * t142 = gm_l0;
                    gm_t88rcucg1b7uk t143 = gm_l5;
                    gm_b0_1 t144 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t142, t143);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t145 = gm_l2;
                    gm_b16_8 t146;
                    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s15tjb1oda5hu));
                    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t147 = gm_f_extend_0cs85gs(t145, t146);
                    gm_l21 = t147;
                    uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                    if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcp2qdr7uqsmui, 248, 21);
                    goto gm_lb6;
                }
gm_lb23:;
                gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 229, 13);
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 224, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_parse_exponent_4uekfcv(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_exponent_4uekfcv F;
    gm_b16_8 gm_l0; /* t */
    uint8_t gm_l1; /* negative */
    int64_t gm_l2; /* v */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l6;
    int64_t gm_l7;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t10 = (t9 > ((uint64_t)0ULL));
    if (t10) {
        void * t11 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t14 = ((t13 == ((uint8_t)45ULL)));
        gm_l6 = t14;
    } else {
        gm_l6 = ((uint8_t)0);
    }
    uint8_t t15 = gm_l6;
    gm_l1 = t15;
    gm_l2 = ((int64_t)0LL);
    gm_b16_8 t16 = gm_l0;
    gm_l3 = t16;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l4;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t22 = gm_l4;
                {
                    {
                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t20) + (uintptr_t)t22 * 1));
                        gm_l5 = t23;
                        {
                            {
                                uint8_t t24 = gm_l5;
                                uint8_t t25 = (t24 >= ((uint8_t)48ULL));
                                if (!t25) {
                                    goto gm_lb7;
                                }
                                uint8_t t26 = gm_l5;
                                uint8_t t27 = (t26 <= ((uint8_t)57ULL));
                                if (!t27) {
                                    goto gm_lb7;
                                }
                                int64_t t28 = gm_l2;
                                int64_t r29; bool o30 = __builtin_mul_overflow(t28, ((int64_t)10LL), &r29);
                                if (o30) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 306, 40);
                                int64_t t31 = r29;
                                uint8_t t32 = gm_l5;
                                uint8_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint8_t)48ULL), &r33);
                                if (o34) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 306, 53);
                                uint8_t t35 = r33;
                                int64_t t36 = (int64_t)t35;
                                int64_t r37; bool o38 = __builtin_add_overflow(t31, t36, &r37);
                                if (o38) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 306, 40);
                                int64_t t39 = r37;
                                gm_l2 = t39;
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 305, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t40 = gm_l4;
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            uint64_t t43 = r41;
            gm_l4 = t43;
        }
    }
gm_lb1:;
    uint8_t t44 = gm_l1;
    if (t44) {
        int64_t t45 = gm_l2;
        if (t45 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 308, 26);
        int64_t t46 = (int64_t)((uint64_t)0 - (uint64_t)t45);
        gm_l7 = t46;
    } else {
        int64_t t47 = gm_l2;
        gm_l7 = t47;
    }
    int64_t t48 = gm_l7;
    F.ret = t48;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_cs32e03j420uj1 gm_f_printer_h7jigd7_c(void * a0, void * a1) { gm_tmm67r2c94d5j gm_r; gm_f_printer_h7jigd7(&gm_r, a0, a1); return GM_CONV(gm_cs32e03j420uj1, gm_r); }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_uguln8a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_sig_uguln8a F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* head */
    void * gm_l2; /* sig */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tlcm4lvu3g1a8 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 8));
    gm_l3 = t8;
    void * t9 = gm_l2;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 40));
    if (t10) {
        void * t11 = gm_l3;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sb7t8ia5pj1l5));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t13 = gm_f_extend_0cs85gs(t11, t12);
        gm_l8 = t13;
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcp2qdr7uqsmui, 159, 20);
    }
    void * t15 = gm_l3;
    gm_b16_8 t16 = gm_l1;
    gm_tnpifv20irnph t17;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
    gm_tftb1odc739t5 t18 = gm_f_write_e9824eu(t15, t17);
    gm_l9 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcp2qdr7uqsmui, 160, 5);
    void * t20 = gm_l2;
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)t20));
    gm_l4 = t21;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t22 = gm_l5;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t24 = (t22 < t23);
            if (!t24) {
                goto gm_lb1;
            }
            {
                uint64_t t25 = gm_l5;
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t28 = gm_l5;
                gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + (uintptr_t)t28 * 4));
                gm_tlcm4lvu3g1a8 t30;
                GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t30 + 0), t25);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30 + 8), t29);
                gm_l10 = t30;
                {
                    {
                        uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                        gm_l6 = t31;
                        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 8));
                        gm_l7 = t32;
                        uint64_t t33 = gm_l6;
                        uint8_t t34 = (t33 > ((uint64_t)0ULL));
                        if (t34) {
                            void * t35 = gm_l3;
                            gm_b16_8 t36;
                            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t35, t36);
                            gm_l11 = t37;
                            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcp2qdr7uqsmui, 162, 20);
                        }
                        void * t39 = gm_l0;
                        gm_t88rcucg1b7uk t40 = gm_l7;
                        gm_b0_1 t41 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t39, t40);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 161, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t42 = gm_l5;
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            uint64_t t45 = r43;
            gm_l5 = t45;
        }
    }
gm_lb1:;
    void * t46 = gm_l3;
    gm_tftb1odc739t5 t47 = gm_f_push_t83rg2m(t46, ((uint8_t)41ULL));
    gm_l12 = t47;
    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcp2qdr7uqsmui, 165, 5);
    void * t49 = gm_l0;
    void * t50 = GM_LD(void *, ((uint8_t *)t49));
    void * t51 = gm_l2;
    gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t51) + 16));
    uint8_t t53 = gm_f_is_unit_n83ttnb(t50, GM_CONV(gm_csguankko4lmhd, t52));
    uint8_t t54 = (uint8_t)!t53;
    if (t54) {
        void * t55 = gm_l3;
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sdbvaa960ee0f));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t55, t56);
        gm_l13 = t57;
        uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcp2qdr7uqsmui, 167, 9);
        void * t59 = gm_l0;
        void * t60 = gm_l2;
        gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t60) + 16));
        gm_b0_1 t62 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t59, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t63 = gm_l2;
    uint64_t t64 = GM_LD(uint64_t, ((((uint8_t *)t63) + 24) + 8));
    uint8_t t65 = (t64 > ((uint64_t)0ULL));
    if (t65) {
        void * t66 = gm_l3;
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s5j8t1cg2erp9));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(7));
        gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
        gm_l14 = t68;
        uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcp2qdr7uqsmui, 171, 9);
        void * t70 = gm_l0;
        void * t71 = gm_l2;
        void * t72 = (void *)(((uint8_t *)t71) + 24);
        gm_b0_1 t73 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t70, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ty_vka2r8h F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* left */
    GM_STACK_CHECK(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t gm_l3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = ((t3 == ((uint32_t)0ULL)));
    if (t4) {
        gm_l3 = ((uint32_t)300ULL);
    } else {
        void * t5 = gm_l0;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 20));
        gm_l3 = t6;
    }
    uint32_t t7 = gm_l3;
    gm_l2 = t7;
    uint32_t t8 = gm_l2;
    uint8_t t9 = ((t8 == ((uint32_t)0ULL)));
    if (t9) {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_soqt3jnmovq5l));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t13 = gm_f_extend_0cs85gs(t11, t12);
        gm_l4 = t13;
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcp2qdr7uqsmui, 44, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 16));
    uint32_t r18; bool o19 = __builtin_add_overflow(t17, ((uint32_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 47, 5);
    uint32_t t20 = r18;
    GM_ST(uint32_t, (((uint8_t *)t15) + 16), t20);
    void * t21 = gm_l0;
    uint32_t t22 = gm_l2;
    uint32_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint32_t)1ULL), &r23);
    if (o24) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 48, 14);
    uint32_t t25 = r23;
    GM_ST(uint32_t, (((uint8_t *)t21) + 20), t25);
    void * t26 = gm_l0;
    gm_t88rcucg1b7uk t27 = gm_l1;
    gm_b0_1 t28 = gm_f_components_dvsj2j9((void *)NULL, cx, NULL, t26, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t29 = gm_l0;
    void * t30 = gm_l0;
    uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t30) + 16));
    uint32_t r32; bool o33 = __builtin_sub_overflow(t31, ((uint32_t)1ULL), &r32);
    if (o33) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 50, 5);
    uint32_t t34 = r32;
    GM_ST(uint32_t, (((uint8_t *)t29) + 16), t34);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_write_debug_string_pns0400 F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    uint8_t gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    void * t15 = gm_l1;
    gm_tftb1odc739t5 t16 = gm_f_push_t83rg2m(t15, ((uint8_t)34ULL));
    gm_l5 = t16;
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcp2qdr7uqsmui, 340, 5);
    gm_b16_8 t18 = gm_l0;
    gm_l2 = t18;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l3;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t24 = gm_l3;
                {
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1));
                        gm_l4 = t25;
                        {
                            {
                                uint8_t t26 = gm_l4;
                                uint8_t t27 = ((t26 == ((uint8_t)34ULL)));
                                if (!t27) {
                                    goto gm_lb7;
                                }
                                void * t28 = gm_l1;
                                gm_b16_8 t29;
                                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_smee1janbl2t9));
                                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t30 = gm_f_extend_0cs85gs(t28, t29);
                                gm_l6 = t30;
                                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcp2qdr7uqsmui, 343, 20);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t32 = gm_l4;
                                uint8_t t33 = ((t32 == ((uint8_t)92ULL)));
                                if (!t33) {
                                    goto gm_lb8;
                                }
                                void * t34 = gm_l1;
                                gm_b16_8 t35;
                                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s3p8gug7bvl1p));
                                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t36 = gm_f_extend_0cs85gs(t34, t35);
                                gm_l7 = t36;
                                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                                if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcp2qdr7uqsmui, 344, 21);
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t38 = gm_l4;
                                uint8_t t39 = ((t38 == ((uint8_t)10ULL)));
                                if (!t39) {
                                    goto gm_lb9;
                                }
                                void * t40 = gm_l1;
                                gm_b16_8 t41;
                                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s6gt8b7hsisgu));
                                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t42 = gm_f_extend_0cs85gs(t40, t41);
                                gm_l8 = t42;
                                uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcp2qdr7uqsmui, 345, 21);
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t44 = gm_l4;
                                uint8_t t45 = ((t44 == ((uint8_t)13ULL)));
                                if (!t45) {
                                    goto gm_lb10;
                                }
                                void * t46 = gm_l1;
                                gm_b16_8 t47;
                                GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s9cb6j5ok7bdk));
                                GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t48 = gm_f_extend_0cs85gs(t46, t47);
                                gm_l9 = t48;
                                uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcp2qdr7uqsmui, 346, 21);
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t50 = gm_l4;
                                uint8_t t51 = ((t50 == ((uint8_t)9ULL)));
                                if (!t51) {
                                    goto gm_lb11;
                                }
                                void * t52 = gm_l1;
                                gm_b16_8 t53;
                                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s22g482qk7a40));
                                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t54 = gm_f_extend_0cs85gs(t52, t53);
                                gm_l10 = t54;
                                uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcp2qdr7uqsmui, 347, 21);
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t56 = gm_l4;
                                uint8_t t57 = ((t56 == ((uint8_t)0ULL)));
                                if (!t57) {
                                    goto gm_lb12;
                                }
                                void * t58 = gm_l1;
                                gm_b16_8 t59;
                                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s2v5amvfkfoll));
                                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t60 = gm_f_extend_0cs85gs(t58, t59);
                                gm_l11 = t60;
                                uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcp2qdr7uqsmui, 348, 18);
                                goto gm_lb6;
                            }
gm_lb12:;
                            {
                                uint8_t t62 = gm_l4;
                                uint8_t t63 = (t62 < ((uint8_t)32ULL));
                                if (t63) {
                                    gm_l12 = ((uint8_t)1);
                                } else {
                                    uint8_t t64 = gm_l4;
                                    uint8_t t65 = ((t64 == ((uint8_t)127ULL)));
                                    gm_l12 = t65;
                                }
                                uint8_t t66 = gm_l12;
                                if (t66) {
                                    void * t67 = gm_l1;
                                    uint8_t t68 = gm_l4;
                                    gm_t9qkh0rsooc8d t69;
                                    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), t68);
                                    gm_tftb1odc739t5 t70 = gm_f_write_kki8q0t(t67, t69);
                                    gm_l13 = t70;
                                    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcp2qdr7uqsmui, 351, 21);
                                } else {
                                    void * t72 = gm_l1;
                                    uint8_t t73 = gm_l4;
                                    gm_tftb1odc739t5 t74 = gm_f_push_t83rg2m(t72, t73);
                                    gm_l14 = t74;
                                    uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                    if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcp2qdr7uqsmui, 353, 21);
                                }
                                goto gm_lb6;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 342, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 341, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t76 = gm_l3;
            uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
            uint64_t t79 = r77;
            gm_l3 = t79;
        }
    }
gm_lb1:;
    void * t80 = gm_l1;
    gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t80, ((uint8_t)34ULL));
    gm_l15 = t81;
    uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcp2qdr7uqsmui, 358, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_write_float_7p0icl6(void *env, gm_node *cx, gm_node *inst, double a0, void * a1) {
    struct gm_fr_write_float_7p0icl6 F;
    double gm_l0; /* x */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* text */
    uint64_t gm_l3; /* e */
    gm_b16_8 gm_l4; /* mantissa */
    int64_t gm_l5; /* exp */
    uint8_t gm_l6; /* negative */
    int64_t gm_l8; /* point */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint8_t gm_l12; /* c */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* k */
    GM_STACK_CHECK(&F, 1570u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tf5srh4mp4b3i gm_l22;
    uint64_t gm_l23;
    gm_tftb1odc739t5 gm_l24;
    uint8_t gm_l25;
    gm_tf5srh4mp4b3i gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    {
        {
            {
                double t19 = gm_l0;
                double t20 = gm_l0;
                double t21 = (t19 - t20);
                uint8_t t22 = (!(t21 == ((double)0e0)));
                if (!t22) {
                    goto gm_lb2;
                }
                double t23 = gm_l0;
                double t24 = gm_l0;
                uint8_t t25 = ((t23 == t24));
                if (!t25) {
                    goto gm_lb2;
                }
                void * t26 = gm_l1;
                double t27 = gm_l0;
                uint8_t t28 = (t27 < ((double)0e0));
                if (t28) {
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_segch6a5dme9u));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(4));
                    gm_l18 = t29;
                } else {
                    gm_b16_8 t30;
                    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sns75reqq32d2));
                    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(3));
                    gm_l18 = t30;
                }
                gm_b16_8 t31 = gm_l18;
                gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t26, t31);
                gm_l19 = t32;
                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcp2qdr7uqsmui, 258, 9);
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        double t34 = gm_l0;
        double t35 = gm_l0;
        uint8_t t36 = (!(t34 == t35));
        if (t36) {
            void * t37 = gm_l1;
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s0lrfrtvffhu7));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
            gm_l20 = t39;
            uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcp2qdr7uqsmui, 262, 9);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        double t41 = gm_l0;
        gm_tidu2er0d7h95 t42;
        GM_ST(double, ((uint8_t *)&t42 + 0), t41);
        gm_f_format_gu67n99((void *)&gm_l21, (void *)NULL, cx, NULL, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srcp2qdr7uqsmui, 265, 13);
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_l2 = t44;
        gm_b16_8 t45 = gm_l2;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_tf5srh4mp4b3i t47 = gm_f_index_of_hdeg351(t46, ((uint8_t)101ULL));
        gm_l22 = t47;
        uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l22))[8] != 0);
        if (t48) {
            uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
            gm_l23 = t49;
        } else {
            void * t50 = gm_l1;
            gm_b16_8 t51 = gm_l2;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_tftb1odc739t5 t53 = gm_f_extend_0cs85gs(t50, t52);
            gm_l24 = t53;
            uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
            if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcp2qdr7uqsmui, 267, 9);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t55 = gm_l23;
        gm_l3 = t55;
    }
    {
        void * t56 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t58 = gm_l3;
        uint64_t t59 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t60 = (uintptr_t)t58;
        if (t59 > t60 || t60 > (uintptr_t)t57) gm_trap_slice(t59, t60, t57, gm_srcp2qdr7uqsmui, 271, 17);
        uint64_t t61 = ((uintptr_t)t58) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t56 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(t61));
        gm_l4 = t62;
        void * t63 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t65 = gm_l3;
        uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
        if (o67) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 272, 32);
        uint64_t t68 = r66;
        uint64_t t69 = (uintptr_t)t68;
        uint64_t t70 = (uintptr_t)t64;
        if (t69 > t70 || t70 > (uintptr_t)t64) gm_trap_slice(t69, t70, t64, gm_srcp2qdr7uqsmui, 272, 27);
        uint64_t t71 = ((uintptr_t)t64) - (uintptr_t)t68;
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t63 + (uintptr_t)t68 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71));
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        int64_t t74 = gm_f_parse_exponent_4uekfcv(t73);
        gm_l5 = t74;
        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint8_t t76 = (t75 > ((uint64_t)0ULL));
        if (t76) {
            void * t77 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)t77) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t80 = ((t79 == ((uint8_t)45ULL)));
            gm_l25 = t80;
        } else {
            gm_l25 = ((uint8_t)0);
        }
        uint8_t t81 = gm_l25;
        gm_l6 = t81;
        uint8_t t82 = gm_l6;
        if (t82) {
            void * t83 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t t85 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t86 = (uintptr_t)t84;
            if (t85 > t86 || t86 > (uintptr_t)t84) gm_trap_slice(t85, t86, t84, gm_srcp2qdr7uqsmui, 274, 30);
            uint64_t t87 = ((uintptr_t)t84) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t88;
            GM_ST(void *, (uint8_t *)&t88, (void *)(((uint8_t *)t83 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(t87));
            gm_l4 = t88;
        }
        gm_trqfvmjtt4sij t89;
        gm_f_list_uvfu1k1((void *)&t89, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t89;
        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        int64_t t91 = (int64_t)t90;
        gm_l8 = t91;
        gm_b16_8 t92 = gm_l4;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_l9 = t93;
        gm_l10 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t94 = gm_l10;
                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t96 = (t94 < t95);
                if (!t96) {
                    goto gm_lb3;
                }
                {
                    uint64_t t97 = gm_l10;
                    void * t98 = GM_LD(void *, ((uint8_t *)&gm_l9));
                    uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint64_t t100 = gm_l10;
                    uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)t98) + (uintptr_t)t100 * 1));
                    gm_tf5srh4mp4b3i t102;
                    GM_ST(uint8_t, (uint8_t *)&t102 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t102 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t102 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t102 + 0), t97);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 8), t101);
                    gm_l26 = t102;
                    {
                        {
                            uint64_t t103 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                            gm_l11 = t103;
                            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
                            gm_l12 = t104;
                            uint8_t t105 = gm_l12;
                            uint8_t t106 = ((t105 == ((uint8_t)46ULL)));
                            if (t106) {
                                uint64_t t107 = gm_l11;
                                int64_t t108 = (int64_t)t107;
                                gm_l8 = t108;
                            } else {
                                void * t109 = (void *)((uint8_t *)&F.l7);
                                uint8_t t110 = gm_l12;
                                gm_tftb1odc739t5 t111 = gm_f_push_t83rg2m(t109, t110);
                                gm_l27 = t111;
                                uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcp2qdr7uqsmui, 281, 13);
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 277, 5);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t113 = gm_l10;
                uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                uint64_t t116 = r114;
                gm_l10 = t116;
            }
        }
gm_lb3:;
    }
    {
        int64_t t117 = gm_l8;
        int64_t t118 = gm_l5;
        int64_t r119; bool o120 = __builtin_add_overflow(t117, t118, &r119);
        if (o120) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 284, 5);
        int64_t t121 = r119;
        gm_l8 = t121;
        uint8_t t122 = gm_l6;
        if (t122) {
            void * t123 = gm_l1;
            gm_tftb1odc739t5 t124 = gm_f_push_t83rg2m(t123, ((uint8_t)45ULL));
            gm_l28 = t124;
            uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
            if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcp2qdr7uqsmui, 285, 19);
        }
        int64_t t126 = gm_l8;
        uint8_t t127 = (t126 <= ((int64_t)0LL));
        if (t127) {
            void * t128 = gm_l1;
            gm_b16_8 t129;
            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sjq55q61p743j));
            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t130 = gm_f_extend_0cs85gs(t128, t129);
            gm_l29 = t130;
            uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
            if (!t131) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcp2qdr7uqsmui, 287, 9);
            gm_l14 = ((uint64_t)0ULL);
            int64_t t132 = gm_l8;
            if (t132 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 288, 22);
            int64_t t133 = (int64_t)((uint64_t)0 - (uint64_t)t132);
            uint64_t t134;
            memset(&t134, 0, sizeof t134); memcpy(&t134, ((uint8_t *)&t133), sizeof t134 < 8 ? sizeof t134 : 8);
            uint64_t t135 = (uint64_t)t134;
            gm_l13 = t135;
            {
                for (;;) {
                    uint64_t t136 = gm_l14;
                    uint64_t t137 = gm_l13;
                    uint8_t t138 = (t136 < t137);
                    if (!t138) {
                        goto gm_lb8;
                    }
                    {
                        {
                            {
                                void * t139 = gm_l1;
                                gm_tftb1odc739t5 t140 = gm_f_push_t83rg2m(t139, ((uint8_t)48ULL));
                                gm_l30 = t140;
                                uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcp2qdr7uqsmui, 288, 55);
                                goto gm_lb11;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 288, 9);
                        }
gm_lb11:;
                    }
gm_lb10:;
                    uint64_t t142 = gm_l14;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l14 = t145;
                }
            }
gm_lb8:;
            void * t146 = gm_l1;
            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
            gm_tftb1odc739t5 t149 = gm_f_extend_0cs85gs(t146, t148);
            gm_l31 = t149;
            uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcp2qdr7uqsmui, 289, 9);
        } else {
            int64_t t151 = gm_l8;
            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
            int64_t t153 = (int64_t)t152;
            uint8_t t154 = (t151 >= t153);
            if (t154) {
                void * t155 = gm_l1;
                gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                gm_tftb1odc739t5 t158 = gm_f_extend_0cs85gs(t155, t157);
                gm_l32 = t158;
                uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                if (!t159) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcp2qdr7uqsmui, 291, 9);
                gm_l16 = ((uint64_t)0ULL);
                int64_t t160 = gm_l8;
                uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                int64_t t162 = (int64_t)t161;
                int64_t r163; bool o164 = __builtin_sub_overflow(t160, t162, &r163);
                if (o164) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 292, 22);
                int64_t t165 = r163;
                uint64_t t166;
                memset(&t166, 0, sizeof t166); memcpy(&t166, ((uint8_t *)&t165), sizeof t166 < 8 ? sizeof t166 : 8);
                uint64_t t167 = (uint64_t)t166;
                gm_l15 = t167;
                {
                    for (;;) {
                        uint64_t t168 = gm_l16;
                        uint64_t t169 = gm_l15;
                        uint8_t t170 = (t168 < t169);
                        if (!t170) {
                            goto gm_lb13;
                        }
                        {
                            {
                                {
                                    void * t171 = gm_l1;
                                    gm_tftb1odc739t5 t172 = gm_f_push_t83rg2m(t171, ((uint8_t)48ULL));
                                    gm_l33 = t172;
                                    uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                    if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcp2qdr7uqsmui, 292, 84);
                                    goto gm_lb16;
                                }
gm_lb17:;
                                gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 292, 9);
                            }
gm_lb16:;
                        }
gm_lb15:;
                        uint64_t t174 = gm_l16;
                        uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                        uint64_t t177 = r175;
                        gm_l16 = t177;
                    }
                }
gm_lb13:;
            } else {
                int64_t t178 = gm_l8;
                uint64_t t179;
                memset(&t179, 0, sizeof t179); memcpy(&t179, ((uint8_t *)&t178), sizeof t179 < 8 ? sizeof t179 : 8);
                uint64_t t180 = (uint64_t)t179;
                gm_l17 = t180;
                void * t181 = gm_l1;
                void * t182 = GM_LD(void *, ((uint8_t *)&F.l7));
                uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                uint64_t t184 = gm_l17;
                uint64_t t185 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t186 = (uintptr_t)t184;
                if (t185 > t186 || t186 > (uintptr_t)t183) gm_trap_slice(t185, t186, t183, gm_srcp2qdr7uqsmui, 295, 20);
                uint64_t t187 = ((uintptr_t)t184) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t188;
                GM_ST(void *, (uint8_t *)&t188, (void *)(((uint8_t *)t182 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(t187));
                gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                gm_tftb1odc739t5 t190 = gm_f_extend_0cs85gs(t181, t189);
                gm_l34 = t190;
                uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                if (!t191) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcp2qdr7uqsmui, 295, 9);
                void * t192 = gm_l1;
                gm_tftb1odc739t5 t193 = gm_f_push_t83rg2m(t192, ((uint8_t)46ULL));
                gm_l35 = t193;
                uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                if (!t194) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcp2qdr7uqsmui, 296, 9);
                void * t195 = gm_l1;
                void * t196 = GM_LD(void *, ((uint8_t *)&F.l7));
                uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                uint64_t t198 = gm_l17;
                uint64_t t199 = (uintptr_t)t198;
                uint64_t t200 = (uintptr_t)t197;
                if (t199 > t200 || t200 > (uintptr_t)t197) gm_trap_slice(t199, t200, t197, gm_srcp2qdr7uqsmui, 297, 20);
                uint64_t t201 = ((uintptr_t)t197) - (uintptr_t)t198;
                gm_b16_8 t202;
                GM_ST(void *, (uint8_t *)&t202, (void *)(((uint8_t *)t196 + (uintptr_t)t198 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(t201));
                gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
                gm_tftb1odc739t5 t204 = gm_f_extend_0cs85gs(t195, t203);
                gm_l36 = t204;
                uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcp2qdr7uqsmui, 297, 9);
            }
        }
    }
    {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_write_int_lq1ktof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2) {
    struct gm_fr_write_int_lq1ktof F;
    gm_b16_8 gm_l0; /* b */
    uint8_t gm_l1; /* signed */
    void * gm_l2; /* out */
    unsigned __int128 gm_l3; /* v */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    uint64_t gm_l9; /* n */
    GM_STACK_CHECK(&F, 700u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_l3 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
    gm_b16_8 t17 = gm_l0;
    gm_l4 = t17;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l5;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                uint64_t t21 = gm_l5;
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t24 = gm_l5;
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1));
                gm_tf5srh4mp4b3i t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t26 + 0), t21);
                GM_ST(uint8_t, ((uint8_t *)&t26 + 8), t25);
                gm_l10 = t26;
                {
                    {
                        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                        gm_l6 = t27;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 8));
                        gm_l7 = t28;
                        unsigned __int128 t29 = gm_l3;
                        uint8_t t30 = gm_l7;
                        unsigned __int128 t31 = (unsigned __int128)t30;
                        uint64_t t32 = gm_l6;
                        uint64_t r33; bool o34 = __builtin_mul_overflow(t32, ((uint64_t)8ULL), &r33);
                        if (o34) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 314, 53);
                        uint64_t t35 = r33;
                        unsigned __int128 t36 = (unsigned __int128)t35;
                        unsigned __int128 n37 = (unsigned __int128)t36;
                        if (n37 >= 128) gm_trap_shift((uint64_t)n37, "u128", gm_srcp2qdr7uqsmui, 314, 37);
                        unsigned __int128 r38 = (unsigned __int128)((unsigned __int128)t31 << (int)n37);
                        bool o39 = (unsigned __int128)(r38 >> (int)n37) != t31;
                        if (o39) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 314, 37);
                        unsigned __int128 t40 = r38;
                        unsigned __int128 t41 = (unsigned __int128)(t29 | t40);
                        gm_l3 = t41;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 314, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t42 = gm_l5;
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            uint64_t t45 = r43;
            gm_l5 = t45;
        }
    }
gm_lb1:;
    {
        {
            uint8_t t46 = gm_l1;
            if (!t46) {
                goto gm_lb7;
            }
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t48 = (t47 > ((uint64_t)0ULL));
            if (!t48) {
                goto gm_lb7;
            }
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t50 = (t49 < ((uint64_t)16ULL));
            if (!t50) {
                goto gm_lb7;
            }
            void * t51 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t r54; bool o55 = __builtin_sub_overflow(t53, ((uint64_t)1ULL), &r54);
            if (o55) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 315, 50);
            uint64_t t56 = r54;
            if ((uintptr_t)t56 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t56, t52, gm_srcp2qdr7uqsmui, 315, 48);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t51) + (uintptr_t)t56 * 1));
            uint8_t t58 = (t57 >= ((uint8_t)128ULL));
            if (!t58) {
                goto gm_lb7;
            }
            unsigned __int128 t59 = gm_l3;
            unsigned __int128 t60 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t r62; bool o63 = __builtin_mul_overflow(t61, ((uint64_t)8ULL), &r62);
            if (o63) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 317, 31);
            uint64_t t64 = r62;
            unsigned __int128 t65 = (unsigned __int128)t64;
            unsigned __int128 n66 = (unsigned __int128)t65;
            if (n66 >= 128) gm_trap_shift((uint64_t)n66, "u128", gm_srcp2qdr7uqsmui, 317, 14);
            unsigned __int128 r67 = (unsigned __int128)((unsigned __int128)t60 << (int)n66);
            bool o68 = (unsigned __int128)(r67 >> (int)n66) != t60;
            if (o68) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 317, 14);
            unsigned __int128 t69 = r67;
            unsigned __int128 t70 = (unsigned __int128)(t59 | t69);
            gm_l3 = t70;
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    {
        {
            uint8_t t71 = gm_l1;
            if (!t71) {
                goto gm_lb9;
            }
            unsigned __int128 t72 = gm_l3;
            unsigned __int128 n73 = (unsigned __int128)((uint64_t)127ULL);
            if (n73 >= 128) gm_trap_shift((uint64_t)n73, "u128", gm_srcp2qdr7uqsmui, 319, 19);
            unsigned __int128 t74 = (unsigned __int128)(t72 >> (int)n73);
            uint8_t t75 = ((t74 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
            if (!t75) {
                goto gm_lb9;
            }
            void * t76 = gm_l2;
            gm_tftb1odc739t5 t77 = gm_f_push_t83rg2m(t76, ((uint8_t)45ULL));
            gm_l11 = t77;
            uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcp2qdr7uqsmui, 320, 9);
            unsigned __int128 t79 = gm_l3;
            unsigned __int128 t80 = (unsigned __int128)~t79;
            unsigned __int128 r81; bool o82 = __builtin_add_overflow(t80, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r81);
            unsigned __int128 t83 = r81;
            gm_l3 = t83;
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb10;
            }
            {
                uint64_t t84 = gm_l9;
                if ((uintptr_t)t84 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t84, ((uint64_t)40ULL), gm_srcp2qdr7uqsmui, 326, 9);
                unsigned __int128 t85 = gm_l3;
                if (((unsigned __int128)(((unsigned __int128)0ULL << 64) | 10ULL)) == 0) gm_trap("division by zero", gm_srcp2qdr7uqsmui, 326, 28);
                unsigned __int128 t86 = (unsigned __int128)(t85 % ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 10ULL)));
                uint8_t t87 = (uint8_t)t86;
                uint8_t r88; bool o89 = __builtin_add_overflow(((uint8_t)48ULL), t87, &r88);
                if (o89) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 326, 21);
                uint8_t t90 = r88;
                GM_ST(uint8_t, (((uint8_t *)&F.l8) + (uintptr_t)t84 * 1), t90);
                uint64_t t91 = gm_l9;
                uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                if (o93) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 327, 9);
                uint64_t t94 = r92;
                gm_l9 = t94;
                unsigned __int128 t95 = gm_l3;
                if (((unsigned __int128)(((unsigned __int128)0ULL << 64) | 10ULL)) == 0) gm_trap("division by zero", gm_srcp2qdr7uqsmui, 328, 9);
                unsigned __int128 t96 = (unsigned __int128)(t95 / ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 10ULL)));
                gm_l3 = t96;
                unsigned __int128 t97 = gm_l3;
                uint8_t t98 = ((t97 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                if (t98) {
                    goto gm_lb10;
                }
            }
gm_lb12:;
        }
    }
gm_lb10:;
    {
        for (;;) {
            uint64_t t99 = gm_l9;
            uint8_t t100 = (t99 > ((uint64_t)0ULL));
            if (!t100) {
                goto gm_lb13;
            }
            {
                uint64_t t101 = gm_l9;
                uint64_t r102; bool o103 = __builtin_sub_overflow(t101, ((uint64_t)1ULL), &r102);
                if (o103) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 332, 9);
                uint64_t t104 = r102;
                gm_l9 = t104;
                void * t105 = gm_l2;
                uint64_t t106 = gm_l9;
                if ((uintptr_t)t106 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t106, ((uint64_t)40ULL), gm_srcp2qdr7uqsmui, 333, 18);
                uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + (uintptr_t)t106 * 1));
                gm_tftb1odc739t5 t108 = gm_f_push_t83rg2m(t105, t107);
                gm_l12 = t108;
                uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcp2qdr7uqsmui, 333, 9);
            }
gm_lb15:;
        }
    }
gm_lb13:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
