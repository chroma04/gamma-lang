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
typedef struct gm_csfd6et11lk21p { _Bool f0; uint32_t f1; } gm_csfd6et11lk21p;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csfd6et11lk21p) == 8 && _Alignof(gm_csfd6et11lk21p) == 4 && offsetof(gm_csfd6et11lk21p, f0) == 0 && offsetof(gm_csfd6et11lk21p, f1) == 4, "layout of `IntW`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint32_t v1; } f1; } gm_tcula66ipo514;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tcula66ipo514) == 32 && _Alignof(gm_tcula66ipo514) == 8 && offsetof(gm_tcula66ipo514, f0) == 0 && offsetof(gm_tcula66ipo514, f1) == 8, "layout of `Flow!u32`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
struct gm_fr_bit_word_7ae821g {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_div_zero_trap_ap1saqd {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_float_op_osevuqr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_format_eruojoe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_hash_u64_32bripn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hi64_vi0hvs0 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_int_conv_rqdvurn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_int_done_am7kd5g {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_int_un_sm4tv7d {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_int_word_ll0f7kf {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_lo64_91giin8 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_mask64_vvoc1f6 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_num_bits_onl28vp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_overflow_trap_j2f8u18 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_sext_g6f00aa {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_shift_word_svovdvk {
    gm_frame h;
    gm_tcula66ipo514 ret;
};
struct gm_fr_word_eq_8jc0uot {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_word_less_l13biee {
    gm_frame h;
    uint8_t ret;
};
__attribute__((aligned(4))) __attribute__((noinline)) uint64_t gm_f_bit_word_7ae821g(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) uint64_t gm_f_float_op_osevuqr(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_hi64_vi0hvs0(uint32_t a0);
__attribute__((aligned(4))) uint64_t gm_f_int_conv_rqdvurn(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, gm_csfd6et11lk21p a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_int_done_am7kd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint8_t a5, uint8_t a6, gm_b16_8 a7);
__attribute__((aligned(4))) void gm_f_int_un_sm4tv7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_ttovhfee8mf28 a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) void gm_f_int_word_ll0f7kf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, gm_ttovhfee8mf28 a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint64_t a5, void * a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_lo64_91giin8(uint32_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_mask64_vvoc1f6(uint32_t a0);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_overflow_result_vqlhh7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, unsigned __int128 a5, uint8_t a6, gm_b16_8 a7);
inline __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_shift_check_a7uaho4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, uint8_t a3, unsigned __int128 a4, uint32_t a5);
__attribute__((aligned(4))) void gm_f_shift_word_svovdvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ttovhfee8mf28 a2, uint64_t a3, uint32_t a4);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3);
extern const uint8_t gm_s5k1c58je5d4s[25];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s9idfla83gb22[59];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const char gm_srch12rfrebvus5[];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcqod65l394oc8[];
extern const gm_tmpl gm_tplgqr018o660ue;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_div_zero_trap_ap1saqd F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_slf2kl43ogurh));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(16));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_eruojoe F;
    gm_tnpifv20irnph gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5k1c58je5d4s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgqr018o660ue), t3);
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
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_u64_32bripn F;
    void * gm_l0; /* h */
    uint64_t gm_l1; /* x */
    unsigned __int128 gm_l2; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t t4 = gm_l1;
    uint64_t t5 = (uint64_t)(t3 ^ t4);
    unsigned __int128 t6 = (unsigned __int128)t5;
    unsigned __int128 r7; bool o8 = __builtin_mul_overflow(t6, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 11400714819323198485ULL)), &r7);
    unsigned __int128 t9 = r7;
    gm_l2 = t9;
    void * t10 = gm_l0;
    unsigned __int128 t11 = gm_l2;
    uint64_t t12 = (uint64_t)t11;
    unsigned __int128 t13 = gm_l2;
    unsigned __int128 n14 = (unsigned __int128)((uint64_t)64ULL);
    if (n14 >= 128) gm_trap_shift((uint64_t)n14, "u128", gm_srcl8pn2pjtmj6k, 407, 26);
    unsigned __int128 t15 = (unsigned __int128)(t13 >> (int)n14);
    uint64_t t16 = (uint64_t)t15;
    uint64_t t17 = (uint64_t)(t12 ^ t16);
    GM_ST(uint64_t, ((uint8_t *)t10), t17);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_hi64_vi0hvs0(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hi64_vi0hvs0 F;
    uint32_t gm_l0; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint32_t)64ULL));
    if (t3) {
        gm_l1 = ((int64_t)9223372036854775807LL);
    } else {
        uint32_t t4 = gm_l0;
        uint32_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint32_t)1ULL), &r5);
        uint32_t t7 = r5;
        uint64_t t8 = (uint64_t)t7;
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srch12rfrebvus5, 23, 76);
        uint64_t r10 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n9);
        bool o11 = (uint64_t)(r10 >> (int)n9) != ((uint64_t)1ULL);
        uint64_t t12 = r10;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        uint64_t t15 = r13;
        int64_t t16;
        memset(&t16, 0, sizeof t16); memcpy(&t16, ((uint8_t *)&t15), sizeof t16 < 8 ? sizeof t16 : 8);
        gm_l1 = t16;
    }
    int64_t t17 = gm_l1;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_lo64_91giin8(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo64_91giin8 F;
    uint32_t gm_l0; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint32_t)64ULL));
    if (t3) {
        gm_l1 = ((int64_t)(-9223372036854775807LL - 1));
    } else {
        uint64_t t4 = (uint64_t)~((uint64_t)0ULL);
        uint32_t t5 = gm_l0;
        uint32_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint32_t)1ULL), &r6);
        uint32_t t8 = r6;
        uint64_t t9 = (uint64_t)t8;
        unsigned __int128 n10 = (unsigned __int128)t9;
        if (n10 >= 64) gm_trap_shift((uint64_t)n10, "u64", gm_srch12rfrebvus5, 20, 75);
        uint64_t r11 = (uint64_t)((uint64_t)t4 << (int)n10);
        bool o12 = (uint64_t)(r11 >> (int)n10) != t4;
        uint64_t t13 = r11;
        int64_t t14;
        memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 8 ? sizeof t14 : 8);
        gm_l1 = t14;
    }
    int64_t t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_mask64_vvoc1f6(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mask64_vvoc1f6 F;
    uint32_t gm_l0; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l1;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint32_t)64ULL));
    if (t3) {
        uint64_t t4 = (uint64_t)~((uint64_t)0ULL);
        gm_l1 = t4;
    } else {
        uint32_t t5 = gm_l0;
        uint64_t t6 = (uint64_t)t5;
        unsigned __int128 n7 = (unsigned __int128)t6;
        if (n7 >= 64) gm_trap_shift((uint64_t)n7, "u64", gm_srch12rfrebvus5, 7, 69);
        uint64_t r8 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n7);
        bool o9 = (uint64_t)(r8 >> (int)n7) != ((uint64_t)1ULL);
        uint64_t t10 = r8;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        uint64_t t13 = r11;
        gm_l1 = t13;
    }
    uint64_t t14 = gm_l1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_bits_onl28vp F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t2) {
        unsigned __int128 t3 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
        unsigned __int128 r4; bool o5 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t3, &r4);
        unsigned __int128 t6 = r4;
        gm_l1 = t6;
    } else {
        unsigned __int128 t7 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
        gm_l1 = t7;
    }
    unsigned __int128 t8 = gm_l1;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_overflow_trap_j2f8u18 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* sym */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_eruojoe((void *)&gm_l3, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcqod65l394oc8, 23, 108);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t8);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sext_g6f00aa F;
    uint64_t gm_l0; /* v */
    uint32_t gm_l1; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t gm_l2;
    {
        {
            uint32_t t7 = gm_l1;
            uint8_t t8 = ((t7 == ((uint32_t)8ULL)));
            if (!t8) {
                goto gm_lb2;
            }
            uint64_t t9 = gm_l0;
            uint8_t t10 = (uint8_t)t9;
            int8_t t11;
            memset(&t11, 0, sizeof t11); memcpy(&t11, ((uint8_t *)&t10), sizeof t11 < 1 ? sizeof t11 : 1);
            int64_t t12 = (int64_t)t11;
            gm_l2 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint32_t t13 = gm_l1;
            uint8_t t14 = ((t13 == ((uint32_t)16ULL)));
            if (!t14) {
                goto gm_lb3;
            }
            uint64_t t15 = gm_l0;
            uint16_t t16 = (uint16_t)t15;
            int16_t t17;
            memset(&t17, 0, sizeof t17); memcpy(&t17, ((uint8_t *)&t16), sizeof t17 < 2 ? sizeof t17 : 2);
            int64_t t18 = (int64_t)t17;
            gm_l2 = t18;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint32_t t19 = gm_l1;
            uint8_t t20 = ((t19 == ((uint32_t)32ULL)));
            if (!t20) {
                goto gm_lb4;
            }
            uint64_t t21 = gm_l0;
            uint32_t t22 = (uint32_t)t21;
            int32_t t23;
            memset(&t23, 0, sizeof t23); memcpy(&t23, ((uint8_t *)&t22), sizeof t23 < 4 ? sizeof t23 : 4);
            int64_t t24 = (int64_t)t23;
            gm_l2 = t24;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint64_t t25 = gm_l0;
            int64_t t26;
            memset(&t26, 0, sizeof t26); memcpy(&t26, ((uint8_t *)&t25), sizeof t26 < 8 ? sizeof t26 : 8);
            gm_l2 = t26;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 11, 41);
    }
