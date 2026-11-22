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
typedef struct gm_cs3cuou245tr5f { _Bool f0; _Bool f1; _Bool f2; } gm_cs3cuou245tr5f;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; } gm_t8d5d2ep1fo6t;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; gm_t9qkh0rsooc8d f10; gm_t9qkh0rsooc8d f11; gm_t9qkh0rsooc8d f12; gm_t9qkh0rsooc8d f13; gm_t9qkh0rsooc8d f14; gm_t9qkh0rsooc8d f15; gm_t9qkh0rsooc8d f16; gm_t9qkh0rsooc8d f17; } gm_ta8d7579em356;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; } gm_tfh78knfg7urq;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_tbgd14esf0jvi a[2]; } gm_t0vh9i8m50f41;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8d5d2ep1fo6t) == 4 && _Alignof(gm_t8d5d2ep1fo6t) == 1 && offsetof(gm_t8d5d2ep1fo6t, f0) == 0 && offsetof(gm_t8d5d2ep1fo6t, f1) == 1 && offsetof(gm_t8d5d2ep1fo6t, f2) == 2 && offsetof(gm_t8d5d2ep1fo6t, f3) == 3, "layout of `(Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_ta8d7579em356) == 18 && _Alignof(gm_ta8d7579em356) == 1 && offsetof(gm_ta8d7579em356, f0) == 0 && offsetof(gm_ta8d7579em356, f1) == 1 && offsetof(gm_ta8d7579em356, f2) == 2 && offsetof(gm_ta8d7579em356, f3) == 3 && offsetof(gm_ta8d7579em356, f4) == 4 && offsetof(gm_ta8d7579em356, f5) == 5 && offsetof(gm_ta8d7579em356, f6) == 6 && offsetof(gm_ta8d7579em356, f7) == 7 && offsetof(gm_ta8d7579em356, f8) == 8 && offsetof(gm_ta8d7579em356, f9) == 9 && offsetof(gm_ta8d7579em356, f10) == 10 && offsetof(gm_ta8d7579em356, f11) == 11 && offsetof(gm_ta8d7579em356, f12) == 12 && offsetof(gm_ta8d7579em356, f13) == 13 && offsetof(gm_ta8d7579em356, f14) == 14 && offsetof(gm_ta8d7579em356, f15) == 15 && offsetof(gm_ta8d7579em356, f16) == 16 && offsetof(gm_ta8d7579em356, f17) == 17, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tfh78knfg7urq) == 10 && _Alignof(gm_tfh78knfg7urq) == 1 && offsetof(gm_tfh78knfg7urq, f0) == 0 && offsetof(gm_tfh78knfg7urq, f1) == 1 && offsetof(gm_tfh78knfg7urq, f2) == 2 && offsetof(gm_tfh78knfg7urq, f3) == 3 && offsetof(gm_tfh78knfg7urq, f4) == 4 && offsetof(gm_tfh78knfg7urq, f5) == 5 && offsetof(gm_tfh78knfg7urq, f6) == 6 && offsetof(gm_tfh78knfg7urq, f7) == 7 && offsetof(gm_tfh78knfg7urq, f8) == 8 && offsetof(gm_tfh78knfg7urq, f9) == 9, "layout of `(Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_th2lqbo77pvsc v1; } f1; } gm_thk954m1ol1ig;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v1; } f1; } gm_toc4mrkn2e7o9;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; } gm_tt7vn9ehj0acr;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t0vh9i8m50f41) == 64 && _Alignof(gm_t0vh9i8m50f41) == 8 && offsetof(gm_t0vh9i8m50f41, a) == 0, "layout of `[2]Edit`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; } gm_t41cj6gvba3r8;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; } gm_t72hb7duiv9b4;
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; gm_t3fcbuehh3bs2 f4; } gm_tak1k6bk5qcen;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thk954m1ol1ig) == 32 && _Alignof(gm_thk954m1ol1ig) == 8 && offsetof(gm_thk954m1ol1ig, f0) == 0 && offsetof(gm_thk954m1ol1ig, f1) == 8, "layout of `Abort!CallArgs`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_toc4mrkn2e7o9) == 48 && _Alignof(gm_toc4mrkn2e7o9) == 8 && offsetof(gm_toc4mrkn2e7o9, f0) == 0 && offsetof(gm_toc4mrkn2e7o9, f1) == 8, "layout of `Abort!Block`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tt7vn9ehj0acr) == 3 && _Alignof(gm_tt7vn9ehj0acr) == 1 && offsetof(gm_tt7vn9ehj0acr, f0) == 0 && offsetof(gm_tt7vn9ehj0acr, f1) == 2, "layout of `?BinOp`");
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t41cj6gvba3r8) == 96 && _Alignof(gm_t41cj6gvba3r8) == 8 && offsetof(gm_t41cj6gvba3r8, f0) == 0 && offsetof(gm_t41cj6gvba3r8, f1) == 32 && offsetof(gm_t41cj6gvba3r8, f2) == 64, "layout of `(TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_t72hb7duiv9b4) == 128 && _Alignof(gm_t72hb7duiv9b4) == 8 && offsetof(gm_t72hb7duiv9b4, f0) == 0 && offsetof(gm_t72hb7duiv9b4, f1) == 32 && offsetof(gm_t72hb7duiv9b4, f2) == 64 && offsetof(gm_t72hb7duiv9b4, f3) == 96, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tak1k6bk5qcen) == 160 && _Alignof(gm_tak1k6bk5qcen) == 8 && offsetof(gm_tak1k6bk5qcen, f0) == 0 && offsetof(gm_tak1k6bk5qcen, f1) == 32 && offsetof(gm_tak1k6bk5qcen, f2) == 64 && offsetof(gm_tak1k6bk5qcen, f3) == 96 && offsetof(gm_tak1k6bk5qcen, f4) == 128, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_tao63pk3r18v5 v1; } f1; } gm_t1vei8bd9e9m7;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t1vei8bd9e9m7) == 96 && _Alignof(gm_t1vei8bd9e9m7) == 8 && offsetof(gm_t1vei8bd9e9m7, f0) == 0 && offsetof(gm_t1vei8bd9e9m7, f1) == 8, "layout of `Subscript`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t1vei8bd9e9m7 v1; } f1; } gm_tb1kjfin6jnc2;
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tb1kjfin6jnc2) == 104 && _Alignof(gm_tb1kjfin6jnc2) == 8 && offsetof(gm_tb1kjfin6jnc2, f0) == 0 && offsetof(gm_tb1kjfin6jnc2, f1) == 8, "layout of `Abort!Subscript`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t7te2tdpebbt4 v1; } f1; } gm_t3q68r4ne5jv8;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
_Static_assert(sizeof(gm_t3q68r4ne5jv8) == 152 && _Alignof(gm_t3q68r4ne5jv8) == 8 && offsetof(gm_t3q68r4ne5jv8, f0) == 0 && offsetof(gm_t3q68r4ne5jv8, f1) == 8, "layout of `Abort!Arg`");
struct gm_fr_add_from_7os0rvi {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_add_op_99op354 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_ambiguous_try_else_0v14t0i {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* d */
    gm_t0vh9i8m50f41 l5; /* <temporary array> */
    gm_t0vh9i8m50f41 l6; /* <temporary array> */
    gm_b0_1 ret;
};
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_kw_p0r3sr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binary_41fr5r5 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l3; /* rhs */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_binary_follows_jeb42bi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bit_from_pu2bq19 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tm4pc0emhrv1g l5; /* t */
    gm_t4gut2c9fj1kt l11; /* d */
    gm_tc3oht4ksth9d l12; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_bit_op_p81j9u3 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_can_begin_expr_rmia9bs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_can_begin_type_a3rjrm7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cmp_op_9gj7im4 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_deny_in_head_06d9o22 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_eat_label_7eokqmm {
    gm_frame h;
    gm_t1s75i08qtptd ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_8ckfo47 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ends_expr_after_name_5t7p9lg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_error_expr_dhdi86k {
    gm_frame h;
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1s091rv {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_format_3nkc0o4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k9iv40 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5nk8afu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dsta2ql {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gqrkfjb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o5o20fv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pb6f6q3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_arith_l6b4tpf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_empty_q2f0b93 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_relational_n3goics {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fvvkq82 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lo_fp82gt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_mk_expr_53qj7nd {
    gm_frame h;
    gm_t2gtbh39ptekd l1; /* k */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_mul_from_mlskgvm {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_mul_op_ab6loj8 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_next_id_a7upaf4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_nth_c0r2nvs {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_nth_is_3fgqmcb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_op_text_3o86dat {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_parens_0pje0fd {
    gm_frame h;
    gm_t0vh9i8m50f41 ret;
};
struct gm_fr_parse_add_dh9nvbt {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_arg_t6fq0nq {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    gm_tc3oht4ksth9d l4; /* e */
    gm_tc3oht4ksth9d l5; /* e */
    gm_t3q68r4ne5jv8 ret;
};
struct gm_fr_parse_bang_irmkots {
    gm_frame h;
    void * l0; /* p */
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_t0mfir12leg2g l3; /* rhs */
    gm_tc3oht4ksth9d l4; /* r */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_bit_h83vs72 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_block_expr_0j6b78q {
    gm_frame h;
    gm_t79rbdv4rafe5 l1; /* block */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_break_hehputf {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* label */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_call_args_32e205h {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* args */
    gm_thk954m1ol1ig ret;
};
struct gm_fr_parse_capture_body_fgf89k2 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_cmp_q5clpig {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* first */
    gm_tc3oht4ksth9d l3; /* lhs */
    gm_tc3oht4ksth9d l6; /* rhs */
    gm_tc3oht4ksth9d l7; /* e */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_continue_ertnf67 {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* label */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_dot_suffix_1fk204i {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_else_smoqgg4 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_t1s75i08qtptd l2; /* capture */
    gm_tc3oht4ksth9d l3; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_expr_bkb2n6e {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fallback_g1vpkra {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_head_expr_6sofdhn {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_import_s1hv231 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_intrinsic_4ou54h3 {
    gm_frame h;
    gm_tm4pc0emhrv1g l1; /* t */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_logic_ajejref {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_mul_35e1t49 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_not_a8b3gjf {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* operand */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_paren_sq1guoi {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* elems */
    gm_tc3oht4ksth9d l8; /* value */
    gm_t1s75i08qtptd l9; /* name */
    gm_tc3oht4ksth9d l10; /* value */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_postfix_ao4kstr {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* base */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_postfix_ops_2p1csn3 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_tc3oht4ksth9d l3; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_prefix_671fi6d {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* operand */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_primary_1vvr4ji {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_range_o9o2al7 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_resume_u919e4d {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* operand */
    gm_tc3oht4ksth9d l7; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_return_fjkmc1q {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_shift_jium2c6 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_subscript_1hujuhl {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_t1vei8bd9e9m7 l3; /* s */
    gm_tc3oht4ksth9d l5; /* index */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_subscript_inner_dhgpovb {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* first */
    gm_tb1kjfin6jnc2 ret;
};
struct gm_fr_prefix_op_japctlh {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_push_jis5lhu {
    gm_frame h;
    gm_t7te2tdpebbt4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l8mrumu {
    gm_frame h;
    gm_tv1o1j44ii4db l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_from_5v6ko9m {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l5; /* rhs */
    gm_tc3oht4ksth9d l6; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_regrow_08uc6h5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_47d6kh3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_diag_big1nsf {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_report_djvi2hf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_shift_from_3cnaq25 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_tc3oht4ksth9d l5; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_shift_op_sro3o9l {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_shrink_to_lo_iok07dh {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqcdhc1ipuua(const uint8_t *a, const uint8_t *b);
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_add_from_7os0rvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ambiguous_try_else_0v14t0i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_at_reported_kw_name_it872te(void * a0);
__attribute__((aligned(4))) void gm_f_binary_41fr5r5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_bit_from_pu2bq19(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_bump_reported_kw_name_gs5eq1i(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_deny_in_head_06d9o22(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_eat_label_7eokqmm(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t1s75i08qtptd gm_f_eat_label_7eokqmm_c(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_format_3nkc0o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5k9iv40(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5nk8afu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gqrkfjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o5o20fv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_pb6f6q3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_is_arith_l6b4tpf(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_q2f0b93(gm_cseahvg8v7p0vs a0);
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_keyword_as_name_ipm23p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) void gm_f_mul_from_mlskgvm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_parens_0pje0fd(void *gm_out, uint32_t a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_t0vh9i8m50f41 gm_f_parens_0pje0fd_c(uint32_t a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_parse_add_dh9nvbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_arg_t6fq0nq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_attributed_00u9rjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_bang_irmkots(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_bit_h83vs72(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_tpg3f1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_break_hehputf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cmp_q5clpig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cont_type_ljmco4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_continue_ertnf67(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_dot_suffix_1fk204i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_effect_lit_cqei081(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_else_smoqgg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fallback_g1vpkra(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_fn_expr_c9357a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_for_q8vk3da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_foreign_l4091na(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_handler_lit_1nkojsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_if_shktfou(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_import_s1hv231(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_logic_ajejref(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_match_1lcgsrh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_not_a8b3gjf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ops_2p1csn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_parse_prefix_671fi6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_primary_1vvr4ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_range_o9o2al7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_resume_u919e4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_return_fjkmc1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_shift_jium2c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_struct_lit_86pp62c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_subscript_1hujuhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_subscript_inner_dhgpovb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_prefix_kfrfcdd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_union_lit_39ap2o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_with_hlmj8pt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh(const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_range_from_5v6ko9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_47d6kh3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) void gm_f_report_diag_big1nsf(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
__attribute__((aligned(4))) void gm_f_shift_from_3cnaq25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_lo_iok07dh(gm_cseahvg8v7p0vs a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_speculate_djd8kc6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_cpvard348ed[4] __attribute__((aligned(4)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1757p1g7h4me[42];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1cqejvllajb6[3];
extern const uint8_t gm_s1llcn0p4qesk[20];
extern const uint8_t gm_s1pandl18hjn0[61];
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s1q0n6t6d7g6n[34];
extern const uint8_t gm_s27pt4vcvtscl[24];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3tpa8q2q7llg[17];
extern const uint8_t gm_s42atdm90l4do[14];
extern const uint8_t gm_s42b9o00en8pl[34];
extern const uint8_t gm_s4niue72anpss[37];
extern const uint8_t gm_s4puodol85qqo[55];
extern const uint8_t gm_s541gi95epov5[25];
extern const uint8_t gm_s5lrfo6g7933d[50];
extern const uint8_t gm_s5us2hgd173vu[27];
extern const uint8_t gm_s5vb1b1mnli2b[72];
extern const uint8_t gm_s8ki76moa277n[23];
extern const uint8_t gm_s8n858pjjep3e[10];
extern const uint8_t gm_s8sl52ot187i2[50];
extern const uint8_t gm_s8vhc5kn5i8tv[38];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_sa3k87epai61e[25];
extern const uint8_t gm_sam3pd3469drv[55];
extern const uint8_t gm_saojtqmfeak72[8];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sb455a3lakuom[60];
extern const uint8_t gm_sbckhsd8r0nr0[20];
extern const uint8_t gm_sh0is7j09prt2[23];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shsfl3e9phpsi[23];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sjrimf03ml0ct[13];
extern const uint8_t gm_skefd3dps40es[40];
extern const uint8_t gm_skh04d6p3n402[79];
extern const uint8_t gm_sl7a8f8pe8le9[21];
extern const uint8_t gm_slp04vsc9cddi[49];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_snemlnnsbdb13[46];
extern const uint8_t gm_so7em1q8clnib[8];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_sqm6u7rqmi1il[19];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0dtioet3p4f6[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_ss2l6r6gldk9s[23];
extern const uint8_t gm_ssd99k9slj99i[3];
extern const uint8_t gm_ssf8mqtfpipac[45];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_sujvknhnh5am8[19];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_svhvj3h6g939d[66];
extern const uint8_t gm_svvbeos735pa5[84];
extern const gm_tmpl gm_tpl3ssq6hkk3qrj;
extern const gm_tmpl gm_tpla843kfhqtvit;
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_tmpl gm_tplft0ecs37svcn;
extern const gm_tmpl gm_tplg2qlrjk1sunc;
extern const gm_tmpl gm_tplk3nkr2eh6don;
extern const gm_tmpl gm_tplotost4gd8bha;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqcdhc1ipuua(const uint8_t *a, const uint8_t *b) {
    if (a[2] != b[2]) return false;
    return !a[2] || gm_eqbqb4gb1d92(a, b);
}
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eq929lennfd3(a, b);
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_54906rm F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t3fcbuehh3bs2 t3;
    gm_f_kind_k239kpg((void *)&t3, t2);
    gm_t9qkh0rsooc8d t4 = gm_l1;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t6 = (uint8_t)(t5 == ((uint8_t)8ULL));
    if (t6) {
        gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t3) + 8));
        t6 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t7), ((uint8_t *)&t4));
    }
    uint8_t t8 = (uint8_t)(t6);
    gm_t3fcbuehh3bs2 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9 + 8), t4);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_kw_p0r3sr9 F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t3fcbuehh3bs2 t3;
    gm_f_kind_k239kpg((void *)&t3, t2);
    gm_t9qkh0rsooc8d t4 = gm_l1;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t6 = (uint8_t)(t5 == ((uint8_t)2ULL));
    if (t6) {
        gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t3) + 8));
        t6 = (uint8_t)gm_eq2ceic3f8rp(((uint8_t *)&t7), ((uint8_t *)&t4));
    }
    uint8_t t8 = (uint8_t)(t6);
    gm_t3fcbuehh3bs2 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9 + 8), t4);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_box_j4urmed F;
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju gm_l1;
    gm_tq7s98ihonkju t2 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpnk3k16op)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t3) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcuiab49hlgv7g, 452, 52);
    void * t4 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
    F.ret = t4;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_qmock9g F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l1;
    uint8_t t4 = gm_f_at_54906rm(t2, t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    gm_tm4pc0emhrv1g t7;
    gm_f_bump_q2i40re((void *)&t7, t6);
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8ckfo47 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 136)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 144)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_expr_dhdi86k F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_t2gtbh39ptekd t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)50ULL));
    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)gm_out), t2, &t4, GM_CONV(gm_cseahvg8v7p0vs, t3));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_fail_e5kqpus F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_t4gut2c9fj1kt t5;
    gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t6 = gm_f_fail_diag_u0q4kp3(t2, &t5);
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
inline __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1s091rv F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_bang_irmkots *)((&F.h)->up))->l0));
    gm_tm4pc0emhrv1g t3;
    gm_f_bump_q2i40re((void *)&t3, t2);
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_bang_irmkots *)((&F.h)->up))->l0));
    gm_f_parse_type_2m93dtq((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_3nkc0o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3nkc0o4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1757p1g7h4me));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg2qlrjk1sunc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5k9iv40(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5k9iv40 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8n858pjjep3e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla843kfhqtvit), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5nk8afu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5nk8afu F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s42b9o00en8pl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplft0ecs37svcn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dsta2ql F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_gqrkfjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_gqrkfjb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ss2l6r6gldk9s));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplotost4gd8bha), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_o5o20fv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_o5o20fv F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svvbeos735pa5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(83));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk3nkr2eh6don), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pb6f6q3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pb6f6q3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1pandl18hjn0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3ssq6hkk3qrj), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_q2f0b93(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_q2f0b93 F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = ((t2 == t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_k239kpg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_f_tok_ai6sc12(t2);
    memmove(gm_out, ((uint8_t *)t3), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_rb20u32 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 132));
    uint32_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 434, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 132), t7);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fa4sjvq();
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
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fvvkq82 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8ckfo47();
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
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo_fp82gt4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tl874t2atnlh3 gm_l1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t2));
    gm_l1 = t3;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mk_expr_53qj7nd F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_next_id_a7upaf4(t2);
    gm_tl874t2atnlh3 t4 = gm_l2;
    gm_t2gtbh39ptekd t5 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l1));
    gm_tc3oht4ksth9d t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 84, 0);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t6 + 8), t5);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 72), t4);
    memmove(gm_out, ((uint8_t *)&t6), 88);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_id_a7upaf4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 56));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)t3));
    gm_l1 = t4;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 56));
    void * t7 = (void *)((uint8_t *)t6);
    gm_l2 = t7;
    void * t8 = gm_l2;
    void * t9 = gm_l2;
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)t9));
    uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 443, 5);
    uint32_t t13 = r11;
    GM_ST(uint32_t, ((uint8_t *)t8), t13);
    uint32_t t14 = gm_l1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_c0r2nvs F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 72));
    uint64_t t7 = gm_l1;
    uint64_t r8; bool o9 = __builtin_add_overflow(t6, t7, &r8);
    if (o9) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 126, 70);
    uint64_t t10 = r8;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 8) + 8));
    uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
    if (o14) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 126, 85);
    uint64_t t15 = r13;
    uint64_t t16 = (t10 < t15 ? t10 : t15);
    if ((uintptr_t)t16 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t16, t4, gm_srcuiab49hlgv7g, 126, 62);
    memmove(gm_out, (((uint8_t *)t3) + (uintptr_t)t16 * 48), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_3fgqmcb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_t3fcbuehh3bs2 t4;
    gm_f_nth_c0r2nvs((void *)&t4, t2, t3);
    gm_t9qkh0rsooc8d t5 = gm_l2;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t7 = (uint8_t)(t6 == ((uint8_t)8ULL));
    if (t7) {
        gm_t9qkh0rsooc8d t8 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t4) + 8));
        t7 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t8), ((uint8_t *)&t5));
    }
    uint8_t t9 = (uint8_t)(t7);
    gm_t3fcbuehh3bs2 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 8), t5);
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_capture_body_fgf89k2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)65ULL));
    uint8_t t4 = gm_f_at_54906rm(t2, t3);
    if (t4) {
        void * t5 = gm_l0;
        gm_f_parse_block_expr_0j6b78q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t6 = gm_l0;
    gm_f_parse_expr_bkb2n6e((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t6);
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
inline __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_mul_35e1t49 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l2;
    void * t2 = gm_l0;
    gm_f_parse_prefix_671fi6d((void *)&gm_l2, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t3) {
    } else {
        gm_to1cssm8dfiv5 t4;
        memset(&t4, 0, sizeof t4);
        ((uint8_t *)&t4)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l2) + 8), 88);
    void * t5 = gm_l0;
    gm_tc3oht4ksth9d t6 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
    gm_f_mul_from_mlskgvm((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5, &t6);
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
inline __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_postfix_ao4kstr F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* allow_bang */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_to1cssm8dfiv5 gm_l3;
    void * t2 = gm_l0;
    gm_f_parse_primary_1vvr4ji((void *)&gm_l3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t3) {
    } else {
        gm_to1cssm8dfiv5 t4;
        memset(&t4, 0, sizeof t4);
        ((uint8_t *)&t4)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 8), 88);
    void * t5 = gm_l0;
    uint8_t t6 = gm_l1;
    gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_f_parse_postfix_ops_2p1csn3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5, &t7, t6);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jis5lhu F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
        gm_b16_8 t9 = gm_f_empty_slice_fa4sjvq();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_08uc6h5(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 144), ((uint8_t *)&F.l1), 144);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 144)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t18), t26);
    gm_tftb1odc739t5 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    ((uint8_t *)&t27)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t27 + 1), GM_ZERO(gm_b0_1));
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l8mrumu F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
        gm_b16_8 t9 = gm_f_empty_slice_8ckfo47();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_47d6kh3(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 136), ((uint8_t *)a1), 136);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 136)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t18), t26);
    gm_tftb1odc739t5 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    ((uint8_t *)&t27)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t27 + 1), GM_ZERO(gm_b0_1));
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_08uc6h5 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)144ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_47d6kh3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_47d6kh3 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_report_diag_big1nsf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_report_diag_big1nsf F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 259, 5);
    uint64_t t7 = r5;
    GM_ST(uint64_t, (((uint8_t *)t2) + 136), t7);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 64));
    gm_f_push_o4ckdr1(t9, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_djvi2hf F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 253, 5);
    uint64_t t7 = r5;
    GM_ST(uint64_t, (((uint8_t *)t2) + 136), t7);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 64));
    gm_tl874t2atnlh3 t10 = gm_l1;
    gm_b16_8 t11 = gm_l2;
    gm_b0_1 t12 = gm_f_report_eardf07((void *)NULL, cx, NULL, t9, t10, t11);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_t4gut2c9fj1kt t5;
    gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_restr_p85qqmv F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_toh0ootodhjue, a1);
    void * t2 = gm_l0;
    gm_toh0ootodhjue t3 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)t2) + 128));
    gm_l2 = t3;
    void * t4 = gm_l0;
    gm_toh0ootodhjue t5 = gm_l1;
    GM_ST(gm_toh0ootodhjue, (((uint8_t *)t4) + 128), t5);
    gm_toh0ootodhjue t6 = gm_l2;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
gm_fprop:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_lo_iok07dh(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shrink_to_lo_iok07dh F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_tl874t2atnlh3 t5;
    GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_bm4ll8d F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_f_tok_ai6sc12(t2);
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 32));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_from_1sqap0o F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
    uint32_t t4 = gm_l1;
    void * t5 = gm_l0;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 80));
    uint32_t t7 = gm_l1;
    uint32_t t8 = (t6 > t7 ? t6 : t7);
    gm_tl874t2atnlh3 t9;
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t3);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 8), t8);
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t2) {
        gm_t9p38b040grms t3;
        memset(&t3, 0, sizeof t3);
        memmove(gm_out, ((uint8_t *)&t3), 32);
        goto gm_fdone;
    }
    gm_tbci5ospk5nnn t4 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9p38b040grms gm_l2;
    unsigned __int128 gm_l3;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_f_to_u128_haka99r((void *)&gm_l2, &t2);
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        unsigned __int128 t4 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l2));
        gm_l3 = t4;
    } else {
        gm_tf5srh4mp4b3i t5;
        memset(&t5, 0, sizeof t5);
        F.ret = t5;
        goto gm_fret;
    }
    unsigned __int128 t6 = gm_l3;
    gm_l1 = t6;
    unsigned __int128 t7 = gm_l1;
    gm_tf5srh4mp4b3i t8;
    uint64_t t9 = (uint64_t)t7;
    gm_tf5srh4mp4b3i t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[8] = 1;
    gm_tf5srh4mp4b3i t11;
    memset(&t11, 0, sizeof t11);
    t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tok_ai6sc12 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 72));
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 8) + 8));
    uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 122, 73);
    uint64_t t11 = r9;
    uint64_t t12 = (t6 < t11 ? t6 : t11);
    if ((uintptr_t)t12 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t12, t4, gm_srcuiab49hlgv7g, 122, 56);
    void * t13 = (void *)(((uint8_t *)t3) + (uintptr_t)t12 * 48);
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_add_from_7os0rvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_add_from_7os0rvi F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 975u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l5;
    gm_tt7vn9ehj0acr gm_l6;
    gm_to1cssm8dfiv5 gm_l7;
    void * t5 = gm_l0;
    gm_tc3oht4ksth9d t6 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
    gm_f_mul_from_mlskgvm((void *)&gm_l5, (void *)NULL, cx, NULL, t5, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t7) {
    } else {
        gm_to1cssm8dfiv5 t8;
        memset(&t8, 0, sizeof t8);
        ((uint8_t *)&t8)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t8), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l5) + 8), 88);
    {
        for (;;) {
            void * t9 = gm_l0;
            gm_t3fcbuehh3bs2 t10;
            gm_f_kind_k239kpg((void *)&t10, t9);
            gm_tt7vn9ehj0acr t11 = gm_f_add_op_99op354(&t10);
            gm_l6 = t11;
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l6))[2] != 0);
            if (!t12) {
                goto gm_lb1;
            }
            gm_ts6oqppjh0ba3 t13 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l6));
            gm_l3 = t13;
            {
                void * t14 = gm_l0;
                gm_tm4pc0emhrv1g t15;
                gm_f_bump_q2i40re((void *)&t15, t14);
                void * t16 = gm_l0;
                gm_f_parse_mul_35e1t49((void *)&gm_l7, (void *)NULL, cx, NULL, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (t17) {
                } else {
                    gm_to1cssm8dfiv5 t18;
                    memset(&t18, 0, sizeof t18);
                    ((uint8_t *)&t18)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t18), 96);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l7) + 8), 88);
                void * t19 = gm_l0;
                gm_ts6oqppjh0ba3 t20 = gm_l3;
                gm_tc3oht4ksth9d t21 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
                gm_tc3oht4ksth9d t22 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l4));
                gm_f_binary_41fr5r5((void *)&F.l2, (void *)NULL, cx, NULL, t19, t20, &t21, &t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tc3oht4ksth9d t23 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_to1cssm8dfiv5 t24;
    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    ((uint8_t *)&t24)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t24 + 8), t23);
    memmove(gm_out, ((uint8_t *)&t24), 96);
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
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_op_99op354 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr gm_l1;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)8ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
            if (!t15) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t17;
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
            gm_tt7vn9ehj0acr t18;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t18), t17);
            ((uint8_t *)&t18)[2] = 1;
            gm_l1 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)8ULL));
            if (!t20) {
                goto gm_lb3;
            }
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)10ULL));
            if (!t22) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)1ULL));
            gm_ts6oqppjh0ba3 t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 1), t23);
            gm_tt7vn9ehj0acr t25;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[2] = 1;
            gm_l1 = t25;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)8ULL));
            if (!t27) {
                goto gm_lb4;
            }
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)14ULL));
            if (!t29) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)2ULL));
            gm_ts6oqppjh0ba3 t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 1), t30);
            gm_tt7vn9ehj0acr t32;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[2] = 1;
            gm_l1 = t32;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)8ULL));
            if (!t34) {
                goto gm_lb5;
            }
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)18ULL));
            if (!t36) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t37;
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
            gm_tt7vn9ehj0acr t39;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t39), t38);
            ((uint8_t *)&t39)[2] = 1;
            gm_l1 = t39;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)8ULL));
            if (!t41) {
                goto gm_lb6;
            }
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
            if (!t43) {
                goto gm_lb6;
            }
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t45;
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t45 + 1), t44);
            gm_tt7vn9ehj0acr t46;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t46), t45);
            ((uint8_t *)&t46)[2] = 1;
            gm_l1 = t46;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)8ULL));
            if (!t48) {
                goto gm_lb7;
            }
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)11ULL));
            if (!t50) {
                goto gm_lb7;
            }
            gm_t9qkh0rsooc8d t51;
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)1ULL));
            gm_ts6oqppjh0ba3 t52;
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 1), t51);
            gm_tt7vn9ehj0acr t53;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t53), t52);
            ((uint8_t *)&t53)[2] = 1;
            gm_l1 = t53;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)8ULL));
            if (!t55) {
                goto gm_lb8;
            }
            uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)15ULL));
            if (!t57) {
                goto gm_lb8;
            }
            gm_t9qkh0rsooc8d t58;
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)2ULL));
            gm_ts6oqppjh0ba3 t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 1), t58);
            gm_tt7vn9ehj0acr t60;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t60), t59);
            ((uint8_t *)&t60)[2] = 1;
            gm_l1 = t60;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)8ULL));
            if (!t62) {
                goto gm_lb9;
            }
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)19ULL));
            if (!t64) {
                goto gm_lb9;
            }
            gm_t9qkh0rsooc8d t65;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t66;
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 1), t65);
            gm_tt7vn9ehj0acr t67;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t67), t66);
            ((uint8_t *)&t67)[2] = 1;
            gm_l1 = t67;
            goto gm_lb1;
        }