gm_lb1:;
    int64_t t27 = gm_l2;
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_eq_8jc0uot F;
    gm_tmn7n0n9raou5 gm_l0; /* how */
    uint64_t gm_l1; /* p */
    uint64_t gm_l2; /* q */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l3;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)3ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint64_t t9 = gm_l1;
            double t10;
            memset(&t10, 0, sizeof t10); memcpy(&t10, ((uint8_t *)&t9), sizeof t10 < 8 ? sizeof t10 : 8);
            uint64_t t11 = gm_l2;
            double t12;
            memset(&t12, 0, sizeof t12); memcpy(&t12, ((uint8_t *)&t11), sizeof t12 < 8 ? sizeof t12 : 8);
            uint8_t t13 = ((t10 == t12));
            gm_l3 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)2ULL));
            if (!t15) {
                goto gm_lb3;
            }
            uint64_t t16 = gm_l1;
            uint32_t t17 = (uint32_t)t16;
            float t18;
            memset(&t18, 0, sizeof t18); memcpy(&t18, ((uint8_t *)&t17), sizeof t18 < 4 ? sizeof t18 : 4);
            uint64_t t19 = gm_l2;
            uint32_t t20 = (uint32_t)t19;
            float t21;
            memset(&t21, 0, sizeof t21); memcpy(&t21, ((uint8_t *)&t20), sizeof t21 < 4 ? sizeof t21 : 4);
            uint8_t t22 = ((t18 == t21));
            gm_l3 = t22;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)4ULL));
            if (!t24) {
                goto gm_lb4;
            }
            uint64_t t25 = gm_l1;
            uint8_t t26 = (!(t25 == ((uint64_t)0ULL)));
            uint64_t t27 = gm_l2;
            uint8_t t28 = (!(t27 == ((uint64_t)0ULL)));
            uint8_t t29 = ((t26 == t28));
            gm_l3 = t29;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint64_t t30 = gm_l1;
            uint64_t t31 = gm_l2;
            uint8_t t32 = ((t30 == t31));
            gm_l3 = t32;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 70, 51);
    }
gm_lb1:;
    uint8_t t33 = gm_l3;
    F.ret = t33;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_less_l13biee F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_tmn7n0n9raou5 gm_l1; /* how */
    uint64_t gm_l2; /* p */
    uint64_t gm_l3; /* q */
    uint32_t gm_l4; /* bits */
    int64_t gm_l5; /* x */
    int64_t gm_l6; /* y */
    double gm_l7; /* x */
    double gm_l8; /* y */
    float gm_l9; /* x */
    float gm_l10; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 458u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
            if (!t28) {
                goto gm_lb2;
            }
            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_l4 = t29;
            uint64_t t30 = gm_l2;
            uint32_t t31 = gm_l4;
            int64_t t32 = gm_f_sext_g6f00aa(t30, t31);
            gm_l5 = t32;
            uint64_t t33 = gm_l3;
            uint32_t t34 = gm_l4;
            int64_t t35 = gm_f_sext_g6f00aa(t33, t34);
            gm_l6 = t35;
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)10ULL));
                    if (!t37) {
                        goto gm_lb4;
                    }
                    int64_t t38 = gm_l5;
                    int64_t t39 = gm_l6;
                    uint8_t t40 = (t38 < t39);
                    gm_l11 = t40;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)11ULL));
                    if (!t42) {
                        goto gm_lb5;
                    }
                    int64_t t43 = gm_l5;
                    int64_t t44 = gm_l6;
                    uint8_t t45 = (t43 <= t44);
                    gm_l11 = t45;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)12ULL));
                    if (!t47) {
                        goto gm_lb6;
                    }
                    int64_t t48 = gm_l5;
                    int64_t t49 = gm_l6;
                    uint8_t t50 = (t48 > t49);
                    gm_l11 = t50;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    int64_t t51 = gm_l5;
                    int64_t t52 = gm_l6;
                    uint8_t t53 = (t51 >= t52);
                    gm_l11 = t53;
                    goto gm_lb3;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 32, 20);
            }
gm_lb3:;
            uint8_t t54 = gm_l11;
            F.ret = t54;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)3ULL));
            if (!t56) {
                goto gm_lb8;
            }
            uint64_t t57 = gm_l2;
            double t58;
            memset(&t58, 0, sizeof t58); memcpy(&t58, ((uint8_t *)&t57), sizeof t58 < 8 ? sizeof t58 : 8);
            gm_l7 = t58;
            uint64_t t59 = gm_l3;
            double t60;
            memset(&t60, 0, sizeof t60); memcpy(&t60, ((uint8_t *)&t59), sizeof t60 < 8 ? sizeof t60 : 8);
            gm_l8 = t60;
            {
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)10ULL));
                    if (!t62) {
                        goto gm_lb10;
                    }
                    double t63 = gm_l7;
                    double t64 = gm_l8;
                    uint8_t t65 = (t63 < t64);
                    gm_l12 = t65;
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)11ULL));
                    if (!t67) {
                        goto gm_lb11;
                    }
                    double t68 = gm_l7;
                    double t69 = gm_l8;
                    uint8_t t70 = (t68 <= t69);
                    gm_l12 = t70;
                    goto gm_lb9;
                }
gm_lb11:;
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)12ULL));
                    if (!t72) {
                        goto gm_lb12;
                    }
                    double t73 = gm_l7;
                    double t74 = gm_l8;
                    uint8_t t75 = (t73 > t74);
                    gm_l12 = t75;
                    goto gm_lb9;
                }
gm_lb12:;
                {
                    double t76 = gm_l7;
                    double t77 = gm_l8;
                    uint8_t t78 = (t76 >= t77);
                    gm_l12 = t78;
                    goto gm_lb9;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 42, 20);
            }
gm_lb9:;
            uint8_t t79 = gm_l12;
            F.ret = t79;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t81 = (uint8_t)(t80 == ((uint8_t)2ULL));
            if (!t81) {
                goto gm_lb14;
            }
            uint64_t t82 = gm_l2;
            uint32_t t83 = (uint32_t)t82;
            float t84;
            memset(&t84, 0, sizeof t84); memcpy(&t84, ((uint8_t *)&t83), sizeof t84 < 4 ? sizeof t84 : 4);
            gm_l9 = t84;
            uint64_t t85 = gm_l3;
            uint32_t t86 = (uint32_t)t85;
            float t87;
            memset(&t87, 0, sizeof t87); memcpy(&t87, ((uint8_t *)&t86), sizeof t87 < 4 ? sizeof t87 : 4);
            gm_l10 = t87;
            {
                {
                    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)10ULL));
                    if (!t89) {
                        goto gm_lb16;
                    }
                    float t90 = gm_l9;
                    float t91 = gm_l10;
                    uint8_t t92 = (t90 < t91);
                    gm_l13 = t92;
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t94 = (uint8_t)(t93 == ((uint8_t)11ULL));
                    if (!t94) {
                        goto gm_lb17;
                    }
                    float t95 = gm_l9;
                    float t96 = gm_l10;
                    uint8_t t97 = (t95 <= t96);
                    gm_l13 = t97;
                    goto gm_lb15;
                }
gm_lb17:;
                {
                    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)12ULL));
                    if (!t99) {
                        goto gm_lb18;
                    }
                    float t100 = gm_l9;
                    float t101 = gm_l10;
                    uint8_t t102 = (t100 > t101);
                    gm_l13 = t102;
                    goto gm_lb15;
                }
gm_lb18:;
                {
                    float t103 = gm_l9;
                    float t104 = gm_l10;
                    uint8_t t105 = (t103 >= t104);
                    gm_l13 = t105;
                    goto gm_lb15;
                }
gm_lb19:;
                gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 52, 20);
            }
gm_lb15:;
            uint8_t t106 = gm_l13;
            F.ret = t106;
            goto gm_fret;
        }
gm_lb14:;
        {
            {
                {
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t108 = (uint8_t)(t107 == ((uint8_t)10ULL));
                    if (!t108) {
                        goto gm_lb22;
                    }
                    uint64_t t109 = gm_l2;
                    uint64_t t110 = gm_l3;
                    uint8_t t111 = (t109 < t110);
                    gm_l14 = t111;
                    goto gm_lb21;
                }
gm_lb22:;
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)11ULL));
                    if (!t113) {
                        goto gm_lb23;
                    }
                    uint64_t t114 = gm_l2;
                    uint64_t t115 = gm_l3;
                    uint8_t t116 = (t114 <= t115);
                    gm_l14 = t116;
                    goto gm_lb21;
                }
gm_lb23:;
                {
                    uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)12ULL));
                    if (!t118) {
                        goto gm_lb24;
                    }
                    uint64_t t119 = gm_l2;
                    uint64_t t120 = gm_l3;
                    uint8_t t121 = (t119 > t120);
                    gm_l14 = t121;
                    goto gm_lb21;
                }
gm_lb24:;
                {
                    uint64_t t122 = gm_l2;
                    uint64_t t123 = gm_l3;
                    uint8_t t124 = (t122 >= t123);
                    gm_l14 = t124;
                    goto gm_lb21;
                }
gm_lb25:;
                gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 59, 21);
            }