gm_lb9:;
        {
            gm_tt7vn9ehj0acr t68;
            memset(&t68, 0, sizeof t68);
            gm_l1 = t68;
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 61, 52);
    }
gm_lb1:;
    gm_tt7vn9ehj0acr t69 = gm_l1;
    F.ret = t69;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354_c(gm_t3fcbuehh3bs2 a0) { return gm_f_add_op_99op354(&a0); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ambiguous_try_else_0v14t0i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_ambiguous_try_else_0v14t0i F;
    void * gm_l0; /* p */
    void * gm_l1; /* lhs */
    void * gm_l2; /* rhs */
    void * gm_l3; /* operand */
    GM_STACK_CHECK(&F, 624u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t4 = gm_l1;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)12ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint8_t t7 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)4ULL));
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
            gm_l3 = t9;
            void * t10 = gm_l1;
            gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 72));
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s5vb1b1mnli2b));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(71));
            gm_t4gut2c9fj1kt t13;
            gm_f_error_s6hn871((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l4 = t13;
            void * t14 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_shsfl3e9phpsi));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(22));
            void * t16 = gm_l0;
            uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)t16));
            void * t18 = gm_l3;
            uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)t18) + 72) + 4));
            void * t20 = gm_l2;
            uint32_t t21 = GM_LD(uint32_t, ((((uint8_t *)t20) + 72) + 8));
            gm_t0vh9i8m50f41 t22;
            gm_f_parens_0pje0fd((void *)&t22, t17, t19, t21);
            F.l5 = t22;
            void * t23 = (void *)((uint8_t *)&F.l5);
            uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t25 = (uintptr_t)((uint64_t)2ULL);
            if (t24 > t25 || t25 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t24, t25, ((uint64_t)2ULL), gm_src0dtioet3p4f6, 154, 47);
            uint64_t t26 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t23 + (uintptr_t)((uint64_t)0ULL) * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
            gm_b0_1 t28 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t14, t15, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t29 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s8ki76moa277n));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(22));
            void * t31 = gm_l0;
            uint32_t t32 = GM_LD(uint32_t, ((uint8_t *)t31));
            void * t33 = gm_l1;
            uint32_t t34 = GM_LD(uint32_t, ((((uint8_t *)t33) + 72) + 4));
            void * t35 = gm_l1;
            uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)t35) + 72) + 8));
            gm_t0vh9i8m50f41 t37;
            gm_f_parens_0pje0fd((void *)&t37, t32, t34, t36);
            F.l6 = t37;
            void * t38 = (void *)((uint8_t *)&F.l6);
            uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t40 = (uintptr_t)((uint64_t)2ULL);
            if (t39 > t40 || t40 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t39, t40, ((uint64_t)2ULL), gm_src0dtioet3p4f6, 155, 47);
            uint64_t t41 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t38 + (uintptr_t)((uint64_t)0ULL) * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41));
            gm_b0_1 t43 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t29, t30, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t44 = gm_l0;
            gm_f_report_diag_big1nsf(t44, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_binary_41fr5r5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3) {
    struct gm_fr_binary_41fr5r5 F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    memcpy(&F.l3, a3, sizeof F.l3);
    void * t2 = gm_l0;
    gm_ts6oqppjh0ba3 t3 = gm_l1;
    void * t4 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t5 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trl9d7m1k5lbd t6;
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t6 + 0), t3);
    GM_ST(void *, ((uint8_t *)&t6 + 8), t4);
    GM_ST(void *, ((uint8_t *)&t6 + 16), t5);
    gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
    gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
    gm_t2gtbh39ptekd t9;
    memset(&t9, 0, sizeof t9);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)13ULL));
    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t9 + 8), t6);
    gm_tl874t2atnlh3 t10 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t7), GM_CONV(gm_cseahvg8v7p0vs, t8)));
    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)gm_out), t2, &t9, GM_CONV(gm_cseahvg8v7p0vs, t10));
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
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binary_follows_jeb42bi F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l3;
    uint8_t gm_l4;
    {
        {
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)2ULL));
            if (!t47) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t48 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t48;
            gm_t9qkh0rsooc8d t49 = gm_l1;
            gm_t9qkh0rsooc8d t50;
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)16ULL));
            gm_t9qkh0rsooc8d t51;
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)15ULL));
            gm_t0am46iohlpol t52;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 0), t50);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 1), t51);
            uint8_t t53 = 0;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t52) + 0, ((uint8_t *)&t49))) t53 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t52) + 1, ((uint8_t *)&t49))) t53 = 1;
            uint8_t t54 = (uint8_t)(t53);
            gm_l3 = t54;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)8ULL));
            if (!t56) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t57 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t57;
            {
                {
                    {
                        {
                            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)67ULL));
                            if (!t59) {
                                goto gm_lb7;
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t61 = (uint8_t)(t60 == ((uint8_t)68ULL));
                            if (!t61) {
                                goto gm_lb8;
                            }
                            goto gm_lb6;
                        }
gm_lb8:;
                        {
                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)41ULL));
                            if (!t63) {
                                goto gm_lb9;
                            }
                            goto gm_lb6;
                        }
gm_lb9:;
                        {
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)42ULL));
                            if (!t65) {
                                goto gm_lb10;
                            }
                            goto gm_lb6;
                        }
gm_lb10:;
                        {
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)43ULL));
                            if (!t67) {
                                goto gm_lb11;
                            }
                            goto gm_lb6;
                        }
gm_lb11:;
                        {
                            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)44ULL));
                            if (!t69) {
                                goto gm_lb12;
                            }
                            goto gm_lb6;
                        }
gm_lb12:;
                        {
                            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)50ULL));
                            if (!t71) {
                                goto gm_lb13;
                            }
                            goto gm_lb6;
                        }
gm_lb13:;
                        {
                            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t73 = (uint8_t)(t72 == ((uint8_t)51ULL));
                            if (!t73) {
                                goto gm_lb14;
                            }
                            goto gm_lb6;
                        }
gm_lb14:;
                        {
                            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)7ULL));
                            if (!t75) {
                                goto gm_lb15;
                            }
                            goto gm_lb6;
                        }
gm_lb15:;
                        {
                            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)8ULL));
                            if (!t77) {
                                goto gm_lb16;
                            }
                            goto gm_lb6;
                        }
gm_lb16:;
                        {
                            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t79 = (uint8_t)(t78 == ((uint8_t)9ULL));
                            if (!t79) {
                                goto gm_lb17;
                            }
                            goto gm_lb6;
                        }
gm_lb17:;
                        goto gm_lb5;
                    }
gm_lb6:;
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    {
                        {
                            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t81 = (uint8_t)(t80 == ((uint8_t)5ULL));
                            if (!t81) {
                                goto gm_lb20;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                        {
                            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)13ULL));
                            if (!t83) {
                                goto gm_lb21;
                            }
                            goto gm_lb19;
                        }
gm_lb21:;
                        {
                            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t85 = (uint8_t)(t84 == ((uint8_t)17ULL));
                            if (!t85) {
                                goto gm_lb22;
                            }
                            goto gm_lb19;
                        }
gm_lb22:;
                        {
                            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)21ULL));
                            if (!t87) {
                                goto gm_lb23;
                            }
                            goto gm_lb19;
                        }
gm_lb23:;
                        {
                            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)6ULL));
                            if (!t89) {
                                goto gm_lb24;
                            }
                            goto gm_lb19;
                        }
gm_lb24:;
                        goto gm_lb18;
                    }
gm_lb19:;
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb18:;
                {
                    {
                        {
                            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)0ULL));
                            if (!t91) {
                                goto gm_lb27;
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        {
                            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)10ULL));
                            if (!t93) {
                                goto gm_lb28;
                            }
                            goto gm_lb26;
                        }
gm_lb28:;
                        {
                            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)14ULL));
                            if (!t95) {
                                goto gm_lb29;
                            }
                            goto gm_lb26;
                        }
gm_lb29:;
                        {
                            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t97 = (uint8_t)(t96 == ((uint8_t)18ULL));
                            if (!t97) {
                                goto gm_lb30;
                            }
                            goto gm_lb26;
                        }
gm_lb30:;
                        {
                            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t99 = (uint8_t)(t98 == ((uint8_t)1ULL));
                            if (!t99) {
                                goto gm_lb31;
                            }
                            goto gm_lb26;
                        }
gm_lb31:;
                        {
                            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t101 = (uint8_t)(t100 == ((uint8_t)11ULL));
                            if (!t101) {
                                goto gm_lb32;
                            }
                            goto gm_lb26;
                        }
gm_lb32:;
                        {
                            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t103 = (uint8_t)(t102 == ((uint8_t)15ULL));
                            if (!t103) {
                                goto gm_lb33;
                            }
                            goto gm_lb26;
                        }
gm_lb33:;
                        {
                            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t105 = (uint8_t)(t104 == ((uint8_t)19ULL));
                            if (!t105) {
                                goto gm_lb34;
                            }
                            goto gm_lb26;
                        }
gm_lb34:;
                        goto gm_lb25;
                    }
gm_lb26:;
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb25:;
                {
                    {
                        {
                            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t107 = (uint8_t)(t106 == ((uint8_t)2ULL));
                            if (!t107) {
                                goto gm_lb37;
                            }
                            goto gm_lb36;
                        }
gm_lb37:;
                        {
                            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)12ULL));
                            if (!t109) {
                                goto gm_lb38;
                            }
                            goto gm_lb36;
                        }
gm_lb38:;
                        {
                            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)16ULL));
                            if (!t111) {
                                goto gm_lb39;
                            }
                            goto gm_lb36;
                        }
gm_lb39:;
                        {
                            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t113 = (uint8_t)(t112 == ((uint8_t)20ULL));
                            if (!t113) {
                                goto gm_lb40;
                            }
                            goto gm_lb36;
                        }
gm_lb40:;
                        {
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)3ULL));
                            if (!t115) {
                                goto gm_lb41;
                            }
                            goto gm_lb36;
                        }
gm_lb41:;
                        {
                            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t117 = (uint8_t)(t116 == ((uint8_t)4ULL));
                            if (!t117) {
                                goto gm_lb42;
                            }
                            goto gm_lb36;
                        }
gm_lb42:;
                        goto gm_lb35;
                    }
gm_lb36:;
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb35:;
                {
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb43:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 88, 18);
            }
gm_lb4:;
            uint8_t t118 = gm_l4;
            gm_l3 = t118;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb44:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 86, 54);
    }
gm_lb1:;
    uint8_t t119 = gm_l3;
    F.ret = t119;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi_c(gm_t3fcbuehh3bs2 a0) { return gm_f_binary_follows_jeb42bi(&a0); }
__attribute__((aligned(4))) void gm_f_bit_from_pu2bq19(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_bit_from_pu2bq19 F;
    void * gm_l0; /* p */
    gm_tt7vn9ehj0acr gm_l3; /* chain */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* word */
    gm_b16_8 gm_l9; /* before */
    gm_b16_8 gm_l10; /* after */
    GM_STACK_CHECK(&F, 2227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l13;
    gm_tt7vn9ehj0acr gm_l14;
    uint8_t gm_l15;
    gm_t9qkh0rsooc8d gm_l16;
    gm_b16_8 gm_l17;
    gm_b16_8 gm_l18;
    gm_b16_8 gm_l19;
    gm_b16_8 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_to1cssm8dfiv5 gm_l24;
    void * t10 = gm_l0;
    gm_tc3oht4ksth9d t11 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
    gm_f_shift_from_3cnaq25((void *)&gm_l13, (void *)NULL, cx, NULL, t10, &t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (t12) {
    } else {
        gm_to1cssm8dfiv5 t13;
        memset(&t13, 0, sizeof t13);
        ((uint8_t *)&t13)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t13), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l13) + 8), 88);
    gm_tt7vn9ehj0acr t14;
    memset(&t14, 0, sizeof t14);
    gm_l3 = t14;
    {
        for (;;) {
            void * t15 = gm_l0;
            gm_t3fcbuehh3bs2 t16;
            gm_f_kind_k239kpg((void *)&t16, t15);
            gm_tt7vn9ehj0acr t17 = gm_f_bit_op_p81j9u3(&t16);
            gm_l14 = t17;
            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l14))[2] != 0);
            if (!t18) {
                goto gm_lb1;
            }
            gm_ts6oqppjh0ba3 t19 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l14));
            gm_l4 = t19;
            {
                {
                    {
                        uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l3))[2] != 0);
                        if (t20) {
                            goto gm_lb5;
                        }
                        gm_l15 = ((uint8_t)1);
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        gm_l15 = ((uint8_t)0);
                        goto gm_lb4;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 302, 12);
                }
gm_lb4:;
                uint8_t t21 = gm_l15;
                if (t21) {
                    gm_ts6oqppjh0ba3 t22 = gm_l4;
                    gm_tt7vn9ehj0acr t23;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
                    ((uint8_t *)&t23)[2] = 1;
                    gm_l3 = t23;
                } else {
                    gm_tt7vn9ehj0acr t24 = gm_l3;
                    gm_ts6oqppjh0ba3 t25 = gm_l4;
                    gm_tt7vn9ehj0acr t26;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26), t25);
                    ((uint8_t *)&t26)[2] = 1;
                    uint8_t t27 = (!gm_eqcdhc1ipuua(((uint8_t *)&t24), ((uint8_t *)&t26)));
                    if (t27) {
                        void * t28 = gm_l0;
                        void * t29 = gm_l0;
                        gm_tl874t2atnlh3 t30 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t29));
                        gm_b16_8 t31;
                        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_svhvj3h6g939d));
                        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(65));
                        gm_b0_1 t32 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t28, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                void * t33 = gm_l0;
                gm_f_bump_q2i40re((void *)&F.l5, t33);
                {
                    {
                        gm_ts6oqppjh0ba3 t34 = gm_l4;
                        gm_ts6oqppjh0ba3 t35;
                        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)7ULL));
                        gm_ts6oqppjh0ba3 t36;
                        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)8ULL));
                        gm_t9ldr1q5hn9fq t37;
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t37 + 0), t35);
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t37 + 2), t36);
                        uint8_t t38 = 0;
                        if (gm_eqbqb4gb1d92(((uint8_t *)&t37) + 0, ((uint8_t *)&t34))) t38 = 1;
                        if (gm_eqbqb4gb1d92(((uint8_t *)&t37) + 2, ((uint8_t *)&t34))) t38 = 1;
                        uint8_t t39 = (uint8_t)(t38);
                        if (!t39) {
                            goto gm_lb8;
                        }
                        void * t40 = gm_l0;
                        gm_ts6oqppjh0ba3 t41 = gm_l4;
                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
                        uint8_t t43 = (uint8_t)((t42 == ((uint8_t)7ULL)));
                        gm_ts6oqppjh0ba3 t44;
                        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)7ULL));
                        if (t43) {
                            gm_t9qkh0rsooc8d t45;
                            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)7ULL));
                            gm_l16 = t45;
                        } else {
                            gm_t9qkh0rsooc8d t46;
                            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)8ULL));
                            gm_l16 = t46;
                        }
                        gm_t9qkh0rsooc8d t47 = gm_l16;
                        uint8_t t48 = gm_f_at_54906rm(t40, t47);
                        if (!t48) {
                            goto gm_lb8;
                        }
                        void * t49 = gm_l0;
                        uint32_t t50 = gm_f_lo_fp82gt4(t49);
                        uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 8));
                        uint8_t t52 = ((t50 == t51));
                        if (!t52) {
                            goto gm_lb8;
                        }
                        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 32));
                        void * t54 = gm_l0;
                        gm_tl874t2atnlh3 t55 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t54));
                        gm_tl874t2atnlh3 t56 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t53), GM_CONV(gm_cseahvg8v7p0vs, t55)));
                        gm_l6 = t56;
                        gm_ts6oqppjh0ba3 t57 = gm_l4;
                        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
                        uint8_t t59 = (uint8_t)((t58 == ((uint8_t)7ULL)));
                        gm_ts6oqppjh0ba3 t60;
                        GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)7ULL));
                        if (t59) {
                            gm_b16_8 t61;
                            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s1cqejvllajb6));
                            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(2));
                            gm_l17 = t61;
                        } else {
                            gm_b16_8 t62;
                            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_ssd99k9slj99i));
                            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(2));
                            gm_l17 = t62;
                        }
                        gm_b16_8 t63 = gm_l17;
                        gm_l7 = t63;
                        gm_ts6oqppjh0ba3 t64 = gm_l4;
                        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t64) + 0));
                        uint8_t t66 = (uint8_t)((t65 == ((uint8_t)7ULL)));
                        gm_ts6oqppjh0ba3 t67;
                        GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)7ULL));
                        if (t66) {
                            gm_b16_8 t68;
                            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s920dpvvv6cej));
                            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(3));
                            gm_l18 = t68;
                        } else {
                            gm_b16_8 t69;
                            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_spi47lg4tr80j));
                            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(2));
                            gm_l18 = t69;
                        }
                        gm_b16_8 t70 = gm_l18;
                        gm_l8 = t70;
                        uint32_t t71 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 72) + 8));
                        uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                        uint8_t t73 = ((t71 == t72));
                        if (t73) {
                            gm_b16_8 t74;
                            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s3p0rbs9hc3lg));
                            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
                            gm_l19 = t74;
                        } else {
                            gm_b16_8 t75;
                            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(0));
                            gm_l19 = t75;
                        }
                        gm_b16_8 t76 = gm_l19;
                        gm_l9 = t76;
                        void * t77 = gm_l0;
                        gm_tm4pc0emhrv1g t78;
                        gm_f_bump_q2i40re((void *)&t78, t77);
                        void * t79 = gm_l0;
                        uint32_t t80 = gm_f_lo_fp82gt4(t79);
                        uint32_t t81 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
                        uint8_t t82 = ((t80 == t81));
                        if (t82) {
                            gm_b16_8 t83;
                            GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s3p0rbs9hc3lg));
                            GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(1));
                            gm_l20 = t83;
                        } else {
                            gm_b16_8 t84;
                            GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(0));
                            gm_l20 = t84;
                        }
                        gm_b16_8 t85 = gm_l20;
                        gm_l10 = t85;
                        gm_tl874t2atnlh3 t86 = gm_l6;
                        gm_b16_8 t87 = gm_l7;
                        gm_b16_8 t88 = gm_l8;
                        gm_t6j6smj7qoctu t89;
                        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t87);
                        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 16), t88);
                        gm_f_format_5nk8afu((void *)&gm_l21, (void *)NULL, cx, NULL, &t89);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                        if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src0dtioet3p4f6, 317, 33);
                        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                        gm_t4gut2c9fj1kt t93;
                        gm_f_error_s6hn871((void *)&t93, (void *)NULL, cx, NULL, t86, t92);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l11 = t93;
                        void * t94 = (void *)((uint8_t *)&F.l11);
                        gm_b16_8 t95 = gm_l7;
                        gm_b16_8 t96 = gm_l8;
                        gm_t6j6smj7qoctu t97;
                        GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t95);
                        GM_ST(gm_b16_8, ((uint8_t *)&t97 + 16), t96);
                        gm_f_format_gqrkfjb((void *)&gm_l22, (void *)NULL, cx, NULL, &t97);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src0dtioet3p4f6, 318, 23);
                        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                        gm_tl874t2atnlh3 t101 = gm_l6;
                        gm_b16_8 t102 = gm_l9;
                        gm_b16_8 t103 = gm_l8;
                        gm_b16_8 t104 = gm_l10;
                        gm_tfeab279c16rr t105;
                        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t102);
                        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 16), t103);
                        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 32), t104);
                        gm_f_format_dsta2ql((void *)&gm_l23, (void *)NULL, cx, NULL, &t105);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src0dtioet3p4f6, 318, 75);
                        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                        gm_b0_1 t109 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t94, t100, t101, t108);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t110 = gm_l0;
                        gm_b0_1 t111 = gm_f_fail_diag_u0q4kp3(t110, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
                        gm_to1cssm8dfiv5 t112;
                        memset(&t112, 0, sizeof t112);
                        ((uint8_t *)&t112)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t112 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t112), 96);
                        goto gm_fdone;
                    }
gm_lb8:;
                }
gm_lb7:;
                void * t113 = gm_l0;
                gm_f_parse_shift_jium2c6((void *)&gm_l24, (void *)NULL, cx, NULL, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                if (t114) {
                } else {
                    gm_to1cssm8dfiv5 t115;
                    memset(&t115, 0, sizeof t115);
                    ((uint8_t *)&t115)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t115 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t115), 96);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l12), (((uint8_t *)&gm_l24) + 8), 88);
                void * t116 = gm_l0;
                gm_ts6oqppjh0ba3 t117 = gm_l4;
                gm_tc3oht4ksth9d t118 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
                gm_tc3oht4ksth9d t119 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l12));
                gm_f_binary_41fr5r5((void *)&F.l2, (void *)NULL, cx, NULL, t116, t117, &t118, &t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tc3oht4ksth9d t120 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_to1cssm8dfiv5 t121;
    GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
    ((uint8_t *)&t121)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t121 + 8), t120);
    memmove(gm_out, ((uint8_t *)&t121), 96);
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_op_p81j9u3 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)8ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)7ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_ts6oqppjh0ba3 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)7ULL));
            gm_tt7vn9ehj0acr t12;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[2] = 1;
            gm_l1 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)8ULL));
            if (!t14) {
                goto gm_lb3;
            }
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)8ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_ts6oqppjh0ba3 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)8ULL));
            gm_tt7vn9ehj0acr t18;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t18), t17);
            ((uint8_t *)&t18)[2] = 1;
            gm_l1 = t18;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)8ULL));
            if (!t20) {
                goto gm_lb4;
            }
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)9ULL));
            if (!t22) {
                goto gm_lb4;
            }
            gm_ts6oqppjh0ba3 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)9ULL));
            gm_tt7vn9ehj0acr t24;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[2] = 1;
            gm_l1 = t24;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_tt7vn9ehj0acr t25;
            memset(&t25, 0, sizeof t25);
            gm_l1 = t25;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 45, 52);
    }
gm_lb1:;
    gm_tt7vn9ehj0acr t26 = gm_l1;
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3_c(gm_t3fcbuehh3bs2 a0) { return gm_f_bit_op_p81j9u3(&a0); }
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_can_begin_expr_rmia9bs F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l3;
    {
        {
            {
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                    if (!t19) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
                    if (!t21) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)3ULL));
                    if (!t23) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)4ULL));
                    if (!t25) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)5ULL));
                    if (!t27) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)6ULL));
                    if (!t29) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)7ULL));
                    if (!t31) {
                        goto gm_lb10;
                    }
                    goto gm_lb3;
                }
gm_lb10:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)2ULL));
            if (!t33) {
                goto gm_lb11;
            }
            gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t34;
            gm_t9qkh0rsooc8d t35 = gm_l1;
            gm_t9qkh0rsooc8d t36;
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)25ULL));
            gm_t9qkh0rsooc8d t37;
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)10ULL));
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)19ULL));
            gm_t9qkh0rsooc8d t39;
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)11ULL));
            gm_t9qkh0rsooc8d t40;
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)24ULL));
            gm_t9qkh0rsooc8d t41;
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)27ULL));
            gm_t9qkh0rsooc8d t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)6ULL));
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)14ULL));
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)13ULL));
            gm_t9qkh0rsooc8d t45;
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)17ULL));
            gm_t9qkh0rsooc8d t46;
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)31ULL));
            gm_t9qkh0rsooc8d t47;
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)12ULL));
            gm_t9qkh0rsooc8d t48;
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)23ULL));
            gm_t9qkh0rsooc8d t49;
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t50;
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)3ULL));
            gm_t9qkh0rsooc8d t51;
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)22ULL));
            gm_t9qkh0rsooc8d t52;
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)26ULL));
            gm_t9qkh0rsooc8d t53;
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)18ULL));
            gm_ta8d7579em356 t54;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 0), t36);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 1), t37);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 2), t38);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 3), t39);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 4), t40);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 5), t41);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 6), t42);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 7), t43);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 8), t44);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 9), t45);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 10), t46);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 11), t47);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 12), t48);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 13), t49);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 14), t50);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 15), t51);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 16), t52);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 17), t53);
            uint8_t t55 = 0;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 0, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 1, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 2, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 3, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 4, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 5, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 6, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 7, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 8, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 9, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 10, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 11, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 12, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 13, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 14, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 15, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 16, ((uint8_t *)&t35))) t55 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t54) + 17, ((uint8_t *)&t35))) t55 = 1;
            uint8_t t56 = (uint8_t)(t55);
            gm_l3 = t56;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)8ULL));
            if (!t58) {
                goto gm_lb12;
            }
            gm_t9qkh0rsooc8d t59 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t59;
            gm_t9qkh0rsooc8d t60 = gm_l2;
            gm_t9qkh0rsooc8d t61;
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)61ULL));
            gm_t9qkh0rsooc8d t62;
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)63ULL));
            gm_t9qkh0rsooc8d t63;
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)58ULL));
            gm_t9qkh0rsooc8d t64;
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t65;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)9ULL));
            gm_t9qkh0rsooc8d t66;
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)7ULL));
            gm_t9qkh0rsooc8d t67;
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)46ULL));
            gm_t9qkh0rsooc8d t68;
            GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)45ULL));
            gm_t9qkh0rsooc8d t69;
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)47ULL));
            gm_t9qkh0rsooc8d t70;
            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)53ULL));
            gm_tfh78knfg7urq t71;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 0), t61);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 1), t62);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 2), t63);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 3), t64);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 4), t65);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 5), t66);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 6), t67);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 7), t68);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 8), t69);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 9), t70);
            uint8_t t72 = 0;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 0, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 1, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 2, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 3, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 4, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 5, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 6, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 7, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 8, ((uint8_t *)&t60))) t72 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t71) + 9, ((uint8_t *)&t60))) t72 = 1;
            uint8_t t73 = (uint8_t)(t72);
            gm_l3 = t73;
            goto gm_lb1;
        }
gm_lb12:;
        {
            {
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)9ULL));
                    if (!t75) {
                        goto gm_lb15;
                    }
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)10ULL));
                    if (!t77) {
                        goto gm_lb16;
                    }
                    goto gm_lb14;
                }
gm_lb16:;
                goto gm_lb13;
            }
gm_lb14:;
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 5, 54);
    }
gm_lb1:;
    uint8_t t78 = gm_l3;
    F.ret = t78;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs_c(gm_t3fcbuehh3bs2 a0) { return gm_f_can_begin_expr_rmia9bs(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_can_begin_type_a3rjrm7 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l3;
    {
        {
            {
                {
                    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
                    if (!t11) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)7ULL));
                    if (!t13) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)2ULL));
            if (!t15) {
                goto gm_lb6;
            }
            gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t16;
            gm_t9qkh0rsooc8d t17 = gm_l1;
            gm_t9qkh0rsooc8d t18;
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)11ULL));
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)24ULL));
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)27ULL));
            gm_t9qkh0rsooc8d t21;
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)6ULL));
            gm_t8d5d2ep1fo6t t22;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 0), t18);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 1), t19);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 2), t20);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 3), t21);
            uint8_t t23 = 0;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t22) + 0, ((uint8_t *)&t17))) t23 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t22) + 1, ((uint8_t *)&t17))) t23 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t22) + 2, ((uint8_t *)&t17))) t23 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t22) + 3, ((uint8_t *)&t17))) t23 = 1;
            uint8_t t24 = (uint8_t)(t23);
            gm_l3 = t24;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)8ULL));
            if (!t26) {
                goto gm_lb7;
            }
            gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t27;
            gm_t9qkh0rsooc8d t28 = gm_l2;
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)45ULL));
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)47ULL));
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)63ULL));
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)61ULL));
            gm_t8d5d2ep1fo6t t33;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 0), t29);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 1), t30);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 2), t31);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 3), t32);
            uint8_t t34 = 0;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t33) + 0, ((uint8_t *)&t28))) t34 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t33) + 1, ((uint8_t *)&t28))) t34 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t33) + 2, ((uint8_t *)&t28))) t34 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t33) + 3, ((uint8_t *)&t28))) t34 = 1;
            uint8_t t35 = (uint8_t)(t34);
            gm_l3 = t35;
            goto gm_lb1;
        }
gm_lb7:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 23, 54);
    }
gm_lb1:;
    uint8_t t36 = gm_l3;
    F.ret = t36;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7_c(gm_t3fcbuehh3bs2 a0) { return gm_f_can_begin_type_a3rjrm7(&a0); }
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_op_9gj7im4 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr gm_l1;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)2ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)16ULL));
            if (!t15) {
                goto gm_lb2;
            }
            gm_ts6oqppjh0ba3 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)12ULL));
            gm_tt7vn9ehj0acr t17;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[2] = 1;
            gm_l1 = t17;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)2ULL));
            if (!t19) {
                goto gm_lb3;
            }
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)15ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_ts6oqppjh0ba3 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)14ULL));
            gm_tt7vn9ehj0acr t23;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[2] = 1;
            gm_l1 = t23;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)8ULL));
            if (!t25) {
                goto gm_lb4;
            }
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)67ULL));
            if (!t27) {
                goto gm_lb4;
            }
            gm_ts6oqppjh0ba3 t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)13ULL));
            gm_tt7vn9ehj0acr t29;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[2] = 1;
            gm_l1 = t29;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)8ULL));
            if (!t31) {
                goto gm_lb5;
            }
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)68ULL));
            if (!t33) {
                goto gm_lb5;
            }
            gm_ts6oqppjh0ba3 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)15ULL));
            gm_tt7vn9ehj0acr t35;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35), t34);
            ((uint8_t *)&t35)[2] = 1;
            gm_l1 = t35;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)8ULL));
            if (!t37) {
                goto gm_lb6;
            }
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)41ULL));
            if (!t39) {
                goto gm_lb6;
            }
            gm_ts6oqppjh0ba3 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)16ULL));
            gm_tt7vn9ehj0acr t41;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t41), t40);
            ((uint8_t *)&t41)[2] = 1;
            gm_l1 = t41;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)8ULL));
            if (!t43) {
                goto gm_lb7;
            }
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)42ULL));
            if (!t45) {
                goto gm_lb7;
            }
            gm_ts6oqppjh0ba3 t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)17ULL));
            gm_tt7vn9ehj0acr t47;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t47), t46);
            ((uint8_t *)&t47)[2] = 1;
            gm_l1 = t47;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)8ULL));
            if (!t49) {
                goto gm_lb8;
            }
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)43ULL));
            if (!t51) {
                goto gm_lb8;
            }
            gm_ts6oqppjh0ba3 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)18ULL));
            gm_tt7vn9ehj0acr t53;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t53), t52);
            ((uint8_t *)&t53)[2] = 1;
            gm_l1 = t53;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)8ULL));
            if (!t55) {
                goto gm_lb9;
            }
            uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)44ULL));
            if (!t57) {
                goto gm_lb9;
            }
            gm_ts6oqppjh0ba3 t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)19ULL));
            gm_tt7vn9ehj0acr t59;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t59), t58);
            ((uint8_t *)&t59)[2] = 1;
            gm_l1 = t59;
            goto gm_lb1;
        }
gm_lb9:;
        {
            gm_tt7vn9ehj0acr t60;
            memset(&t60, 0, sizeof t60);
            gm_l1 = t60;
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 33, 52);
    }
gm_lb1:;
    gm_tt7vn9ehj0acr t61 = gm_l1;
    F.ret = t61;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4_c(gm_t3fcbuehh3bs2 a0) { return gm_f_cmp_op_9gj7im4(&a0); }
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_deny_in_head_06d9o22(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_deny_in_head_06d9o22 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 128));
    if (t3) {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        gm_tl874t2atnlh3 t6 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t5));
        gm_b16_8 t7 = gm_l1;
        gm_tnpifv20irnph t8;
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
        gm_f_format_3nkc0o4((void *)&gm_l2, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src0dtioet3p4f6, 589, 52);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t4, t6, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t13;
        ((uint8_t *)&t13)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t13 + 1), GM_ZERO(gm_b0_1));
        F.ret = t13;
        goto gm_fret;
    }
    gm_t9qkh0rsooc8d t14;
    ((uint8_t *)&t14)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t14 + 1), GM_ZERO(gm_b0_1));
    F.ret = t14;
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
inline __attribute__((aligned(4))) void gm_f_eat_label_7eokqmm(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_label_7eokqmm F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* sym */
    GM_STACK_CHECK_SHALLOW(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3fcbuehh3bs2 gm_l2;
    gm_tm4pc0emhrv1g gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_f_kind_k239kpg((void *)&gm_l2, t4);
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
            gm_l1 = t7;
            gm_b16_8 t8 = gm_l1;
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 88));
            uint8_t t11 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t10) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t10))) + i * 16, ((uint8_t *)&t8))) { t11 = 1; break; }
            uint8_t t12 = (uint8_t)(!t11);
            if (t12) {
                gm_t1s75i08qtptd t13;
                memset(&t13, 0, sizeof t13);
                memmove(gm_out, ((uint8_t *)&t13), 40);
                goto gm_fdone;
            }
            gm_b16_8 t14 = gm_l1;
            void * t15 = gm_l0;
            gm_f_bump_q2i40re((void *)&gm_l3, t15);
            gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 32));
            gm_t08olq97rscn3 t17;
            GM_ST(uint32_t, (uint8_t *)&t17 + 28, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t14);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t17 + 16), t16);
            gm_t1s75i08qtptd t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t18), t17);
            ((uint8_t *)&t18)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t18), 40);
            goto gm_fdone;
        }
gm_lb2:;
        gm_t1s75i08qtptd t19;
        memset(&t19, 0, sizeof t19);
        memmove(gm_out, ((uint8_t *)&t19), 40);
        goto gm_fdone;
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1s75i08qtptd gm_f_eat_label_7eokqmm_c(void * a0) { gm_t1s75i08qtptd gm_r; gm_f_eat_label_7eokqmm(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ends_expr_after_name_5t7p9lg F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    {
        {
            {
                {
                    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
                    if (!t14) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)3ULL));
                    if (!t16) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)4ULL));
                    if (!t18) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)5ULL));
                    if (!t20) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)2ULL));
                    if (!t22) {
                        goto gm_lb8;
                    }
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)19ULL));
                    if (!t24) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
                    if (!t26) {
                        goto gm_lb9;
                    }
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)25ULL));
                    if (!t28) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)2ULL));
                    if (!t30) {
                        goto gm_lb10;
                    }
                    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)10ULL));
                    if (!t32) {
                        goto gm_lb10;
                    }
                    goto gm_lb3;
                }
gm_lb10:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 787, 60);
    }
gm_lb1:;
    uint8_t t33 = gm_l1;
    F.ret = t33;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg_c(gm_t3fcbuehh3bs2 a0) { return gm_f_ends_expr_after_name_5t7p9lg(&a0); }
extern __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_is_arith_l6b4tpf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_arith_l6b4tpf F;
    void * gm_l0; /* e */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    void * t14 = gm_l0;
    {
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)13ULL));
            if (!t16) {
                goto gm_lb2;
            }
            gm_ts6oqppjh0ba3 t17 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t14) + 8) + 8));
            gm_l1 = t17;
            {
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                    if (!t19) {
                        goto gm_lb4;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 26);
            }
gm_lb3:;
            uint8_t t20 = gm_l6;
            if (t20) {
                gm_l5 = ((uint8_t)1);
            } else {
                {
                    {
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
                        if (!t22) {
                            goto gm_lb7;
                        }
                        gm_l7 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        gm_l7 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 43);
                }
gm_lb6:;
                uint8_t t23 = gm_l7;
                gm_l5 = t23;
            }
            uint8_t t24 = gm_l5;
            if (t24) {
                gm_l4 = ((uint8_t)1);
            } else {
                {
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
                        if (!t26) {
                            goto gm_lb10;
                        }
                        gm_l8 = ((uint8_t)1);
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        gm_l8 = ((uint8_t)0);
                        goto gm_lb9;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 60);
                }
gm_lb9:;
                uint8_t t27 = gm_l8;
                gm_l4 = t27;
            }
            uint8_t t28 = gm_l4;
            if (t28) {
                gm_l3 = ((uint8_t)1);
            } else {
                gm_ts6oqppjh0ba3 t29 = gm_l1;
                gm_ts6oqppjh0ba3 t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)4ULL));
                gm_ts6oqppjh0ba3 t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)5ULL));
                gm_t9ldr1q5hn9fq t32;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t32 + 0), t30);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t32 + 2), t31);
                uint8_t t33 = 0;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t32) + 0, ((uint8_t *)&t29))) t33 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t32) + 2, ((uint8_t *)&t29))) t33 = 1;
                uint8_t t34 = (uint8_t)(t33);
                gm_l3 = t34;
            }
            uint8_t t35 = gm_l3;
            gm_l2 = t35;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 99, 47);
    }
gm_lb1:;
    uint8_t t36 = gm_l2;
    F.ret = t36;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_relational_n3goics F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 328u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9qkh0rsooc8d t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)41ULL));
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)42ULL));
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)43ULL));
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)44ULL));
    gm_t3fcbuehh3bs2 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t6 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 8), t2);
    gm_t3fcbuehh3bs2 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t7 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t7 + 8), t3);
    gm_t3fcbuehh3bs2 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t8 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8 + 8), t4);
    gm_t3fcbuehh3bs2 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9 + 8), t5);
    gm_t72hb7duiv9b4 t10;
    GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t10 + 0), t6);
    GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t10 + 32), t7);
    GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t10 + 64), t8);
    GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t10 + 96), t9);
    gm_t3fcbuehh3bs2 t11 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)a0));
    uint8_t t12 = 0;
    if (gm_eqt7ftn1841u(((uint8_t *)&t10) + 0, ((uint8_t *)&t11))) t12 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t10) + 32, ((uint8_t *)&t11))) t12 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t10) + 64, ((uint8_t *)&t11))) t12 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t10) + 96, ((uint8_t *)&t11))) t12 = 1;
    uint8_t t13 = (uint8_t)(t12);
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics_c(gm_t3fcbuehh3bs2 a0) { return gm_f_is_relational_n3goics(&a0); }
inline __attribute__((aligned(4))) void gm_f_mul_from_mlskgvm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_mul_from_mlskgvm F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 686u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tt7vn9ehj0acr gm_l5;
    gm_to1cssm8dfiv5 gm_l6;
    memmove(((uint8_t *)&F.l2), ((uint8_t *)a1), 88);
    {
        for (;;) {
            void * t5 = gm_l0;
            gm_t3fcbuehh3bs2 t6;
            gm_f_kind_k239kpg((void *)&t6, t5);
            gm_tt7vn9ehj0acr t7 = gm_f_mul_op_ab6loj8(&t6);
            gm_l5 = t7;
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l5))[2] != 0);
            if (!t8) {
                goto gm_lb1;
            }
            gm_ts6oqppjh0ba3 t9 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l5));
            gm_l3 = t9;
            {
                void * t10 = gm_l0;
                gm_tm4pc0emhrv1g t11;
                gm_f_bump_q2i40re((void *)&t11, t10);
                void * t12 = gm_l0;
                gm_f_parse_prefix_671fi6d((void *)&gm_l6, (void *)NULL, cx, NULL, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (t13) {
                } else {
                    gm_to1cssm8dfiv5 t14;
                    memset(&t14, 0, sizeof t14);
                    ((uint8_t *)&t14)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t14 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t14), 96);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l6) + 8), 88);
                void * t15 = gm_l0;
                gm_ts6oqppjh0ba3 t16 = gm_l3;
                gm_tc3oht4ksth9d t17 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
                gm_tc3oht4ksth9d t18 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l4));
                gm_f_binary_41fr5r5((void *)&F.l2, (void *)NULL, cx, NULL, t15, t16, &t17, &t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tc3oht4ksth9d t19 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_to1cssm8dfiv5 t20;
    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
    ((uint8_t *)&t20)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t20 + 8), t19);
    memmove(gm_out, ((uint8_t *)&t20), 96);
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mul_op_ab6loj8 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr gm_l1;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)8ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)2ULL));
            if (!t13) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tt7vn9ehj0acr t16;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[2] = 1;
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)8ULL));
            if (!t18) {
                goto gm_lb3;
            }
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)12ULL));
            if (!t20) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t21;
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)1ULL));
            gm_ts6oqppjh0ba3 t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 1), t21);
            gm_tt7vn9ehj0acr t23;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[2] = 1;
            gm_l1 = t23;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)8ULL));
            if (!t25) {
                goto gm_lb4;
            }
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)16ULL));
            if (!t27) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
            gm_ts6oqppjh0ba3 t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 1), t28);
            gm_tt7vn9ehj0acr t30;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t30), t29);
            ((uint8_t *)&t30)[2] = 1;
            gm_l1 = t30;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)8ULL));
            if (!t32) {
                goto gm_lb5;
            }
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)20ULL));
            if (!t34) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t35;
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t36;
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 1), t35);
            gm_tt7vn9ehj0acr t37;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t37), t36);
            ((uint8_t *)&t37)[2] = 1;
            gm_l1 = t37;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)8ULL));
            if (!t39) {
                goto gm_lb6;
            }
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)3ULL));
            if (!t41) {
                goto gm_lb6;
            }
            gm_ts6oqppjh0ba3 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)4ULL));
            gm_tt7vn9ehj0acr t43;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t43), t42);
            ((uint8_t *)&t43)[2] = 1;
            gm_l1 = t43;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)8ULL));
            if (!t45) {
                goto gm_lb7;
            }
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)4ULL));
            if (!t47) {
                goto gm_lb7;
            }
            gm_ts6oqppjh0ba3 t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)5ULL));
            gm_tt7vn9ehj0acr t49;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t49), t48);
            ((uint8_t *)&t49)[2] = 1;
            gm_l1 = t49;
            goto gm_lb1;
        }
gm_lb7:;
        {
            gm_tt7vn9ehj0acr t50;
            memset(&t50, 0, sizeof t50);
            gm_l1 = t50;
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 73, 52);
    }
gm_lb1:;
    gm_tt7vn9ehj0acr t51 = gm_l1;
    F.ret = t51;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8_c(gm_t3fcbuehh3bs2 a0) { return gm_f_mul_op_ab6loj8(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_text_3o86dat F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    gm_t9qkh0rsooc8d gm_l2; /* kw */
    GM_STACK_CHECK_SHALLOW(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 gm_l3;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)8ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t8 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t8;
            gm_t9qkh0rsooc8d t9 = gm_l1;
            gm_b16_8 t10 = gm_f_punct_text_ov4r40u(t9);
            gm_l3 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)2ULL));
            if (!t12) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t13;
            gm_t9qkh0rsooc8d t14 = gm_l2;
            gm_b16_8 t15 = gm_f_keyword_text_149oqn4(t14);
            gm_l3 = t15;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(0));
            gm_l3 = t16;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 252, 49);
    }
gm_lb1:;
    gm_b16_8 t17 = gm_l3;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat_c(gm_t3fcbuehh3bs2 a0) { return gm_f_op_text_3o86dat(&a0); }
inline __attribute__((aligned(4))) void gm_f_parens_0pje0fd(void *gm_out, uint32_t a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parens_0pje0fd F;
    uint32_t gm_l0; /* file */
    uint32_t gm_l1; /* lo */
    uint32_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint32_t t4 = gm_l1;
    gm_tl874t2atnlh3 t5;
    GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t4);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_smooaouqmm8dj));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
    gm_tbgd14esf0jvi t7;
    GM_ST(uint32_t, (uint8_t *)&t7 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t7 + 0), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t6);
    uint32_t t8 = gm_l0;
    uint32_t t9 = gm_l2;
    uint32_t t10 = gm_l2;
    gm_tl874t2atnlh3 t11;
    GM_ST(uint32_t, ((uint8_t *)&t11 + 0), t8);
    GM_ST(uint32_t, ((uint8_t *)&t11 + 4), t9);
    GM_ST(uint32_t, ((uint8_t *)&t11 + 8), t10);
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s1asl1e1tvb0k));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
    gm_tbgd14esf0jvi t13;
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t13 + 0), t11);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t12);
    gm_t0vh9i8m50f41 t14;
    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t14 + 0), t7);
    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t14 + 32), t13);
    memmove(gm_out, ((uint8_t *)&t14), 64);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t0vh9i8m50f41 gm_f_parens_0pje0fd_c(uint32_t a0, uint32_t a1, uint32_t a2) { gm_t0vh9i8m50f41 gm_r; gm_f_parens_0pje0fd(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_parse_add_dh9nvbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_add_dh9nvbt F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 531u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l2;
    void * t2 = gm_l0;
    gm_f_parse_prefix_671fi6d((void *)&gm_l2, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t3) {
    } else {
        gm_to1cssm8dfiv5 t4;
        memset(&t4, 0, sizeof t4);
        ((uint8_t *)&t4)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l2) + 8), 88);
    void * t5 = gm_l0;
    gm_t3fcbuehh3bs2 t6;
    gm_f_kind_k239kpg((void *)&t6, t5);
    uint8_t t7 = gm_f_binary_follows_jeb42bi(&t6);
    uint8_t t8 = (uint8_t)!t7;
    if (t8) {
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
        gm_to1cssm8dfiv5 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        ((uint8_t *)&t10)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t10 + 8), t9);
        memmove(gm_out, ((uint8_t *)&t10), 96);
        goto gm_fdone;
    }
    void * t11 = gm_l0;
    gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
    gm_f_add_from_7os0rvi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t11, &t12);
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
inline __attribute__((aligned(4))) void gm_f_parse_arg_t6fq0nq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_arg_t6fq0nq F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_t08olq97rscn3 gm_l3; /* name */
    GM_STACK_CHECK(&F, 3063u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l6;
    gm_t4jn8fl3lmk5k gm_l7;
    gm_to1cssm8dfiv5 gm_l8;
    gm_to1cssm8dfiv5 gm_l9;
    void * t4 = gm_l0;
    uint32_t t5 = gm_f_lo_fp82gt4(t4);
    gm_l1 = t5;
    void * t6 = gm_l0;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)50ULL));
    uint8_t t8 = gm_f_at_54906rm(t6, t7);
    if (t8) {
        void * t9 = gm_l0;
        gm_tm4pc0emhrv1g t10;
        gm_f_bump_q2i40re((void *)&t10, t9);
        void * t11 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&gm_l6, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (t12) {
        } else {
            gm_t3q68r4ne5jv8 t13;
            memset(&t13, 0, sizeof t13);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t13), 152);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l6) + 8), 88);
        gm_tc3oht4ksth9d t14 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
        void * t15 = gm_l0;
        uint32_t t16 = gm_l1;
        gm_t22mtv3sd9prf t17;
        memset(&t17, 0, sizeof t17);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t17 + 8), t14);
        gm_tl874t2atnlh3 t18 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t15, t16));
        gm_t7te2tdpebbt4 t19;
        GM_ST(uint32_t, (uint8_t *)&t19 + 140, 0);
        GM_ST(gm_t22mtv3sd9prf, ((uint8_t *)&t19 + 0), t17);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t19 + 128), t18);
        gm_t3q68r4ne5jv8 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        ((uint8_t *)&t20)[0] = 0;
        GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)&t20 + 8), t19);
        memmove(gm_out, ((uint8_t *)&t20), 152);
        goto gm_fdone;
    }
    {
        {
            void * t21 = gm_l0;
            uint8_t t22 = gm_f_nth_is_name_oqhoobd(t21, ((uint64_t)0ULL));
            if (!t22) {
                goto gm_lb2;
            }
            void * t23 = gm_l0;
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)56ULL));
            uint8_t t25 = gm_f_nth_is_3fgqmcb(t23, ((uint64_t)1ULL), t24);
            if (!t25) {
                goto gm_lb2;
            }
            void * t26 = gm_l0;
            gm_t3fcbuehh3bs2 t27;
            gm_f_kind_k239kpg((void *)&t27, t26);
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            uint8_t t29 = (uint8_t)((t28 == ((uint8_t)1ULL)));
            gm_t3fcbuehh3bs2 t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t30 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t30 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t30 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)1ULL));
            if (t29) {
                void * t31 = gm_l0;
                void * t32 = gm_l0;
                gm_tl874t2atnlh3 t33 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t32));
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s4niue72anpss));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(36));
                gm_b0_1 t35 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t31, t33, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t36 = gm_l0;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s3tpa8q2q7llg));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(16));
            gm_f_parse_name_figjvbe((void *)&gm_l7, (void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (t38) {
            } else {
                gm_t3q68r4ne5jv8 t39;
                memset(&t39, 0, sizeof t39);
                ((uint8_t *)&t39)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t39), 152);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l7) + 8), 32);
            void * t40 = gm_l0;
            gm_tm4pc0emhrv1g t41;
            gm_f_bump_q2i40re((void *)&t41, t40);
            void * t42 = gm_l0;
            gm_f_parse_expr_bkb2n6e((void *)&gm_l8, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (t43) {
            } else {
                gm_t3q68r4ne5jv8 t44;
                memset(&t44, 0, sizeof t44);
                ((uint8_t *)&t44)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t44 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t44), 152);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l8) + 8), 88);
            gm_t08olq97rscn3 t45 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l3));
            gm_tc3oht4ksth9d t46 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l4));
            gm_t56mj5hnm2uc6 t47;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t47 + 0), t45);
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t47 + 32), t46);
            void * t48 = gm_l0;
            uint32_t t49 = gm_l1;
            gm_t22mtv3sd9prf t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t50 + 8), t47);
            gm_tl874t2atnlh3 t51 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t48, t49));
            gm_t7te2tdpebbt4 t52;
            GM_ST(uint32_t, (uint8_t *)&t52 + 140, 0);
            GM_ST(gm_t22mtv3sd9prf, ((uint8_t *)&t52 + 0), t50);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t52 + 128), t51);
            gm_t3q68r4ne5jv8 t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            ((uint8_t *)&t53)[0] = 0;
            GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)&t53 + 8), t52);
            memmove(gm_out, ((uint8_t *)&t53), 152);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t54 = gm_l0;
    gm_f_parse_expr_bkb2n6e((void *)&gm_l9, (void *)NULL, cx, NULL, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t55) {
    } else {
        gm_t3q68r4ne5jv8 t56;
        memset(&t56, 0, sizeof t56);
        ((uint8_t *)&t56)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t56 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t56), 152);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l9) + 8), 88);
    gm_tc3oht4ksth9d t57 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l5));
    void * t58 = gm_l0;
    uint32_t t59 = gm_l1;
    gm_t22mtv3sd9prf t60;
    memset(&t60, 0, sizeof t60);
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t60 + 8), t57);
    gm_tl874t2atnlh3 t61 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t58, t59));
    gm_t7te2tdpebbt4 t62;
    GM_ST(uint32_t, (uint8_t *)&t62 + 140, 0);
    GM_ST(gm_t22mtv3sd9prf, ((uint8_t *)&t62 + 0), t60);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t62 + 128), t61);
    gm_t3q68r4ne5jv8 t63;
    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
    ((uint8_t *)&t63)[0] = 0;
    GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)&t63 + 8), t62);
    memmove(gm_out, ((uint8_t *)&t63), 152);
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
inline __attribute__((aligned(4))) void gm_f_parse_bang_irmkots(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_bang_irmkots F;
    uint32_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 738u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
    gm_l2 = t4;
    void * t5 = F.l0;
    gm_t3fcbuehh3bs2 t6;
    gm_f_nth_c0r2nvs((void *)&t6, t5, ((uint64_t)1ULL));
    uint8_t t7 = gm_f_can_begin_type_a3rjrm7(&t6);
    if (t7) {
        void * t8 = F.l0;
        gm_closure t9 = ((gm_closure){(void *)gm_f_fn_1s091rv, (void *)&F.h, cx});
        gm_f_speculate_djd8kc6((void *)&F.l3, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t10 = (uint8_t)((((uint8_t *)&F.l3))[88] != 0);
                if (!t10) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l4), ((uint8_t *)&F.l3), 88);
                void * t11 = F.l0;
                void * t12 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t13 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3mf58gv9kf3n t14;
                GM_ST(void *, ((uint8_t *)&t14 + 0), t12);
                GM_ST(void *, ((uint8_t *)&t14 + 8), t13);
                void * t15 = F.l0;
                uint32_t t16 = gm_l2;
                gm_t2gtbh39ptekd t17;
                memset(&t17, 0, sizeof t17);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)17ULL));
                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t17 + 8), t14);
                gm_tl874t2atnlh3 t18 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t15, t16));
                gm_tc3oht4ksth9d t19;
                gm_f_mk_expr_53qj7nd((void *)&t19, t11, &t17, GM_CONV(gm_cseahvg8v7p0vs, t18));
                gm_to1cssm8dfiv5 t20;
                GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                ((uint8_t *)&t20)[0] = 0;
                GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t20 + 8), t19);
                memmove(gm_out, ((uint8_t *)&t20), 96);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t21 = F.l0;
    gm_tm4pc0emhrv1g t22;
    gm_f_bump_q2i40re((void *)&t22, t21);
    void * t23 = F.l0;
    void * t24 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t25 = F.l0;
    uint32_t t26 = gm_l2;
    gm_t2gtbh39ptekd t27;
    memset(&t27, 0, sizeof t27);
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)16ULL));
    GM_ST(void *, ((uint8_t *)&t27 + 8), t24);
    gm_tl874t2atnlh3 t28 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t25, t26));
    gm_tc3oht4ksth9d t29;
    gm_f_mk_expr_53qj7nd((void *)&t29, t23, &t27, GM_CONV(gm_cseahvg8v7p0vs, t28));
    gm_to1cssm8dfiv5 t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
    ((uint8_t *)&t30)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t30 + 8), t29);
    memmove(gm_out, ((uint8_t *)&t30), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_bit_h83vs72(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_bit_h83vs72 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 531u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l2;
    void * t2 = gm_l0;
    gm_f_parse_prefix_671fi6d((void *)&gm_l2, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t3) {
    } else {
        gm_to1cssm8dfiv5 t4;
        memset(&t4, 0, sizeof t4);
        ((uint8_t *)&t4)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l2) + 8), 88);
    void * t5 = gm_l0;
    gm_t3fcbuehh3bs2 t6;
    gm_f_kind_k239kpg((void *)&t6, t5);
    uint8_t t7 = gm_f_binary_follows_jeb42bi(&t6);
    uint8_t t8 = (uint8_t)!t7;
    if (t8) {
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
        gm_to1cssm8dfiv5 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        ((uint8_t *)&t10)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t10 + 8), t9);
        memmove(gm_out, ((uint8_t *)&t10), 96);
        goto gm_fdone;
    }
    void * t11 = gm_l0;
    gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
    gm_f_bit_from_pu2bq19((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t11, &t12);
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
inline __attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_block_expr_0j6b78q F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 592u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l2;
    gm_toc4mrkn2e7o9 gm_l3;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saojtqmfeak72));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t5) {
    } else {
        gm_to1cssm8dfiv5 t6;
        memset(&t6, 0, sizeof t6);
        ((uint8_t *)&t6)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t6), 96);
        goto gm_fdone;
    }
    void * t7 = gm_l0;
    gm_f_parse_block_tpg3f1i((void *)&gm_l3, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t8) {
    } else {
        gm_to1cssm8dfiv5 t9;
        memset(&t9, 0, sizeof t9);
        ((uint8_t *)&t9)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t9), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l3) + 8), 40);
    void * t10 = gm_l0;
    gm_t79rbdv4rafe5 t11 = GM_LD(gm_t79rbdv4rafe5, ((uint8_t *)&F.l1));
    gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 24));
    gm_t2gtbh39ptekd t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t13 + 48, 0);
    GM_ST(uint64_t, (uint8_t *)&t13 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)44ULL));
    GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t13 + 8), t11);
    gm_tc3oht4ksth9d t14;
    gm_f_mk_expr_53qj7nd((void *)&t14, t10, &t13, GM_CONV(gm_cseahvg8v7p0vs, t12));
    gm_to1cssm8dfiv5 t15;
    GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
    ((uint8_t *)&t15)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
    memmove(gm_out, ((uint8_t *)&t15), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_break_hehputf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_break_hehputf F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* sym */
    void * gm_l4; /* value */
    GM_STACK_CHECK(&F, 1053u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    gm_t3fcbuehh3bs2 gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_to1cssm8dfiv5 gm_l8;
    void * t7 = gm_l0;
    uint32_t t8 = gm_f_lo_fp82gt4(t7);
    gm_l1 = t8;
    void * t9 = gm_l0;
    gm_tm4pc0emhrv1g t10;
    gm_f_bump_q2i40re((void *)&t10, t9);
    void * t11 = gm_l0;
    gm_f_eat_label_7eokqmm((void *)&F.l2, t11);
    {
        {
            uint8_t t12 = (uint8_t)((((uint8_t *)&F.l2))[32] != 0);
            if (t12) {
                goto gm_lb2;
            }
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 773, 8);
    }
gm_lb1:;
    uint8_t t13 = gm_l5;
    if (t13) {
        {
            {
                void * t14 = gm_l0;
                gm_f_kind_k239kpg((void *)&gm_l6, t14);
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
                if (!t16) {
                    goto gm_lb5;
                }
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_l3 = t17;
                void * t18 = gm_l0;
                gm_t3fcbuehh3bs2 t19;
                gm_f_nth_c0r2nvs((void *)&t19, t18, ((uint64_t)1ULL));
                uint8_t t20 = gm_f_ends_expr_after_name_5t7p9lg(&t19);
                if (t20) {
                    void * t21 = gm_l0;
                    void * t22 = gm_l0;
                    gm_tl874t2atnlh3 t23 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t22));
                    gm_b16_8 t24 = gm_l3;
                    gm_tnpifv20irnph t25;
                    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
                    gm_f_format_pb6f6q3((void *)&gm_l7, (void *)NULL, cx, NULL, t25);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src0dtioet3p4f6, 776, 42);
                    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                    gm_b0_1 t29 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t21, t23, t28);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_to1cssm8dfiv5 t30;
                    memset(&t30, 0, sizeof t30);
                    ((uint8_t *)&t30)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t30 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t30), 96);
                    goto gm_fdone;
                }
                goto gm_lb4;
            }