gm_lb21:;
            uint8_t t125 = gm_l14;
            F.ret = t125;
            goto gm_fret;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 28, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((noinline)) uint64_t gm_f_bit_word_7ae821g(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_word_7ae821g F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_ttovhfee8mf28 gm_l1; /* t */
    uint64_t gm_l2; /* v */
    uint64_t gm_l3; /* k */
    uint32_t gm_l4; /* n */
    uint64_t gm_l5; /* h */
    uint64_t gm_l6; /* x */
    uint32_t gm_l7; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 651u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_ttovhfee8mf28, a1);
    gm_l2 = a2;
    gm_l3 = a3;
    uint64_t gm_l8;
    uint64_t gm_l9;
    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_l4 = t12;
    gm_tvdg94bk7aa22 t13 = gm_l0;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
    uint8_t t15 = (uint8_t)((t14 == ((uint8_t)62ULL)));
    gm_tvdg94bk7aa22 t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)62ULL));
    if (t15) {
        uint64_t t17 = gm_l3;
        uint64_t t18 = (uint64_t)(((uint64_t)14695981039346656037ULL) ^ t17);
        gm_l5 = t18;
        void * t19 = (void *)((uint8_t *)&gm_l5);
        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)&gm_l1));
        if (t20) {
            uint64_t t21 = gm_l2;
            uint32_t t22 = gm_l4;
            int64_t t23 = gm_f_sext_g6f00aa(t21, t22);
            uint64_t t24;
            memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 8 ? sizeof t24 : 8);
            gm_l8 = t24;
        } else {
            uint64_t t25 = gm_l2;
            uint32_t t26 = gm_l4;
            uint64_t t27 = gm_f_mask64_vvoc1f6(t26);
            uint64_t t28 = (uint64_t)(t25 & t27);
            gm_l8 = t28;
        }
        uint64_t t29 = gm_l8;
        gm_f_hash_u64_32bripn(t19, t29);
        uint64_t t30 = gm_l5;
        F.ret = t30;
        goto gm_fret;
    }
    uint64_t t31 = gm_l2;
    uint32_t t32 = gm_l4;
    uint64_t t33 = gm_f_mask64_vvoc1f6(t32);
    uint64_t t34 = (uint64_t)(t31 & t33);
    gm_l6 = t34;
    {
        {
            {
                {
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)32ULL));
                    if (!t36) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)33ULL));
                    if (!t38) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            uint64_t t39 = gm_l3;
            uint32_t t40 = gm_l4;
            uint64_t t41 = (uint64_t)t40;
            if (t41 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 114, 19);
            uint64_t t42 = (uint64_t)(t39 % t41);
            uint32_t t43 = (uint32_t)t42;
            gm_l7 = t43;
            gm_tvdg94bk7aa22 t44 = gm_l0;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
            uint8_t t46 = (uint8_t)((t45 == ((uint8_t)33ULL)));
            gm_tvdg94bk7aa22 t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)33ULL));
            if (t46) {
                uint32_t t48 = gm_l4;
                uint32_t t49 = gm_l7;
                uint32_t r50; bool o51 = __builtin_sub_overflow(t48, t49, &r50);
                if (o51) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 115, 35);
                uint32_t t52 = r50;
                uint32_t t53 = gm_l4;
                if (t53 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 115, 34);
                uint32_t t54 = (uint32_t)(t52 % t53);
                gm_l7 = t54;
            }
            uint32_t t55 = gm_l7;
            uint8_t t56 = ((t55 == ((uint32_t)0ULL)));
            if (t56) {
                uint64_t t57 = gm_l6;
                F.ret = t57;
                goto gm_fret;
            }
            uint64_t t58 = gm_l6;
            uint32_t t59 = gm_l7;
            uint64_t t60 = (uint64_t)t59;
            unsigned __int128 n61 = (unsigned __int128)t60;
            if (n61 >= 64) gm_trap_shift((uint64_t)n61, "u64", gm_srch12rfrebvus5, 117, 22);
            uint64_t r62 = (uint64_t)((uint64_t)t58 << (int)n61);
            bool o63 = (uint64_t)(r62 >> (int)n61) != t58;
            uint64_t t64 = r62;
            uint64_t t65 = gm_l6;
            uint32_t t66 = gm_l4;
            uint32_t t67 = gm_l7;
            uint32_t r68; bool o69 = __builtin_sub_overflow(t66, t67, &r68);
            if (o69) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 117, 48);
            uint32_t t70 = r68;
            uint64_t t71 = (uint64_t)t70;
            unsigned __int128 n72 = (unsigned __int128)t71;
            if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srch12rfrebvus5, 117, 39);
            uint64_t t73 = (uint64_t)(t65 >> (int)n72);
            uint64_t t74 = (uint64_t)(t64 | t73);
            uint32_t t75 = gm_l4;
            uint64_t t76 = gm_f_mask64_vvoc1f6(t75);
            uint64_t t77 = (uint64_t)(t74 & t76);
            F.ret = t77;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t79 = (uint8_t)(t78 == ((uint8_t)28ULL));
            if (!t79) {
                goto gm_lb6;
            }
            uint64_t t80 = gm_l6;
            uint32_t t81 = (uint32_t)gm_clz(((unsigned __int128)(uint64_t)(t80) & (((unsigned __int128)1 << 64) - 1)), 64);
            uint64_t t82 = (uint64_t)t81;
            uint32_t t83 = gm_l4;
            uint32_t r84; bool o85 = __builtin_sub_overflow(((uint32_t)64ULL), t83, &r84);
            if (o85) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 119, 43);
            uint32_t t86 = r84;
            uint64_t t87 = (uint64_t)t86;
            uint64_t r88; bool o89 = __builtin_sub_overflow(t82, t87, &r88);
            if (o89) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 119, 24);
            uint64_t t90 = r88;
            F.ret = t90;
            goto gm_fret;
        }
gm_lb6:;
        {
            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)29ULL));
            if (!t92) {
                goto gm_lb7;
            }
            uint64_t t93 = gm_l6;
            uint8_t t94 = ((t93 == ((uint64_t)0ULL)));
            if (t94) {
                uint32_t t95 = gm_l4;
                uint64_t t96 = (uint64_t)t95;
                gm_l9 = t96;
            } else {
                uint64_t t97 = gm_l6;
                uint32_t t98 = (uint32_t)gm_ctz(((unsigned __int128)(uint64_t)(t97) & (((unsigned __int128)1 << 64) - 1)), 64);
                uint64_t t99 = (uint64_t)t98;
                gm_l9 = t99;
            }
            uint64_t t100 = gm_l9;
            F.ret = t100;
            goto gm_fret;
        }
gm_lb7:;
        {
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)30ULL));
            if (!t102) {
                goto gm_lb8;
            }
            uint64_t t103 = gm_l6;
            uint32_t t104 = (uint32_t)gm_popcount(((unsigned __int128)(uint64_t)(t103) & (((unsigned __int128)1 << 64) - 1)));
            uint64_t t105 = (uint64_t)t104;
            F.ret = t105;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t107 = (uint8_t)(t106 == ((uint8_t)31ULL));
            if (!t107) {
                goto gm_lb9;
            }
            uint64_t t108 = gm_l6;
            uint64_t t109 = (uint64_t)gm_bswap(((unsigned __int128)(uint64_t)(t108) & (((unsigned __int128)1 << 64) - 1)), 64);
            uint64_t t110 = t109;
            uint32_t t111 = gm_l4;
            uint32_t r112; bool o113 = __builtin_sub_overflow(((uint32_t)64ULL), t111, &r112);
            if (o113) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 122, 43);
            uint32_t t114 = r112;
            uint64_t t115 = (uint64_t)t114;
            unsigned __int128 n116 = (unsigned __int128)t115;
            if (n116 >= 64) gm_trap_shift((uint64_t)n116, "u64", gm_srch12rfrebvus5, 122, 26);
            uint64_t t117 = (uint64_t)(t110 >> (int)n116);
            F.ret = t117;
            goto gm_fret;
        }
gm_lb9:;
        {
            uint64_t t118 = gm_l6;
            uint64_t t119 = (uint64_t)gm_bitreverse(((unsigned __int128)(uint64_t)(t118) & (((unsigned __int128)1 << 64) - 1)), 64);
            uint64_t t120 = t119;
            uint32_t t121 = gm_l4;
            uint32_t r122; bool o123 = __builtin_sub_overflow(((uint32_t)64ULL), t121, &r122);
            if (o123) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 123, 43);
            uint32_t t124 = r122;
            uint64_t t125 = (uint64_t)t124;
            unsigned __int128 n126 = (unsigned __int128)t125;
            if (n126 >= 64) gm_trap_shift((uint64_t)n126, "u64", gm_srch12rfrebvus5, 123, 21);
            uint64_t t127 = (uint64_t)(t120 >> (int)n126);
            F.ret = t127;
            goto gm_fret;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 112, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_float_op_osevuqr(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_op_osevuqr F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    uint8_t gm_l1; /* wide */
    uint64_t gm_l2; /* p */
    uint64_t gm_l3; /* q */
    double gm_l4; /* x */
    double gm_l5; /* y */
    double gm_l6; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    double gm_l7;
    double gm_l8;
    double gm_l9;
    uint64_t gm_l10;
    uint8_t t7 = gm_l1;
    if (t7) {
        uint64_t t8 = gm_l2;
        double t9;
        memset(&t9, 0, sizeof t9); memcpy(&t9, ((uint8_t *)&t8), sizeof t9 < 8 ? sizeof t9 : 8);
        gm_l7 = t9;
    } else {
        uint64_t t10 = gm_l2;
        uint32_t t11 = (uint32_t)t10;
        float t12;
        memset(&t12, 0, sizeof t12); memcpy(&t12, ((uint8_t *)&t11), sizeof t12 < 4 ? sizeof t12 : 4);
        double t13 = (double)t12;
        gm_l7 = t13;
    }
    double t14 = gm_l7;
    gm_l4 = t14;
    uint8_t t15 = gm_l1;
    if (t15) {
        uint64_t t16 = gm_l3;
        double t17;
        memset(&t17, 0, sizeof t17); memcpy(&t17, ((uint8_t *)&t16), sizeof t17 < 8 ? sizeof t17 : 8);
        gm_l8 = t17;
    } else {
        uint64_t t18 = gm_l3;
        uint32_t t19 = (uint32_t)t18;
        float t20;
        memset(&t20, 0, sizeof t20); memcpy(&t20, ((uint8_t *)&t19), sizeof t20 < 4 ? sizeof t20 : 4);
        double t21 = (double)t20;
        gm_l8 = t21;
    }
    double t22 = gm_l8;
    gm_l5 = t22;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
            if (!t24) {
                goto gm_lb2;
            }
            double t25 = gm_l4;
            double t26 = gm_l5;
            double t27 = (t25 + t26);
            gm_l9 = t27;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
            if (!t29) {
                goto gm_lb3;
            }
            double t30 = gm_l4;
            double t31 = gm_l5;
            double t32 = (t30 - t31);
            gm_l9 = t32;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)2ULL));
            if (!t34) {
                goto gm_lb4;
            }
            double t35 = gm_l4;
            double t36 = gm_l5;
            double t37 = (t35 * t36);
            gm_l9 = t37;
            goto gm_lb1;
        }