gm_lb5:;
        }
gm_lb4:;
    }
    gm_l4 = ((void *)0);
    void * t31 = gm_l0;
    gm_t3fcbuehh3bs2 t32;
    gm_f_kind_k239kpg((void *)&t32, t31);
    uint8_t t33 = gm_f_can_begin_expr_rmia9bs(&t32);
    if (t33) {
        void * t34 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&gm_l8, (void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (t35) {
        } else {
            gm_to1cssm8dfiv5 t36;
            memset(&t36, 0, sizeof t36);
            ((uint8_t *)&t36)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t36 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t36), 96);
            goto gm_fdone;
        }
        void * t37 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l8) + 8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t37;
    }
    void * t38 = gm_l0;
    void * t39 = gm_l4;
    gm_t1s75i08qtptd t40 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)&F.l2));
    gm_tchr52g0ema53 t41;
    GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t41 + 0), t40);
    GM_ST(void *, ((uint8_t *)&t41 + 40), t39);
    void * t42 = gm_l0;
    uint32_t t43 = gm_l1;
    gm_t2gtbh39ptekd t44;
    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t44 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)46ULL));
    GM_ST(gm_tchr52g0ema53, ((uint8_t *)&t44 + 8), t41);
    gm_tl874t2atnlh3 t45 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t42, t43));
    gm_tc3oht4ksth9d t46;
    gm_f_mk_expr_53qj7nd((void *)&t46, t38, &t44, GM_CONV(gm_cseahvg8v7p0vs, t45));
    gm_to1cssm8dfiv5 t47;
    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
    ((uint8_t *)&t47)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t47 + 8), t46);
    memmove(gm_out, ((uint8_t *)&t47), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_call_args_32e205h F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    uint8_t gm_l3; /* trailing */
    GM_STACK_CHECK(&F, 593u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t0u0j6voh3loe gm_l4;
    gm_t3q68r4ne5jv8 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_t0u0j6voh3loe gm_l7;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)61ULL));
        gm_t0u0j6voh3loe t10 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t7, t9, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t10;
        uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (t11) {
        } else {
            gm_thk954m1ol1ig t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t12 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t12), 32);
            goto gm_fdone;
        }
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 4));
        void * t14 = gm_l0;
        gm_toh0ootodhjue t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t16 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t14, GM_CONV(gm_cs3cuou245tr5f, t15)));
        gm_l1 = t16;
        gm_trqfvmjtt4sij t17;
        gm_f_list_6oos54v((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld6_1;
        }
        F.l2 = t17;
        gm_l3 = ((uint8_t)0);
        {
            for (;;) {
                void * t18 = gm_l0;
                gm_t9qkh0rsooc8d t19;
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)62ULL));
                uint8_t t20 = gm_f_at_54906rm(t18, t19);
                uint8_t t21 = (uint8_t)!t20;
                if (!t21) {
                    goto gm_lb2;
                }
                {
                    void * t22 = (void *)((uint8_t *)&F.l2);
                    void * t23 = gm_l0;
                    gm_f_parse_arg_t6fq0nq((void *)&gm_l5, (void *)NULL, cx, NULL, t23);
                    if (gm_unw.kind) {
                        goto gm_ld6_1;
                    }
                    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                    if (t24) {
                    } else {
                        gm_thk954m1ol1ig t25;
                        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
                        ((uint8_t *)&t25)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), GM_ZERO(gm_b0_1));
                        memmove(&F.ret, ((uint8_t *)&t25), 32);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld6_1;
                    }
                    gm_tftb1odc739t5 t26 = gm_f_push_jis5lhu(t22, &(*(gm_t7te2tdpebbt4 *)(((uint8_t *)&gm_l5) + 8)));
                    gm_l6 = t26;
                    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src0dtioet3p4f6, 558, 9);
                    gm_l3 = ((uint8_t)0);
                    void * t28 = gm_l0;
                    gm_t9qkh0rsooc8d t29;
                    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)57ULL));
                    uint8_t t30 = gm_f_eat_qmock9g(t28, t29);
                    uint8_t t31 = (uint8_t)!t30;
                    if (t31) {
                        goto gm_lb2;
                    }
                    gm_l3 = ((uint8_t)1);
                }
gm_lb4:;
            }
        }
gm_lb2:;
        void * t32 = gm_l0;
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s5us2hgd173vu));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(26));
        gm_t9qkh0rsooc8d t34;
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)62ULL));
        gm_t0u0j6voh3loe t35 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t32, t34, t33);
        if (gm_unw.kind) {
            goto gm_ld6_1;
        }
        gm_l7 = t35;
        uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (t36) {
        } else {
            gm_thk954m1ol1ig t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 24, 0);
            ((uint8_t *)&t37)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t37 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t37), 32);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld6_1;
        }
        gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 4));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        uint8_t t40 = gm_l3;
        gm_th2lqbo77pvsc t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t39);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 16), t40);
        gm_thk954m1ol1ig t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        ((uint8_t *)&t42)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t42 + 8), t41);
        memmove(&F.ret, ((uint8_t *)&t42), 32);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld6_1;
    }
    {
        void * t43 = gm_l0;
        gm_toh0ootodhjue t44 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t43) + 128), t44);
    }
    goto gm_ls45;
gm_ld6_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t46 = gm_l0;
        gm_toh0ootodhjue t47 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t46) + 128), t47);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls45:;
gm_lb1:;
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
extern __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cmp_q5clpig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_cmp_q5clpig F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* start_pos */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    uint8_t gm_l5; /* first_rel */
    gm_tl874t2atnlh3 gm_l8; /* at */
    uint8_t gm_l10; /* negated */
    gm_th2lqbo77pvsc gm_l11; /* fix */
    gm_t08olq97rscn3 gm_l12; /* mid */
    int32_t gm_l13; /* count */
    gm_b16_8 gm_l14; /* text */
    GM_STACK_CHECK(&F, 3069u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l15;
    gm_to1cssm8dfiv5 gm_l16;
    gm_tt7vn9ehj0acr gm_l17;
    gm_ts6oqppjh0ba3 gm_l18;
    gm_to1cssm8dfiv5 gm_l19;
    uint8_t gm_l20;
    gm_tt7vn9ehj0acr gm_l21;
    uint8_t gm_l22;
    gm_tabb710d9mq2e gm_l23;
    uint8_t gm_l24;
    gm_tt7vn9ehj0acr gm_l25;
    gm_to1cssm8dfiv5 gm_l26;
    void * t17 = gm_l0;
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 72));
    gm_l1 = t18;
    void * t19 = gm_l0;
    gm_f_parse_prefix_671fi6d((void *)&gm_l15, (void *)NULL, cx, NULL, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (t20) {
    } else {
        gm_to1cssm8dfiv5 t21;
        memset(&t21, 0, sizeof t21);
        ((uint8_t *)&t21)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t21), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l15) + 8), 88);
    void * t22 = gm_l0;
    gm_t3fcbuehh3bs2 t23;
    gm_f_kind_k239kpg((void *)&t23, t22);
    uint8_t t24 = gm_f_binary_follows_jeb42bi(&t23);
    uint8_t t25 = (uint8_t)!t24;
    if (t25) {
        gm_tc3oht4ksth9d t26 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
        gm_to1cssm8dfiv5 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        ((uint8_t *)&t27)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t27 + 8), t26);
        memmove(gm_out, ((uint8_t *)&t27), 96);
        goto gm_fdone;
    }
    void * t28 = gm_l0;
    gm_tc3oht4ksth9d t29 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_f_range_from_5v6ko9m((void *)&gm_l16, (void *)NULL, cx, NULL, t28, &t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (t30) {
    } else {
        gm_to1cssm8dfiv5 t31;
        memset(&t31, 0, sizeof t31);
        ((uint8_t *)&t31)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t31), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l16) + 8), 88);
    void * t32 = gm_l0;
    gm_t3fcbuehh3bs2 t33;
    gm_f_kind_k239kpg((void *)&t33, t32);
    gm_tt7vn9ehj0acr t34 = gm_f_cmp_op_9gj7im4(&t33);
    gm_l17 = t34;
    uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l17))[2] != 0);
    if (t35) {
        gm_ts6oqppjh0ba3 t36 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l17));
        gm_l18 = t36;
    } else {
        gm_tc3oht4ksth9d t37 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
        gm_to1cssm8dfiv5 t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
        ((uint8_t *)&t38)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t38 + 8), t37);
        memmove(gm_out, ((uint8_t *)&t38), 96);
        goto gm_fdone;
    }
    gm_ts6oqppjh0ba3 t39 = gm_l18;
    gm_l4 = t39;
    void * t40 = gm_l0;
    gm_t3fcbuehh3bs2 t41;
    gm_f_kind_k239kpg((void *)&t41, t40);
    uint8_t t42 = gm_f_is_relational_n3goics(&t41);
    gm_l5 = t42;
    void * t43 = gm_l0;
    gm_tm4pc0emhrv1g t44;
    gm_f_bump_q2i40re((void *)&t44, t43);
    void * t45 = gm_l0;
    gm_f_parse_range_o9o2al7((void *)&gm_l19, (void *)NULL, cx, NULL, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (t46) {
    } else {
        gm_to1cssm8dfiv5 t47;
        memset(&t47, 0, sizeof t47);
        ((uint8_t *)&t47)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t47 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t47), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l19) + 8), 88);
    void * t48 = gm_l0;
    gm_ts6oqppjh0ba3 t49 = gm_l4;
    gm_tc3oht4ksth9d t50 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
    gm_tc3oht4ksth9d t51 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l6));
    gm_f_binary_41fr5r5((void *)&F.l7, (void *)NULL, cx, NULL, t48, t49, &t50, &t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t52 = gm_l0;
    gm_t3fcbuehh3bs2 t53;
    gm_f_kind_k239kpg((void *)&t53, t52);
    gm_tt7vn9ehj0acr t54 = gm_f_cmp_op_9gj7im4(&t53);
    gm_l21 = t54;
    {
        {
            uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l21))[2] != 0);
            if (t55) {
                goto gm_lb2;
            }
            gm_l20 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l20 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 225, 8);
    }
gm_lb1:;
    uint8_t t56 = gm_l20;
    if (t56) {
        gm_tc3oht4ksth9d t57 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l7));
        gm_to1cssm8dfiv5 t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        ((uint8_t *)&t58)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t58 + 8), t57);
        memmove(gm_out, ((uint8_t *)&t58), 96);
        goto gm_fdone;
    }
    void * t59 = gm_l0;
    gm_tl874t2atnlh3 t60 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t59));
    gm_l8 = t60;
    gm_tl874t2atnlh3 t61 = gm_l8;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s5lrfo6g7933d));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(49));
    gm_t4gut2c9fj1kt t63;
    gm_f_error_s6hn871((void *)&t63, (void *)NULL, cx, NULL, t61, t62);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t63;
    uint64_t t64 = gm_l1;
    uint8_t t65 = (t64 > ((uint64_t)0ULL));
    if (t65) {
        void * t66 = gm_l0;
        void * t67 = GM_LD(void *, (((uint8_t *)t66) + 8));
        uint64_t t68 = GM_LD(uint64_t, ((((uint8_t *)t66) + 8) + 8));
        uint64_t t69 = gm_l1;
        uint64_t r70; bool o71 = __builtin_sub_overflow(t69, ((uint64_t)1ULL), &r70);
        if (o71) gm_trap("integer overflow in `-`", gm_src0dtioet3p4f6, 231, 41);
        uint64_t t72 = r70;
        if ((uintptr_t)t72 >= (uintptr_t)(t68)) gm_trap_index((uintptr_t)t72, t68, gm_src0dtioet3p4f6, 231, 34);
        gm_t9qkh0rsooc8d t73;
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)18ULL));
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t67) + (uintptr_t)t72 * 48) + 0));
        uint8_t t75 = (uint8_t)(t74 == ((uint8_t)2ULL));
        if (t75) {
            gm_t9qkh0rsooc8d t76 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t67) + (uintptr_t)t72 * 48) + 8));
            t75 = (uint8_t)gm_eq2ceic3f8rp(((uint8_t *)&t76), ((uint8_t *)&t73));
        }
        uint8_t t77 = (uint8_t)(t75);
        gm_t3fcbuehh3bs2 t78 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)t67) + (uintptr_t)t72 * 48));
        gm_t3fcbuehh3bs2 t79;
        GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t79 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t79 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t79 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t79 + 8), t73);
        gm_l22 = t77;
    } else {
        gm_l22 = ((uint8_t)0);
    }
    uint8_t t80 = gm_l22;
    gm_l10 = t80;
    gm_th2lqbo77pvsc t81;
    memset(&t81, 0, sizeof t81);
    gm_l11 = t81;
    {
        {
            uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 8) + 0));
            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)6ULL));
            if (!t83) {
                goto gm_lb5;
            }
            memmove(((uint8_t *)&gm_l12), ((((uint8_t *)&F.l6) + 8) + 8), 32);
            {
                {
                    uint8_t t84 = gm_l5;
                    if (!t84) {
                        goto gm_lb7;
                    }
                    void * t85 = gm_l0;
                    gm_t3fcbuehh3bs2 t86;
                    gm_f_kind_k239kpg((void *)&t86, t85);
                    uint8_t t87 = gm_f_is_relational_n3goics(&t86);
                    if (!t87) {
                        goto gm_lb7;
                    }
                    uint8_t t88 = gm_l10;
                    uint8_t t89 = (uint8_t)!t88;
                    if (!t89) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                    void * t91 = gm_l0;
                    gm_t3fcbuehh3bs2 t92;
                    gm_f_kind_k239kpg((void *)&t92, t91);
                    gm_b16_8 t93 = gm_f_op_text_3o86dat(&t92);
                    gm_t6j6smj7qoctu t94;
                    GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t90);
                    GM_ST(gm_b16_8, ((uint8_t *)&t94 + 16), t93);
                    gm_f_format_5k9iv40((void *)&gm_l23, (void *)NULL, cx, NULL, &t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                    if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src0dtioet3p4f6, 235, 19);
                    gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                    gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                    gm_th2lqbo77pvsc t98;
                    GM_ST(uint8_t, (uint8_t *)&t98 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t98 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t98 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t98), t97);
                    ((uint8_t *)&t98)[16] = 1;
                    gm_l11 = t98;
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    gm_l13 = ((int32_t)0LL);
    {
        for (;;) {
            void * t99 = gm_l0;
            gm_t3fcbuehh3bs2 t100;
            gm_f_kind_k239kpg((void *)&t100, t99);
            gm_tt7vn9ehj0acr t101 = gm_f_cmp_op_9gj7im4(&t100);
            gm_l25 = t101;
            {
                {
                    uint8_t t102 = (uint8_t)((((uint8_t *)&gm_l25))[2] != 0);
                    if (t102) {
                        goto gm_lb12;
                    }
                    gm_l24 = ((uint8_t)0);
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    gm_l24 = ((uint8_t)1);
                    goto gm_lb11;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 239, 11);
            }
gm_lb11:;
            uint8_t t103 = gm_l24;
            if (!t103) {
                goto gm_lb8;
            }
            {
                void * t104 = gm_l0;
                gm_tm4pc0emhrv1g t105;
                gm_f_bump_q2i40re((void *)&t105, t104);
                void * t106 = gm_l0;
                gm_f_parse_range_o9o2al7((void *)&gm_l26, (void *)NULL, cx, NULL, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                if (t107) {
                } else {
                    gm_to1cssm8dfiv5 t108;
                    memset(&t108, 0, sizeof t108);
                    ((uint8_t *)&t108)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t108 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t108), 96);
                    goto gm_fdone;
                }
                int32_t t109 = gm_l13;
                int32_t r110; bool o111 = __builtin_add_overflow(t109, ((int32_t)1LL), &r110);
                if (o111) gm_trap("integer overflow in `+`", gm_src0dtioet3p4f6, 242, 9);
                int32_t t112 = r110;
                gm_tc3oht4ksth9d t113 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&gm_l26) + 8));
                gm_l13 = t112;
            }
gm_lb10:;
        }
    }
gm_lb8:;
    {
        {
            uint8_t t114 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
            if (!t114) {
                goto gm_lb15;
            }
            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l14 = t115;
            int32_t t116 = gm_l13;
            uint8_t t117 = ((t116 == ((int32_t)1LL)));
            if (t117) {
                void * t118 = (void *)((uint8_t *)&F.l9);
                gm_b16_8 t119;
                GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s27pt4vcvtscl));
                GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(23));
                gm_tl874t2atnlh3 t120 = gm_l8;
                gm_b16_8 t121 = gm_l14;
                gm_b0_1 t122 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t118, t119, t120, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb14;
        }
gm_lb15:;
    }
gm_lb14:;
    void * t123 = gm_l0;
    gm_f_report_diag_big1nsf(t123, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
    gm_tc3oht4ksth9d t124 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l7));
    gm_to1cssm8dfiv5 t125;
    GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
    ((uint8_t *)&t125)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t125 + 8), t124);
    memmove(gm_out, ((uint8_t *)&t125), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_continue_ertnf67(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_continue_ertnf67 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* sym */
    GM_STACK_CHECK(&F, 698u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_t3fcbuehh3bs2 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t7 = gm_l0;
    uint32_t t8 = gm_f_lo_fp82gt4(t7);
    gm_l1 = t8;
    void * t9 = gm_l0;
    gm_tm4pc0emhrv1g t10;
    gm_f_bump_q2i40re((void *)&t10, t9);
    void * t11 = gm_l0;
    gm_f_eat_label_7eokqmm((void *)&F.l2, t11);
    {
        {
            uint8_t t12 = (uint8_t)((((uint8_t *)&F.l2))[32] != 0);
            if (t12) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 796, 8);
    }
gm_lb1:;
    uint8_t t13 = gm_l4;
    if (t13) {
        {
            {
                void * t14 = gm_l0;
                gm_f_kind_k239kpg((void *)&gm_l5, t14);
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
                if (!t16) {
                    goto gm_lb5;
                }
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                gm_l3 = t17;
                void * t18 = gm_l0;
                void * t19 = gm_l0;
                gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t19));
                gm_b16_8 t21 = gm_l3;
                gm_tnpifv20irnph t22;
                GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
                gm_f_format_pb6f6q3((void *)&gm_l6, (void *)NULL, cx, NULL, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src0dtioet3p4f6, 798, 38);
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                gm_b0_1 t26 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t18, t20, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to1cssm8dfiv5 t27;
                memset(&t27, 0, sizeof t27);
                ((uint8_t *)&t27)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t27), 96);
                goto gm_fdone;
            }
gm_lb5:;
        }
gm_lb4:;
    }
    void * t28 = gm_l0;
    gm_t1s75i08qtptd t29 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)&F.l2));
    void * t30 = gm_l0;
    uint32_t t31 = gm_l1;
    gm_t2gtbh39ptekd t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t32 + 48, 0);
    GM_ST(uint64_t, (uint8_t *)&t32 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)47ULL));
    GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t32 + 8), t29);
    gm_tl874t2atnlh3 t33 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t30, t31));
    gm_tc3oht4ksth9d t34;
    gm_f_mk_expr_53qj7nd((void *)&t34, t28, &t32, GM_CONV(gm_cseahvg8v7p0vs, t33));
    gm_to1cssm8dfiv5 t35;
    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
    ((uint8_t *)&t35)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t35 + 8), t34);
    memmove(gm_out, ((uint8_t *)&t35), 96);
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
__attribute__((aligned(4))) void gm_f_parse_dot_suffix_1fk204i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_dot_suffix_1fk204i F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* n */
    gm_t9qkh0rsooc8d gm_l5; /* k */
    gm_t08olq97rscn3 gm_l6; /* name */
    void * gm_l7; /* l */
    uint32_t gm_l8; /* index */
    uint64_t gm_l9; /* v */
    GM_STACK_CHECK(&F, 1678u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_t3fcbuehh3bs2 gm_l10;
    gm_tf5srh4mp4b3i gm_l11;
    uint8_t gm_l12;
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
    gm_l2 = t12;
    void * t13 = gm_l0;
    gm_tm4pc0emhrv1g t14;
    gm_f_bump_q2i40re((void *)&t14, t13);
    void * t15 = gm_l0;
    gm_tl874t2atnlh3 t16 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t15));
    gm_l3 = t16;
    void * t17 = gm_l0;
    gm_f_kind_k239kpg((void *)&gm_l10, t17);
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
            if (!t19) {
                goto gm_lb2;
            }
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
            gm_l4 = t20;
            void * t21 = gm_l0;
            gm_tm4pc0emhrv1g t22;
            gm_f_bump_q2i40re((void *)&t22, t21);
            void * t23 = gm_l0;
            void * t24 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t25 = gm_l4;
            gm_tl874t2atnlh3 t26 = gm_l3;
            gm_t08olq97rscn3 t27;
            GM_ST(uint32_t, (uint8_t *)&t27 + 28, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t25);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t27 + 16), t26);
            gm_tioie2ju3q4ul t28;
            GM_ST(void *, ((uint8_t *)&t28 + 0), t24);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t28 + 8), t27);
            void * t29 = gm_l0;
            uint32_t t30 = gm_l2;
            gm_t2gtbh39ptekd t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t31 + 8), t28);
            gm_tl874t2atnlh3 t32 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t29, t30));
            gm_tc3oht4ksth9d t33;
            gm_f_mk_expr_53qj7nd((void *)&t33, t23, &t31, GM_CONV(gm_cseahvg8v7p0vs, t32));
            gm_to1cssm8dfiv5 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            ((uint8_t *)&t34)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t34 + 8), t33);
            memmove(gm_out, ((uint8_t *)&t34), 96);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)2ULL));
            if (!t36) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l10) + 8));
            gm_l5 = t37;
            void * t38 = gm_l0;
            gm_t9qkh0rsooc8d t39 = gm_l5;
            gm_f_keyword_as_name_ipm23p9((void *)&gm_l6, (void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t40 = gm_l0;
            void * t41 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t08olq97rscn3 t42 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l6));
            gm_tioie2ju3q4ul t43;
            GM_ST(void *, ((uint8_t *)&t43 + 0), t41);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t43 + 8), t42);
            void * t44 = gm_l0;
            uint32_t t45 = gm_l2;
            gm_t2gtbh39ptekd t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t46 + 8), t43);
            gm_tl874t2atnlh3 t47 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t44, t45));
            gm_tc3oht4ksth9d t48;
            gm_f_mk_expr_53qj7nd((void *)&t48, t40, &t46, GM_CONV(gm_cseahvg8v7p0vs, t47));
            gm_to1cssm8dfiv5 t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            ((uint8_t *)&t49)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t49 + 8), t48);
            memmove(gm_out, ((uint8_t *)&t49), 96);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
            if (!t51) {
                goto gm_lb4;
            }
            void * t52 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
            gm_l7 = t52;
            void * t53 = gm_l0;
            gm_tm4pc0emhrv1g t54;
            gm_f_bump_q2i40re((void *)&t54, t53);
            gm_l8 = ((uint32_t)0ULL);
            {
                {
                    void * t55 = gm_l7;
                    gm_tf5srh4mp4b3i t56 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)t55)));
                    gm_l11 = t56;
                    uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
                    if (!t57) {
                        goto gm_lb6;
                    }
                    uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                    gm_l9 = t58;
                    uint64_t t59 = gm_l9;
                    uint8_t t60 = (t59 <= ((uint64_t)4294967295ULL));
                    if (!t60) {
                        goto gm_lb6;
                    }
                    void * t61 = gm_l7;
                    {
                        {
                            uint8_t t62 = (uint8_t)(((((uint8_t *)t61) + 48))[8] != 0);
                            if (t62) {
                                goto gm_lb8;
                            }
                            gm_l12 = ((uint8_t)1);
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            gm_l12 = ((uint8_t)0);
                            goto gm_lb7;
                        }
gm_lb9:;
                        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 470, 63);
                    }
gm_lb7:;
                    uint8_t t63 = gm_l12;
                    if (!t63) {
                        goto gm_lb6;
                    }
                    uint64_t t64 = gm_l9;
                    uint32_t t65 = (uint32_t)t64;
                    gm_l8 = t65;
                    goto gm_lb5;
                }
gm_lb6:;
                void * t66 = gm_l0;
                gm_tl874t2atnlh3 t67 = gm_l3;
                gm_b16_8 t68;
                GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sbckhsd8r0nr0));
                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(19));
                gm_b0_1 t69 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t66, t67, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb5:;
            void * t70 = gm_l0;
            void * t71 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t72 = gm_l8;
            gm_tl874t2atnlh3 t73 = gm_l3;
            gm_tupk62cr6lbdu t74;
            GM_ST(void *, ((uint8_t *)&t74 + 0), t71);
            GM_ST(uint32_t, ((uint8_t *)&t74 + 8), t72);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t74 + 12), t73);
            void * t75 = gm_l0;
            uint32_t t76 = gm_l2;
            gm_t2gtbh39ptekd t77;
            memset(&t77, 0, sizeof t77);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)23ULL));
            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t77 + 8), t74);
            gm_tl874t2atnlh3 t78 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t75, t76));
            gm_tc3oht4ksth9d t79;
            gm_f_mk_expr_53qj7nd((void *)&t79, t70, &t77, GM_CONV(gm_cseahvg8v7p0vs, t78));
            gm_to1cssm8dfiv5 t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            ((uint8_t *)&t80)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t80 + 8), t79);
            memmove(gm_out, ((uint8_t *)&t80), 96);
            goto gm_fdone;
        }
gm_lb4:;
        {
            void * t81 = gm_l0;
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s8vhc5kn5i8tv));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(37));
            gm_b0_1 t83 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t84;
            memset(&t84, 0, sizeof t84);
            ((uint8_t *)&t84)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t84 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t84), 96);
            goto gm_fdone;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 458, 5);
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_parse_else_smoqgg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_else_smoqgg4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 1440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l4;
    gm_t4jn8fl3lmk5k gm_l5;
    uint8_t gm_l6;
    gm_to1cssm8dfiv5 gm_l7;
    void * t7 = gm_l0;
    gm_f_parse_logic_ajejref((void *)&gm_l4, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t8) {
    } else {
        gm_to1cssm8dfiv5 t9;
        memset(&t9, 0, sizeof t9);
        ((uint8_t *)&t9)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t9), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l4) + 8), 88);
    void * t10 = gm_l0;
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)7ULL));
    uint8_t t12 = gm_f_at_kw_p0r3sr9(t10, t11);
    uint8_t t13 = (uint8_t)!t12;
    if (t13) {
        gm_tc3oht4ksth9d t14 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
        gm_to1cssm8dfiv5 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        ((uint8_t *)&t15)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
        memmove(gm_out, ((uint8_t *)&t15), 96);
        goto gm_fdone;
    }
    void * t16 = gm_l0;
    gm_tm4pc0emhrv1g t17;
    gm_f_bump_q2i40re((void *)&t17, t16);
    gm_t1s75i08qtptd t18;
    memset(&t18, 0, sizeof t18);
    F.l2 = t18;
    {
        {
            void * t19 = gm_l0;
            uint8_t t20 = gm_f_nth_is_name_oqhoobd(t19, ((uint64_t)0ULL));
            if (!t20) {
                goto gm_lb2;
            }
            void * t21 = gm_l0;
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)59ULL));
            uint8_t t23 = gm_f_nth_is_3fgqmcb(t21, ((uint64_t)1ULL), t22);
            if (!t23) {
                goto gm_lb2;
            }
            void * t24 = gm_l0;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s1pvh8pfrs3ir));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(6));
            gm_f_parse_name_figjvbe((void *)&gm_l5, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (t26) {
            } else {
                gm_to1cssm8dfiv5 t27;
                memset(&t27, 0, sizeof t27);
                ((uint8_t *)&t27)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t27), 96);
                goto gm_fdone;
            }
            gm_t08olq97rscn3 t28 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l5) + 8));
            gm_t1s75i08qtptd t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[32] = 1;
            F.l2 = t29;
            void * t30 = gm_l0;
            gm_tm4pc0emhrv1g t31;
            gm_f_bump_q2i40re((void *)&t31, t30);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t32 = gm_l0;
    {
        {
            uint8_t t33 = (uint8_t)((((uint8_t *)&F.l2))[32] != 0);
            if (t33) {
                goto gm_lb4;
            }
            gm_l6 = ((uint8_t)0);
            goto gm_lb3;
        }
gm_lb4:;
        {
            gm_l6 = ((uint8_t)1);
            goto gm_lb3;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 135, 33);
    }