gm_lb4:;
        {
            double t38 = gm_l4;
            double t39 = gm_l5;
            double t40 = (t38 / t39);
            gm_l9 = t40;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 81, 10);
    }
gm_lb1:;
    double t41 = gm_l9;
    gm_l6 = t41;
    uint8_t t42 = gm_l1;
    if (t42) {
        double t43 = gm_l6;
        uint64_t t44;
        memset(&t44, 0, sizeof t44); memcpy(&t44, ((uint8_t *)&t43), sizeof t44 < 8 ? sizeof t44 : 8);
        gm_l10 = t44;
    } else {
        double t45 = gm_l6;
        float t46 = (float)((double)t45);
        uint32_t t47;
        memset(&t47, 0, sizeof t47); memcpy(&t47, ((uint8_t *)&t46), sizeof t47 < 4 ? sizeof t47 : 4);
        uint64_t t48 = (uint64_t)t47;
        gm_l10 = t48;
    }
    uint64_t t49 = gm_l10;
    F.ret = t49;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_hi64_vi0hvs0(uint32_t a0);
inline __attribute__((aligned(4))) uint64_t gm_f_int_conv_rqdvurn(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, gm_csfd6et11lk21p a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_conv_rqdvurn F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_ttovhfee8mf28 gm_l1; /* from */
    gm_ttovhfee8mf28 gm_l2; /* to */
    uint64_t gm_l3; /* v */
    unsigned __int128 gm_l4; /* x */
    uint64_t gm_l5; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_ttovhfee8mf28, a1);
    gm_l2 = GM_CONV(gm_ttovhfee8mf28, a2);
    gm_l3 = a3;
    unsigned __int128 gm_l6;
    uint64_t gm_l7;
    gm_tvdg94bk7aa22 t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    uint8_t t4 = (uint8_t)((t3 == ((uint8_t)6ULL)));
    gm_tvdg94bk7aa22 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)6ULL));
    if (t4) {
        uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&gm_l1));
        if (t6) {
            uint64_t t7 = gm_l3;
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            int64_t t9 = gm_f_sext_g6f00aa(t7, t8);
            __int128 t10 = (__int128)t9;
            unsigned __int128 t11;
            memset(&t11, 0, sizeof t11); memcpy(&t11, ((uint8_t *)&t10), sizeof t11 < 16 ? sizeof t11 : 16);
            gm_l6 = t11;
        } else {
            uint64_t t12 = gm_l3;
            unsigned __int128 t13 = (unsigned __int128)t12;
            gm_l6 = t13;
        }
        unsigned __int128 t14 = gm_l6;
        gm_l4 = t14;
        uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&gm_l1));
        unsigned __int128 t18 = gm_l4;
        gm_t86h6hhm6d76n t19;
        gm_f_num_val_i3upeur((void *)&t19, t17, t18);
        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_t86h6hhm6d76n t22;
        gm_f_num_sat_66dqcpl((void *)&t22, &t19, t20, t21);
        unsigned __int128 t23 = gm_f_num_bits_onl28vp(&t22);
        unsigned __int128 t24 = gm_f_num_ext_348kmbu(t15, t16, t23);
        uint64_t t25 = (uint64_t)t24;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint64_t t27 = gm_f_mask64_vvoc1f6(t26);
        uint64_t t28 = (uint64_t)(t25 & t27);
        F.ret = t28;
        goto gm_fret;
    }
    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&gm_l1));
    if (t29) {
        uint64_t t30 = gm_l3;
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        int64_t t32 = gm_f_sext_g6f00aa(t30, t31);
        uint64_t t33;
        memset(&t33, 0, sizeof t33); memcpy(&t33, ((uint8_t *)&t32), sizeof t33 < 8 ? sizeof t33 : 8);
        gm_l7 = t33;
    } else {
        uint64_t t34 = gm_l3;
        gm_l7 = t34;
    }
    uint64_t t35 = gm_l7;
    gm_l5 = t35;
    uint64_t t36 = gm_l5;
    uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
    uint64_t t38 = gm_f_mask64_vvoc1f6(t37);
    uint64_t t39 = (uint64_t)(t36 & t38);
    F.ret = t39;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_int_done_am7kd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint8_t a5, uint8_t a6, gm_b16_8 a7) {
    struct gm_fr_int_done_am7kd5g F;
    void * gm_l0; /* m */
    void * gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_ttovhfee8mf28 gm_l3; /* t */
    uint64_t gm_l4; /* low */
    uint8_t gm_l5; /* over */
    uint8_t gm_l6; /* below */
    gm_b16_8 gm_l7; /* sym */
    uint64_t gm_l8; /* mask */
    GM_STACK_CHECK(&F, 381u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_tdlro53ljvkl0 gm_l9;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    uint64_t t3 = gm_f_mask64_vvoc1f6(t2);
    gm_l8 = t3;
    uint8_t t4 = gm_l5;
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        uint64_t t6 = gm_l4;
        uint64_t t7 = gm_l8;
        uint64_t t8 = (uint64_t)(t6 & t7);
        gm_t833u93mr4iob t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
        ((uint8_t *)&t9)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t9 + 8), t8);
        memmove(gm_out, ((uint8_t *)&t9), 32);
        goto gm_fdone;
    }
    void * t10 = gm_l0;
    void * t11 = gm_l1;
    gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t11) + 72));
    gm_t9qkh0rsooc8d t13 = gm_l2;
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    uint64_t t16 = gm_l4;
    unsigned __int128 t17 = (unsigned __int128)t16;
    uint8_t t18 = gm_l6;
    gm_b16_8 t19 = gm_l7;
    gm_f_overflow_result_vqlhh7t((void *)&gm_l9, (void *)NULL, cx, NULL, t10, t12, t13, t14, t15, t17, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t20) {
    } else {
        gm_t76123gfet73g t21 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l9) + 16));
        gm_t833u93mr4iob t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        ((uint8_t *)&t22)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t22 + 8), t21);
        memmove(gm_out, ((uint8_t *)&t22), 32);
        goto gm_fdone;
    }
    unsigned __int128 t23 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l9) + 16));
    uint64_t t24 = (uint64_t)t23;
    uint64_t t25 = gm_l8;
    uint64_t t26 = (uint64_t)(t24 & t25);
    gm_t833u93mr4iob t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t27 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
    ((uint8_t *)&t27)[0] = 0;
    GM_ST(uint64_t, ((uint8_t *)&t27 + 8), t26);
    memmove(gm_out, ((uint8_t *)&t27), 32);
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
inline __attribute__((aligned(4))) void gm_f_int_un_sm4tv7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_ttovhfee8mf28 a2, uint64_t a3, void * a4) {
    struct gm_fr_int_un_sm4tv7d F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    gm_ttovhfee8mf28 gm_l2; /* t */
    uint64_t gm_l3; /* v */
    void * gm_l4; /* n */
    uint64_t gm_l5; /* mask */
    int64_t gm_l6; /* x */
    GM_STACK_CHECK(&F, 654u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tcfa6l7reoosn gm_l7;
    gm_tcfa6l7reoosn gm_l8;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
    uint64_t t3 = gm_f_mask64_vvoc1f6(t2);
    gm_l5 = t3;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    if (t6) {
        uint64_t t8 = gm_l3;
        uint64_t t9 = (uint64_t)~t8;
        uint64_t t10 = gm_l5;
        uint64_t t11 = (uint64_t)(t9 & t10);
        gm_t833u93mr4iob t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
        ((uint8_t *)&t12)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t12 + 8), t11);
        memmove(gm_out, ((uint8_t *)&t12), 32);
        goto gm_fdone;
    }
    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
    if (t13) {
        uint64_t t14 = gm_l3;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        int64_t t16 = gm_f_sext_g6f00aa(t14, t15);
        gm_l6 = t16;
        int64_t t17 = gm_l6;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        int64_t t19 = gm_f_lo64_91giin8(t18);
        uint8_t t20 = ((t17 == t19));
        if (t20) {
            void * t21 = gm_l0;
            void * t22 = gm_l4;
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
            gm_f_checked_overflow_ma1bm1g((void *)&gm_l7, (void *)NULL, cx, NULL, t21, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (t25) {
            } else {
                gm_t76123gfet73g t26 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l7) + 8));
                gm_t833u93mr4iob t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                ((uint8_t *)&t27)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), t26);
                memmove(gm_out, ((uint8_t *)&t27), 32);
                goto gm_fdone;
            }
            uint64_t t28 = gm_l3;
            gm_t833u93mr4iob t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
            ((uint8_t *)&t29)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t29 + 8), t28);
            memmove(gm_out, ((uint8_t *)&t29), 32);
            goto gm_fdone;
        }
        int64_t t30 = gm_l6;
        int64_t r31; bool o32 = __builtin_sub_overflow(((int64_t)0LL), t30, &r31);
        int64_t t33 = r31;
        uint64_t t34;
        memset(&t34, 0, sizeof t34); memcpy(&t34, ((uint8_t *)&t33), sizeof t34 < 8 ? sizeof t34 : 8);
        uint64_t t35 = gm_l5;
        uint64_t t36 = (uint64_t)(t34 & t35);
        gm_t833u93mr4iob t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 24, 0);
        ((uint8_t *)&t37)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t37 + 8), t36);
        memmove(gm_out, ((uint8_t *)&t37), 32);
        goto gm_fdone;
    }
    uint64_t t38 = gm_l3;
    uint8_t t39 = ((t38 == ((uint64_t)0ULL)));
    if (t39) {
        gm_t833u93mr4iob t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
        ((uint8_t *)&t40)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t40 + 8), ((uint64_t)0ULL));
        memmove(gm_out, ((uint8_t *)&t40), 32);
        goto gm_fdone;
    }
    void * t41 = gm_l0;
    void * t42 = gm_l4;
    gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 72));
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(1));
    gm_f_checked_overflow_ma1bm1g((void *)&gm_l8, (void *)NULL, cx, NULL, t41, t43, t44);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t45) {
    } else {
        gm_t76123gfet73g t46 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l8) + 8));
        gm_t833u93mr4iob t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        ((uint8_t *)&t47)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t47 + 8), t46);
        memmove(gm_out, ((uint8_t *)&t47), 32);
        goto gm_fdone;
    }
    uint64_t t48 = gm_l3;
    uint64_t r49; bool o50 = __builtin_sub_overflow(((uint64_t)0ULL), t48, &r49);
    uint64_t t51 = r49;
    uint64_t t52 = gm_l5;
    uint64_t t53 = (uint64_t)(t51 & t52);
    gm_t833u93mr4iob t54;
    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t54 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t54 + 24, 0);
    ((uint8_t *)&t54)[0] = 0;
    GM_ST(uint64_t, ((uint8_t *)&t54 + 8), t53);
    memmove(gm_out, ((uint8_t *)&t54), 32);
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
__attribute__((aligned(4))) void gm_f_int_word_ll0f7kf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, gm_ttovhfee8mf28 a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint64_t a5, void * a6) {
    struct gm_fr_int_word_ll0f7kf F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_ttovhfee8mf28 gm_l2; /* t */
    gm_ttovhfee8mf28 gm_l3; /* shift */
    uint64_t gm_l4; /* p */
    uint64_t gm_l5; /* q */
    void * gm_l6; /* n */
    uint64_t gm_l7; /* mask */
    uint32_t gm_l8; /* bits */
    gm_t9qkh0rsooc8d gm_l9; /* fl */
    int64_t gm_l10; /* x */
    int64_t gm_l11; /* y */
    int64_t gm_l12; /* s */
    uint8_t gm_l13; /* over */
    uint64_t gm_l14; /* s */
    uint8_t gm_l15; /* over */
    gm_t9qkh0rsooc8d gm_l16; /* fl */
    int64_t gm_l17; /* x */
    int64_t gm_l18; /* y */
    int64_t gm_l19; /* s */
    uint8_t gm_l20; /* over */
    gm_t9qkh0rsooc8d gm_l21; /* fl */
    int64_t gm_l22; /* x */
    int64_t gm_l23; /* y */
    int64_t gm_l24; /* s */
    __int128 gm_l25; /* s */
    uint64_t gm_l26; /* s */
    unsigned __int128 gm_l27; /* s */
    gm_t9qkh0rsooc8d gm_l28; /* fl */
    uint32_t gm_l29; /* k */
    uint64_t gm_l30; /* low */
    int64_t gm_l31; /* v */
    int64_t gm_l32; /* w */
    uint32_t gm_l33; /* k */
    uint8_t gm_l34; /* is_div */
    int64_t gm_l35; /* x */
    int64_t gm_l36; /* y */
    GM_STACK_CHECK(&F, 3308u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    uint8_t gm_l37;
    uint8_t gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    gm_tcula66ipo514 gm_l44;
    gm_tcula66ipo514 gm_l45;
    gm_t833u93mr4iob gm_l46;
    gm_t833u93mr4iob gm_l47;
    int64_t gm_l48;
    {
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint64_t t17 = gm_f_mask64_vvoc1f6(t16);
        gm_l7 = t17;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_l8 = t18;
        {
            {
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
                    if (!t20) {
                        goto gm_lb2;
                    }
                    gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                    gm_l9 = t21;
                    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
                    if (t22) {
                        uint64_t t23 = gm_l4;
                        uint32_t t24 = gm_l8;
                        int64_t t25 = gm_f_sext_g6f00aa(t23, t24);
                        gm_l10 = t25;
                        uint64_t t26 = gm_l5;
                        uint32_t t27 = gm_l8;
                        int64_t t28 = gm_f_sext_g6f00aa(t26, t27);
                        gm_l11 = t28;
                        int64_t t29 = gm_l10;
                        int64_t t30 = gm_l11;
                        int64_t r31; bool o32 = __builtin_add_overflow(t29, t30, &r31);
                        int64_t t33 = r31;
                        gm_l12 = t33;
                        uint32_t t34 = gm_l8;
                        uint8_t t35 = (t34 >= ((uint32_t)64ULL));
                        if (t35) {
                            int64_t t36 = gm_l10;
                            int64_t t37 = gm_l12;
                            int64_t t38 = (int64_t)(t36 ^ t37);
                            int64_t t39 = gm_l11;
                            int64_t t40 = gm_l12;
                            int64_t t41 = (int64_t)(t39 ^ t40);
                            int64_t t42 = (int64_t)(t38 & t41);
                            uint8_t t43 = (t42 < ((int64_t)0LL));
                            gm_l37 = t43;
                        } else {
                            int64_t t44 = gm_l12;
                            uint32_t t45 = gm_l8;
                            int64_t t46 = gm_f_lo64_91giin8(t45);
                            uint8_t t47 = (t44 < t46);
                            if (t47) {
                                gm_l38 = ((uint8_t)1);
                            } else {
                                int64_t t48 = gm_l12;
                                uint32_t t49 = gm_l8;
                                int64_t t50 = gm_f_hi64_vi0hvs0(t49);
                                uint8_t t51 = (t48 > t50);
                                gm_l38 = t51;
                            }
                            uint8_t t52 = gm_l38;
                            gm_l37 = t52;
                        }
                        uint8_t t53 = gm_l37;
                        gm_l13 = t53;
                        void * t54 = gm_l0;
                        void * t55 = gm_l6;
                        gm_t9qkh0rsooc8d t56 = gm_l9;
                        gm_ttovhfee8mf28 t57 = gm_l2;
                        int64_t t58 = gm_l12;
                        uint64_t t59;
                        memset(&t59, 0, sizeof t59); memcpy(&t59, ((uint8_t *)&t58), sizeof t59 < 8 ? sizeof t59 : 8);
                        uint8_t t60 = gm_l13;
                        int64_t t61 = gm_l11;
                        uint8_t t62 = (t61 < ((int64_t)0LL));
                        gm_b16_8 t63;
                        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sajhum9b4nr96));
                        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
                        gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t54, t55, t56, t57, t59, t60, t62, t63);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    uint64_t t64 = gm_l4;
                    uint64_t t65 = gm_l5;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t64, t65, &r66);
                    uint64_t t68 = r66;
                    gm_l14 = t68;
                    uint32_t t69 = gm_l8;
                    uint8_t t70 = (t69 >= ((uint32_t)64ULL));
                    if (t70) {
                        uint64_t t71 = gm_l14;
                        uint64_t t72 = gm_l4;
                        uint8_t t73 = (t71 < t72);
                        gm_l39 = t73;
                    } else {
                        uint64_t t74 = gm_l14;
                        uint64_t t75 = gm_l7;
                        uint8_t t76 = (t74 > t75);
                        gm_l39 = t76;
                    }
                    uint8_t t77 = gm_l39;
                    gm_l15 = t77;
                    void * t78 = gm_l0;
                    void * t79 = gm_l6;
                    gm_t9qkh0rsooc8d t80 = gm_l9;
                    gm_ttovhfee8mf28 t81 = gm_l2;
                    uint64_t t82 = gm_l14;
                    uint8_t t83 = gm_l15;
                    gm_b16_8 t84;
                    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sajhum9b4nr96));
                    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(1));
                    gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t78, t79, t80, t81, t82, t83, ((uint8_t)0), t84);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)1ULL));
                    if (!t86) {
                        goto gm_lb3;
                    }
                    gm_t9qkh0rsooc8d t87 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                    gm_l16 = t87;
                    uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
                    if (t88) {
                        uint64_t t89 = gm_l4;
                        uint32_t t90 = gm_l8;
                        int64_t t91 = gm_f_sext_g6f00aa(t89, t90);
                        gm_l17 = t91;
                        uint64_t t92 = gm_l5;
                        uint32_t t93 = gm_l8;
                        int64_t t94 = gm_f_sext_g6f00aa(t92, t93);
                        gm_l18 = t94;
                        int64_t t95 = gm_l17;
                        int64_t t96 = gm_l18;
                        int64_t r97; bool o98 = __builtin_sub_overflow(t95, t96, &r97);
                        int64_t t99 = r97;
                        gm_l19 = t99;
                        uint32_t t100 = gm_l8;
                        uint8_t t101 = (t100 >= ((uint32_t)64ULL));
                        if (t101) {
                            int64_t t102 = gm_l17;
                            int64_t t103 = gm_l18;
                            int64_t t104 = (int64_t)(t102 ^ t103);
                            int64_t t105 = gm_l17;
                            int64_t t106 = gm_l19;
                            int64_t t107 = (int64_t)(t105 ^ t106);
                            int64_t t108 = (int64_t)(t104 & t107);
                            uint8_t t109 = (t108 < ((int64_t)0LL));
                            gm_l40 = t109;
                        } else {
                            int64_t t110 = gm_l19;
                            uint32_t t111 = gm_l8;
                            int64_t t112 = gm_f_lo64_91giin8(t111);
                            uint8_t t113 = (t110 < t112);
                            if (t113) {
                                gm_l41 = ((uint8_t)1);
                            } else {
                                int64_t t114 = gm_l19;
                                uint32_t t115 = gm_l8;
                                int64_t t116 = gm_f_hi64_vi0hvs0(t115);
                                uint8_t t117 = (t114 > t116);
                                gm_l41 = t117;
                            }
                            uint8_t t118 = gm_l41;
                            gm_l40 = t118;
                        }
                        uint8_t t119 = gm_l40;
                        gm_l20 = t119;
                        void * t120 = gm_l0;
                        void * t121 = gm_l6;
                        gm_t9qkh0rsooc8d t122 = gm_l16;
                        gm_ttovhfee8mf28 t123 = gm_l2;
                        int64_t t124 = gm_l19;
                        uint64_t t125;
                        memset(&t125, 0, sizeof t125); memcpy(&t125, ((uint8_t *)&t124), sizeof t125 < 8 ? sizeof t125 : 8);
                        uint8_t t126 = gm_l20;
                        int64_t t127 = gm_l18;
                        uint8_t t128 = (t127 > ((int64_t)0LL));
                        gm_b16_8 t129;
                        GM_ST(void *, (uint8_t *)&t129, (void *)(gm_skolmnfc0ccbd));
                        GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(1));
                        gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t120, t121, t122, t123, t125, t126, t128, t129);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    void * t130 = gm_l0;
                    void * t131 = gm_l6;
                    gm_t9qkh0rsooc8d t132 = gm_l16;
                    gm_ttovhfee8mf28 t133 = gm_l2;
                    uint64_t t134 = gm_l4;
                    uint64_t t135 = gm_l5;
                    uint64_t r136; bool o137 = __builtin_sub_overflow(t134, t135, &r136);
                    uint64_t t138 = r136;
                    uint64_t t139 = gm_l4;
                    uint64_t t140 = gm_l5;
                    uint8_t t141 = (t139 < t140);
                    gm_b16_8 t142;
                    GM_ST(void *, (uint8_t *)&t142, (void *)(gm_skolmnfc0ccbd));
                    GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(1));
                    gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t130, t131, t132, t133, t138, t141, ((uint8_t)1), t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb3:;
            }
            {
                {
                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t144 = (uint8_t)(t143 == ((uint8_t)2ULL));
                    if (!t144) {
                        goto gm_lb4;
                    }
                    gm_t9qkh0rsooc8d t145 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                    gm_l21 = t145;
                    uint8_t t146 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
                    if (t146) {
                        uint64_t t147 = gm_l4;
                        uint32_t t148 = gm_l8;
                        int64_t t149 = gm_f_sext_g6f00aa(t147, t148);
                        gm_l22 = t149;
                        uint64_t t150 = gm_l5;
                        uint32_t t151 = gm_l8;
                        int64_t t152 = gm_f_sext_g6f00aa(t150, t151);
                        gm_l23 = t152;
                        uint32_t t153 = gm_l8;
                        uint8_t t154 = (t153 <= ((uint32_t)32ULL));
                        if (t154) {
                            int64_t t155 = gm_l22;
                            int64_t t156 = gm_l23;
                            int64_t r157; bool o158 = __builtin_mul_overflow(t155, t156, &r157);
                            if (o158) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 177, 26);
                            int64_t t159 = r157;
                            gm_l24 = t159;
                            void * t160 = gm_l0;
                            void * t161 = gm_l6;
                            gm_t9qkh0rsooc8d t162 = gm_l21;
                            gm_ttovhfee8mf28 t163 = gm_l2;
                            int64_t t164 = gm_l24;
                            uint64_t t165;
                            memset(&t165, 0, sizeof t165); memcpy(&t165, ((uint8_t *)&t164), sizeof t165 < 8 ? sizeof t165 : 8);
                            int64_t t166 = gm_l24;
                            uint32_t t167 = gm_l8;
                            int64_t t168 = gm_f_lo64_91giin8(t167);
                            uint8_t t169 = (t166 < t168);
                            if (t169) {
                                gm_l42 = ((uint8_t)1);
                            } else {
                                int64_t t170 = gm_l24;
                                uint32_t t171 = gm_l8;
                                int64_t t172 = gm_f_hi64_vi0hvs0(t171);
                                uint8_t t173 = (t170 > t172);
                                gm_l42 = t173;
                            }
                            uint8_t t174 = gm_l42;
                            int64_t t175 = gm_l24;
                            uint8_t t176 = (t175 < ((int64_t)0LL));
                            gm_b16_8 t177;
                            GM_ST(void *, (uint8_t *)&t177, (void *)(gm_s6s96daqmhcs9));
                            GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(1));
                            gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t160, t161, t162, t163, t165, t174, t176, t177);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
                        int64_t t178 = gm_l22;
                        __int128 t179 = (__int128)t178;
                        int64_t t180 = gm_l23;
                        __int128 t181 = (__int128)t180;
                        __int128 r182; bool o183 = __builtin_mul_overflow(t179, t181, &r182);
                        if (o183) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 180, 22);
                        __int128 t184 = r182;
                        gm_l25 = t184;
                        void * t185 = gm_l0;
                        void * t186 = gm_l6;
                        gm_t9qkh0rsooc8d t187 = gm_l21;
                        gm_ttovhfee8mf28 t188 = gm_l2;
                        __int128 t189 = gm_l25;
                        int64_t t190 = (int64_t)t189;
                        uint64_t t191;
                        memset(&t191, 0, sizeof t191); memcpy(&t191, ((uint8_t *)&t190), sizeof t191 < 8 ? sizeof t191 : 8);
                        __int128 t192 = gm_l25;
                        uint32_t t193 = gm_l8;
                        int64_t t194 = gm_f_lo64_91giin8(t193);
                        __int128 t195 = (__int128)t194;
                        uint8_t t196 = (t192 < t195);
                        if (t196) {
                            gm_l43 = ((uint8_t)1);
                        } else {
                            __int128 t197 = gm_l25;
                            uint32_t t198 = gm_l8;
                            int64_t t199 = gm_f_hi64_vi0hvs0(t198);
                            __int128 t200 = (__int128)t199;
                            uint8_t t201 = (t197 > t200);
                            gm_l43 = t201;
                        }
                        uint8_t t202 = gm_l43;
                        __int128 t203 = gm_l25;
                        uint8_t t204 = (t203 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                        gm_b16_8 t205;
                        GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s6s96daqmhcs9));
                        GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(1));
                        gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t185, t186, t187, t188, t191, t202, t204, t205);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    uint32_t t206 = gm_l8;
                    uint8_t t207 = (t206 <= ((uint32_t)32ULL));
                    if (t207) {
                        uint64_t t208 = gm_l4;
                        uint64_t t209 = gm_l5;
                        uint64_t r210; bool o211 = __builtin_mul_overflow(t208, t209, &r210);
                        if (o211) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 184, 22);
                        uint64_t t212 = r210;
                        gm_l26 = t212;
                        void * t213 = gm_l0;
                        void * t214 = gm_l6;
                        gm_t9qkh0rsooc8d t215 = gm_l21;
                        gm_ttovhfee8mf28 t216 = gm_l2;
                        uint64_t t217 = gm_l26;
                        uint64_t t218 = gm_l26;
                        uint64_t t219 = gm_l7;
                        uint8_t t220 = (t218 > t219);
                        gm_b16_8 t221;
                        GM_ST(void *, (uint8_t *)&t221, (void *)(gm_s6s96daqmhcs9));
                        GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(1));
                        gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t213, t214, t215, t216, t217, t220, ((uint8_t)0), t221);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    uint64_t t222 = gm_l4;
                    unsigned __int128 t223 = (unsigned __int128)t222;
                    uint64_t t224 = gm_l5;
                    unsigned __int128 t225 = (unsigned __int128)t224;
                    unsigned __int128 r226; bool o227 = __builtin_mul_overflow(t223, t225, &r226);
                    if (o227) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 187, 18);
                    unsigned __int128 t228 = r226;
                    gm_l27 = t228;
                    void * t229 = gm_l0;
                    void * t230 = gm_l6;
                    gm_t9qkh0rsooc8d t231 = gm_l21;
                    gm_ttovhfee8mf28 t232 = gm_l2;
                    unsigned __int128 t233 = gm_l27;
                    uint64_t t234 = (uint64_t)t233;
                    unsigned __int128 t235 = gm_l27;
                    uint64_t t236 = gm_l7;
                    unsigned __int128 t237 = (unsigned __int128)t236;
                    uint8_t t238 = (t235 > t237);
                    gm_b16_8 t239;
                    GM_ST(void *, (uint8_t *)&t239, (void *)(gm_s6s96daqmhcs9));
                    GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(1));
                    gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t229, t230, t231, t232, t234, t238, ((uint8_t)0), t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t241 = (uint8_t)(t240 == ((uint8_t)3ULL));
                    if (!t241) {
                        goto gm_lb5;
                    }
                    gm_t9qkh0rsooc8d t242 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                    gm_l28 = t242;
                    void * t243 = gm_l0;
                    void * t244 = gm_l6;
                    gm_ttovhfee8mf28 t245 = gm_l3;
                    uint64_t t246 = gm_l5;
                    uint32_t t247 = gm_l8;
                    gm_f_shift_word_svovdvk((void *)&gm_l44, (void *)NULL, cx, NULL, t243, t244, t245, t246, t247);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                    if (t248) {
                    } else {
                        gm_t76123gfet73g t249 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l44) + 8));
                        gm_t833u93mr4iob t250;
                        GM_ST(uint8_t, (uint8_t *)&t250 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t250 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t250 + 4, 0);
                        ((uint8_t *)&t250)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t250 + 8), t249);
                        memmove(gm_out, ((uint8_t *)&t250), 32);
                        goto gm_fdone;
                    }
                    uint32_t t251 = GM_LD(uint32_t, (((uint8_t *)&gm_l44) + 8));
                    gm_l29 = t251;
                    uint8_t t252 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
                    uint8_t t253 = (uint8_t)!t252;
                    if (t253) {
                        uint64_t t254 = gm_l4;
                        uint32_t t255 = gm_l29;
                        uint64_t t256 = (uint64_t)t255;
                        unsigned __int128 n257 = (unsigned __int128)t256;
                        if (n257 >= 64) gm_trap_shift((uint64_t)n257, "u64", gm_srch12rfrebvus5, 193, 25);
                        uint64_t r258 = (uint64_t)((uint64_t)t254 << (int)n257);
                        bool o259 = (uint64_t)(r258 >> (int)n257) != t254;
                        uint64_t t260 = r258;
                        uint64_t t261 = gm_l7;
                        uint64_t t262 = (uint64_t)(t260 & t261);
                        gm_l30 = t262;
                        void * t263 = gm_l0;
                        void * t264 = gm_l6;
                        gm_t9qkh0rsooc8d t265 = gm_l28;
                        gm_ttovhfee8mf28 t266 = gm_l2;
                        uint64_t t267 = gm_l30;
                        uint64_t t268 = gm_l30;
                        uint32_t t269 = gm_l29;
                        uint64_t t270 = (uint64_t)t269;
                        unsigned __int128 n271 = (unsigned __int128)t270;
                        if (n271 >= 64) gm_trap_shift((uint64_t)n271, "u64", gm_srch12rfrebvus5, 194, 51);
                        uint64_t t272 = (uint64_t)(t268 >> (int)n271);
                        uint64_t t273 = gm_l4;
                        uint8_t t274 = (!(t272 == t273));
                        gm_b16_8 t275;
                        GM_ST(void *, (uint8_t *)&t275, (void *)(gm_si62ejtp0vbct));
                        GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(2));
                        gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t263, t264, t265, t266, t267, t274, ((uint8_t)0), t275);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    uint64_t t276 = gm_l4;
                    uint32_t t277 = gm_l8;
                    int64_t t278 = gm_f_sext_g6f00aa(t276, t277);
                    gm_l31 = t278;
                    int64_t t279 = gm_l31;
                    uint64_t t280;
                    memset(&t280, 0, sizeof t280); memcpy(&t280, ((uint8_t *)&t279), sizeof t280 < 8 ? sizeof t280 : 8);
                    uint32_t t281 = gm_l29;
                    uint64_t t282 = (uint64_t)t281;
                    unsigned __int128 n283 = (unsigned __int128)t282;
                    if (n283 >= 64) gm_trap_shift((uint64_t)n283, "u64", gm_srch12rfrebvus5, 197, 24);
                    uint64_t r284 = (uint64_t)((uint64_t)t280 << (int)n283);
                    bool o285 = (uint64_t)(r284 >> (int)n283) != t280;
                    uint64_t t286 = r284;
                    uint64_t t287 = gm_l7;
                    uint64_t t288 = (uint64_t)(t286 & t287);
                    uint32_t t289 = gm_l8;
                    int64_t t290 = gm_f_sext_g6f00aa(t288, t289);
                    gm_l32 = t290;
                    void * t291 = gm_l0;
                    void * t292 = gm_l6;
                    gm_t9qkh0rsooc8d t293 = gm_l28;
                    gm_ttovhfee8mf28 t294 = gm_l2;
                    int64_t t295 = gm_l32;
                    uint64_t t296;
                    memset(&t296, 0, sizeof t296); memcpy(&t296, ((uint8_t *)&t295), sizeof t296 < 8 ? sizeof t296 : 8);
                    int64_t t297 = gm_l32;
                    uint32_t t298 = gm_l29;
                    unsigned __int128 n299 = (unsigned __int128)t298;
                    if (n299 >= 64) gm_trap_shift((uint64_t)n299, "i64", gm_srch12rfrebvus5, 198, 55);
                    int64_t t300 = (int64_t)(t297 >> (int)n299);
                    int64_t t301 = gm_l31;
                    uint8_t t302 = (!(t300 == t301));
                    int64_t t303 = gm_l31;
                    uint8_t t304 = (t303 < ((int64_t)0LL));
                    gm_b16_8 t305;
                    GM_ST(void *, (uint8_t *)&t305, (void *)(gm_si62ejtp0vbct));
                    GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(2));
                    gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t291, t292, t293, t294, t296, t302, t304, t305);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t306 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t307 = (uint8_t)(t306 == ((uint8_t)6ULL));
                    if (!t307) {
                        goto gm_lb6;
                    }
                    void * t308 = gm_l0;
                    void * t309 = gm_l6;
                    gm_ttovhfee8mf28 t310 = gm_l3;
                    uint64_t t311 = gm_l5;
                    uint32_t t312 = gm_l8;
                    gm_f_shift_word_svovdvk((void *)&gm_l45, (void *)NULL, cx, NULL, t308, t309, t310, t311, t312);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t313 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                    if (t313) {
                    } else {
                        gm_t76123gfet73g t314 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l45) + 8));
                        gm_t833u93mr4iob t315;
                        GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t315 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
                        ((uint8_t *)&t315)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t315 + 8), t314);
                        memmove(gm_out, ((uint8_t *)&t315), 32);
                        goto gm_fdone;
                    }
                    uint32_t t316 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 8));
                    gm_l33 = t316;
                    uint8_t t317 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
                    if (t317) {
                        uint64_t t318 = gm_l4;
                        uint32_t t319 = gm_l8;
                        int64_t t320 = gm_f_sext_g6f00aa(t318, t319);
                        uint32_t t321 = gm_l33;
                        unsigned __int128 n322 = (unsigned __int128)t321;
                        if (n322 >= 64) gm_trap_shift((uint64_t)n322, "i64", gm_srch12rfrebvus5, 202, 35);
                        int64_t t323 = (int64_t)(t320 >> (int)n322);
                        uint64_t t324;
                        memset(&t324, 0, sizeof t324); memcpy(&t324, ((uint8_t *)&t323), sizeof t324 < 8 ? sizeof t324 : 8);
                        uint64_t t325 = gm_l7;
                        uint64_t t326 = (uint64_t)(t324 & t325);
                        gm_t833u93mr4iob t327;
                        GM_ST(uint8_t, (uint8_t *)&t327 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t327 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t327 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t327 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t327 + 24, 0);
                        ((uint8_t *)&t327)[0] = 0;
                        GM_ST(uint64_t, ((uint8_t *)&t327 + 8), t326);
                        memmove(gm_out, ((uint8_t *)&t327), 32);
                        goto gm_fdone;
                    }
                    uint64_t t328 = gm_l4;
                    uint32_t t329 = gm_l33;
                    uint64_t t330 = (uint64_t)t329;
                    unsigned __int128 n331 = (unsigned __int128)t330;
                    if (n331 >= 64) gm_trap_shift((uint64_t)n331, "u64", gm_srch12rfrebvus5, 203, 20);
                    uint64_t t332 = (uint64_t)(t328 >> (int)n331);
                    gm_t833u93mr4iob t333;
                    GM_ST(uint8_t, (uint8_t *)&t333 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t333 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t333 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t333 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t333 + 24, 0);
                    ((uint8_t *)&t333)[0] = 0;
                    GM_ST(uint64_t, ((uint8_t *)&t333 + 8), t332);
                    memmove(gm_out, ((uint8_t *)&t333), 32);
                    goto gm_fdone;
                }
gm_lb6:;
                {
                    {
                        {
                            uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t335 = (uint8_t)(t334 == ((uint8_t)4ULL));
                            if (!t335) {
                                goto gm_lb9;
                            }
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t337 = (uint8_t)(t336 == ((uint8_t)5ULL));
                            if (!t337) {
                                goto gm_lb10;
                            }
                            goto gm_lb8;
                        }
gm_lb10:;
                        goto gm_lb7;
                    }
gm_lb8:;
                    uint64_t t338 = gm_l5;
                    uint8_t t339 = ((t338 == ((uint64_t)0ULL)));
                    if (t339) {
                        void * t340 = gm_l0;
                        void * t341 = gm_l6;
                        gm_tl874t2atnlh3 t342 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t341) + 72));
                        gm_t76123gfet73g t343;
                        gm_f_div_zero_trap_ap1saqd((void *)&t343, (void *)NULL, cx, NULL, t340, t342);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t833u93mr4iob t344;
                        GM_ST(uint8_t, (uint8_t *)&t344 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t344 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t344 + 4, 0);
                        ((uint8_t *)&t344)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t344 + 8), t343);
                        memmove(gm_out, ((uint8_t *)&t344), 32);
                        goto gm_fdone;
                    }
                    gm_ts6oqppjh0ba3 t345 = gm_l1;
                    uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&t345) + 0));
                    uint8_t t347 = (uint8_t)((t346 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t348;
                    GM_ST(uint8_t, (uint8_t *)&t348 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)4ULL));
                    gm_l34 = t347;
                    uint8_t t349 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
                    uint8_t t350 = (uint8_t)!t349;
                    if (t350) {
                        uint8_t t351 = gm_l34;
                        if (t351) {
                            uint64_t t352 = gm_l4;
                            uint64_t t353 = gm_l5;
                            if (t353 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 208, 50);
                            uint64_t t354 = (uint64_t)(t352 / t353);
                            gm_t833u93mr4iob t355;
                            GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t355 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t355 + 24, 0);
                            ((uint8_t *)&t355)[0] = 0;
                            GM_ST(uint64_t, ((uint8_t *)&t355 + 8), t354);
                            gm_l46 = t355;
                        } else {
                            uint64_t t356 = gm_l4;
                            uint64_t t357 = gm_l5;
                            if (t357 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 208, 65);
                            uint64_t t358 = (uint64_t)(t356 % t357);
                            gm_t833u93mr4iob t359;
                            GM_ST(uint8_t, (uint8_t *)&t359 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t359 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t359 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t359 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t359 + 24, 0);
                            ((uint8_t *)&t359)[0] = 0;
                            GM_ST(uint64_t, ((uint8_t *)&t359 + 8), t358);
                            gm_l46 = t359;
                        }
                        memmove(gm_out, ((uint8_t *)&gm_l46), 32);
                        goto gm_fdone;
                    }
                    uint64_t t360 = gm_l4;
                    uint32_t t361 = gm_l8;
                    int64_t t362 = gm_f_sext_g6f00aa(t360, t361);
                    gm_l35 = t362;
                    uint64_t t363 = gm_l5;
                    uint32_t t364 = gm_l8;
                    int64_t t365 = gm_f_sext_g6f00aa(t363, t364);
                    gm_l36 = t365;
                    int64_t t366 = gm_l36;
                    uint8_t t367 = ((t366 == ((int64_t)-1LL)));
                    if (t367) {
                        int64_t t368 = gm_l35;
                        uint32_t t369 = gm_l8;
                        int64_t t370 = gm_f_lo64_91giin8(t369);
                        uint8_t t371 = ((t368 == t370));
                        if (t371) {
                            void * t372 = gm_l0;
                            void * t373 = gm_l6;
                            gm_tl874t2atnlh3 t374 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t373) + 72));
                            gm_ts6oqppjh0ba3 t375 = gm_l1;
                            gm_b16_8 t376 = gm_f_op_sym_6olbepb(t375);
                            gm_t76123gfet73g t377;
                            gm_f_overflow_trap_j2f8u18((void *)&t377, (void *)NULL, cx, NULL, t372, t374, t376);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t833u93mr4iob t378;
                            GM_ST(uint8_t, (uint8_t *)&t378 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t378 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t378 + 4, 0);
                            ((uint8_t *)&t378)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t378 + 8), t377);
                            memmove(gm_out, ((uint8_t *)&t378), 32);
                            goto gm_fdone;
                        }
                        uint8_t t379 = gm_l34;
                        if (t379) {
                            int64_t t380 = gm_l35;
                            int64_t r381; bool o382 = __builtin_sub_overflow(((int64_t)0LL), t380, &r381);
                            int64_t t383 = r381;
                            uint64_t t384;
                            memset(&t384, 0, sizeof t384); memcpy(&t384, ((uint8_t *)&t383), sizeof t384 < 8 ? sizeof t384 : 8);
                            uint64_t t385 = gm_l7;
                            uint64_t t386 = (uint64_t)(t384 & t385);
                            gm_t833u93mr4iob t387;
                            GM_ST(uint8_t, (uint8_t *)&t387 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t387 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t387 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t387 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t387 + 24, 0);
                            ((uint8_t *)&t387)[0] = 0;
                            GM_ST(uint64_t, ((uint8_t *)&t387 + 8), t386);
                            gm_l47 = t387;
                        } else {
                            gm_t833u93mr4iob t388;
                            GM_ST(uint8_t, (uint8_t *)&t388 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t388 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t388 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t388 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t388 + 24, 0);
                            ((uint8_t *)&t388)[0] = 0;
                            GM_ST(uint64_t, ((uint8_t *)&t388 + 8), ((uint64_t)0ULL));
                            gm_l47 = t388;
                        }
                        memmove(gm_out, ((uint8_t *)&gm_l47), 32);
                        goto gm_fdone;
                    }
                    uint8_t t389 = gm_l34;
                    if (t389) {
                        int64_t t390 = gm_l35;
                        int64_t t391 = gm_l36;
                        if (t391 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 215, 33);
                        if (t390 == ((int64_t)(-9223372036854775807LL - 1)) && t391 == (int64_t)-1) gm_trap("integer overflow in `/`", gm_srch12rfrebvus5, 215, 33);
                        int64_t t392 = (int64_t)(t390 / t391);
                        gm_l48 = t392;
                    } else {
                        int64_t t393 = gm_l35;
                        int64_t t394 = gm_l36;
                        if (t394 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 215, 48);
                        if (t393 == ((int64_t)(-9223372036854775807LL - 1)) && t394 == (int64_t)-1) gm_trap("integer overflow in `%`", gm_srch12rfrebvus5, 215, 48);
                        int64_t t395 = (int64_t)(t393 % t394);
                        gm_l48 = t395;
                    }
                    int64_t t396 = gm_l48;
                    uint64_t t397;
                    memset(&t397, 0, sizeof t397); memcpy(&t397, ((uint8_t *)&t396), sizeof t397 < 8 ? sizeof t397 : 8);
                    uint64_t t398 = gm_l7;
                    uint64_t t399 = (uint64_t)(t397 & t398);
                    gm_t833u93mr4iob t400;
                    GM_ST(uint8_t, (uint8_t *)&t400 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t400 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t400 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t400 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t400 + 24, 0);
                    ((uint8_t *)&t400)[0] = 0;
                    GM_ST(uint64_t, ((uint8_t *)&t400 + 8), t399);
                    memmove(gm_out, ((uint8_t *)&t400), 32);
                    goto gm_fdone;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t401 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t402 = (uint8_t)(t401 == ((uint8_t)7ULL));
                    if (!t402) {
                        goto gm_lb11;
                    }
                    uint64_t t403 = gm_l4;
                    uint64_t t404 = gm_l5;
                    uint64_t t405 = (uint64_t)(t403 & t404);
                    gm_t833u93mr4iob t406;
                    GM_ST(uint8_t, (uint8_t *)&t406 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t406 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t406 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t406 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t406 + 24, 0);
                    ((uint8_t *)&t406)[0] = 0;
                    GM_ST(uint64_t, ((uint8_t *)&t406 + 8), t405);
                    memmove(gm_out, ((uint8_t *)&t406), 32);
                    goto gm_fdone;
                }