gm_lb3:;
    uint8_t t34 = gm_l6;
    gm_f_parse_fallback_g1vpkra((void *)&gm_l7, (void *)NULL, cx, NULL, t32, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t35) {
    } else {
        gm_to1cssm8dfiv5 t36;
        memset(&t36, 0, sizeof t36);
        ((uint8_t *)&t36)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t36), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l7) + 8), 88);
    void * t37 = gm_l0;
    void * t38 = (void *)((uint8_t *)&F.l1);
    void * t39 = (void *)((uint8_t *)&F.l3);
    gm_b0_1 t40 = gm_f_ambiguous_try_else_0v14t0i((void *)NULL, cx, NULL, t37, t38, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t41 = gm_l0;
    void * t42 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t1s75i08qtptd t43 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)&F.l2));
    void * t44 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tioi3jh9v22g5 t45;
    GM_ST(void *, ((uint8_t *)&t45 + 0), t42);
    GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t45 + 8), t43);
    GM_ST(void *, ((uint8_t *)&t45 + 48), t44);
    gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 72));
    gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
    gm_t2gtbh39ptekd t48;
    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tioi3jh9v22g5, ((uint8_t *)&t48 + 8), t45);
    gm_tl874t2atnlh3 t49 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t46), GM_CONV(gm_cseahvg8v7p0vs, t47)));
    gm_tc3oht4ksth9d t50;
    gm_f_mk_expr_53qj7nd((void *)&t50, t41, &t48, GM_CONV(gm_cseahvg8v7p0vs, t49));
    gm_to1cssm8dfiv5 t51;
    GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
    ((uint8_t *)&t51)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t51 + 8), t50);
    memmove(gm_out, ((uint8_t *)&t51), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_expr_bkb2n6e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l1;
    {
        void * t4 = gm_l0;
        gm_t9qkh0rsooc8d t5 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t5;
        uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t6) {
        } else {
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t7), 96);
            goto gm_fdone;
        }
        void * t8 = gm_l0;
        gm_to1cssm8dfiv5 t9;
        gm_f_parse_else_smoqgg4((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        memmove(&F.ret, ((uint8_t *)&t9), 96);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld3_1;
    }
    {
        void * t10 = gm_l0;
        gm_f_leave_rb20u32(t10);
    }
    goto gm_ls11;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t12 = gm_l0;
        gm_f_leave_rb20u32(t12);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls11:;
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_parse_fallback_g1vpkra(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_fallback_g1vpkra F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* captured */
    GM_STACK_CHECK(&F, 273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9qkh0rsooc8d gm_l2;
    {
        uint8_t t6 = gm_l1;
        if (t6) {
            void * t7 = gm_l0;
            gm_f_parse_capture_body_fgf89k2((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        {
            {
                void * t8 = gm_l0;
                gm_t9qkh0rsooc8d t9;
                GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)65ULL));
                uint8_t t10 = gm_f_at_54906rm(t8, t9);
                if (!t10) {
                    goto gm_lb3;
                }
                void * t11 = gm_l0;
                uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t11) + 128));
                uint8_t t13 = (uint8_t)!t12;
                if (!t13) {
                    goto gm_lb3;
                }
                void * t14 = gm_l0;
                gm_f_parse_block_expr_0j6b78q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        void * t15 = gm_l0;
        gm_t9qkh0rsooc8d t16 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t16;
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (t17) {
        } else {
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t18), 96);
            goto gm_fdone;
        }
        void * t19 = gm_l0;
        gm_to1cssm8dfiv5 t20;
        gm_f_parse_else_smoqgg4((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_ld5_1;
        }
        memmove(&F.ret, ((uint8_t *)&t20), 96);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld5_1;
    }
    {
        void * t21 = gm_l0;
        gm_f_leave_rb20u32(t21);
    }
    goto gm_ls22;
gm_ld5_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t23 = gm_l0;
        gm_f_leave_rb20u32(t23);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls22:;
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_head_expr_6sofdhn F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    GM_STACK_CHECK(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t4 = gm_l0;
        gm_toh0ootodhjue t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t5 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t5 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t6 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t4, GM_CONV(gm_cs3cuou245tr5f, t5)));
        gm_l1 = t6;
        void * t7 = gm_l0;
        gm_to1cssm8dfiv5 t8;
        gm_f_parse_expr_bkb2n6e((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        memmove(&F.ret, ((uint8_t *)&t8), 96);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld3_1;
    }
    {
        void * t9 = gm_l0;
        gm_toh0ootodhjue t10 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t9) + 128), t10);
    }
    goto gm_ls11;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t12 = gm_l0;
        gm_toh0ootodhjue t13 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t12) + 128), t13);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls11:;
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_parse_import_s1hv231(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_import_s1hv231 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_tibupt4dmcqai gm_l2; /* l */
    GM_STACK_CHECK(&F, 626u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3fcbuehh3bs2 gm_l3;
    void * t4 = gm_l0;
    uint32_t t5 = gm_f_lo_fp82gt4(t4);
    gm_l1 = t5;
    void * t6 = gm_l0;
    gm_tm4pc0emhrv1g t7;
    gm_f_bump_q2i40re((void *)&t7, t6);
    {
        {
            void * t8 = gm_l0;
            gm_f_kind_k239kpg((void *)&gm_l3, t8);
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)5ULL));
            if (!t10) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), (((uint8_t *)&gm_l3) + 8), 24);
            void * t11 = gm_l0;
            gm_tm4pc0emhrv1g t12;
            gm_f_bump_q2i40re((void *)&t12, t11);
            void * t13 = gm_l0;
            gm_tibupt4dmcqai t14 = GM_LD(gm_tibupt4dmcqai, ((uint8_t *)&gm_l2));
            void * t15 = gm_l0;
            uint32_t t16 = gm_l1;
            gm_t2gtbh39ptekd t17;
            memset(&t17, 0, sizeof t17);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)38ULL));
            GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t17 + 8), t14);
            gm_tl874t2atnlh3 t18 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t15, t16));
            gm_tc3oht4ksth9d t19;
            gm_f_mk_expr_53qj7nd((void *)&t19, t13, &t17, GM_CONV(gm_cseahvg8v7p0vs, t18));
            gm_to1cssm8dfiv5 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t20 + 8), t19);
            memmove(gm_out, ((uint8_t *)&t20), 96);
            goto gm_fdone;
        }
gm_lb2:;
        void * t21 = gm_l0;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s8sl52ot187i2));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(49));
        gm_b0_1 t23 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t24;
        memset(&t24, 0, sizeof t24);
        ((uint8_t *)&t24)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t24 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t24), 96);
        goto gm_fdone;
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_intrinsic_4ou54h3 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l2; /* sym */
    gm_t08olq97rscn3 gm_l3; /* name */
    gm_th2lqbo77pvsc gm_l4; /* args */
    GM_STACK_CHECK(&F, 913u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_thk954m1ol1ig gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    void * t4 = gm_l0;
    gm_f_bump_q2i40re((void *)&F.l1, t4);
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
            gm_l2 = t7;
            gm_b16_8 t8 = gm_l2;
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 32));
            gm_t08olq97rscn3 t10;
            GM_ST(uint32_t, (uint8_t *)&t10 + 28, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t8);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t10 + 16), t9);
            gm_l3 = t10;
            gm_th2lqbo77pvsc t11;
            memset(&t11, 0, sizeof t11);
            gm_l4 = t11;
            void * t12 = gm_l0;
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)61ULL));
            uint8_t t14 = gm_f_at_54906rm(t12, t13);
            if (t14) {
                void * t15 = gm_l0;
                gm_f_parse_call_args_32e205h((void *)&gm_l5, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                if (t16) {
                } else {
                    gm_to1cssm8dfiv5 t17;
                    memset(&t17, 0, sizeof t17);
                    ((uint8_t *)&t17)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t17), 96);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&gm_l6), (((uint8_t *)&gm_l5) + 8), 24);
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                gm_th2lqbo77pvsc t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t19), t18);
                ((uint8_t *)&t19)[16] = 1;
                gm_l4 = t19;
            }
            void * t20 = gm_l0;
            gm_t08olq97rscn3 t21 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l3));
            gm_th2lqbo77pvsc t22 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4));
            gm_tet9guhev5r4u t23;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t23 + 0), t21);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t23 + 32), t22);
            void * t24 = gm_l0;
            uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 32) + 4));
            gm_t2gtbh39ptekd t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)49ULL));
            GM_ST(gm_tet9guhev5r4u, ((uint8_t *)&t26 + 8), t23);
            gm_tl874t2atnlh3 t27 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t24, t25));
            gm_tc3oht4ksth9d t28;
            gm_f_mk_expr_53qj7nd((void *)&t28, t20, &t26, GM_CONV(gm_cseahvg8v7p0vs, t27));
            gm_to1cssm8dfiv5 t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            ((uint8_t *)&t29)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t29 + 8), t28);
            memmove(gm_out, ((uint8_t *)&t29), 96);
            goto gm_fdone;
        }
gm_lb2:;
        void * t30 = gm_l0;
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sjrimf03ml0ct));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(12));
        gm_b0_1 t32 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t33;
        memset(&t33, 0, sizeof t33);
        ((uint8_t *)&t33)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t33 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t33), 96);
        goto gm_fdone;
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_parse_logic_ajejref(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_logic_ajejref F;
    void * gm_l0; /* p */
    gm_tt7vn9ehj0acr gm_l2; /* chain */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 941u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l5;
    uint8_t gm_l6;
    uint8_t gm_l7;
    gm_to1cssm8dfiv5 gm_l8;
    void * t8 = gm_l0;
    gm_f_parse_not_a8b3gjf((void *)&gm_l5, (void *)NULL, cx, NULL, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t9) {
    } else {
        gm_to1cssm8dfiv5 t10;
        memset(&t10, 0, sizeof t10);
        ((uint8_t *)&t10)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t10 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t10), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l5) + 8), 88);
    gm_tt7vn9ehj0acr t11;
    memset(&t11, 0, sizeof t11);
    gm_l2 = t11;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                gm_ts6oqppjh0ba3 t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)20ULL));
                gm_l3 = t12;
                void * t13 = gm_l0;
                gm_t9qkh0rsooc8d t14;
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)20ULL));
                uint8_t t15 = gm_f_at_kw_p0r3sr9(t13, t14);
                if (t15) {
                    gm_ts6oqppjh0ba3 t16;
                    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)21ULL));
                    gm_l3 = t16;
                } else {
                    void * t17 = gm_l0;
                    gm_t9qkh0rsooc8d t18;
                    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
                    uint8_t t19 = gm_f_at_kw_p0r3sr9(t17, t18);
                    uint8_t t20 = (uint8_t)!t19;
                    if (t20) {
                        gm_l6 = ((uint8_t)1);
                    } else {
                        void * t21 = gm_l0;
                        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t21) + 128) + 1));
                        gm_l6 = t22;
                    }
                    uint8_t t23 = gm_l6;
                    if (t23) {
                        goto gm_lb1;
                    }
                }
                {
                    {
                        uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l2))[2] != 0);
                        if (t24) {
                            goto gm_lb5;
                        }
                        gm_l7 = ((uint8_t)1);
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        gm_l7 = ((uint8_t)0);
                        goto gm_lb4;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 185, 12);
                }
gm_lb4:;
                uint8_t t25 = gm_l7;
                if (t25) {
                    gm_ts6oqppjh0ba3 t26 = gm_l3;
                    gm_tt7vn9ehj0acr t27;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t27), t26);
                    ((uint8_t *)&t27)[2] = 1;
                    gm_l2 = t27;
                } else {
                    gm_tt7vn9ehj0acr t28 = gm_l2;
                    gm_ts6oqppjh0ba3 t29 = gm_l3;
                    gm_tt7vn9ehj0acr t30;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t30), t29);
                    ((uint8_t *)&t30)[2] = 1;
                    uint8_t t31 = (!gm_eqcdhc1ipuua(((uint8_t *)&t28), ((uint8_t *)&t30)));
                    if (t31) {
                        void * t32 = gm_l0;
                        void * t33 = gm_l0;
                        gm_tl874t2atnlh3 t34 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t33));
                        gm_b16_8 t35;
                        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_snemlnnsbdb13));
                        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(45));
                        gm_b0_1 t36 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t32, t34, t35);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                void * t37 = gm_l0;
                gm_tm4pc0emhrv1g t38;
                gm_f_bump_q2i40re((void *)&t38, t37);
                void * t39 = gm_l0;
                gm_f_parse_not_a8b3gjf((void *)&gm_l8, (void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (t40) {
                } else {
                    gm_to1cssm8dfiv5 t41;
                    memset(&t41, 0, sizeof t41);
                    ((uint8_t *)&t41)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t41 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t41), 96);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l8) + 8), 88);
                void * t42 = gm_l0;
                gm_ts6oqppjh0ba3 t43 = gm_l3;
                gm_tc3oht4ksth9d t44 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
                gm_tc3oht4ksth9d t45 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l4));
                gm_f_binary_41fr5r5((void *)&F.l1, (void *)NULL, cx, NULL, t42, t43, &t44, &t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tc3oht4ksth9d t46 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
    gm_to1cssm8dfiv5 t47;
    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
    ((uint8_t *)&t47)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t47 + 8), t46);
    memmove(gm_out, ((uint8_t *)&t47), 96);
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
extern __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_not_a8b3gjf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_not_a8b3gjf F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 728u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l3;
    gm_to1cssm8dfiv5 gm_l4;
    {
        void * t4 = gm_l0;
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)18ULL));
        uint8_t t6 = gm_f_at_kw_p0r3sr9(t4, t5);
        uint8_t t7 = (uint8_t)!t6;
        if (t7) {
            void * t8 = gm_l0;
            gm_f_parse_cmp_q5clpig((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
        void * t11 = gm_l0;
        gm_tm4pc0emhrv1g t12;
        gm_f_bump_q2i40re((void *)&t12, t11);
        void * t13 = gm_l0;
        gm_t9qkh0rsooc8d t14 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t14;
        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t15) {
        } else {
            gm_to1cssm8dfiv5 t16;
            memset(&t16, 0, sizeof t16);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t16), 96);
            goto gm_fdone;
        }
        void * t17 = gm_l0;
        gm_f_parse_not_a8b3gjf((void *)&gm_l4, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (t18) {
        } else {
            gm_to1cssm8dfiv5 t19;
            memset(&t19, 0, sizeof t19);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t19 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t19), 96);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld3_1;
        }
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l4) + 8), 88);
        void * t20 = gm_l0;
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)5ULL));
        void * t22 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        gm_t8ghgl2jj0qha t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 0), t21);
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        void * t24 = gm_l0;
        uint32_t t25 = gm_l1;
        gm_t2gtbh39ptekd t26;
        memset(&t26, 0, sizeof t26);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t26 + 8), t23);
        gm_tl874t2atnlh3 t27 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t24, t25));
        gm_tc3oht4ksth9d t28;
        gm_f_mk_expr_53qj7nd((void *)&t28, t20, &t26, GM_CONV(gm_cseahvg8v7p0vs, t27));
        gm_to1cssm8dfiv5 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        ((uint8_t *)&t29)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t29 + 8), t28);
        memmove(&F.ret, ((uint8_t *)&t29), 96);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld3_1;
    }
    {
        void * t30 = gm_l0;
        gm_f_leave_rb20u32(t30);
    }
    goto gm_ls31;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t32 = gm_l0;
        gm_f_leave_rb20u32(t32);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls31:;
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_paren_sq1guoi F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    uint32_t gm_l2; /* start */
    gm_toh0ootodhjue gm_l3; /* old */
    uint8_t gm_l5; /* comma */
    gm_tqs5p4n2tq19v gm_l6; /* named */
    uint32_t gm_l7; /* elem_start */
    gm_tl874t2atnlh3 gm_l11; /* whole */
    GM_STACK_CHECK(&F, 2934u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t0u0j6voh3loe gm_l12;
    gm_tl874t2atnlh3 gm_l13;
    gm_to1cssm8dfiv5 gm_l14;
    gm_to1cssm8dfiv5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_t4jn8fl3lmk5k gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    gm_tl874t2atnlh3 gm_l21;
    gm_to1cssm8dfiv5 gm_l22;
    gm_to1cssm8dfiv5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_t0u0j6voh3loe gm_l25;
    uint8_t gm_l26;
    {
        {
            void * t23 = gm_l0;
            uint32_t t24 = gm_f_lo_fp82gt4(t23);
            gm_l2 = t24;
            void * t25 = gm_l0;
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(0));
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)61ULL));
            gm_t0u0j6voh3loe t28 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t25, t27, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t28;
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (t29) {
            } else {
                gm_to1cssm8dfiv5 t30;
                memset(&t30, 0, sizeof t30);
                ((uint8_t *)&t30)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t30 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t30), 96);
                goto gm_fdone;
            }
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 4));
            void * t32 = gm_l0;
            gm_toh0ootodhjue t33;
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t33 + 1), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t33 + 2), ((uint8_t)0));
            gm_toh0ootodhjue t34 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t32, GM_CONV(gm_cs3cuou245tr5f, t33)));
            gm_l3 = t34;
            gm_trqfvmjtt4sij t35;
            gm_f_list_fvvkq82((void *)&t35, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld22_1;
            }
            F.l4 = t35;
            gm_l5 = ((uint8_t)0);
            gm_tqs5p4n2tq19v t36;
            memset(&t36, 0, sizeof t36);
            gm_l6 = t36;
            {
                for (;;) {
                    void * t37 = gm_l0;
                    gm_t9qkh0rsooc8d t38;
                    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)62ULL));
                    uint8_t t39 = gm_f_at_54906rm(t37, t38);
                    uint8_t t40 = (uint8_t)!t39;
                    if (!t40) {
                        goto gm_lb2;
                    }
                    {
                        void * t41 = gm_l0;
                        uint32_t t42 = gm_f_lo_fp82gt4(t41);
                        gm_l7 = t42;
                        void * t43 = gm_l0;
                        gm_t9qkh0rsooc8d t44;
                        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)50ULL));
                        uint8_t t45 = gm_f_at_54906rm(t43, t44);
                        if (t45) {
                            void * t46 = gm_l0;
                            gm_tm4pc0emhrv1g t47;
                            gm_f_bump_q2i40re((void *)&t47, t46);
                            gm_t9qkh0rsooc8d t48 = gm_l1;
                            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                            uint8_t t50 = (uint8_t)(!(t49 == ((uint8_t)0ULL)));
                            gm_t9qkh0rsooc8d t51;
                            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
                            if (t50) {
                                void * t52 = gm_l0;
                                void * t53 = gm_l0;
                                uint32_t t54 = GM_LD(uint32_t, ((uint8_t *)t53));
                                uint32_t t55 = gm_l7;
                                void * t56 = gm_l0;
                                gm_tl874t2atnlh3 t57 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t56));
                                gm_l13 = t57;
                                uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
                                gm_tl874t2atnlh3 t59;
                                GM_ST(uint32_t, ((uint8_t *)&t59 + 0), t54);
                                GM_ST(uint32_t, ((uint8_t *)&t59 + 4), t55);
                                GM_ST(uint32_t, ((uint8_t *)&t59 + 8), t58);
                                gm_b16_8 t60;
                                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_shbaullahutpe));
                                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(36));
                                gm_b0_1 t61 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t52, t59, t60);
                                if (gm_unw.kind) {
                                    goto gm_ld22_1;
                                }
                            }
                            gm_t9qkh0rsooc8d t62 = gm_l1;
                            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
                            uint8_t t64 = (uint8_t)((t63 == ((uint8_t)0ULL)));
                            gm_t9qkh0rsooc8d t65;
                            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
                            if (t64) {
                                void * t66 = gm_l0;
                                gm_f_parse_expr_bkb2n6e((void *)&gm_l14, (void *)NULL, cx, NULL, t66);
                                if (gm_unw.kind) {
                                    goto gm_ld22_1;
                                }
                            } else {
                                void * t67 = gm_l0;
                                gm_f_parse_type_2m93dtq((void *)&gm_l14, (void *)NULL, cx, NULL, t67);
                                if (gm_unw.kind) {
                                    goto gm_ld22_1;
                                }
                            }
                            memmove(((uint8_t *)&gm_l15), ((uint8_t *)&gm_l14), 96);
                            uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                            if (t68) {
                            } else {
                                gm_to1cssm8dfiv5 t69;
                                memset(&t69, 0, sizeof t69);
                                ((uint8_t *)&t69)[0] = 1;
                                GM_ST(gm_b0_1, ((uint8_t *)&t69 + 8), GM_ZERO(gm_b0_1));
                                memmove(&F.ret, ((uint8_t *)&t69), 96);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld22_1;
                            }
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l15) + 8), 88);
                            void * t70 = (void *)((uint8_t *)&F.l4);
                            gm_t1s75i08qtptd t71;
                            memset(&t71, 0, sizeof t71);
                            gm_tc3oht4ksth9d t72 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l8));
                            gm_tv1o1j44ii4db t73;
                            GM_ST(uint8_t, (uint8_t *)&t73 + 129, 0);
                            GM_ST(uint16_t, (uint8_t *)&t73 + 130, 0);
                            GM_ST(uint32_t, (uint8_t *)&t73 + 132, 0);
                            GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t73 + 0), t71);
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t73 + 40), t72);
                            GM_ST(uint8_t, ((uint8_t *)&t73 + 128), ((uint8_t)1));
                            gm_tftb1odc739t5 t74 = gm_f_push_l8mrumu(t70, &t73);
                            gm_l16 = t74;
                            uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                            if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src0dtioet3p4f6, 704, 13);
                            void * t76 = gm_l0;
                            gm_t9qkh0rsooc8d t77;
                            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)57ULL));
                            uint8_t t78 = gm_f_eat_qmock9g(t76, t77);
                            gm_l5 = t78;
                            uint8_t t79 = gm_l5;
                            uint8_t t80 = (uint8_t)!t79;
                            if (t80) {
                                goto gm_lb2;
                            }
                            goto gm_lb4;
                        }
                        gm_t1s75i08qtptd t81;
                        memset(&t81, 0, sizeof t81);
                        F.l9 = t81;
                        {
                            {
                                void * t82 = gm_l0;
                                uint8_t t83 = gm_f_nth_is_name_oqhoobd(t82, ((uint64_t)0ULL));
                                if (!t83) {
                                    goto gm_lb6;
                                }
                                void * t84 = gm_l0;
                                gm_t9qkh0rsooc8d t85;
                                GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)56ULL));
                                uint8_t t86 = gm_f_nth_is_3fgqmcb(t84, ((uint64_t)1ULL), t85);
                                if (!t86) {
                                    goto gm_lb6;
                                }
                                void * t87 = gm_l0;
                                gm_b16_8 t88;
                                GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s1pvh8pfrs3ir));
                                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
                                gm_f_parse_name_figjvbe((void *)&gm_l17, (void *)NULL, cx, NULL, t87, t88);
                                if (gm_unw.kind) {
                                    goto gm_ld22_1;
                                }
                                uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (t89) {
                                } else {
                                    gm_to1cssm8dfiv5 t90;
                                    memset(&t90, 0, sizeof t90);
                                    ((uint8_t *)&t90)[0] = 1;
                                    GM_ST(gm_b0_1, ((uint8_t *)&t90 + 8), GM_ZERO(gm_b0_1));
                                    memmove(&F.ret, ((uint8_t *)&t90), 96);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                    goto gm_ld22_1;
                                }
                                gm_t08olq97rscn3 t91 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l17) + 8));
                                gm_t1s75i08qtptd t92;
                                GM_ST(uint8_t, (uint8_t *)&t92 + 33, 0);
                                GM_ST(uint16_t, (uint8_t *)&t92 + 34, 0);
                                GM_ST(uint32_t, (uint8_t *)&t92 + 36, 0);
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t92), t91);
                                ((uint8_t *)&t92)[32] = 1;
                                F.l9 = t92;
                                void * t93 = gm_l0;
                                gm_tm4pc0emhrv1g t94;
                                gm_f_bump_q2i40re((void *)&t94, t93);
                                goto gm_lb5;
                            }
gm_lb6:;
                        }
gm_lb5:;
                        {
                            {
                                uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
                                if (t95) {
                                    goto gm_lb8;
                                }
                                gm_l18 = ((uint8_t)1);
                                goto gm_lb7;
                            }
gm_lb8:;
                            {
                                gm_l18 = ((uint8_t)0);
                                goto gm_lb7;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 714, 12);
                        }
gm_lb7:;
                        uint8_t t96 = gm_l18;
                        if (t96) {
                            {
                                {
                                    uint8_t t97 = (uint8_t)((((uint8_t *)&F.l9))[32] != 0);
                                    if (t97) {
                                        goto gm_lb11;
                                    }
                                    gm_l19 = ((uint8_t)0);
                                    goto gm_lb10;
                                }
gm_lb11:;
                                {
                                    gm_l19 = ((uint8_t)1);
                                    goto gm_lb10;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 715, 21);
                            }
gm_lb10:;
                            uint8_t t98 = gm_l19;
                            gm_tqs5p4n2tq19v t99;
                            GM_ST(uint8_t, ((uint8_t *)&t99), t98);
                            ((uint8_t *)&t99)[1] = 1;
                            gm_l6 = t99;
                        } else {
                            gm_tqs5p4n2tq19v t100 = gm_l6;
                            {
                                {
                                    uint8_t t101 = (uint8_t)((((uint8_t *)&F.l9))[32] != 0);
                                    if (t101) {
                                        goto gm_lb14;
                                    }
                                    gm_l20 = ((uint8_t)0);
                                    goto gm_lb13;
                                }
gm_lb14:;
                                {
                                    gm_l20 = ((uint8_t)1);
                                    goto gm_lb13;
                                }
gm_lb15:;
                                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 716, 33);
                            }
gm_lb13:;
                            uint8_t t102 = gm_l20;
                            gm_tqs5p4n2tq19v t103;
                            GM_ST(uint8_t, ((uint8_t *)&t103), t102);
                            ((uint8_t *)&t103)[1] = 1;
                            uint8_t t104 = (!gm_eqebiqthsuot(((uint8_t *)&t100), ((uint8_t *)&t103)));
                            if (t104) {
                                void * t105 = gm_l0;
                                void * t106 = gm_l0;
                                uint32_t t107 = GM_LD(uint32_t, ((uint8_t *)t106));
                                uint32_t t108 = gm_l7;
                                void * t109 = gm_l0;
                                gm_tl874t2atnlh3 t110 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t109));
                                gm_l21 = t110;
                                uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 8));
                                gm_tl874t2atnlh3 t112;
                                GM_ST(uint32_t, ((uint8_t *)&t112 + 0), t107);
                                GM_ST(uint32_t, ((uint8_t *)&t112 + 4), t108);
                                GM_ST(uint32_t, ((uint8_t *)&t112 + 8), t111);
                                gm_b16_8 t113;
                                GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sam3pd3469drv));
                                GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(54));
                                gm_b0_1 t114 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t105, t112, t113);
                                if (gm_unw.kind) {
                                    goto gm_ld22_1;
                                }
                            }
                        }
                        gm_t9qkh0rsooc8d t115 = gm_l1;
                        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t115) + 0));
                        uint8_t t117 = (uint8_t)((t116 == ((uint8_t)0ULL)));
                        gm_t9qkh0rsooc8d t118;
                        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
                        if (t117) {
                            void * t119 = gm_l0;
                            gm_f_parse_expr_bkb2n6e((void *)&gm_l22, (void *)NULL, cx, NULL, t119);
                            if (gm_unw.kind) {
                                goto gm_ld22_1;
                            }
                        } else {
                            void * t120 = gm_l0;
                            gm_f_parse_type_2m93dtq((void *)&gm_l22, (void *)NULL, cx, NULL, t120);
                            if (gm_unw.kind) {
                                goto gm_ld22_1;
                            }
                        }
                        memmove(((uint8_t *)&gm_l23), ((uint8_t *)&gm_l22), 96);
                        uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (t121) {
                        } else {
                            gm_to1cssm8dfiv5 t122;
                            memset(&t122, 0, sizeof t122);
                            ((uint8_t *)&t122)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t122 + 8), GM_ZERO(gm_b0_1));
                            memmove(&F.ret, ((uint8_t *)&t122), 96);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                            goto gm_ld22_1;
                        }
                        memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l23) + 8), 88);
                        void * t123 = (void *)((uint8_t *)&F.l4);
                        gm_t1s75i08qtptd t124 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)&F.l9));
                        gm_tc3oht4ksth9d t125 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l10));
                        gm_tv1o1j44ii4db t126;
                        GM_ST(uint8_t, (uint8_t *)&t126 + 129, 0);
                        GM_ST(uint16_t, (uint8_t *)&t126 + 130, 0);
                        GM_ST(uint32_t, (uint8_t *)&t126 + 132, 0);
                        GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t126 + 0), t124);
                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t126 + 40), t125);
                        GM_ST(uint8_t, ((uint8_t *)&t126 + 128), ((uint8_t)0));
                        gm_tftb1odc739t5 t127 = gm_f_push_l8mrumu(t123, &t126);
                        gm_l24 = t127;
                        uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src0dtioet3p4f6, 720, 9);
                        void * t129 = gm_l0;
                        gm_t9qkh0rsooc8d t130;
                        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)57ULL));
                        uint8_t t131 = gm_f_eat_qmock9g(t129, t130);
                        gm_l5 = t131;
                        uint8_t t132 = gm_l5;
                        uint8_t t133 = (uint8_t)!t132;
                        if (t133) {
                            goto gm_lb2;
                        }
                    }
gm_lb4:;
                }
            }