gm_lb11:;
                {
                    uint8_t t407 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t408 = (uint8_t)(t407 == ((uint8_t)8ULL));
                    if (!t408) {
                        goto gm_lb12;
                    }
                    uint64_t t409 = gm_l4;
                    uint64_t t410 = gm_l5;
                    uint64_t t411 = (uint64_t)(t409 | t410);
                    gm_t833u93mr4iob t412;
                    GM_ST(uint8_t, (uint8_t *)&t412 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t412 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t412 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t412 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t412 + 24, 0);
                    ((uint8_t *)&t412)[0] = 0;
                    GM_ST(uint64_t, ((uint8_t *)&t412 + 8), t411);
                    memmove(gm_out, ((uint8_t *)&t412), 32);
                    goto gm_fdone;
                }
gm_lb12:;
                {
                    uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t414 = (uint8_t)(t413 == ((uint8_t)9ULL));
                    if (!t414) {
                        goto gm_lb13;
                    }
                    uint64_t t415 = gm_l4;
                    uint64_t t416 = gm_l5;
                    uint64_t t417 = (uint64_t)(t415 ^ t416);
                    gm_t833u93mr4iob t418;
                    GM_ST(uint8_t, (uint8_t *)&t418 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t418 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t418 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t418 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t418 + 24, 0);
                    ((uint8_t *)&t418)[0] = 0;
                    GM_ST(uint64_t, ((uint8_t *)&t418 + 8), t417);
                    memmove(gm_out, ((uint8_t *)&t418), 32);
                    goto gm_fdone;
                }