gm_lb2:;
        }
        {
            void * t134 = gm_l0;
            gm_b16_8 t135;
            GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sa3k87epai61e));
            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(24));
            gm_t9qkh0rsooc8d t136;
            GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)62ULL));
            gm_t0u0j6voh3loe t137 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t134, t136, t135);
            if (gm_unw.kind) {
                goto gm_ld22_1;
            }
            gm_l25 = t137;
            uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
            if (t138) {
            } else {
                gm_to1cssm8dfiv5 t139;
                memset(&t139, 0, sizeof t139);
                ((uint8_t *)&t139)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t139 + 8), GM_ZERO(gm_b0_1));
                memmove(&F.ret, ((uint8_t *)&t139), 96);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                goto gm_ld22_1;
            }
            gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l25) + 4));
            void * t141 = gm_l0;
            uint32_t t142 = gm_l2;
            gm_tl874t2atnlh3 t143 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t141, t142));
            gm_l11 = t143;
            {
                {
                    uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                    uint8_t t145 = ((t144 == ((uint64_t)1ULL)));
                    if (!t145) {
                        goto gm_lb17;
                    }
                    uint8_t t146 = gm_l5;
                    uint8_t t147 = (uint8_t)!t146;
                    if (!t147) {
                        goto gm_lb17;
                    }
                    void * t148 = GM_LD(void *, ((uint8_t *)&F.l4));
                    uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t149)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t149, gm_src0dtioet3p4f6, 726, 47);
                    {
                        {
                            uint8_t t150 = (uint8_t)(((((uint8_t *)t148) + (uintptr_t)((uint64_t)0ULL) * 136))[32] != 0);
                            if (t150) {
                                goto gm_lb19;
                            }
                            gm_l26 = ((uint8_t)1);
                            goto gm_lb18;
                        }
gm_lb19:;
                        {
                            gm_l26 = ((uint8_t)0);
                            goto gm_lb18;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 726, 47);
                    }
gm_lb18:;
                    uint8_t t151 = gm_l26;
                    if (!t151) {
                        goto gm_lb17;
                    }
                    void * t152 = GM_LD(void *, ((uint8_t *)&F.l4));
                    uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t153)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t153, gm_src0dtioet3p4f6, 726, 83);
                    uint8_t t154 = GM_LD(uint8_t, ((((uint8_t *)t152) + (uintptr_t)((uint64_t)0ULL) * 136) + 128));
                    uint8_t t155 = (uint8_t)!t154;
                    if (!t155) {
                        goto gm_lb17;
                    }
                    void * t156 = gm_l0;
                    void * t157 = GM_LD(void *, ((uint8_t *)&F.l4));
                    uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t158)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t158, gm_src0dtioet3p4f6, 727, 37);
                    void * t159 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((((uint8_t *)t157) + (uintptr_t)((uint64_t)0ULL) * 136) + 40)));
                    if (gm_unw.kind) {
                        goto gm_ld22_1;
                    }
                    gm_tl874t2atnlh3 t160 = gm_l11;
                    gm_t2gtbh39ptekd t161;
                    memset(&t161, 0, sizeof t161);
                    GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)9ULL));
                    GM_ST(void *, ((uint8_t *)&t161 + 8), t159);
                    gm_tc3oht4ksth9d t162;
                    gm_f_mk_expr_53qj7nd((void *)&t162, t156, &t161, GM_CONV(gm_cseahvg8v7p0vs, t160));
                    gm_to1cssm8dfiv5 t163;
                    GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                    ((uint8_t *)&t163)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t163 + 8), t162);
                    memmove(&F.ret, ((uint8_t *)&t163), 96);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld22_1;
                }
gm_lb17:;
            }
gm_lb16:;
            void * t164 = gm_l0;
            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_tl874t2atnlh3 t166 = gm_l11;
            gm_t2gtbh39ptekd t167;
            memset(&t167, 0, sizeof t167);
            GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)10ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t167 + 8), t165);
            gm_tc3oht4ksth9d t168;
            gm_f_mk_expr_53qj7nd((void *)&t168, t164, &t167, GM_CONV(gm_cseahvg8v7p0vs, t166));
            gm_to1cssm8dfiv5 t169;
            GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
            ((uint8_t *)&t169)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t169 + 8), t168);
            memmove(&F.ret, ((uint8_t *)&t169), 96);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld22_1;
        }
    }
    {
        void * t170 = gm_l0;
        gm_toh0ootodhjue t171 = gm_l3;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t170) + 128), t171);
    }
    goto gm_ls172;
gm_ld22_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t173 = gm_l0;
        gm_toh0ootodhjue t174 = gm_l3;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t173) + 128), t174);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls172:;
gm_lb1:;
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
extern __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ops_2p1csn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_parse_postfix_ops_2p1csn3 F;
    void * gm_l0; /* p */
    uint8_t gm_l2; /* allow_bang */
    uint32_t gm_l4; /* links */
    uint32_t gm_l5; /* start */
    gm_t3fcbuehh3bs2 gm_l6; /* k */
    gm_th2lqbo77pvsc gm_l7; /* args */
    GM_STACK_CHECK(&F, 2410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_to1cssm8dfiv5 gm_l11;
    gm_thk954m1ol1ig gm_l12;
    gm_to1cssm8dfiv5 gm_l13;
    gm_to1cssm8dfiv5 gm_l14;
    memmove(((uint8_t *)&F.l3), ((uint8_t *)a1), 88);
    gm_l4 = ((uint32_t)0ULL);
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 72) + 4));
                gm_l5 = t13;
                void * t14 = gm_l0;
                gm_f_kind_k239kpg((void *)&gm_l6, t14);
                gm_t9qkh0rsooc8d t15;
                GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)45ULL));
                gm_t9qkh0rsooc8d t16;
                GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)47ULL));
                gm_t9qkh0rsooc8d t17;
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)61ULL));
                gm_t9qkh0rsooc8d t18;
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)63ULL));
                gm_t9qkh0rsooc8d t19;
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)58ULL));
                gm_t3fcbuehh3bs2 t20;
                GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t20 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t20 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t20 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 8), t15);
                gm_t3fcbuehh3bs2 t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 8), t16);
                gm_t3fcbuehh3bs2 t22;
                GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t22 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 8), t17);
                gm_t3fcbuehh3bs2 t23;
                GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t23 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t23 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 8), t18);
                gm_t3fcbuehh3bs2 t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 8), t19);
                gm_tak1k6bk5qcen t25;
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t25 + 0), t20);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t25 + 32), t21);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t25 + 64), t22);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t25 + 96), t23);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t25 + 128), t24);
                gm_t3fcbuehh3bs2 t26 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)&gm_l6));
                uint8_t t27 = 0;
                if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 0, ((uint8_t *)&t26))) t27 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 32, ((uint8_t *)&t26))) t27 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 64, ((uint8_t *)&t26))) t27 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 96, ((uint8_t *)&t26))) t27 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 128, ((uint8_t *)&t26))) t27 = 1;
                uint8_t t28 = (uint8_t)(t27);
                if (t28) {
                    gm_l8 = ((uint8_t)1);
                } else {
                    uint8_t t29 = gm_l2;
                    if (t29) {
                        gm_t9qkh0rsooc8d t30;
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)46ULL));
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t32 = (uint8_t)(t31 == ((uint8_t)8ULL));
                        if (t32) {
                            gm_t9qkh0rsooc8d t33 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                            t32 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t33), ((uint8_t *)&t30));
                        }
                        uint8_t t34 = (uint8_t)(t32);
                        gm_t3fcbuehh3bs2 t35 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)&gm_l6));
                        gm_t3fcbuehh3bs2 t36;
                        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t36 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t36 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t36 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)8ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 8), t30);
                        gm_l9 = t34;
                    } else {
                        gm_l9 = ((uint8_t)0);
                    }
                    uint8_t t37 = gm_l9;
                    gm_l8 = t37;
                }
                uint8_t t38 = gm_l8;
                if (t38) {
                    uint32_t t39 = gm_l4;
                    uint32_t r40; bool o41 = __builtin_add_overflow(t39, ((uint32_t)1ULL), &r40);
                    if (o41) gm_trap("integer overflow in `+`", gm_src0dtioet3p4f6, 426, 13);
                    uint32_t t42 = r40;
                    gm_l4 = t42;
                    uint32_t t43 = gm_l4;
                    uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)gm_cpvard348ed));
                    uint8_t t45 = (t43 > t44);
                    if (t45) {
                        void * t46 = gm_l0;
                        void * t47 = gm_l0;
                        gm_tl874t2atnlh3 t48 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t47));
                        uint32_t t49 = GM_LD(uint32_t, ((uint8_t *)gm_cpvard348ed));
                        gm_t88rcucg1b7uk t50;
                        GM_ST(uint32_t, ((uint8_t *)&t50 + 0), t49);
                        gm_f_format_o5o20fv((void *)&gm_l10, (void *)NULL, cx, NULL, t50);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src0dtioet3p4f6, 428, 42);
                        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                        gm_b0_1 t54 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t46, t48, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_to1cssm8dfiv5 t55;
                        memset(&t55, 0, sizeof t55);
                        ((uint8_t *)&t55)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t55 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t55), 96);
                        goto gm_fdone;
                    }
                }
                {
                    {
                        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t57 = (uint8_t)(t56 == ((uint8_t)8ULL));
                        if (!t57) {
                            goto gm_lb5;
                        }
                        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 8) + 0));
                        uint8_t t59 = (uint8_t)(t58 == ((uint8_t)45ULL));
                        if (!t59) {
                            goto gm_lb5;
                        }
                        void * t60 = gm_l0;
                        gm_tm4pc0emhrv1g t61;
                        gm_f_bump_q2i40re((void *)&t61, t60);
                        void * t62 = gm_l0;
                        void * t63 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t64 = gm_l0;
                        uint32_t t65 = gm_l5;
                        gm_t2gtbh39ptekd t66;
                        memset(&t66, 0, sizeof t66);
                        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)15ULL));
                        GM_ST(void *, ((uint8_t *)&t66 + 8), t63);
                        gm_tl874t2atnlh3 t67 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t64, t65));
                        gm_f_mk_expr_53qj7nd((void *)&F.l3, t62, &t66, GM_CONV(gm_cseahvg8v7p0vs, t67));
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t69 = (uint8_t)(t68 == ((uint8_t)8ULL));
                        if (!t69) {
                            goto gm_lb6;
                        }
                        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 8) + 0));
                        uint8_t t71 = (uint8_t)(t70 == ((uint8_t)46ULL));
                        if (!t71) {
                            goto gm_lb6;
                        }
                        uint8_t t72 = gm_l2;
                        if (!t72) {
                            goto gm_lb6;
                        }
                        void * t73 = gm_l0;
                        gm_tc3oht4ksth9d t74 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
                        gm_f_parse_bang_irmkots((void *)&gm_l11, (void *)NULL, cx, NULL, t73, &t74);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (t75) {
                        } else {
                            gm_to1cssm8dfiv5 t76;
                            memset(&t76, 0, sizeof t76);
                            ((uint8_t *)&t76)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t76 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t76), 96);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l11) + 8), 88);
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t78 = (uint8_t)(t77 == ((uint8_t)8ULL));
                        if (!t78) {
                            goto gm_lb7;
                        }
                        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 8) + 0));
                        uint8_t t80 = (uint8_t)(t79 == ((uint8_t)47ULL));
                        if (!t80) {
                            goto gm_lb7;
                        }
                        void * t81 = gm_l0;
                        gm_tm4pc0emhrv1g t82;
                        gm_f_bump_q2i40re((void *)&t82, t81);
                        void * t83 = gm_l0;
                        void * t84 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t85 = gm_l0;
                        uint32_t t86 = gm_l5;
                        gm_t2gtbh39ptekd t87;
                        memset(&t87, 0, sizeof t87);
                        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)18ULL));
                        GM_ST(void *, ((uint8_t *)&t87 + 8), t84);
                        gm_tl874t2atnlh3 t88 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t85, t86));
                        gm_f_mk_expr_53qj7nd((void *)&F.l3, t83, &t87, GM_CONV(gm_cseahvg8v7p0vs, t88));
                        goto gm_lb4;
                    }
gm_lb7:;
                    {
                        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t90 = (uint8_t)(t89 == ((uint8_t)8ULL));
                        if (!t90) {
                            goto gm_lb8;
                        }
                        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 8) + 0));
                        uint8_t t92 = (uint8_t)(t91 == ((uint8_t)61ULL));
                        if (!t92) {
                            goto gm_lb8;
                        }
                        void * t93 = gm_l0;
                        gm_f_parse_call_args_32e205h((void *)&gm_l12, (void *)NULL, cx, NULL, t93);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (t94) {
                        } else {
                            gm_to1cssm8dfiv5 t95;
                            memset(&t95, 0, sizeof t95);
                            ((uint8_t *)&t95)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t95 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t95), 96);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l12) + 8), 24);
                        void * t96 = gm_l0;
                        void * t97 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 16));
                        gm_tlljg61a2ctgq t100;
                        GM_ST(uint8_t, (uint8_t *)&t100 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t100 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t100 + 28, 0);
                        GM_ST(void *, ((uint8_t *)&t100 + 0), t97);
                        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 8), t98);
                        GM_ST(uint8_t, ((uint8_t *)&t100 + 24), t99);
                        void * t101 = gm_l0;
                        uint32_t t102 = gm_l5;
                        gm_t2gtbh39ptekd t103;
                        GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t103 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t103 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t103 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t103 + 8), t100);
                        gm_tl874t2atnlh3 t104 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t101, t102));
                        gm_f_mk_expr_53qj7nd((void *)&F.l3, t96, &t103, GM_CONV(gm_cseahvg8v7p0vs, t104));
                        goto gm_lb4;
                    }
gm_lb8:;
                    {
                        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t106 = (uint8_t)(t105 == ((uint8_t)8ULL));
                        if (!t106) {
                            goto gm_lb9;
                        }
                        uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 8) + 0));
                        uint8_t t108 = (uint8_t)(t107 == ((uint8_t)63ULL));
                        if (!t108) {
                            goto gm_lb9;
                        }
                        void * t109 = gm_l0;
                        gm_tc3oht4ksth9d t110 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
                        gm_f_parse_subscript_1hujuhl((void *)&gm_l13, (void *)NULL, cx, NULL, t109, &t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (t111) {
                        } else {
                            gm_to1cssm8dfiv5 t112;
                            memset(&t112, 0, sizeof t112);
                            ((uint8_t *)&t112)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t112 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t112), 96);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l13) + 8), 88);
                        goto gm_lb4;
                    }
gm_lb9:;
                    {
                        uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t114 = (uint8_t)(t113 == ((uint8_t)8ULL));
                        if (!t114) {
                            goto gm_lb10;
                        }
                        uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 8) + 0));
                        uint8_t t116 = (uint8_t)(t115 == ((uint8_t)58ULL));
                        if (!t116) {
                            goto gm_lb10;
                        }
                        void * t117 = gm_l0;
                        gm_tc3oht4ksth9d t118 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
                        gm_f_parse_dot_suffix_1fk204i((void *)&gm_l14, (void *)NULL, cx, NULL, t117, &t118);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (t119) {
                        } else {
                            gm_to1cssm8dfiv5 t120;
                            memset(&t120, 0, sizeof t120);
                            ((uint8_t *)&t120)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t120 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t120), 96);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l14) + 8), 88);
                        goto gm_lb4;
                    }
gm_lb10:;
                    {
                        goto gm_lb1;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 431, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tc3oht4ksth9d t121 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
    gm_to1cssm8dfiv5 t122;
    GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
    ((uint8_t *)&t122)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t122 + 8), t121);
    memmove(gm_out, ((uint8_t *)&t122), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_prefix_671fi6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_prefix_671fi6d F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    uint32_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 766u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l4;
    gm_t9qkh0rsooc8d gm_l5;
    gm_t9qkh0rsooc8d gm_l6;
    gm_to1cssm8dfiv5 gm_l7;
    {
        void * t4 = gm_l0;
        gm_t3fcbuehh3bs2 t5;
        gm_f_kind_k239kpg((void *)&t5, t4);
        gm_taari1s0m51a7 t6 = gm_f_prefix_op_japctlh(&t5);
        gm_l4 = t6;
        uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l4))[1] != 0);
        if (t7) {
            gm_t9qkh0rsooc8d t8 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
            gm_l5 = t8;
        } else {
            void * t9 = gm_l0;
            gm_f_parse_postfix_ao4kstr((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t9, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_t9qkh0rsooc8d t10 = gm_l5;
        gm_l1 = t10;
        void * t11 = gm_l0;
        uint32_t t12 = gm_f_lo_fp82gt4(t11);
        gm_l2 = t12;
        void * t13 = gm_l0;
        gm_tm4pc0emhrv1g t14;
        gm_f_bump_q2i40re((void *)&t14, t13);
        void * t15 = gm_l0;
        gm_t9qkh0rsooc8d t16 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t16;
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (t17) {
        } else {
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t18), 96);
            goto gm_fdone;
        }
        void * t19 = gm_l0;
        gm_f_parse_prefix_671fi6d((void *)&gm_l7, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (t20) {
        } else {
            gm_to1cssm8dfiv5 t21;
            memset(&t21, 0, sizeof t21);
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t21 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t21), 96);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld3_1;
        }
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l7) + 8), 88);
        void * t22 = gm_l0;
        gm_t9qkh0rsooc8d t23 = gm_l1;
        void * t24 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        gm_t8ghgl2jj0qha t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 0), t23);
        GM_ST(void *, ((uint8_t *)&t25 + 8), t24);
        void * t26 = gm_l0;
        uint32_t t27 = gm_l2;
        gm_t2gtbh39ptekd t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t28 + 8), t25);
        gm_tl874t2atnlh3 t29 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t26, t27));
        gm_tc3oht4ksth9d t30;
        gm_f_mk_expr_53qj7nd((void *)&t30, t22, &t28, GM_CONV(gm_cseahvg8v7p0vs, t29));
        gm_to1cssm8dfiv5 t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        ((uint8_t *)&t31)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t31 + 8), t30);
        memmove(&F.ret, ((uint8_t *)&t31), 96);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld3_1;
    }
    {
        void * t32 = gm_l0;
        gm_f_leave_rb20u32(t32);
    }
    goto gm_ls33;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t34 = gm_l0;
        gm_f_leave_rb20u32(t34);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls33:;
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_parse_primary_1vvr4ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_primary_1vvr4ji F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_t3fcbuehh3bs2 gm_l2; /* k */
    gm_t08olq97rscn3 gm_l3; /* label */
    gm_t08olq97rscn3 gm_l4; /* id */
    void * gm_l5; /* l */
    void * gm_l6; /* l */
    gm_tibupt4dmcqai gm_l7; /* l */
    gm_b16_8 gm_l8; /* name */
    gm_t08olq97rscn3 gm_l9; /* name */
    GM_STACK_CHECK(&F, 6349u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l10;
    gm_t4jn8fl3lmk5k gm_l11;
    gm_t9qkh0rsooc8d gm_l12;
    gm_t9qkh0rsooc8d gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_t3fcbuehh3bs2 gm_l16;
    uint8_t gm_l17;
    gm_t3fcbuehh3bs2 gm_l18;
    gm_t4jn8fl3lmk5k gm_l19;
    gm_t9qkh0rsooc8d gm_l20;
    gm_t9qkh0rsooc8d gm_l21;
    gm_t9qkh0rsooc8d gm_l22;
    gm_t9qkh0rsooc8d gm_l23;
    {
        void * t61 = gm_l0;
        gm_tl874t2atnlh3 t62 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t61));
        gm_l1 = t62;
        void * t63 = gm_l0;
        gm_f_kind_k239kpg((void *)&gm_l2, t63);
        {
            {
                {
                    {
                        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                        uint8_t t65 = (uint8_t)(t64 == ((uint8_t)0ULL));
                        if (!t65) {
                            goto gm_lb4;
                        }
                        gm_l10 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l10 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 597, 8);
                }
gm_lb3:;
                uint8_t t66 = gm_l10;
                if (!t66) {
                    goto gm_lb2;
                }
                void * t67 = gm_l0;
                gm_t9qkh0rsooc8d t68;
                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)56ULL));
                uint8_t t69 = gm_f_nth_is_3fgqmcb(t67, ((uint64_t)1ULL), t68);
                if (!t69) {
                    goto gm_lb2;
                }
                void * t70 = gm_l0;
                gm_t3fcbuehh3bs2 t71;
                gm_f_nth_c0r2nvs((void *)&t71, t70, ((uint64_t)2ULL));
                gm_t9qkh0rsooc8d t72;
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)31ULL));
                gm_t9qkh0rsooc8d t73;
                GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)12ULL));
                gm_t3fcbuehh3bs2 t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t74 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t74 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t74 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t74 + 8), t72);
                gm_t3fcbuehh3bs2 t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t75 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t75 + 8), t73);
                gm_t23tohc57ebhi t76;
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t76 + 0), t74);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t76 + 32), t75);
                uint8_t t77 = 0;
                if (gm_eqt7ftn1841u(((uint8_t *)&t76) + 0, ((uint8_t *)&t71))) t77 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t76) + 32, ((uint8_t *)&t71))) t77 = 1;
                uint8_t t78 = (uint8_t)(t77);
                if (!t78) {
                    goto gm_lb2;
                }
                void * t79 = gm_l0;
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_so7em1q8clnib));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(7));
                gm_f_parse_ident_n2c6b75((void *)&gm_l11, (void *)NULL, cx, NULL, t79, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (t81) {
                } else {
                    gm_to1cssm8dfiv5 t82;
                    memset(&t82, 0, sizeof t82);
                    ((uint8_t *)&t82)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t82 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t82), 96);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l11) + 8), 32);
                void * t83 = gm_l0;
                gm_tm4pc0emhrv1g t84;
                gm_f_bump_q2i40re((void *)&t84, t83);
                void * t85 = gm_l0;
                gm_t9qkh0rsooc8d t86;
                GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)31ULL));
                uint8_t t87 = gm_f_at_kw_p0r3sr9(t85, t86);
                if (t87) {
                    void * t88 = gm_l0;
                    gm_b16_8 t89;
                    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s1llcn0p4qesk));
                    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(19));
                    gm_t9qkh0rsooc8d t90 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t88, t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t90;
                    uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (t91) {
                    } else {
                        gm_to1cssm8dfiv5 t92;
                        memset(&t92, 0, sizeof t92);
                        ((uint8_t *)&t92)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t92 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t92), 96);
                        goto gm_fdone;
                    }
                    void * t93 = gm_l0;
                    gm_t08olq97rscn3 t94 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l3));
                    gm_t1s75i08qtptd t95;
                    GM_ST(uint8_t, (uint8_t *)&t95 + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&t95 + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&t95 + 36, 0);
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t95), t94);
                    ((uint8_t *)&t95)[32] = 1;
                    gm_f_parse_with_hlmj8pt((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t93, &t95);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                void * t96 = gm_l0;
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sqm6u7rqmi1il));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(18));
                gm_t9qkh0rsooc8d t98 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t96, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t98;
                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (t99) {
                } else {
                    gm_to1cssm8dfiv5 t100;
                    memset(&t100, 0, sizeof t100);
                    ((uint8_t *)&t100)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t100 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t100), 96);
                    goto gm_fdone;
                }
                void * t101 = gm_l0;
                gm_t08olq97rscn3 t102 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l3));
                gm_t1s75i08qtptd t103;
                GM_ST(uint8_t, (uint8_t *)&t103 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t103 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t103 + 36, 0);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t103), t102);
                ((uint8_t *)&t103)[32] = 1;
                gm_f_parse_for_q8vk3da((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t101, &t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                {
                    {
                        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                        uint8_t t105 = (uint8_t)(t104 == ((uint8_t)2ULL));
                        if (!t105) {
                            goto gm_lb9;
                        }
                        gm_l14 = ((uint8_t)1);
                        goto gm_lb8;
                    }
gm_lb9:;
                    {
                        gm_l14 = ((uint8_t)0);
                        goto gm_lb8;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 607, 8);
                }
gm_lb8:;
                uint8_t t106 = gm_l14;
                if (!t106) {
                    goto gm_lb7;
                }
                gm_t9qkh0rsooc8d t107;
                GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)25ULL));
                gm_t9qkh0rsooc8d t108;
                GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)10ULL));
                gm_t9qkh0rsooc8d t109;
                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)19ULL));
                gm_t3fcbuehh3bs2 t110;
                GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t110 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t110 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t110 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t110 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t110 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t110 + 8), t107);
                gm_t3fcbuehh3bs2 t111;
                GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t111 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t111 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t111 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t111 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t111 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t111 + 8), t108);
                gm_t3fcbuehh3bs2 t112;
                GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t112 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t112 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t112 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t112 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t112 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t112 + 8), t109);
                gm_t41cj6gvba3r8 t113;
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t113 + 0), t110);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t113 + 32), t111);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t113 + 64), t112);
                gm_t3fcbuehh3bs2 t114 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)&gm_l2));
                uint8_t t115 = 0;
                if (gm_eqt7ftn1841u(((uint8_t *)&t113) + 0, ((uint8_t *)&t114))) t115 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t113) + 32, ((uint8_t *)&t114))) t115 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t113) + 64, ((uint8_t *)&t114))) t115 = 1;
                uint8_t t116 = (uint8_t)(!t115);
                if (!t116) {
                    goto gm_lb7;
                }
                void * t117 = gm_l0;
                uint8_t t118 = gm_f_at_reported_kw_name_it872te(t117);
                if (!t118) {
                    goto gm_lb7;
                }
                void * t119 = gm_l0;
                gm_f_bump_reported_kw_name_gs5eq1i((void *)&gm_l4, t119);
                void * t120 = gm_l0;
                gm_t08olq97rscn3 t121 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l4));
                gm_tl874t2atnlh3 t122 = gm_l1;
                gm_t2gtbh39ptekd t123;
                GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t123 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t123 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t123 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)6ULL));
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t123 + 8), t121);
                gm_tc3oht4ksth9d t124;
                gm_f_mk_expr_53qj7nd((void *)&t124, t120, &t123, GM_CONV(gm_cseahvg8v7p0vs, t122));
                gm_to1cssm8dfiv5 t125;
                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                ((uint8_t *)&t125)[0] = 0;
                GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t125 + 8), t124);
                memmove(gm_out, ((uint8_t *)&t125), 96);
                goto gm_fdone;
            }
gm_lb7:;
        }
gm_lb6:;
        {
            {
                {
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)3ULL));
                    if (!t127) {
                        goto gm_lb12;
                    }
                    void * t128 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
                    gm_l5 = t128;
                    void * t129 = gm_l0;
                    gm_tm4pc0emhrv1g t130;
                    gm_f_bump_q2i40re((void *)&t130, t129);
                    void * t131 = gm_l0;
                    void * t132 = gm_l5;
                    gm_tl874t2atnlh3 t133 = gm_l1;
                    gm_t2gtbh39ptekd t134;
                    memset(&t134, 0, sizeof t134);
                    GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)0ULL));
                    GM_ST(void *, ((uint8_t *)&t134 + 8), t132);
                    gm_tc3oht4ksth9d t135;
                    gm_f_mk_expr_53qj7nd((void *)&t135, t131, &t134, GM_CONV(gm_cseahvg8v7p0vs, t133));
                    gm_to1cssm8dfiv5 t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    ((uint8_t *)&t136)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t136 + 8), t135);
                    memmove(gm_out, ((uint8_t *)&t136), 96);
                    goto gm_fdone;
                }
gm_lb12:;
                {
                    uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t138 = (uint8_t)(t137 == ((uint8_t)4ULL));
                    if (!t138) {
                        goto gm_lb13;
                    }
                    void * t139 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
                    gm_l6 = t139;
                    void * t140 = gm_l0;
                    gm_tm4pc0emhrv1g t141;
                    gm_f_bump_q2i40re((void *)&t141, t140);
                    void * t142 = gm_l0;
                    void * t143 = gm_l6;
                    gm_tl874t2atnlh3 t144 = gm_l1;
                    gm_t2gtbh39ptekd t145;
                    memset(&t145, 0, sizeof t145);
                    GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)1ULL));
                    GM_ST(void *, ((uint8_t *)&t145 + 8), t143);
                    gm_tc3oht4ksth9d t146;
                    gm_f_mk_expr_53qj7nd((void *)&t146, t142, &t145, GM_CONV(gm_cseahvg8v7p0vs, t144));
                    gm_to1cssm8dfiv5 t147;
                    GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                    ((uint8_t *)&t147)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t147 + 8), t146);
                    memmove(gm_out, ((uint8_t *)&t147), 96);
                    goto gm_fdone;
                }
gm_lb13:;
                {
                    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t149 = (uint8_t)(t148 == ((uint8_t)5ULL));
                    if (!t149) {
                        goto gm_lb14;
                    }
                    memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l2) + 8), 24);
                    void * t150 = gm_l0;
                    gm_tm4pc0emhrv1g t151;
                    gm_f_bump_q2i40re((void *)&t151, t150);
                    void * t152 = gm_l0;
                    gm_tibupt4dmcqai t153 = GM_LD(gm_tibupt4dmcqai, ((uint8_t *)&gm_l7));
                    gm_tl874t2atnlh3 t154 = gm_l1;
                    gm_t2gtbh39ptekd t155;
                    memset(&t155, 0, sizeof t155);
                    GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t155 + 8), t153);
                    gm_tc3oht4ksth9d t156;
                    gm_f_mk_expr_53qj7nd((void *)&t156, t152, &t155, GM_CONV(gm_cseahvg8v7p0vs, t154));
                    gm_to1cssm8dfiv5 t157;
                    GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
                    ((uint8_t *)&t157)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t157 + 8), t156);
                    memmove(gm_out, ((uint8_t *)&t157), 96);
                    goto gm_fdone;
                }
gm_lb14:;
                {
                    {
                        {
                            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t159 = (uint8_t)(t158 == ((uint8_t)2ULL));
                            if (!t159) {
                                goto gm_lb17;
                            }
                            uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                            uint8_t t161 = (uint8_t)(t160 == ((uint8_t)25ULL));
                            if (!t161) {
                                goto gm_lb17;
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        {
                            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t163 = (uint8_t)(t162 == ((uint8_t)2ULL));
                            if (!t163) {
                                goto gm_lb18;
                            }
                            uint8_t t164 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                            uint8_t t165 = (uint8_t)(t164 == ((uint8_t)10ULL));
                            if (!t165) {
                                goto gm_lb18;
                            }
                            goto gm_lb16;
                        }
gm_lb18:;
                        goto gm_lb15;
                    }
gm_lb16:;
                    void * t166 = gm_l0;
                    gm_tm4pc0emhrv1g t167;
                    gm_f_bump_q2i40re((void *)&t167, t166);
                    void * t168 = gm_l0;
                    gm_t9qkh0rsooc8d t169;
                    GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)25ULL));
                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t171 = (uint8_t)(t170 == ((uint8_t)2ULL));
                    if (t171) {
                        gm_t9qkh0rsooc8d t172 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 8));
                        t171 = (uint8_t)gm_eq2ceic3f8rp(((uint8_t *)&t172), ((uint8_t *)&t169));
                    }
                    uint8_t t173 = (uint8_t)(t171);
                    gm_tl874t2atnlh3 t174 = gm_l1;
                    gm_t3fcbuehh3bs2 t175 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)&gm_l2));
                    gm_t3fcbuehh3bs2 t176;
                    GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t176 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t176 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t176 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&t176 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t176 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t176 + 8), t169);
                    gm_t2gtbh39ptekd t177;
                    memset(&t177, 0, sizeof t177);
                    GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)3ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t177 + 8), t173);
                    gm_tc3oht4ksth9d t178;
                    gm_f_mk_expr_53qj7nd((void *)&t178, t168, &t177, GM_CONV(gm_cseahvg8v7p0vs, t174));
                    gm_to1cssm8dfiv5 t179;
                    GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                    ((uint8_t *)&t179)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t179 + 8), t178);
                    memmove(gm_out, ((uint8_t *)&t179), 96);
                    goto gm_fdone;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t181 = (uint8_t)(t180 == ((uint8_t)2ULL));
                    if (!t181) {
                        goto gm_lb19;
                    }
                    uint8_t t182 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t183 = (uint8_t)(t182 == ((uint8_t)19ULL));
                    if (!t183) {
                        goto gm_lb19;
                    }
                    void * t184 = gm_l0;
                    gm_tm4pc0emhrv1g t185;
                    gm_f_bump_q2i40re((void *)&t185, t184);
                    void * t186 = gm_l0;
                    gm_tl874t2atnlh3 t187 = gm_l1;
                    gm_t2gtbh39ptekd t188;
                    memset(&t188, 0, sizeof t188);
                    GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)4ULL));
                    gm_tc3oht4ksth9d t189;
                    gm_f_mk_expr_53qj7nd((void *)&t189, t186, &t188, GM_CONV(gm_cseahvg8v7p0vs, t187));
                    gm_to1cssm8dfiv5 t190;
                    GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                    ((uint8_t *)&t190)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t190 + 8), t189);
                    memmove(gm_out, ((uint8_t *)&t190), 96);
                    goto gm_fdone;
                }
gm_lb19:;
                {
                    uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)1ULL));
                    if (!t192) {
                        goto gm_lb20;
                    }
                    void * t193 = gm_l0;
                    gm_tm4pc0emhrv1g t194;
                    gm_f_bump_q2i40re((void *)&t194, t193);
                    void * t195 = gm_l0;
                    gm_tl874t2atnlh3 t196 = gm_l1;
                    gm_t2gtbh39ptekd t197;
                    memset(&t197, 0, sizeof t197);
                    GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)7ULL));
                    gm_tc3oht4ksth9d t198;
                    gm_f_mk_expr_53qj7nd((void *)&t198, t195, &t197, GM_CONV(gm_cseahvg8v7p0vs, t196));
                    gm_to1cssm8dfiv5 t199;
                    GM_ST(uint8_t, (uint8_t *)&t199 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t199 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
                    ((uint8_t *)&t199)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t199 + 8), t198);
                    memmove(gm_out, ((uint8_t *)&t199), 96);
                    goto gm_fdone;
                }
gm_lb20:;
                {
                    uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t201 = (uint8_t)(t200 == ((uint8_t)8ULL));
                    if (!t201) {
                        goto gm_lb21;
                    }
                    uint8_t t202 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t203 = (uint8_t)(t202 == ((uint8_t)53ULL));
                    if (!t203) {
                        goto gm_lb21;
                    }
                    void * t204 = gm_l0;
                    gm_tm4pc0emhrv1g t205;
                    gm_f_bump_q2i40re((void *)&t205, t204);
                    void * t206 = gm_l0;
                    gm_tl874t2atnlh3 t207 = gm_l1;
                    gm_t2gtbh39ptekd t208;
                    memset(&t208, 0, sizeof t208);
                    GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)5ULL));
                    gm_tc3oht4ksth9d t209;
                    gm_f_mk_expr_53qj7nd((void *)&t209, t206, &t208, GM_CONV(gm_cseahvg8v7p0vs, t207));
                    gm_to1cssm8dfiv5 t210;
                    GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                    ((uint8_t *)&t210)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t210 + 8), t209);
                    memmove(gm_out, ((uint8_t *)&t210), 96);
                    goto gm_fdone;
                }
gm_lb21:;
                {
                    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t212 = (uint8_t)(t211 == ((uint8_t)0ULL));
                    if (!t212) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
                    gm_l8 = t213;
                    {
                        {
                            gm_b16_8 t214 = gm_l8;
                            gm_b16_8 t215;
                            GM_ST(void *, (uint8_t *)&t215, (void *)(gm_st2dnokm1h0jd));
                            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(4));
                            uint8_t t216 = (gm_eq0iqiubfm7e(((uint8_t *)&t214), ((uint8_t *)&t215)));
                            if (!t216) {
                                goto gm_lb24;
                            }
                            void * t217 = gm_l0;
                            gm_t9qkh0rsooc8d t218;
                            GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)61ULL));
                            uint8_t t219 = gm_f_nth_is_3fgqmcb(t217, ((uint64_t)1ULL), t218);
                            if (!t219) {
                                goto gm_lb24;
                            }
                            void * t220 = gm_l0;
                            gm_f_parse_cont_type_ljmco4q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t220);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb24:;
                    }
gm_lb23:;
                    {
                        {
                            gm_b16_8 t221 = gm_l8;
                            gm_b16_8 t222;
                            GM_ST(void *, (uint8_t *)&t222, (void *)(gm_sic5vsema0r36));
                            GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(7));
                            uint8_t t223 = (gm_eq0iqiubfm7e(((uint8_t *)&t221), ((uint8_t *)&t222)));
                            if (!t223) {
                                goto gm_lb26;
                            }
                            void * t224 = gm_l0;
                            gm_f_nth_c0r2nvs((void *)&gm_l16, t224, ((uint64_t)1ULL));
                            {
                                {
                                    uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                                    uint8_t t226 = (uint8_t)(t225 == ((uint8_t)0ULL));
                                    if (!t226) {
                                        goto gm_lb28;
                                    }
                                    gm_l15 = ((uint8_t)1);
                                    goto gm_lb27;
                                }
gm_lb28:;
                                {
                                    gm_l15 = ((uint8_t)0);
                                    goto gm_lb27;
                                }
gm_lb29:;
                                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 643, 38);
                            }
gm_lb27:;
                            uint8_t t227 = gm_l15;
                            if (!t227) {
                                goto gm_lb26;
                            }
                            void * t228 = gm_l0;
                            gm_f_parse_handler_lit_1nkojsu((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t228);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb26:;
                    }
gm_lb25:;
                    {
                        {
                            gm_b16_8 t229 = gm_l8;
                            gm_b16_8 t230;
                            GM_ST(void *, (uint8_t *)&t230, (void *)(gm_sv0fbf330lmcd));
                            GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(7));
                            uint8_t t231 = (gm_eq0iqiubfm7e(((uint8_t *)&t229), ((uint8_t *)&t230)));
                            if (!t231) {
                                goto gm_lb31;
                            }
                            void * t232 = gm_l0;
                            gm_f_nth_c0r2nvs((void *)&gm_l18, t232, ((uint64_t)1ULL));
                            {
                                {
                                    uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                                    uint8_t t234 = (uint8_t)(t233 == ((uint8_t)5ULL));
                                    if (!t234) {
                                        goto gm_lb33;
                                    }
                                    gm_l17 = ((uint8_t)1);
                                    goto gm_lb32;
                                }
gm_lb33:;
                                {
                                    gm_l17 = ((uint8_t)0);
                                    goto gm_lb32;
                                }
gm_lb34:;
                                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 644, 38);
                            }
gm_lb32:;
                            uint8_t t235 = gm_l17;
                            if (!t235) {
                                goto gm_lb31;
                            }
                            void * t236 = gm_l0;
                            gm_f_parse_foreign_l4091na((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t236);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb31:;
                    }
gm_lb30:;
                    void * t237 = gm_l0;
                    gm_tm4pc0emhrv1g t238;
                    gm_f_bump_q2i40re((void *)&t238, t237);
                    void * t239 = gm_l0;
                    gm_b16_8 t240 = gm_l8;
                    gm_tl874t2atnlh3 t241 = gm_l1;
                    gm_t08olq97rscn3 t242;
                    GM_ST(uint32_t, (uint8_t *)&t242 + 28, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t242 + 0), t240);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t242 + 16), t241);
                    gm_tl874t2atnlh3 t243 = gm_l1;
                    gm_t2gtbh39ptekd t244;
                    GM_ST(uint8_t, (uint8_t *)&t244 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t244 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t244 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)6ULL));
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t244 + 8), t242);
                    gm_tc3oht4ksth9d t245;
                    gm_f_mk_expr_53qj7nd((void *)&t245, t239, &t244, GM_CONV(gm_cseahvg8v7p0vs, t243));
                    gm_to1cssm8dfiv5 t246;
                    GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                    ((uint8_t *)&t246)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t246 + 8), t245);
                    memmove(gm_out, ((uint8_t *)&t246), 96);
                    goto gm_fdone;
                }
gm_lb22:;
            }
            {
                {
                    uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t248 = (uint8_t)(t247 == ((uint8_t)8ULL));
                    if (!t248) {
                        goto gm_lb35;
                    }
                    uint8_t t249 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t250 = (uint8_t)(t249 == ((uint8_t)61ULL));
                    if (!t250) {
                        goto gm_lb35;
                    }
                    void * t251 = gm_l0;
                    gm_t9qkh0rsooc8d t252;
                    GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)0ULL));
                    gm_f_parse_paren_sq1guoi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t251, t252);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb35:;
                {
                    uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t254 = (uint8_t)(t253 == ((uint8_t)8ULL));
                    if (!t254) {
                        goto gm_lb36;
                    }
                    uint8_t t255 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t256 = (uint8_t)(t255 == ((uint8_t)58ULL));
                    if (!t256) {
                        goto gm_lb36;
                    }
                    void * t257 = gm_l0;
                    gm_tm4pc0emhrv1g t258;
                    gm_f_bump_q2i40re((void *)&t258, t257);
                    void * t259 = gm_l0;
                    gm_b16_8 t260;
                    GM_ST(void *, (uint8_t *)&t260, (void *)(gm_s541gi95epov5));
                    GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(24));
                    gm_f_parse_ident_n2c6b75((void *)&gm_l19, (void *)NULL, cx, NULL, t259, t260);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (t261) {
                    } else {
                        gm_to1cssm8dfiv5 t262;
                        memset(&t262, 0, sizeof t262);
                        ((uint8_t *)&t262)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t262 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t262), 96);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l9), (((uint8_t *)&gm_l19) + 8), 32);
                    void * t263 = gm_l0;
                    gm_t08olq97rscn3 t264 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l9));
                    void * t265 = gm_l0;
                    uint32_t t266 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
                    gm_t2gtbh39ptekd t267;
                    GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t267 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t267 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t267 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)8ULL));
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t267 + 8), t264);
                    gm_tl874t2atnlh3 t268 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t265, t266));
                    gm_tc3oht4ksth9d t269;
                    gm_f_mk_expr_53qj7nd((void *)&t269, t263, &t267, GM_CONV(gm_cseahvg8v7p0vs, t268));
                    gm_to1cssm8dfiv5 t270;
                    GM_ST(uint8_t, (uint8_t *)&t270 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t270 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t270 + 4, 0);
                    ((uint8_t *)&t270)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t270 + 8), t269);
                    memmove(gm_out, ((uint8_t *)&t270), 96);
                    goto gm_fdone;
                }
gm_lb36:;
                {
                    {
                        {
                            uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t272 = (uint8_t)(t271 == ((uint8_t)8ULL));
                            if (!t272) {
                                goto gm_lb39;
                            }
                            uint8_t t273 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                            uint8_t t274 = (uint8_t)(t273 == ((uint8_t)45ULL));
                            if (!t274) {
                                goto gm_lb39;
                            }
                            goto gm_lb38;
                        }
gm_lb39:;
                        {
                            uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t276 = (uint8_t)(t275 == ((uint8_t)8ULL));
                            if (!t276) {
                                goto gm_lb40;
                            }
                            uint8_t t277 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                            uint8_t t278 = (uint8_t)(t277 == ((uint8_t)47ULL));
                            if (!t278) {
                                goto gm_lb40;
                            }
                            goto gm_lb38;
                        }
gm_lb40:;
                        {
                            uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t280 = (uint8_t)(t279 == ((uint8_t)8ULL));
                            if (!t280) {
                                goto gm_lb41;
                            }
                            uint8_t t281 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                            uint8_t t282 = (uint8_t)(t281 == ((uint8_t)63ULL));
                            if (!t282) {
                                goto gm_lb41;
                            }
                            goto gm_lb38;
                        }
gm_lb41:;
                        goto gm_lb37;
                    }
gm_lb38:;
                    void * t283 = gm_l0;
                    gm_t9qkh0rsooc8d t284;
                    GM_ST(uint8_t, ((uint8_t *)&t284 + 0), ((uint8_t)0ULL));
                    gm_f_parse_type_prefix_kfrfcdd((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t283, t284);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb37:;
                {
                    uint8_t t285 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t286 = (uint8_t)(t285 == ((uint8_t)2ULL));
                    if (!t286) {
                        goto gm_lb42;
                    }
                    uint8_t t287 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t288 = (uint8_t)(t287 == ((uint8_t)11ULL));
                    if (!t288) {
                        goto gm_lb42;
                    }
                    void * t289 = gm_l0;
                    gm_f_parse_fn_expr_c9357a4((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t289);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb42:;
            }
            {
                {
                    uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t291 = (uint8_t)(t290 == ((uint8_t)2ULL));
                    if (!t291) {
                        goto gm_lb43;
                    }
                    uint8_t t292 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t293 = (uint8_t)(t292 == ((uint8_t)24ULL));
                    if (!t293) {
                        goto gm_lb43;
                    }
                    void * t294 = gm_l0;
                    gm_f_parse_struct_lit_86pp62c((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t294);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb43:;
                {
                    uint8_t t295 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t296 = (uint8_t)(t295 == ((uint8_t)2ULL));
                    if (!t296) {
                        goto gm_lb44;
                    }
                    uint8_t t297 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t298 = (uint8_t)(t297 == ((uint8_t)27ULL));
                    if (!t298) {
                        goto gm_lb44;
                    }
                    void * t299 = gm_l0;
                    gm_f_parse_union_lit_39ap2o5((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t299);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb44:;
                {
                    uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t301 = (uint8_t)(t300 == ((uint8_t)2ULL));
                    if (!t301) {
                        goto gm_lb45;
                    }
                    uint8_t t302 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t303 = (uint8_t)(t302 == ((uint8_t)6ULL));
                    if (!t303) {
                        goto gm_lb45;
                    }
                    void * t304 = gm_l0;
                    gm_f_parse_effect_lit_cqei081((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t304);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb45:;
                {
                    uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t306 = (uint8_t)(t305 == ((uint8_t)2ULL));
                    if (!t306) {
                        goto gm_lb46;
                    }
                    uint8_t t307 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t308 = (uint8_t)(t307 == ((uint8_t)14ULL));
                    if (!t308) {
                        goto gm_lb46;
                    }
                    void * t309 = gm_l0;
                    gm_f_parse_import_s1hv231((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t309);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb46:;
                {
                    uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t311 = (uint8_t)(t310 == ((uint8_t)2ULL));
                    if (!t311) {
                        goto gm_lb47;
                    }
                    uint8_t t312 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t313 = (uint8_t)(t312 == ((uint8_t)13ULL));
                    if (!t313) {
                        goto gm_lb47;
                    }
                    void * t314 = gm_l0;
                    gm_b16_8 t315;
                    GM_ST(void *, (uint8_t *)&t315, (void *)(gm_sujvknhnh5am8));
                    GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(18));
                    gm_t9qkh0rsooc8d t316 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t314, t315);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t316;
                    uint8_t t317 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (t317) {
                    } else {
                        gm_to1cssm8dfiv5 t318;
                        memset(&t318, 0, sizeof t318);
                        ((uint8_t *)&t318)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t318 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t318), 96);
                        goto gm_fdone;
                    }
                    void * t319 = gm_l0;
                    gm_f_parse_if_shktfou((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t319);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb47:;
                {
                    uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t321 = (uint8_t)(t320 == ((uint8_t)2ULL));
                    if (!t321) {
                        goto gm_lb48;
                    }
                    uint8_t t322 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t323 = (uint8_t)(t322 == ((uint8_t)17ULL));
                    if (!t323) {
                        goto gm_lb48;
                    }
                    void * t324 = gm_l0;
                    gm_b16_8 t325;
                    GM_ST(void *, (uint8_t *)&t325, (void *)(gm_sl7a8f8pe8le9));
                    GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(20));
                    gm_t9qkh0rsooc8d t326 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t324, t325);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t326;
                    uint8_t t327 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                    if (t327) {
                    } else {
                        gm_to1cssm8dfiv5 t328;
                        memset(&t328, 0, sizeof t328);
                        ((uint8_t *)&t328)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t328 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t328), 96);
                        goto gm_fdone;
                    }
                    void * t329 = gm_l0;
                    gm_f_parse_match_1lcgsrh((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t329);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb48:;
            }
            {
                {
                    uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t331 = (uint8_t)(t330 == ((uint8_t)2ULL));
                    if (!t331) {
                        goto gm_lb49;
                    }
                    uint8_t t332 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t333 = (uint8_t)(t332 == ((uint8_t)31ULL));
                    if (!t333) {
                        goto gm_lb49;
                    }
                    void * t334 = gm_l0;
                    gm_b16_8 t335;
                    GM_ST(void *, (uint8_t *)&t335, (void *)(gm_s1llcn0p4qesk));
                    GM_ST(uintptr_t, (uint8_t *)&t335 + 8, (uintptr_t)(19));
                    gm_t9qkh0rsooc8d t336 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t334, t335);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t336;
                    uint8_t t337 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                    if (t337) {
                    } else {
                        gm_to1cssm8dfiv5 t338;
                        memset(&t338, 0, sizeof t338);
                        ((uint8_t *)&t338)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t338 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t338), 96);
                        goto gm_fdone;
                    }
                    void * t339 = gm_l0;
                    gm_t1s75i08qtptd t340;
                    memset(&t340, 0, sizeof t340);
                    gm_f_parse_with_hlmj8pt((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t339, &t340);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb49:;
                {
                    uint8_t t341 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t342 = (uint8_t)(t341 == ((uint8_t)2ULL));
                    if (!t342) {
                        goto gm_lb50;
                    }
                    uint8_t t343 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t344 = (uint8_t)(t343 == ((uint8_t)12ULL));
                    if (!t344) {
                        goto gm_lb50;
                    }
                    void * t345 = gm_l0;
                    gm_b16_8 t346;
                    GM_ST(void *, (uint8_t *)&t346, (void *)(gm_sqm6u7rqmi1il));
                    GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(18));
                    gm_t9qkh0rsooc8d t347 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t345, t346);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t347;
                    uint8_t t348 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                    if (t348) {
                    } else {
                        gm_to1cssm8dfiv5 t349;
                        memset(&t349, 0, sizeof t349);
                        ((uint8_t *)&t349)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t349 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t349), 96);
                        goto gm_fdone;
                    }
                    void * t350 = gm_l0;
                    gm_t1s75i08qtptd t351;
                    memset(&t351, 0, sizeof t351);
                    gm_f_parse_for_q8vk3da((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t350, &t351);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb50:;
                {
                    uint8_t t352 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t353 = (uint8_t)(t352 == ((uint8_t)2ULL));
                    if (!t353) {
                        goto gm_lb51;
                    }
                    uint8_t t354 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t355 = (uint8_t)(t354 == ((uint8_t)23ULL));
                    if (!t355) {
                        goto gm_lb51;
                    }
                    void * t356 = gm_l0;
                    gm_f_parse_return_fjkmc1q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t356);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb51:;
                {
                    uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t358 = (uint8_t)(t357 == ((uint8_t)2ULL));
                    if (!t358) {
                        goto gm_lb52;
                    }
                    uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t360 = (uint8_t)(t359 == ((uint8_t)1ULL));
                    if (!t360) {
                        goto gm_lb52;
                    }
                    void * t361 = gm_l0;
                    gm_f_parse_break_hehputf((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t361);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb52:;
                {
                    uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t363 = (uint8_t)(t362 == ((uint8_t)2ULL));
                    if (!t363) {
                        goto gm_lb53;
                    }
                    uint8_t t364 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t365 = (uint8_t)(t364 == ((uint8_t)3ULL));
                    if (!t365) {
                        goto gm_lb53;
                    }
                    void * t366 = gm_l0;
                    gm_f_parse_continue_ertnf67((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t366);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb53:;
                {
                    uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t368 = (uint8_t)(t367 == ((uint8_t)2ULL));
                    if (!t368) {
                        goto gm_lb54;
                    }
                    uint8_t t369 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t370 = (uint8_t)(t369 == ((uint8_t)22ULL));
                    if (!t370) {
                        goto gm_lb54;
                    }
                    void * t371 = gm_l0;
                    gm_f_parse_resume_u919e4d((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t371);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb54:;
            }
            {
                {
                    uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t373 = (uint8_t)(t372 == ((uint8_t)6ULL));
                    if (!t373) {
                        goto gm_lb55;
                    }
                    void * t374 = gm_l0;
                    gm_f_parse_intrinsic_4ou54h3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t374);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb55:;
                {
                    uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t376 = (uint8_t)(t375 == ((uint8_t)7ULL));
                    if (!t376) {
                        goto gm_lb56;
                    }
                    void * t377 = gm_l0;
                    gm_t9qkh0rsooc8d t378;
                    GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)0ULL));
                    gm_f_parse_attributed_00u9rjp((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t377, t378);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb56:;
                {
                    uint8_t t379 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t380 = (uint8_t)(t379 == ((uint8_t)2ULL));
                    if (!t380) {
                        goto gm_lb57;
                    }
                    uint8_t t381 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t382 = (uint8_t)(t381 == ((uint8_t)18ULL));
                    if (!t382) {
                        goto gm_lb57;
                    }
                    void * t383 = gm_l0;
                    gm_tl874t2atnlh3 t384 = gm_l1;
                    gm_b16_8 t385;
                    GM_ST(void *, (uint8_t *)&t385, (void *)(gm_sb455a3lakuom));
                    GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(59));
                    gm_b0_1 t386 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t383, t384, t385);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_to1cssm8dfiv5 t387;
                    memset(&t387, 0, sizeof t387);
                    ((uint8_t *)&t387)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t387 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t387), 96);
                    goto gm_fdone;
                }
gm_lb57:;
                {
                    uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t389 = (uint8_t)(t388 == ((uint8_t)2ULL));
                    if (!t389) {
                        goto gm_lb58;
                    }
                    uint8_t t390 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t391 = (uint8_t)(t390 == ((uint8_t)26ULL));
                    if (!t391) {
                        goto gm_lb58;
                    }
                    void * t392 = gm_l0;
                    gm_tl874t2atnlh3 t393 = gm_l1;
                    gm_b16_8 t394;
                    GM_ST(void *, (uint8_t *)&t394, (void *)(gm_slp04vsc9cddi));
                    GM_ST(uintptr_t, (uint8_t *)&t394 + 8, (uintptr_t)(48));
                    gm_b0_1 t395 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t392, t393, t394);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_to1cssm8dfiv5 t396;
                    memset(&t396, 0, sizeof t396);
                    ((uint8_t *)&t396)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t396 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t396), 96);
                    goto gm_fdone;
                }
gm_lb58:;
                {
                    void * t397 = gm_l0;
                    gm_b16_8 t398;
                    GM_ST(void *, (uint8_t *)&t398, (void *)(gm_s42atdm90l4do));
                    GM_ST(uintptr_t, (uint8_t *)&t398 + 8, (uintptr_t)(13));
                    gm_b0_1 t399 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t397, t398);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_to1cssm8dfiv5 t400;
                    memset(&t400, 0, sizeof t400);
                    ((uint8_t *)&t400)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t400 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t400), 96);
                    goto gm_fdone;
                }
gm_lb59:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 611, 5);
            }
        }