gm_lb13:;
                {
                    void * t419 = gm_l0;
                    void * t420 = gm_l6;
                    gm_tl874t2atnlh3 t421 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t420) + 72));
                    gm_b16_8 t422;
                    GM_ST(void *, (uint8_t *)&t422, (void *)(gm_s9idfla83gb22));
                    GM_ST(uintptr_t, (uint8_t *)&t422 + 8, (uintptr_t)(58));
                    gm_t76123gfet73g t423;
                    gm_f_trap_qh84l9d((void *)&t423, (void *)NULL, cx, NULL, t419, t421, t422);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t833u93mr4iob t424;
                    GM_ST(uint8_t, (uint8_t *)&t424 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t424 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t424 + 4, 0);
                    ((uint8_t *)&t424)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t424 + 8), t423);
                    memmove(gm_out, ((uint8_t *)&t424), 32);
                    goto gm_fdone;
                }
gm_lb14:;
                gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 149, 5);
            }
        }
gm_lb1:;
    }
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
extern __attribute__((aligned(4))) int64_t gm_f_lo64_91giin8(uint32_t a0);
extern __attribute__((aligned(4))) uint64_t gm_f_mask64_vvoc1f6(uint32_t a0);
extern __attribute__((aligned(4))) int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_shift_word_svovdvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ttovhfee8mf28 a2, uint64_t a3, uint32_t a4) {
    struct gm_fr_shift_word_svovdvk F;
    void * gm_l0; /* m */
    void * gm_l1; /* n */
    gm_ttovhfee8mf28 gm_l2; /* rw */
    uint64_t gm_l3; /* q */
    uint32_t gm_l4; /* bits */
    uint8_t gm_l5; /* neg */
    GM_STACK_CHECK(&F, 250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l6;
    uint64_t gm_l7;
    uint8_t t4 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
    if (t4) {
        uint64_t t5 = gm_l3;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        int64_t t7 = gm_f_sext_g6f00aa(t5, t6);
        uint8_t t8 = (t7 < ((int64_t)0LL));
        gm_l6 = t8;
    } else {
        gm_l6 = ((uint8_t)0);
    }
    uint8_t t9 = gm_l6;
    gm_l5 = t9;
    {
        {
            uint8_t t10 = gm_l5;
            uint8_t t11 = (uint8_t)!t10;
            if (!t11) {
                goto gm_lb2;
            }
            uint64_t t12 = gm_l3;
            uint32_t t13 = gm_l4;
            uint64_t t14 = (uint64_t)t13;
            uint8_t t15 = (t12 < t14);
            if (!t15) {
                goto gm_lb2;
            }
            uint64_t t16 = gm_l3;
            uint32_t t17 = (uint32_t)t16;
            gm_tcula66ipo514 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
            memmove(gm_out, ((uint8_t *)&t18), 32);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t19 = gm_l0;
    void * t20 = gm_l1;
    gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
    void * t22 = gm_l1;
    gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + 56));
    uint8_t t24 = gm_l5;
    uint8_t t25 = gm_l5;
    if (t25) {
        uint64_t t26 = gm_l3;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        int64_t t28 = gm_f_sext_g6f00aa(t26, t27);
        int64_t r29; bool o30 = __builtin_sub_overflow(((int64_t)0LL), t28, &r29);
        int64_t t31 = r29;
        uint64_t t32;
        memset(&t32, 0, sizeof t32); memcpy(&t32, ((uint8_t *)&t31), sizeof t32 < 8 ? sizeof t32 : 8);
        gm_l7 = t32;
    } else {
        uint64_t t33 = gm_l3;
        gm_l7 = t33;
    }
    uint64_t t34 = gm_l7;
    unsigned __int128 t35 = (unsigned __int128)t34;
    uint32_t t36 = gm_l4;
    gm_f_shift_check_a7uaho4((void *)&(*(gm_tcula66ipo514 *)gm_out), (void *)NULL, cx, NULL, t19, t21, t23, t24, t35, t36);
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
extern __attribute__((aligned(4))) uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3);