gm_lb11:;
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
inline __attribute__((aligned(4))) void gm_f_parse_range_o9o2al7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_range_o9o2al7 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 531u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l2;
    void * t2 = gm_l0;
    gm_f_parse_prefix_671fi6d((void *)&gm_l2, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t3) {
    } else {
        gm_to1cssm8dfiv5 t4;
        memset(&t4, 0, sizeof t4);
        ((uint8_t *)&t4)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l2) + 8), 88);
    void * t5 = gm_l0;
    gm_t3fcbuehh3bs2 t6;
    gm_f_kind_k239kpg((void *)&t6, t5);
    uint8_t t7 = gm_f_binary_follows_jeb42bi(&t6);
    uint8_t t8 = (uint8_t)!t7;
    if (t8) {
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
        gm_to1cssm8dfiv5 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        ((uint8_t *)&t10)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t10 + 8), t9);
        memmove(gm_out, ((uint8_t *)&t10), 96);
        goto gm_fdone;
    }
    void * t11 = gm_l0;
    gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
    gm_f_range_from_5v6ko9m((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t11, &t12);
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
inline __attribute__((aligned(4))) void gm_f_parse_resume_u919e4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_resume_u919e4d F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_tl874t2atnlh3 gm_l3; /* whole */
    void * gm_l4; /* callee */
    gm_b16_8 gm_l5; /* args */
    void * gm_l6; /* value */
    GM_STACK_CHECK(&F, 882u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    void * t11 = gm_l0;
    uint32_t t12 = gm_f_lo_fp82gt4(t11);
    gm_l1 = t12;
    void * t13 = gm_l0;
    gm_tm4pc0emhrv1g t14;
    gm_f_bump_q2i40re((void *)&t14, t13);
    void * t15 = gm_l0;
    gm_f_parse_postfix_ao4kstr((void *)&gm_l8, (void *)NULL, cx, NULL, t15, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t16) {
    } else {
        gm_to1cssm8dfiv5 t17;
        memset(&t17, 0, sizeof t17);
        ((uint8_t *)&t17)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t17), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l8) + 8), 88);
    void * t18 = gm_l0;
    uint32_t t19 = gm_l1;
    gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t18, t19));
    gm_l3 = t20;
    {
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)19ULL));
            if (!t22) {
                goto gm_lb2;
            }
            void * t23 = GM_LD(void *, ((((uint8_t *)&F.l2) + 8) + 8));
            gm_l4 = t23;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l2) + 8) + 8) + 8));
            gm_l5 = t24;
            {
                {
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t26 = (t25 <= ((uint64_t)1ULL));
                    if (!t26) {
                        goto gm_lb4;
                    }
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
                    if (t28) {
                        gm_l9 = ((uint8_t)1);
                    } else {
                        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_src0dtioet3p4f6, 811, 48);
                        {
                            {
                                uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
                                if (!t32) {
                                    goto gm_lb6;
                                }
                                gm_l10 = ((uint8_t)1);
                                goto gm_lb5;
                            }
gm_lb6:;
                            {
                                gm_l10 = ((uint8_t)0);
                                goto gm_lb5;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 811, 48);
                        }
gm_lb5:;
                        uint8_t t33 = gm_l10;
                        gm_l9 = t33;
                    }
                    uint8_t t34 = gm_l9;
                    if (!t34) {
                        goto gm_lb4;
                    }
                    gm_l6 = ((void *)0);
                    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t36 = ((t35 == ((uint64_t)1ULL)));
                    if (t36) {
                        {
                            {
                                void * t37 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t38, gm_src0dtioet3p4f6, 814, 38);
                                uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t37) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                                if (!t40) {
                                    goto gm_lb9;
                                }
                                memmove(((uint8_t *)&F.l7), ((((uint8_t *)t37) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                                void * t41 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l7)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l6 = t41;
                                goto gm_lb8;
                            }
gm_lb9:;
                        }
gm_lb8:;
                    }
                    void * t42 = gm_l0;
                    void * t43 = gm_l4;
                    void * t44 = gm_l6;
                    gm_t3mf58gv9kf3n t45;
                    GM_ST(void *, ((uint8_t *)&t45 + 0), t43);
                    GM_ST(void *, ((uint8_t *)&t45 + 8), t44);
                    gm_tl874t2atnlh3 t46 = gm_l3;
                    gm_t2gtbh39ptekd t47;
                    memset(&t47, 0, sizeof t47);
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)48ULL));
                    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t47 + 8), t45);
                    gm_tc3oht4ksth9d t48;
                    gm_f_mk_expr_53qj7nd((void *)&t48, t42, &t47, GM_CONV(gm_cseahvg8v7p0vs, t46));
                    gm_to1cssm8dfiv5 t49;
                    GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                    ((uint8_t *)&t49)[0] = 0;
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t49 + 8), t48);
                    memmove(gm_out, ((uint8_t *)&t49), 96);
                    goto gm_fdone;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t50 = gm_l0;
    gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
    gm_b16_8 t52;
    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_skh04d6p3n402));
    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(78));
    gm_b0_1 t53 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t50, t51, t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_to1cssm8dfiv5 t54;
    memset(&t54, 0, sizeof t54);
    ((uint8_t *)&t54)[0] = 1;
    GM_ST(gm_b0_1, ((uint8_t *)&t54 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t54), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_return_fjkmc1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_return_fjkmc1q F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* value */
    GM_STACK_CHECK(&F, 626u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l3;
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_lo_fp82gt4(t2);
    gm_l1 = t3;
    void * t4 = gm_l0;
    gm_tm4pc0emhrv1g t5;
    gm_f_bump_q2i40re((void *)&t5, t4);
    gm_l2 = ((void *)0);
    void * t6 = gm_l0;
    gm_t3fcbuehh3bs2 t7;
    gm_f_kind_k239kpg((void *)&t7, t6);
    uint8_t t8 = gm_f_can_begin_expr_rmia9bs(&t7);
    if (t8) {
        void * t9 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&gm_l3, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t10) {
        } else {
            gm_to1cssm8dfiv5 t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t11), 96);
            goto gm_fdone;
        }
        void * t12 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l3) + 8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t12;
    }
    void * t13 = gm_l0;
    void * t14 = gm_l2;
    void * t15 = gm_l0;
    uint32_t t16 = gm_l1;
    gm_t2gtbh39ptekd t17;
    memset(&t17, 0, sizeof t17);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)45ULL));
    GM_ST(void *, ((uint8_t *)&t17 + 8), t14);
    gm_tl874t2atnlh3 t18 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t15, t16));
    gm_tc3oht4ksth9d t19;
    gm_f_mk_expr_53qj7nd((void *)&t19, t13, &t17, GM_CONV(gm_cseahvg8v7p0vs, t18));
    gm_to1cssm8dfiv5 t20;
    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
    ((uint8_t *)&t20)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t20 + 8), t19);
    memmove(gm_out, ((uint8_t *)&t20), 96);
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
inline __attribute__((aligned(4))) void gm_f_parse_shift_jium2c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_shift_jium2c6 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 531u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l2;
    void * t2 = gm_l0;
    gm_f_parse_prefix_671fi6d((void *)&gm_l2, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t3) {
    } else {
        gm_to1cssm8dfiv5 t4;
        memset(&t4, 0, sizeof t4);
        ((uint8_t *)&t4)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l2) + 8), 88);
    void * t5 = gm_l0;
    gm_t3fcbuehh3bs2 t6;
    gm_f_kind_k239kpg((void *)&t6, t5);
    uint8_t t7 = gm_f_binary_follows_jeb42bi(&t6);
    uint8_t t8 = (uint8_t)!t7;
    if (t8) {
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
        gm_to1cssm8dfiv5 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        ((uint8_t *)&t10)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t10 + 8), t9);
        memmove(gm_out, ((uint8_t *)&t10), 96);
        goto gm_fdone;
    }
    void * t11 = gm_l0;
    gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
    gm_f_shift_from_3cnaq25((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t11, &t12);
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
inline __attribute__((aligned(4))) void gm_f_parse_subscript_1hujuhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_subscript_1hujuhl F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    gm_tao63pk3r18v5 gm_l6; /* x */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    GM_STACK_CHECK(&F, 1159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tb1kjfin6jnc2 gm_l8;
    gm_t0u0j6voh3loe gm_l9;
    uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
    gm_l2 = t5;
    void * t6 = gm_l0;
    gm_tm4pc0emhrv1g t7;
    gm_f_bump_q2i40re((void *)&t7, t6);
    void * t8 = gm_l0;
    gm_f_parse_subscript_inner_dhgpovb((void *)&gm_l8, (void *)NULL, cx, NULL, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t9) {
    } else {
        gm_to1cssm8dfiv5 t10;
        memset(&t10, 0, sizeof t10);
        ((uint8_t *)&t10)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t10 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t10), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l8) + 8), 96);
    void * t11 = gm_l0;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sh0is7j09prt2));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(22));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)64ULL));
    gm_t0u0j6voh3loe t14 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t11, t13, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t15) {
    } else {
        gm_to1cssm8dfiv5 t16;
        memset(&t16, 0, sizeof t16);
        ((uint8_t *)&t16)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t16), 96);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 4));
    void * t18 = gm_l0;
    uint32_t t19 = gm_l2;
    gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t18, t19));
    gm_l4 = t20;
    {
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
            if (!t22) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l5), (((uint8_t *)&F.l3) + 8), 88);
            void * t23 = gm_l0;
            void * t24 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t25 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t26;
            GM_ST(void *, ((uint8_t *)&t26 + 0), t24);
            GM_ST(void *, ((uint8_t *)&t26 + 8), t25);
            gm_tl874t2atnlh3 t27 = gm_l4;
            gm_t2gtbh39ptekd t28;
            memset(&t28, 0, sizeof t28);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)20ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t28 + 8), t26);
            gm_tc3oht4ksth9d t29;
            gm_f_mk_expr_53qj7nd((void *)&t29, t23, &t28, GM_CONV(gm_cseahvg8v7p0vs, t27));
            gm_to1cssm8dfiv5 t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            ((uint8_t *)&t30)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t30 + 8), t29);
            memmove(gm_out, ((uint8_t *)&t30), 96);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
            if (!t32) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l6), (((uint8_t *)&F.l3) + 8), 32);
            memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l6), 32);
            void * t33 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, ((uint8_t *)&gm_l7), t33);
            void * t34 = gm_l0;
            gm_tao63pk3r18v5 t35 = GM_LD(gm_tao63pk3r18v5, ((uint8_t *)&gm_l7));
            gm_tl874t2atnlh3 t36 = gm_l4;
            gm_t2gtbh39ptekd t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)21ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t37 + 8), t35);
            gm_tc3oht4ksth9d t38;
            gm_f_mk_expr_53qj7nd((void *)&t38, t34, &t37, GM_CONV(gm_cseahvg8v7p0vs, t36));
            gm_to1cssm8dfiv5 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            ((uint8_t *)&t39)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t39 + 8), t38);
            memmove(gm_out, ((uint8_t *)&t39), 96);
            goto gm_fdone;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 502, 5);
    }
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_parse_subscript_inner_dhgpovb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_subscript_inner_dhgpovb F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    void * gm_l2; /* placeholder */
    uint8_t gm_l3; /* inclusive */
    gm_tl874t2atnlh3 gm_l4; /* op_span */
    void * gm_l5; /* hi */
    gm_ts6oqppjh0ba3 gm_l7; /* op */
    void * gm_l8; /* lhs */
    void * gm_l9; /* rhs */
    uint8_t gm_l10; /* inclusive */
    uint8_t gm_l11; /* open */
    void * gm_l12; /* hi */
    GM_STACK_CHECK(&F, 1861u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l13;
    gm_tm4pc0emhrv1g gm_l14;
    gm_to1cssm8dfiv5 gm_l15;
    uint8_t gm_l16;
    gm_to1cssm8dfiv5 gm_l17;
    uint8_t gm_l18;
    void * gm_l19;
    {
        void * t13 = gm_l0;
        gm_toh0ootodhjue t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t14 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t14 + 2), ((uint8_t)1));
        gm_toh0ootodhjue t15 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t13, GM_CONV(gm_cs3cuou245tr5f, t14)));
        gm_l1 = t15;
        void * t16 = gm_l0;
        void * t17 = gm_l0;
        gm_tl874t2atnlh3 t18 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t17));
        gm_tc3oht4ksth9d t19;
        gm_f_error_expr_dhdi86k((void *)&t19, t16, GM_CONV(gm_cseahvg8v7p0vs, t18));
        void * t20 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_ld12_1;
        }
        gm_l2 = t20;
        void * t21 = gm_l0;
        gm_t9qkh0rsooc8d t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)50ULL));
        uint8_t t23 = gm_f_at_54906rm(t21, t22);
        if (t23) {
            gm_l13 = ((uint8_t)1);
        } else {
            void * t24 = gm_l0;
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)51ULL));
            uint8_t t26 = gm_f_at_54906rm(t24, t25);
            gm_l13 = t26;
        }
        uint8_t t27 = gm_l13;
        if (t27) {
            void * t28 = gm_l0;
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)51ULL));
            uint8_t t30 = gm_f_at_54906rm(t28, t29);
            gm_l3 = t30;
            void * t31 = gm_l0;
            gm_f_bump_q2i40re((void *)&gm_l14, t31);
            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l14) + 32));
            gm_l4 = t32;
            gm_l5 = ((void *)0);
            void * t33 = gm_l0;
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)64ULL));
            uint8_t t35 = gm_f_at_54906rm(t33, t34);
            uint8_t t36 = (uint8_t)!t35;
            if (t36) {
                void * t37 = gm_l0;
                gm_f_parse_expr_bkb2n6e((void *)&gm_l15, (void *)NULL, cx, NULL, t37);
                if (gm_unw.kind) {
                    goto gm_ld12_1;
                }
                uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (t38) {
                } else {
                    gm_tb1kjfin6jnc2 t39;
                    memset(&t39, 0, sizeof t39);
                    ((uint8_t *)&t39)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), GM_ZERO(gm_b0_1));
                    memmove(&F.ret, ((uint8_t *)&t39), 104);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld12_1;
                }
                void * t40 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l15) + 8)));
                if (gm_unw.kind) {
                    goto gm_ld12_1;
                }
                gm_l5 = t40;
            }
            {
                {
                    uint8_t t41 = gm_l3;
                    if (!t41) {
                        goto gm_lb3;
                    }
                    {
                        {
                            uint8_t t42 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                            if (t42) {
                                goto gm_lb5;
                            }
                            gm_l16 = ((uint8_t)1);
                            goto gm_lb4;
                        }
gm_lb5:;
                        {
                            gm_l16 = ((uint8_t)0);
                            goto gm_lb4;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 527, 26);
                    }
gm_lb4:;
                    uint8_t t43 = gm_l16;
                    if (!t43) {
                        goto gm_lb3;
                    }
                    void * t44 = gm_l0;
                    gm_tl874t2atnlh3 t45 = gm_l4;
                    gm_b16_8 t46;
                    GM_ST(void *, (uint8_t *)&t46, (void *)(gm_skefd3dps40es));
                    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(39));
                    gm_b0_1 t47 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t44, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_ld12_1;
                    }
                    goto gm_lb2;
                }
gm_lb3:;
            }
gm_lb2:;
            void * t48 = gm_l2;
            void * t49 = gm_l5;
            uint8_t t50 = gm_l3;
            gm_tao63pk3r18v5 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 28, 0);
            GM_ST(void *, ((uint8_t *)&t51 + 0), t48);
            GM_ST(void *, ((uint8_t *)&t51 + 8), ((void *)0));
            GM_ST(void *, ((uint8_t *)&t51 + 16), t49);
            GM_ST(uint8_t, ((uint8_t *)&t51 + 24), t50);
            gm_t1vei8bd9e9m7 t52;
            memset(&t52, 0, sizeof t52);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t52 + 8), t51);
            gm_tb1kjfin6jnc2 t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            ((uint8_t *)&t53)[0] = 0;
            GM_ST(gm_t1vei8bd9e9m7, ((uint8_t *)&t53 + 8), t52);
            memmove(&F.ret, ((uint8_t *)&t53), 104);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld12_1;
        }
        void * t54 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&gm_l17, (void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_ld12_1;
        }
        uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (t55) {
        } else {
            gm_tb1kjfin6jnc2 t56;
            memset(&t56, 0, sizeof t56);
            ((uint8_t *)&t56)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t56 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t56), 104);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld12_1;
        }
        memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l17) + 8), 88);
        {
            {
                uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 8) + 0));
                uint8_t t58 = (uint8_t)(t57 == ((uint8_t)13ULL));
                if (!t58) {
                    goto gm_lb8;
                }
                gm_ts6oqppjh0ba3 t59 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)&F.l6) + 8) + 8));
                gm_l7 = t59;
                void * t60 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 8) + 8));
                gm_l8 = t60;
                void * t61 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 8) + 16));
                gm_l9 = t61;
                gm_ts6oqppjh0ba3 t62 = gm_l7;
                gm_ts6oqppjh0ba3 t63;
                GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)10ULL));
                gm_ts6oqppjh0ba3 t64;
                GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)11ULL));
                gm_t9ldr1q5hn9fq t65;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t65 + 0), t63);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t65 + 2), t64);
                uint8_t t66 = 0;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t65) + 0, ((uint8_t *)&t62))) t66 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t65) + 2, ((uint8_t *)&t62))) t66 = 1;
                uint8_t t67 = (uint8_t)(t66);
                if (t67) {
                    gm_ts6oqppjh0ba3 t68 = gm_l7;
                    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
                    uint8_t t70 = (uint8_t)((t69 == ((uint8_t)11ULL)));
                    gm_ts6oqppjh0ba3 t71;
                    GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)11ULL));
                    gm_l10 = t70;
                    void * t72 = gm_l9;
                    uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t72) + 8) + 0));
                    uint8_t t74 = (uint8_t)((t73 == ((uint8_t)50ULL)));
                    gm_t2gtbh39ptekd t75 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t72) + 8));
                    gm_t2gtbh39ptekd t76;
                    memset(&t76, 0, sizeof t76);
                    GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)50ULL));
                    if (t74) {
                        void * t77 = gm_l9;
                        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                        uint8_t t79 = gm_f_is_empty_q2f0b93(GM_CONV(gm_cseahvg8v7p0vs, t78));
                        gm_l18 = t79;
                    } else {
                        gm_l18 = ((uint8_t)0);
                    }
                    uint8_t t80 = gm_l18;
                    gm_l11 = t80;
                    {
                        {
                            uint8_t t81 = gm_l11;
                            if (!t81) {
                                goto gm_lb10;
                            }
                            uint8_t t82 = gm_l10;
                            if (!t82) {
                                goto gm_lb10;
                            }
                            void * t83 = gm_l0;
                            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                            gm_b16_8 t85;
                            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_skefd3dps40es));
                            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(39));
                            gm_b0_1 t86 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t83, t84, t85);
                            if (gm_unw.kind) {
                                goto gm_ld12_1;
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint8_t t87 = gm_l11;
                    if (t87) {
                        gm_l19 = ((void *)0);
                    } else {
                        void * t88 = gm_l9;
                        gm_l19 = t88;
                    }
                    void * t89 = gm_l19;
                    gm_l12 = t89;
                    void * t90 = gm_l2;
                    void * t91 = gm_l8;
                    void * t92 = gm_l12;
                    uint8_t t93 = gm_l10;
                    gm_tao63pk3r18v5 t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 28, 0);
                    GM_ST(void *, ((uint8_t *)&t94 + 0), t90);
                    GM_ST(void *, ((uint8_t *)&t94 + 8), t91);
                    GM_ST(void *, ((uint8_t *)&t94 + 16), t92);
                    GM_ST(uint8_t, ((uint8_t *)&t94 + 24), t93);
                    gm_t1vei8bd9e9m7 t95;
                    memset(&t95, 0, sizeof t95);
                    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t95 + 8), t94);
                    gm_tb1kjfin6jnc2 t96;
                    GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                    ((uint8_t *)&t96)[0] = 0;
                    GM_ST(gm_t1vei8bd9e9m7, ((uint8_t *)&t96 + 8), t95);
                    memmove(&F.ret, ((uint8_t *)&t96), 104);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld12_1;
                }
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
        gm_tc3oht4ksth9d t97 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l6));
        gm_t1vei8bd9e9m7 t98;
        GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t98 + 8), t97);
        gm_tb1kjfin6jnc2 t99;
        GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
        ((uint8_t *)&t99)[0] = 0;
        GM_ST(gm_t1vei8bd9e9m7, ((uint8_t *)&t99 + 8), t98);
        memmove(&F.ret, ((uint8_t *)&t99), 104);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld12_1;
    }
    {
        void * t100 = gm_l0;
        gm_toh0ootodhjue t101 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t100) + 128), t101);
    }
    goto gm_ls102;
gm_ld12_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t103 = gm_l0;
        gm_toh0ootodhjue t104 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t103) + 128), t104);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls102:;
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_op_japctlh F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_taari1s0m51a7 gm_l1;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)8ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)1ULL));
            if (!t12) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t14;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[1] = 1;
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)8ULL));
            if (!t16) {
                goto gm_lb3;
            }
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)9ULL));
            if (!t18) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t20;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20), t19);
            ((uint8_t *)&t20)[1] = 1;
            gm_l1 = t20;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)8ULL));
            if (!t22) {
                goto gm_lb4;
            }
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)7ULL));
            if (!t24) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t26;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[1] = 1;
            gm_l1 = t26;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)8ULL));
            if (!t28) {
                goto gm_lb5;
            }
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)46ULL));
            if (!t30) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)3ULL));
            gm_taari1s0m51a7 t32;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[1] = 1;
            gm_l1 = t32;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)2ULL));
            if (!t34) {
                goto gm_lb6;
            }
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)26ULL));
            if (!t36) {
                goto gm_lb6;
            }
            gm_t9qkh0rsooc8d t37;
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)4ULL));
            gm_taari1s0m51a7 t38;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[1] = 1;
            gm_l1 = t38;
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_taari1s0m51a7 t39;
            memset(&t39, 0, sizeof t39);
            gm_l1 = t39;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 391, 54);
    }
gm_lb1:;
    gm_taari1s0m51a7 t40 = gm_l1;
    F.ret = t40;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh_c(gm_t3fcbuehh3bs2 a0) { return gm_f_prefix_op_japctlh(&a0); }
__attribute__((aligned(4))) void gm_f_range_from_5v6ko9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_range_from_5v6ko9m F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    uint8_t gm_l4; /* open */
    GM_STACK_CHECK(&F, 1631u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l7;
    uint8_t gm_l8;
    gm_tc3oht4ksth9d gm_l9;
    gm_to1cssm8dfiv5 gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_to1cssm8dfiv5 gm_l13;
    void * t5 = gm_l0;
    gm_tc3oht4ksth9d t6 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
    gm_f_bit_from_pu2bq19((void *)&gm_l7, (void *)NULL, cx, NULL, t5, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t7) {
    } else {
        gm_to1cssm8dfiv5 t8;
        memset(&t8, 0, sizeof t8);
        ((uint8_t *)&t8)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t8), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l7) + 8), 88);
    gm_ts6oqppjh0ba3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)10ULL));
    gm_l3 = t9;
    void * t10 = gm_l0;
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)51ULL));
    uint8_t t12 = gm_f_at_54906rm(t10, t11);
    if (t12) {
        gm_ts6oqppjh0ba3 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)11ULL));
        gm_l3 = t13;
    } else {
        void * t14 = gm_l0;
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)50ULL));
        uint8_t t16 = gm_f_at_54906rm(t14, t15);
        uint8_t t17 = (uint8_t)!t16;
        if (t17) {
            gm_tc3oht4ksth9d t18 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
            gm_to1cssm8dfiv5 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t19 + 8), t18);
            memmove(gm_out, ((uint8_t *)&t19), 96);
            goto gm_fdone;
        }
    }
    void * t20 = gm_l0;
    gm_tm4pc0emhrv1g t21;
    gm_f_bump_q2i40re((void *)&t21, t20);
    void * t22 = gm_l0;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 128) + 2));
    if (t23) {
        void * t24 = gm_l0;
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)64ULL));
        uint8_t t26 = gm_f_at_54906rm(t24, t25);
        gm_l8 = t26;
    } else {
        gm_l8 = ((uint8_t)0);
    }
    uint8_t t27 = gm_l8;
    gm_l4 = t27;
    uint8_t t28 = gm_l4;
    if (t28) {
        void * t29 = gm_l0;
        void * t30 = gm_l0;
        gm_tl874t2atnlh3 t31 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t30));
        gm_tl874t2atnlh3 t32 = GM_CONV(gm_tl874t2atnlh3, gm_f_shrink_to_lo_iok07dh(GM_CONV(gm_cseahvg8v7p0vs, t31)));
        gm_f_error_expr_dhdi86k((void *)&gm_l9, t29, GM_CONV(gm_cseahvg8v7p0vs, t32));
    } else {
        void * t33 = gm_l0;
        gm_f_parse_bit_h83vs72((void *)&gm_l10, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (t34) {
        } else {
            gm_to1cssm8dfiv5 t35;
            memset(&t35, 0, sizeof t35);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t35 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t35), 96);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&gm_l9), (((uint8_t *)&gm_l10) + 8), 88);
    }
    memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l9), 88);
    void * t36 = gm_l0;
    gm_ts6oqppjh0ba3 t37 = gm_l3;
    gm_tc3oht4ksth9d t38 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_tc3oht4ksth9d t39 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l5));
    gm_f_binary_41fr5r5((void *)&F.l6, (void *)NULL, cx, NULL, t36, t37, &t38, &t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t40 = gm_l0;
    gm_t9qkh0rsooc8d t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)50ULL));
    uint8_t t42 = gm_f_at_54906rm(t40, t41);
    if (t42) {
        gm_l11 = ((uint8_t)1);
    } else {
        void * t43 = gm_l0;
        gm_t9qkh0rsooc8d t44;
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)51ULL));
        uint8_t t45 = gm_f_at_54906rm(t43, t44);
        gm_l11 = t45;
    }
    uint8_t t46 = gm_l11;
    if (t46) {
        void * t47 = gm_l0;
        void * t48 = gm_l0;
        gm_tl874t2atnlh3 t49 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t48));
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s1q0n6t6d7g6n));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(33));
        gm_b0_1 t51 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t47, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            for (;;) {
                void * t52 = gm_l0;
                gm_t9qkh0rsooc8d t53;
                GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)50ULL));
                uint8_t t54 = gm_f_at_54906rm(t52, t53);
                if (t54) {
                    gm_l12 = ((uint8_t)1);
                } else {
                    void * t55 = gm_l0;
                    gm_t9qkh0rsooc8d t56;
                    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)51ULL));
                    uint8_t t57 = gm_f_at_54906rm(t55, t56);
                    gm_l12 = t57;
                }
                uint8_t t58 = gm_l12;
                if (!t58) {
                    goto gm_lb1;
                }
                {
                    void * t59 = gm_l0;
                    gm_tm4pc0emhrv1g t60;
                    gm_f_bump_q2i40re((void *)&t60, t59);
                    void * t61 = gm_l0;
                    gm_f_parse_bit_h83vs72((void *)&gm_l13, (void *)NULL, cx, NULL, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                    if (t62) {
                    } else {
                        gm_to1cssm8dfiv5 t63;
                        memset(&t63, 0, sizeof t63);
                        ((uint8_t *)&t63)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t63 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t63), 96);
                        goto gm_fdone;
                    }
                }
gm_lb3:;
            }
        }
gm_lb1:;
    }
    gm_tc3oht4ksth9d t64 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l6));
    gm_to1cssm8dfiv5 t65;
    GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
    ((uint8_t *)&t65)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t65 + 8), t64);
    memmove(gm_out, ((uint8_t *)&t65), 96);
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
__attribute__((aligned(4))) void gm_f_shift_from_3cnaq25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_shift_from_3cnaq25 F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 1652u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l6;
    gm_tt7vn9ehj0acr gm_l7;
    gm_ts6oqppjh0ba3 gm_l8;
    gm_to1cssm8dfiv5 gm_l9;
    uint8_t gm_l10;
    gm_tt7vn9ehj0acr gm_l11;
    uint8_t gm_l12;
    gm_tt7vn9ehj0acr gm_l13;
    gm_to1cssm8dfiv5 gm_l14;
    void * t11 = gm_l0;
    gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
    gm_f_add_from_7os0rvi((void *)&gm_l6, (void *)NULL, cx, NULL, t11, &t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t13) {
    } else {
        gm_to1cssm8dfiv5 t14;
        memset(&t14, 0, sizeof t14);
        ((uint8_t *)&t14)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t14 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t14), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l6) + 8), 88);
    void * t15 = gm_l0;
    gm_t3fcbuehh3bs2 t16;
    gm_f_kind_k239kpg((void *)&t16, t15);
    gm_tt7vn9ehj0acr t17 = gm_f_shift_op_sro3o9l(&t16);
    gm_l7 = t17;
    uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l7))[2] != 0);
    if (t18) {
        gm_ts6oqppjh0ba3 t19 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l7));
        gm_l8 = t19;
    } else {
        gm_tc3oht4ksth9d t20 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
        gm_to1cssm8dfiv5 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t21 + 8), t20);
        memmove(gm_out, ((uint8_t *)&t21), 96);
        goto gm_fdone;
    }
    gm_ts6oqppjh0ba3 t22 = gm_l8;
    gm_l3 = t22;
    void * t23 = gm_l0;
    gm_tm4pc0emhrv1g t24;
    gm_f_bump_q2i40re((void *)&t24, t23);
    void * t25 = gm_l0;
    gm_f_parse_add_dh9nvbt((void *)&gm_l9, (void *)NULL, cx, NULL, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t26) {
    } else {
        gm_to1cssm8dfiv5 t27;
        memset(&t27, 0, sizeof t27);
        ((uint8_t *)&t27)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t27), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l9) + 8), 88);
    void * t28 = (void *)((uint8_t *)&F.l2);
    uint8_t t29 = gm_f_is_arith_l6b4tpf(t28);
    if (t29) {
        void * t30 = gm_l0;
        gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s4puodol85qqo));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(54));
        gm_b0_1 t33 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t30, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t34 = (void *)((uint8_t *)&F.l4);
        uint8_t t35 = gm_f_is_arith_l6b4tpf(t34);
        if (t35) {
            void * t36 = gm_l0;
            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s4puodol85qqo));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(54));
            gm_b0_1 t39 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t36, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    void * t40 = gm_l0;
    gm_ts6oqppjh0ba3 t41 = gm_l3;
    gm_tc3oht4ksth9d t42 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_tc3oht4ksth9d t43 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l4));
    gm_f_binary_41fr5r5((void *)&F.l5, (void *)NULL, cx, NULL, t40, t41, &t42, &t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t44 = gm_l0;
    gm_t3fcbuehh3bs2 t45;
    gm_f_kind_k239kpg((void *)&t45, t44);
    gm_tt7vn9ehj0acr t46 = gm_f_shift_op_sro3o9l(&t45);
    gm_l11 = t46;
    {
        {
            uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l11))[2] != 0);
            if (t47) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 346, 8);
    }
gm_lb1:;
    uint8_t t48 = gm_l10;
    if (t48) {
        void * t49 = gm_l0;
        void * t50 = gm_l0;
        gm_tl874t2atnlh3 t51 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t50));
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_ssf8mqtfpipac));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(44));
        gm_b0_1 t53 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t49, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            for (;;) {
                void * t54 = gm_l0;
                gm_t3fcbuehh3bs2 t55;
                gm_f_kind_k239kpg((void *)&t55, t54);
                gm_tt7vn9ehj0acr t56 = gm_f_shift_op_sro3o9l(&t55);
                gm_l13 = t56;
                {
                    {
                        uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l13))[2] != 0);
                        if (t57) {
                            goto gm_lb8;
                        }
                        gm_l12 = ((uint8_t)0);
                        goto gm_lb7;
                    }
gm_lb8:;
                    {
                        gm_l12 = ((uint8_t)1);
                        goto gm_lb7;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 348, 15);
                }
gm_lb7:;
                uint8_t t58 = gm_l12;
                if (!t58) {
                    goto gm_lb4;
                }
                {
                    void * t59 = gm_l0;
                    gm_tm4pc0emhrv1g t60;
                    gm_f_bump_q2i40re((void *)&t60, t59);
                    void * t61 = gm_l0;
                    gm_f_parse_add_dh9nvbt((void *)&gm_l14, (void *)NULL, cx, NULL, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (t62) {
                    } else {
                        gm_to1cssm8dfiv5 t63;
                        memset(&t63, 0, sizeof t63);
                        ((uint8_t *)&t63)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t63 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t63), 96);
                        goto gm_fdone;
                    }
                }
gm_lb6:;
            }
        }
gm_lb4:;
    }
    gm_tc3oht4ksth9d t64 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l5));
    gm_to1cssm8dfiv5 t65;
    GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
    ((uint8_t *)&t65)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t65 + 8), t64);
    memmove(gm_out, ((uint8_t *)&t65), 96);
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shift_op_sro3o9l F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr gm_l1;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)8ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)5ULL));
            if (!t12) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            gm_tt7vn9ehj0acr t15;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[2] = 1;
            gm_l1 = t15;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)8ULL));
            if (!t17) {
                goto gm_lb3;
            }
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)13ULL));
            if (!t19) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)1ULL));
            gm_ts6oqppjh0ba3 t21;
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 1), t20);
            gm_tt7vn9ehj0acr t22;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[2] = 1;
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)8ULL));
            if (!t24) {
                goto gm_lb4;
            }
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)17ULL));
            if (!t26) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)2ULL));
            gm_ts6oqppjh0ba3 t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 1), t27);
            gm_tt7vn9ehj0acr t29;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[2] = 1;
            gm_l1 = t29;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)8ULL));
            if (!t31) {
                goto gm_lb5;
            }
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)21ULL));
            if (!t33) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t35;
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 1), t34);
            gm_tt7vn9ehj0acr t36;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t36), t35);
            ((uint8_t *)&t36)[2] = 1;
            gm_l1 = t36;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)8ULL));
            if (!t38) {
                goto gm_lb6;
            }
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)6ULL));
            if (!t40) {
                goto gm_lb6;
            }
            gm_ts6oqppjh0ba3 t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)6ULL));
            gm_tt7vn9ehj0acr t42;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42), t41);
            ((uint8_t *)&t42)[2] = 1;
            gm_l1 = t42;
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_tt7vn9ehj0acr t43;
            memset(&t43, 0, sizeof t43);
            gm_l1 = t43;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 52, 54);
    }
gm_lb1:;
    gm_tt7vn9ehj0acr t44 = gm_l1;
    F.ret = t44;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l_c(gm_t3fcbuehh3bs2 a0) { return gm_f_shift_op_sro3o9l(&a0); }
