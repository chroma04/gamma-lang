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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; void * f1; gm_t88rcucg1b7uk f2; uint8_t pe[4]; } gm_tkmj998anon0r;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkmj998anon0r) == 24 && _Alignof(gm_tkmj998anon0r) == 8 && offsetof(gm_tkmj998anon0r, f0) == 0 && offsetof(gm_tkmj998anon0r, f1) == 8 && offsetof(gm_tkmj998anon0r, f2) == 16, "layout of `PendingConst`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_calls_on_value_q0pa6h5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_default_4bf68q8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_const_array_kn5cgp9 {
    gm_frame h;
    gm_tc3oht4ksth9d l11; /* x */
    gm_t7te2tdpebbt4 l14; /* arg */
    gm_tc3oht4ksth9d l15; /* x */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_check_d48mnu7 {
    gm_frame h;
    gm_tckdobunaulee l14; /* v */
    gm_tckdobunaulee l28; /* v */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_construction_61mg03r {
    gm_frame h;
    gm_tpautr4br7h3l l9; /* def */
    gm_trqfvmjtt4sij l10; /* given */
    gm_t7te2tdpebbt4 l15; /* arg */
    gm_tc3oht4ksth9d l17; /* x */
    gm_t0mglksrkfa5e l22; /* f */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_type_7rijs95 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_construction_type_pk33veh {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_empty_slice_g6fvoq5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_eq_ceufbo4 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_expect_bool_edv2577 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_expect_type_uld3786 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_field_visible_7kr8oue {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_float_finite_ak3diof {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* q */
    uint8_t ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_path_qa34i5j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_push_7jrb8if {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ohmrmof {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_suffix_type_icfvn23 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_to_float_o1iop8t {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_untyped_fits_tp6gphv {
    gm_frame h;
    gm_t5vbkdq81qfs7 l6; /* r */
    gm_t0ialelq3ql1k l7; /* v */
    gm_tbci5ospk5nnn l8; /* i */
    gm_tngtuiuskn938 l10; /* q */
    gm_tngtuiuskn938 l11; /* q */
    uint8_t ret;
};
struct gm_fr_untyped_if_plain_lbrp24p {
    gm_frame h;
    gm_tkv1vap90vht2 l5; /* r */
    gm_tkv1vap90vht2 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq09el5ous7u(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0uoi35f1be(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2dij7vp475(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq4eilu97soo(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqeggohi50ng(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjhti3ghd62(const uint8_t *a, const uint8_t *b);
inline bool gm_eqlolcr6ikvg(const uint8_t *a, const uint8_t *b);
inline bool gm_eqm4u0k078hs(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq98vpo7v5c(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrm9iu7q40e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt4v14i9rtv(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_default_4bf68q8(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_array_kn5cgp9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_construction_61mg03r(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3, gm_tkn42u7na3t81 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_type_pk33veh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_bool_edv2577(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_type_uld3786(void * a0, gm_csguankko4lmhd a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_field_visible_7kr8oue(void * a0, uint64_t a1, uint32_t a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_float_finite_ak3diof(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_from_f64_1parp9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7jrb8if(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ohmrmof(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_suffix_type_icfvn23(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tj2m9dbh1ekv6 a1);
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_untyped_fits_tp6gphv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern const uint8_t gm_s14frn0dagfs0[25];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sna4vk4o059d0[10];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src2eo2k3vc53po[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq09el5ous7u(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqeggohi50ng(a + 0, b + 0)) return false;
    if (!gm_eqeggohi50ng(a + 48, b + 48)) return false;
    return true;
}
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0uoi35f1be(const uint8_t *a, const uint8_t *b) {
    if (a[208] != b[208]) return false;
    return !a[208] || gm_eqm4u0k078hs(a, b);
}
inline bool gm_eq2dij7vp475(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq4eilu97soo(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqlolcr6ikvg(a + 0, b + 0)) return false;
    if (!gm_eq2dij7vp475(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqeggohi50ng(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eqq98vpo7v5c(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqjhti3ghd62(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq09el5ous7u(a + 0, b + 0)) return false;
    if (!gm_eq09el5ous7u(a + 96, b + 96)) return false;
    return true;
}
inline bool gm_eqlolcr6ikvg(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqm4u0k078hs(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqt4v14i9rtv(a + 8, b + 8);
    return true;
}
inline bool gm_eqq98vpo7v5c(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqrm9iu7q40e(a + 0, b + 0)) return false;
    if (!gm_eq6groq37ram(a + 16, b + 16)) return false;
    if (!gm_eq4eilu97soo(a + 24, b + 24)) return false;
    return true;
}
inline bool gm_eqrm9iu7q40e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 4) == 0;
}
inline bool gm_eqt4v14i9rtv(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqeggohi50ng(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eq09el5ous7u(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eq929lennfd3(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqjhti3ghd62(a + 8, b + 8);
    return true;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)4ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_calls_on_value_q0pa6h5 F;
    void * gm_l0; /* callee */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    void * t6 = gm_l0;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)22ULL));
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l1 = t9;
            void * t10 = gm_l1;
            uint8_t t11 = gm_f_is_path_qa34i5j(t10);
            uint8_t t12 = (uint8_t)!t11;
            gm_l2 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)20ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 270, 58);
    }
gm_lb1:;
    uint8_t t15 = gm_l2;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g6fvoq5 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_ceufbo4 F;
    GM_STACK_CHECK_SHALLOW(&F, 6u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
    if (t2) {
        uint8_t t3 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
        gm_l2 = t3;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t4 = gm_l2;
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_tl874t2atnlh3 t5 = gm_l1;
    gm_b16_8 t6 = gm_l2;
    gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_field_visible_7kr8oue(void * a0, uint64_t a1, uint32_t a2, void * a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_visible_7kr8oue F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* id */
    void * gm_l3; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = gm_l3;
    gm_t9qkh0rsooc8d t4 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t3) + 20));
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 1088));
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1088) + 8));
    uint32_t t8 = gm_l2;
    uint64_t t9 = (uint64_t)t8;
    if ((uintptr_t)t9 >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)t9, t7, gm_src2eo2k3vc53po, 216, 109);
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t6) + (uintptr_t)t9 * 184) + 16));
    uint64_t t11 = gm_l1;
    uint8_t t12 = gm_f_visible_ll07dr9(t2, t4, t10, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_e79cvtl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sig8ahbvm8eb8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpll8q455cp9mhi), t2);
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t3;
    gm_f_one_08qg1op((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t4;
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t4 + 48), t3);
    memmove(gm_out, ((uint8_t *)&t4), 96);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 40) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)1ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    uint16_t t4 = gm_l2;
    gm_tjcj4j2gbh6c8 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(uint16_t, ((uint8_t *)&t5 + 2), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)26ULL)));
    gm_t53at80pfdus3 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)26ULL));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_path_qa34i5j F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    void * t6 = gm_l0;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)6ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)22ULL));
            if (!t10) {
                goto gm_lb3;
            }
            void * t11 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l1 = t11;
            void * t12 = gm_l1;
            uint8_t t13 = gm_f_is_path_qa34i5j(t12);
            gm_l2 = t13;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 276, 46);
    }
gm_lb1:;
    uint8_t t14 = gm_l2;
    F.ret = t14;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7jrb8if(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7jrb8if F;
    void * gm_l0; /* l */
    gm_tkmj998anon0r gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_g6fvoq5();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ohmrmof(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)a1), 24);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
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
        gm_b16_8 t9 = gm_f_empty_slice_s1lbe5s();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hkmvuvc(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ohmrmof(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ohmrmof F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tbngattl1fo51 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t5 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t2, ((uint8_t)1), t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t5;
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
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1) {
    struct gm_fr_to_float_o1iop8t F;
    uint16_t gm_l1; /* bits */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint16_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint16_t)16ULL)));
    if (t3) {
        double t4 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t4;
        goto gm_fret;
    }
    uint16_t t5 = gm_l1;
    uint8_t t6 = ((t5 == ((uint16_t)32ULL)));
    if (t6) {
        float t7 = gm_f_to_f32_8bnhsji((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        double t8 = (double)t7;
        F.ret = t8;
        goto gm_fret;
    }
    double t9 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t9;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t2, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)gm_c3t36dlh4g5));
    gm_b16_8 t5 = gm_l2;
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_f_format_e79cvtl((void *)&gm_l3, (void *)NULL, cx, NULL, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srclv0dcbbfhves, 643, 19);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_default_4bf68q8(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_check_default_4bf68q8 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* hook */
    GM_STACK_CHECK(&F, 314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l5;
    uint8_t gm_l6;
    gm_tkn42u7na3t81 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l3;
    uint8_t t10 = gm_f_is_error_8k27gd1(t8, GM_CONV(gm_csguankko4lmhd, t9));
    if (t10) {
        gm_l5 = ((uint8_t)1);
    } else {
        void * t11 = gm_l0;
        uint64_t t12 = gm_l1;
        void * t13 = gm_l2;
        gm_t88rcucg1b7uk t14 = gm_l3;
        gm_tkn42u7na3t81 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[4] = 1;
        gm_tkn42u7na3t81 t16 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t11, t12, t13, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t16;
        {
            {
                uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
                if (t17) {
                    goto gm_lb2;
                }
                gm_l6 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l6 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 30, 30);
        }
gm_lb1:;
        uint8_t t18 = gm_l6;
        gm_l5 = t18;
    }
    uint8_t t19 = gm_l5;
    if (t19) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t20 = gm_l0;
            uint8_t t21 = (uint8_t)(GM_LD(void *, (((uint8_t *)t20) + 2288)) != 0);
            if (!t21) {
                goto gm_lb5;
            }
            void * t22 = GM_LD(void *, (((uint8_t *)t20) + 2288));
            gm_l4 = t22;
            void * t23 = gm_l0;
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l3;
            uint8_t t26 = gm_f_has_param_u9t0efs(t24, GM_CONV(gm_csguankko4lmhd, t25));
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) {
                goto gm_lb5;
            }
            void * t28 = gm_l4;
            void * t29 = gm_l0;
            uint64_t t30 = gm_l1;
            void * t31 = gm_l2;
            gm_t88rcucg1b7uk t32 = gm_l3;
            gm_tkn42u7na3t81 t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33), t32);
            ((uint8_t *)&t33)[4] = 1;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sna4vk4o059d0));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(9));
            void * t35 = gm_l2;
            gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t35) + 72));
            void * t37 = t28;
            gm_t9805qtonbsag t38;
            if (GM_PTR_TAGGED(t37)) {
                t38 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t37))(t29, t30, t31, t33, t34, GM_CONV(gm_cseahvg8v7p0vs, t36));
            } else {
                t38 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t37)(NULL, cx, NULL, t29, t30, t31, t33, t34, t36);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb5:;
    }
gm_lb4:;
    void * t39 = gm_l0;
    void * t40 = (void *)(((uint8_t *)t39) + 464);
    uint64_t t41 = gm_l1;
    void * t42 = gm_l2;
    gm_t88rcucg1b7uk t43 = gm_l3;
    gm_tkmj998anon0r t44;
    GM_ST(uint32_t, (uint8_t *)&t44 + 20, 0);
    GM_ST(uint64_t, ((uint8_t *)&t44 + 0), t41);
    GM_ST(void *, ((uint8_t *)&t44 + 8), t42);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44 + 16), t43);
    gm_tftb1odc739t5 t45 = gm_f_push_7jrb8if(t40, &t44);
    gm_l8 = t45;
    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src2eo2k3vc53po, 37, 5);
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_array_kn5cgp9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_const_array_kn5cgp9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    void * gm_l4; /* call */
    gm_b16_8 gm_l5; /* args */
    gm_t88rcucg1b7uk gm_l6; /* t */
    uint64_t gm_l7; /* len */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_tnnjeerolos0a gm_l9; /* kind */
    gm_t08olq97rscn3 gm_l10; /* n */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 804u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l16;
    void * gm_l17;
    gm_tkn42u7na3t81 gm_l18;
    gm_t88rcucg1b7uk gm_l19;
    gm_t53at80pfdus3 gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    gm_tkn42u7na3t81 gm_l23;
    uint8_t gm_l24;
    gm_tkn42u7na3t81 gm_l25;
    void * t21 = gm_l2;
    void * t22 = gm_f_construction_call_ktqtrpo(t21);
    gm_l16 = t22;
    uint8_t t23 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l16)) != 0);
    if (t23) {
        void * t24 = GM_LD(void *, ((uint8_t *)&gm_l16));
        gm_l17 = t24;
    } else {
        gm_tkn42u7na3t81 t25;
        memset(&t25, 0, sizeof t25);
        F.ret = t25;
        goto gm_fret;
    }
    void * t26 = gm_l17;
    gm_l4 = t26;
    {
        {
            void * t27 = gm_l4;
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)19ULL));
            if (!t29) {
                goto gm_lb2;
            }
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((((uint8_t *)t27) + 8) + 8) + 8));
            gm_l5 = t30;
            void * t31 = gm_l0;
            uint64_t t32 = gm_l1;
            void * t33 = gm_l2;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            gm_tkn42u7na3t81 t35 = gm_f_construction_type_pk33veh((void *)NULL, cx, NULL, t31, t32, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t35;
            uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l18))[4] != 0);
            if (t36) {
                gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l18));
                gm_l19 = t37;
            } else {
                gm_tkn42u7na3t81 t38;
                memset(&t38, 0, sizeof t38);
                F.ret = t38;
                goto gm_fret;
            }
            gm_t88rcucg1b7uk t39 = gm_l19;
            gm_l6 = t39;
            {
                {
                    void * t40 = gm_l0;
                    void * t41 = (void *)(((uint8_t *)t40) + 8);
                    gm_t88rcucg1b7uk t42 = gm_l6;
                    gm_f_kind_jcnt4hm((void *)&gm_l20, t41, GM_CONV(gm_csguankko4lmhd, t42));
                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)11ULL));
                    if (!t44) {
                        goto gm_lb4;
                    }
                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                    gm_l7 = t45;
                    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l20) + 8) + 8));
                    gm_l8 = t46;
                    gm_tnnjeerolos0a t47 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l20) + 8) + 12));
                    gm_l9 = t47;
                    gm_tnnjeerolos0a t48 = gm_l9;
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                    uint8_t t50 = (uint8_t)(!(t49 == ((uint8_t)0ULL)));
                    gm_tnnjeerolos0a t51;
                    GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
                    if (t50) {
                        gm_tkn42u7na3t81 t52;
                        memset(&t52, 0, sizeof t52);
                        F.ret = t52;
                        goto gm_fret;
                    }
                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t54 = ((t53 == ((uint64_t)1ULL)));
                    if (t54) {
                        {
                            {
                                void * t55 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t56, gm_src2eo2k3vc53po, 139, 28);
                                uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t55) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                uint8_t t58 = (uint8_t)(t57 == ((uint8_t)1ULL));
                                if (!t58) {
                                    goto gm_lb6;
                                }
                                memmove(((uint8_t *)&gm_l10), ((((uint8_t *)t55) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 32);
                                memmove(((uint8_t *)&F.l11), (((((uint8_t *)t55) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 32), 88);
                                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                                gm_b16_8 t60;
                                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_spevct0q3cjgb));
                                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(4));
                                uint8_t t61 = (!gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
                                if (t61) {
                                    gm_l21 = ((uint8_t)1);
                                } else {
                                    void * t62 = gm_l0;
                                    uint64_t t63 = gm_l1;
                                    void * t64 = (void *)((uint8_t *)&F.l11);
                                    gm_t88rcucg1b7uk t65 = gm_l8;
                                    gm_tkn42u7na3t81 t66;
                                    GM_ST(uint8_t, (uint8_t *)&t66 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t66 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t66), t65);
                                    ((uint8_t *)&t66)[4] = 1;
                                    gm_tkn42u7na3t81 t67 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t62, t63, t64, t66);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l23 = t67;
                                    {
                                        {
                                            uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l23))[4] != 0);
                                            if (t68) {
                                                goto gm_lb8;
                                            }
                                            gm_l22 = ((uint8_t)1);
                                            goto gm_lb7;
                                        }
gm_lb8:;
                                        {
                                            gm_l22 = ((uint8_t)0);
                                            goto gm_lb7;
                                        }
gm_lb9:;
                                        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 140, 40);
                                    }
gm_lb7:;
                                    uint8_t t69 = gm_l22;
                                    gm_l21 = t69;
                                }
                                uint8_t t70 = gm_l21;
                                if (t70) {
                                    gm_tkn42u7na3t81 t71;
                                    memset(&t71, 0, sizeof t71);
                                    F.ret = t71;
                                    goto gm_fret;
                                }
                                void * t72 = gm_l0;
                                gm_t88rcucg1b7uk t73 = gm_l6;
                                gm_tkn42u7na3t81 t74 = gm_l3;
                                gm_tkn42u7na3t81 t75 = gm_f_expect_type_uld3786(t72, GM_CONV(gm_csguankko4lmhd, t73), t74);
                                F.ret = t75;
                                goto gm_fret;
                            }
gm_lb6:;
                        }
gm_lb5:;
                    }
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t77 = (uint64_t)t76;
                    uint64_t t78 = gm_l7;
                    uint8_t t79 = (!(t77 == t78));
                    if (t79) {
                        gm_tkn42u7na3t81 t80;
                        memset(&t80, 0, sizeof t80);
                        F.ret = t80;
                        goto gm_fret;
                    }
                    gm_b16_8 t81 = gm_l5;
                    gm_l12 = t81;
                    gm_l13 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t82 = gm_l13;
                            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint8_t t84 = (t82 < t83);
                            if (!t84) {
                                goto gm_lb10;
                            }
                            {
                                void * t85 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t87 = gm_l13;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t85) + (uintptr_t)t87 * 144), 144);
                                        {
                                            {
                                                uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                                uint8_t t89 = (uint8_t)(t88 == ((uint8_t)0ULL));
                                                if (!t89) {
                                                    goto gm_lb16;
                                                }
                                                memmove(((uint8_t *)&F.l15), (((uint8_t *)&F.l14) + 8), 88);
                                                void * t90 = gm_l0;
                                                uint64_t t91 = gm_l1;
                                                void * t92 = (void *)((uint8_t *)&F.l15);
                                                gm_t88rcucg1b7uk t93 = gm_l8;
                                                gm_tkn42u7na3t81 t94;
                                                GM_ST(uint8_t, (uint8_t *)&t94 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t94 + 6, 0);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94), t93);
                                                ((uint8_t *)&t94)[4] = 1;
                                                gm_tkn42u7na3t81 t95 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t90, t91, t92, t94);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l25 = t95;
                                                {
                                                    {
                                                        uint8_t t96 = (uint8_t)((((uint8_t *)&gm_l25))[4] != 0);
                                                        if (t96) {
                                                            goto gm_lb18;
                                                        }
                                                        gm_l24 = ((uint8_t)1);
                                                        goto gm_lb17;
                                                    }
gm_lb18:;
                                                    {
                                                        gm_l24 = ((uint8_t)0);
                                                        goto gm_lb17;
                                                    }
gm_lb19:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 147, 12);
                                                }
gm_lb17:;
                                                uint8_t t97 = gm_l24;
                                                if (t97) {
                                                    gm_tkn42u7na3t81 t98;
                                                    memset(&t98, 0, sizeof t98);
                                                    F.ret = t98;
                                                    goto gm_fret;
                                                }
                                                goto gm_lb15;
                                            }
gm_lb16:;
                                            gm_tkn42u7na3t81 t99;
                                            memset(&t99, 0, sizeof t99);
                                            F.ret = t99;
                                            goto gm_fret;
                                        }
gm_lb15:;
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 145, 5);
                                }
gm_lb13:;
                            }
gm_lb12:;
                            uint64_t t100 = gm_l13;
                            uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                            uint64_t t103 = r101;
                            gm_l13 = t103;
                        }
                    }
gm_lb10:;
                    void * t104 = gm_l0;
                    gm_t88rcucg1b7uk t105 = gm_l6;
                    gm_tkn42u7na3t81 t106 = gm_l3;
                    gm_tkn42u7na3t81 t107 = gm_f_expect_type_uld3786(t104, GM_CONV(gm_csguankko4lmhd, t105), t106);
                    F.ret = t107;
                    goto gm_fret;
                }
gm_lb4:;
                gm_tkn42u7na3t81 t108;
                memset(&t108, 0, sizeof t108);
                F.ret = t108;
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_tkn42u7na3t81 t109;
        memset(&t109, 0, sizeof t109);
        F.ret = t109;
        goto gm_fret;
    }
gm_lb1:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_const_check_d48mnu7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    void * gm_l4; /* inner */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_tibupt4dmcqai gm_l6; /* lit */
    gm_t08olq97rscn3 gm_l7; /* id */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* target_ */
    gm_t88rcucg1b7uk gm_l10; /* inner */
    gm_tep2drsl9408b gm_l11; /* u */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l15; /* lit */
    gm_tj2m9dbh1ekv6 gm_l16; /* suffix */
    gm_t88rcucg1b7uk gm_l17; /* t */
    void * gm_l18; /* lit */
    gm_t9qkh0rsooc8d gm_l19; /* suffix */
    gm_t88rcucg1b7uk gm_l20; /* t */
    void * gm_l21; /* base */
    gm_t08olq97rscn3 gm_l22; /* name */
    uint64_t gm_l23; /* errors */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_tep2drsl9408b gm_l25; /* u */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t88rcucg1b7uk gm_l29; /* x */
    void * gm_l30; /* callee */
    gm_b16_8 gm_l31; /* args */
    gm_t88rcucg1b7uk gm_l32; /* t */
    GM_STACK_CHECK(&F, 2193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t88rcucg1b7uk gm_l33;
    gm_tkn42u7na3t81 gm_l34;
    uint8_t gm_l35;
    gm_t53at80pfdus3 gm_l36;
    gm_t88rcucg1b7uk gm_l37;
    gm_t53at80pfdus3 gm_l38;
    gm_tdlpv0q69lov1 gm_l39;
    gm_tep2drsl9408b gm_l40;
    gm_tkn42u7na3t81 gm_l41;
    uint8_t gm_l42;
    uint16_t gm_l43;
    uint8_t gm_l44;
    gm_tf5srh4mp4b3i gm_l45;
    uint8_t gm_l46;
    gm_tdlpv0q69lov1 gm_l47;
    gm_tep2drsl9408b gm_l48;
    uint8_t gm_l49;
    gm_t88rcucg1b7uk gm_l50;
    gm_tkn42u7na3t81 gm_l51;
    uint8_t gm_l52;
    gm_t88rcucg1b7uk gm_l53;
    uint8_t gm_l54;
    uint8_t gm_l55;
    gm_t9805qtonbsag gm_l56;
    gm_tkn42u7na3t81 gm_l57;
    {
        void * t57 = gm_l2;
        {
            {
                {
                    uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)9ULL));
                    if (!t59) {
                        goto gm_lb2;
                    }
                    void * t60 = GM_LD(void *, ((((uint8_t *)t57) + 8) + 8));
                    gm_l4 = t60;
                    void * t61 = gm_l0;
                    uint64_t t62 = gm_l1;
                    void * t63 = gm_l4;
                    gm_tkn42u7na3t81 t64 = gm_l3;
                    gm_tkn42u7na3t81 t65 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t61, t62, t63, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t65;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)4ULL));
                    if (!t67) {
                        goto gm_lb3;
                    }
                    uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                    if (t68) {
                        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
                        gm_l33 = t69;
                    } else {
                        gm_tkn42u7na3t81 t70;
                        memset(&t70, 0, sizeof t70);
                        F.ret = t70;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t71 = gm_l33;
                    gm_l5 = t71;
                    void * t72 = gm_l0;
                    void * t73 = (void *)(((uint8_t *)t72) + 8);
                    gm_t88rcucg1b7uk t74 = gm_l5;
                    gm_f_kind_jcnt4hm((void *)&gm_l36, t73, GM_CONV(gm_csguankko4lmhd, t74));
                    {
                        {
                            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 0));
                            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)13ULL));
                            if (!t76) {
                                goto gm_lb5;
                            }
                            gm_l35 = ((uint8_t)1);
                            goto gm_lb4;
                        }
gm_lb5:;
                        {
                            gm_l35 = ((uint8_t)0);
                            goto gm_lb4;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 47, 23);
                    }
gm_lb4:;
                    uint8_t t77 = gm_l35;
                    if (t77) {
                        gm_t88rcucg1b7uk t78 = gm_l5;
                        gm_tkn42u7na3t81 t79;
                        GM_ST(uint8_t, (uint8_t *)&t79 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t79 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t79), t78);
                        ((uint8_t *)&t79)[4] = 1;
                        gm_l34 = t79;
                    } else {
                        gm_tkn42u7na3t81 t80;
                        memset(&t80, 0, sizeof t80);
                        gm_l34 = t80;
                    }
                    gm_tkn42u7na3t81 t81 = gm_l34;
                    F.ret = t81;
                    goto gm_fret;
                }
gm_lb3:;
                {
                    uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)3ULL));
                    if (!t83) {
                        goto gm_lb7;
                    }
                    void * t84 = gm_l0;
                    gm_tkn42u7na3t81 t85 = gm_l3;
                    gm_tkn42u7na3t81 t86 = gm_f_expect_bool_edv2577((void *)NULL, cx, NULL, t84, t85);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t86;
                    goto gm_fret;
                }
gm_lb7:;
                {
                    uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)2ULL));
                    if (!t88) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&gm_l6), ((((uint8_t *)t57) + 8) + 8), 24);
                    gm_t9qkh0rsooc8d t89 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 16));
                    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t89) + 0));
                    uint8_t t91 = (uint8_t)(!(t90 == ((uint8_t)0ULL)));
                    gm_t9qkh0rsooc8d t92;
                    GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
                    if (t91) {
                        gm_tkn42u7na3t81 t93;
                        memset(&t93, 0, sizeof t93);
                        F.ret = t93;
                        goto gm_fret;
                    }
                    void * t94 = gm_l0;
                    void * t95 = gm_l0;
                    void * t96 = (void *)(((uint8_t *)t95) + 8);
                    gm_t88rcucg1b7uk t97 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t96);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tkn42u7na3t81 t98 = gm_l3;
                    gm_tkn42u7na3t81 t99 = gm_f_expect_type_uld3786(t94, GM_CONV(gm_csguankko4lmhd, t97), t98);
                    F.ret = t99;
                    goto gm_fret;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t101 = (uint8_t)(t100 == ((uint8_t)8ULL));
                    if (!t101) {
                        goto gm_lb9;
                    }
                    memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t57) + 8) + 8), 32);
                    uint8_t t102 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                    if (t102) {
                        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
                        gm_l37 = t103;
                    } else {
                        gm_tkn42u7na3t81 t104;
                        memset(&t104, 0, sizeof t104);
                        F.ret = t104;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t105 = gm_l37;
                    gm_l8 = t105;
                    gm_t88rcucg1b7uk t106 = gm_l8;
                    gm_l9 = t106;
                    {
                        {
                            void * t107 = gm_l0;
                            void * t108 = (void *)(((uint8_t *)t107) + 8);
                            gm_t88rcucg1b7uk t109 = gm_l8;
                            gm_f_kind_jcnt4hm((void *)&gm_l38, t108, GM_CONV(gm_csguankko4lmhd, t109));
                            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)13ULL));
                            if (!t111) {
                                goto gm_lb11;
                            }
                            gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l38) + 8));
                            gm_l10 = t112;
                            gm_t88rcucg1b7uk t113 = gm_l10;
                            gm_l9 = t113;
                            goto gm_lb10;
                        }
gm_lb11:;
                    }
gm_lb10:;
                    void * t114 = gm_l0;
                    gm_t88rcucg1b7uk t115 = gm_l9;
                    gm_f_union_variants_a7khaqp((void *)&gm_l39, t114, GM_CONV(gm_csguankko4lmhd, t115));
                    uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l39))[24] != 0);
                    if (t116) {
                        memmove(((uint8_t *)&gm_l40), ((uint8_t *)&gm_l39), 24);
                    } else {
                        gm_tkn42u7na3t81 t117;
                        memset(&t117, 0, sizeof t117);
                        F.ret = t117;
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l40), 24);
                    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                    gm_l12 = t118;
                    gm_l13 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t119 = gm_l13;
                            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint8_t t121 = (t119 < t120);
                            if (!t121) {
                                goto gm_lb12;
                            }
                            {
                                void * t122 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t124 = gm_l13;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t122) + (uintptr_t)t124 * 72), 72);
                                        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                        uint8_t t127 = (gm_eq0iqiubfm7e(((uint8_t *)&t125), ((uint8_t *)&t126)));
                                        if (t127) {
                                            {
                                                {
                                                    uint8_t t128 = (uint8_t)(((((uint8_t *)&F.l14) + 16))[4] != 0);
                                                    if (t128) {
                                                        goto gm_lb18;
                                                    }
                                                    gm_l42 = ((uint8_t)1);
                                                    goto gm_lb17;
                                                }
gm_lb18:;
                                                {
                                                    gm_l42 = ((uint8_t)0);
                                                    goto gm_lb17;
                                                }
gm_lb19:;
                                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 60, 50);
                                            }
gm_lb17:;
                                            uint8_t t129 = gm_l42;
                                            if (t129) {
                                                gm_t88rcucg1b7uk t130 = gm_l8;
                                                gm_tkn42u7na3t81 t131;
                                                GM_ST(uint8_t, (uint8_t *)&t131 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t131 + 6, 0);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t131), t130);
                                                ((uint8_t *)&t131)[4] = 1;
                                                gm_l41 = t131;
                                            } else {
                                                gm_tkn42u7na3t81 t132;
                                                memset(&t132, 0, sizeof t132);
                                                gm_l41 = t132;
                                            }
                                            gm_tkn42u7na3t81 t133 = gm_l41;
                                            F.ret = t133;
                                            goto gm_fret;
                                        }
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 59, 13);
                                }
gm_lb15:;
                            }
gm_lb14:;
                            uint64_t t134 = gm_l13;
                            uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                            uint64_t t137 = r135;
                            gm_l13 = t137;
                        }
                    }
gm_lb12:;
                    gm_tkn42u7na3t81 t138;
                    memset(&t138, 0, sizeof t138);
                    F.ret = t138;
                    goto gm_fret;
                }
gm_lb9:;
            }
            {
                {
                    uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t140 = (uint8_t)(t139 == ((uint8_t)0ULL));
                    if (!t140) {
                        goto gm_lb20;
                    }
                    void * t141 = GM_LD(void *, ((((uint8_t *)t57) + 8) + 8));
                    gm_l15 = t141;
                    {
                        {
                            void * t142 = gm_l15;
                            uint8_t t143 = (uint8_t)(((((uint8_t *)t142) + 48))[8] != 0);
                            if (!t143) {
                                goto gm_lb22;
                            }
                            gm_tj2m9dbh1ekv6 t144 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)t142) + 48));
                            gm_l16 = t144;
                            void * t145 = gm_l0;
                            gm_tj2m9dbh1ekv6 t146 = gm_l16;
                            gm_t88rcucg1b7uk t147 = gm_f_suffix_type_icfvn23((void *)NULL, cx, NULL, t145, t146);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l17 = t147;
                            void * t148 = gm_l0;
                            void * t149 = gm_l15;
                            gm_t88rcucg1b7uk t150 = gm_l17;
                            uint8_t t151 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t148, &(*(gm_tbci5ospk5nnn *)((uint8_t *)t149)), t150);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t152 = (uint8_t)!t151;
                            if (t152) {
                                gm_tkn42u7na3t81 t153;
                                memset(&t153, 0, sizeof t153);
                                F.ret = t153;
                                goto gm_fret;
                            }
                            void * t154 = gm_l0;
                            gm_t88rcucg1b7uk t155 = gm_l17;
                            gm_tkn42u7na3t81 t156 = gm_l3;
                            gm_tkn42u7na3t81 t157 = gm_f_expect_type_uld3786(t154, GM_CONV(gm_csguankko4lmhd, t155), t156);
                            F.ret = t157;
                            goto gm_fret;
                        }
gm_lb22:;
                    }
gm_lb21:;
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t159 = (uint8_t)(t158 == ((uint8_t)1ULL));
                    if (!t159) {
                        goto gm_lb23;
                    }
                    void * t160 = GM_LD(void *, ((((uint8_t *)t57) + 8) + 8));
                    gm_l18 = t160;
                    {
                        {
                            void * t161 = gm_l18;
                            uint8_t t162 = (uint8_t)(((((uint8_t *)t161) + 96))[1] != 0);
                            if (!t162) {
                                goto gm_lb25;
                            }
                            gm_t9qkh0rsooc8d t163 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t161) + 96));
                            gm_l19 = t163;
                            void * t164 = gm_l18;
                            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)t164) + 98));
                            if (t165) {
                                gm_tkn42u7na3t81 t166;
                                memset(&t166, 0, sizeof t166);
                                F.ret = t166;
                                goto gm_fret;
                            }
                            void * t167 = gm_l0;
                            void * t168 = (void *)(((uint8_t *)t167) + 8);
                            {
                                {
                                    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                                    uint8_t t170 = (uint8_t)(t169 == ((uint8_t)0ULL));
                                    if (!t170) {
                                        goto gm_lb27;
                                    }
                                    gm_l43 = ((uint16_t)16ULL);
                                    goto gm_lb26;
                                }
gm_lb27:;
                                {
                                    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                                    uint8_t t172 = (uint8_t)(t171 == ((uint8_t)1ULL));
                                    if (!t172) {
                                        goto gm_lb28;
                                    }
                                    gm_l43 = ((uint16_t)32ULL);
                                    goto gm_lb26;
                                }
gm_lb28:;
                                {
                                    uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                                    uint8_t t174 = (uint8_t)(t173 == ((uint8_t)2ULL));
                                    if (!t174) {
                                        goto gm_lb29;
                                    }
                                    gm_l43 = ((uint16_t)64ULL);
                                    goto gm_lb26;
                                }
gm_lb29:;
                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 74, 42);
                            }
gm_lb26:;
                            uint16_t t175 = gm_l43;
                            gm_t53at80pfdus3 t176;
                            memset(&t176, 0, sizeof t176);
                            GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)3ULL));
                            GM_ST(uint16_t, ((uint8_t *)&t176 + 8), t175);
                            gm_t88rcucg1b7uk t177 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t168, &t176);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l20 = t177;
                            void * t178 = gm_l0;
                            void * t179 = gm_l18;
                            gm_t88rcucg1b7uk t180 = gm_l20;
                            uint8_t t181 = gm_f_float_finite_ak3diof((void *)NULL, cx, NULL, t178, &(*(gm_tngtuiuskn938 *)((uint8_t *)t179)), t180);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t182 = (uint8_t)!t181;
                            if (t182) {
                                gm_tkn42u7na3t81 t183;
                                memset(&t183, 0, sizeof t183);
                                F.ret = t183;
                                goto gm_fret;
                            }
                            void * t184 = gm_l0;
                            gm_t88rcucg1b7uk t185 = gm_l20;
                            gm_tkn42u7na3t81 t186 = gm_l3;
                            gm_tkn42u7na3t81 t187 = gm_f_expect_type_uld3786(t184, GM_CONV(gm_csguankko4lmhd, t185), t186);
                            F.ret = t187;
                            goto gm_fret;
                        }
gm_lb25:;
                    }
gm_lb24:;
                    goto gm_lb1;
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t189 = (uint8_t)(t188 == ((uint8_t)22ULL));
                    if (!t189) {
                        goto gm_lb30;
                    }
                    void * t190 = GM_LD(void *, ((((uint8_t *)t57) + 8) + 8));
                    gm_l21 = t190;
                    memmove(((uint8_t *)&gm_l22), (((((uint8_t *)t57) + 8) + 8) + 8), 32);
                    {
                        {
                            void * t191 = gm_l0;
                            uint64_t t192 = gm_l1;
                            void * t193 = gm_l21;
                            gm_tf5srh4mp4b3i t194 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t191, t192, t193);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l45 = t194;
                            {
                                {
                                    uint8_t t195 = (uint8_t)((((uint8_t *)&gm_l45))[8] != 0);
                                    if (t195) {
                                        goto gm_lb34;
                                    }
                                    gm_l44 = ((uint8_t)1);
                                    goto gm_lb33;
                                }
gm_lb34:;
                                {
                                    gm_l44 = ((uint8_t)0);
                                    goto gm_lb33;
                                }
gm_lb35:;
                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 85, 16);
                            }
gm_lb33:;
                            uint8_t t196 = gm_l44;
                            if (!t196) {
                                goto gm_lb32;
                            }
                            void * t197 = gm_l0;
                            uint64_t t198 = gm_l1;
                            void * t199 = gm_l21;
                            uint8_t t200 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t197, t198, t199, ((uint32_t)0ULL));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (!t200) {
                                goto gm_lb32;
                            }
                            void * t201 = gm_l0;
                            void * t202 = GM_LD(void *, ((uint8_t *)t201));
                            void * t203 = GM_LD(void *, (((uint8_t *)t202) + 8));
                            uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)t203) + 8));
                            gm_l23 = t204;
                            void * t205 = gm_l0;
                            uint64_t t206 = gm_l1;
                            void * t207 = gm_l21;
                            gm_t88rcucg1b7uk t208 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t205, t206, t207);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l24 = t208;
                            void * t209 = gm_l0;
                            void * t210 = GM_LD(void *, ((uint8_t *)t209));
                            void * t211 = GM_LD(void *, (((uint8_t *)t210) + 8));
                            uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)t211) + 8));
                            uint64_t t213 = gm_l23;
                            uint8_t t214 = (!(t212 == t213));
                            if (t214) {
                                gm_l46 = ((uint8_t)1);
                            } else {
                                void * t215 = gm_l0;
                                void * t216 = (void *)(((uint8_t *)t215) + 8);
                                gm_t88rcucg1b7uk t217 = gm_l24;
                                uint8_t t218 = gm_f_is_error_8k27gd1(t216, GM_CONV(gm_csguankko4lmhd, t217));
                                gm_l46 = t218;
                            }
                            uint8_t t219 = gm_l46;
                            if (t219) {
                                gm_tkn42u7na3t81 t220;
                                memset(&t220, 0, sizeof t220);
                                F.ret = t220;
                                goto gm_fret;
                            }
                            void * t221 = gm_l0;
                            gm_t88rcucg1b7uk t222 = gm_l24;
                            gm_f_union_variants_a7khaqp((void *)&gm_l47, t221, GM_CONV(gm_csguankko4lmhd, t222));
                            uint8_t t223 = (uint8_t)((((uint8_t *)&gm_l47))[24] != 0);
                            if (t223) {
                                memmove(((uint8_t *)&gm_l48), ((uint8_t *)&gm_l47), 24);
                            } else {
                                gm_tkn42u7na3t81 t224;
                                memset(&t224, 0, sizeof t224);
                                F.ret = t224;
                                goto gm_fret;
                            }
                            memmove(((uint8_t *)&gm_l25), ((uint8_t *)&gm_l48), 24);
                            gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                            gm_l26 = t225;
                            gm_l27 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t226 = gm_l27;
                                    uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                    uint8_t t228 = (t226 < t227);
                                    if (!t228) {
                                        goto gm_lb36;
                                    }
                                    {
                                        void * t229 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                        uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                        uint64_t t231 = gm_l27;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l28), (((uint8_t *)t229) + (uintptr_t)t231 * 72), 72);
                                                gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                                                gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                                                uint8_t t234 = (gm_eq0iqiubfm7e(((uint8_t *)&t232), ((uint8_t *)&t233)));
                                                if (t234) {
                                                    {
                                                        {
                                                            uint8_t t235 = (uint8_t)(((((uint8_t *)&F.l28) + 16))[4] != 0);
                                                            if (t235) {
                                                                goto gm_lb42;
                                                            }
                                                            gm_l49 = ((uint8_t)0);
                                                            goto gm_lb41;
                                                        }
gm_lb42:;
                                                        {
                                                            gm_l49 = ((uint8_t)1);
                                                            goto gm_lb41;
                                                        }
gm_lb43:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 92, 28);
                                                    }
gm_lb41:;
                                                    uint8_t t236 = gm_l49;
                                                    if (t236) {
                                                        gm_tkn42u7na3t81 t237;
                                                        memset(&t237, 0, sizeof t237);
                                                        F.ret = t237;
                                                        goto gm_fret;
                                                    }
                                                    uint8_t t238 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                                                    if (t238) {
                                                        gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
                                                        gm_l50 = t239;
                                                    } else {
                                                        gm_t88rcucg1b7uk t240 = gm_l24;
                                                        gm_tkn42u7na3t81 t241;
                                                        GM_ST(uint8_t, (uint8_t *)&t241 + 5, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t241 + 6, 0);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t241), t240);
                                                        ((uint8_t *)&t241)[4] = 1;
                                                        F.ret = t241;
                                                        goto gm_fret;
                                                    }
                                                    gm_t88rcucg1b7uk t242 = gm_l50;
                                                    gm_l29 = t242;
                                                    gm_t88rcucg1b7uk t243 = gm_l29;
                                                    gm_t88rcucg1b7uk t244 = gm_l24;
                                                    uint8_t t245 = (gm_eq9m6mvtmgde(((uint8_t *)&t243), ((uint8_t *)&t244)));
                                                    if (t245) {
                                                        gm_l52 = ((uint8_t)1);
                                                    } else {
                                                        void * t246 = gm_l0;
                                                        void * t247 = (void *)(((uint8_t *)t246) + 8);
                                                        gm_t88rcucg1b7uk t248 = gm_l29;
                                                        gm_t53at80pfdus3 t249;
                                                        gm_f_kind_jcnt4hm((void *)&t249, t247, GM_CONV(gm_csguankko4lmhd, t248));
                                                        gm_t88rcucg1b7uk t250 = gm_l24;
                                                        uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&t249) + 0));
                                                        uint8_t t252 = (uint8_t)(t251 == ((uint8_t)13ULL));
                                                        if (t252) {
                                                            gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t249) + 8));
                                                            t252 = (uint8_t)gm_eq9m6mvtmgde(((uint8_t *)&t253), ((uint8_t *)&t250));
                                                        }
                                                        uint8_t t254 = (uint8_t)(t252);
                                                        gm_t53at80pfdus3 t255;
                                                        memset(&t255, 0, sizeof t255);
                                                        GM_ST(uint8_t, ((uint8_t *)&t255 + 0), ((uint8_t)13ULL));
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t255 + 8), t250);
                                                        gm_l52 = t254;
                                                    }
                                                    uint8_t t256 = gm_l52;
                                                    if (t256) {
                                                        gm_t88rcucg1b7uk t257 = gm_l29;
                                                        gm_tkn42u7na3t81 t258;
                                                        GM_ST(uint8_t, (uint8_t *)&t258 + 5, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t258 + 6, 0);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t258), t257);
                                                        ((uint8_t *)&t258)[4] = 1;
                                                        gm_l51 = t258;
                                                    } else {
                                                        gm_tkn42u7na3t81 t259;
                                                        memset(&t259, 0, sizeof t259);
                                                        gm_l51 = t259;
                                                    }
                                                    gm_tkn42u7na3t81 t260 = gm_l51;
                                                    F.ret = t260;
                                                    goto gm_fret;
                                                }
                                                goto gm_lb39;
                                            }
gm_lb40:;
                                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 90, 17);
                                        }
gm_lb39:;
                                    }
gm_lb38:;
                                    uint64_t t261 = gm_l27;
                                    uint64_t r262; bool o263 = __builtin_add_overflow(t261, ((uint64_t)1ULL), &r262);
                                    uint64_t t264 = r262;
                                    gm_l27 = t264;
                                }
                            }
gm_lb36:;
                            gm_tkn42u7na3t81 t265;
                            memset(&t265, 0, sizeof t265);
                            F.ret = t265;
                            goto gm_fret;
                        }
gm_lb32:;
                    }
gm_lb31:;
                    goto gm_lb1;
                }
gm_lb30:;
            }
            {
                {
                    uint8_t t266 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t267 = (uint8_t)(t266 == ((uint8_t)19ULL));
                    if (!t267) {
                        goto gm_lb44;
                    }
                    void * t268 = GM_LD(void *, ((((uint8_t *)t57) + 8) + 8));
                    gm_l30 = t268;
                    gm_b16_8 t269 = GM_LD(gm_b16_8, (((((uint8_t *)t57) + 8) + 8) + 8));
                    gm_l31 = t269;
                    void * t270 = gm_l0;
                    uint64_t t271 = gm_l1;
                    void * t272 = gm_l30;
                    gm_b16_8 t273 = gm_l31;
                    gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&t273));
                    gm_tkn42u7na3t81 t275 = gm_l3;
                    gm_tkn42u7na3t81 t276 = gm_f_const_construction_61mg03r((void *)NULL, cx, NULL, t270, t271, t272, t274, t275);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t276;
                    goto gm_fret;
                }
gm_lb44:;
                {
                    uint8_t t277 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                    uint8_t t278 = (uint8_t)(t277 == ((uint8_t)28ULL));
                    if (!t278) {
                        goto gm_lb45;
                    }
                    void * t279 = gm_l0;
                    uint64_t t280 = gm_l1;
                    void * t281 = gm_l2;
                    gm_tkn42u7na3t81 t282 = gm_l3;
                    gm_tkn42u7na3t81 t283 = gm_f_const_array_kn5cgp9((void *)NULL, cx, NULL, t279, t280, t281, t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t283;
                    goto gm_fret;
                }
gm_lb45:;
                {
                    goto gm_lb1;
                }
gm_lb46:;
                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 43, 5);
            }
        }
gm_lb1:;
    }
    {
        uint8_t t284 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
        if (t284) {
            gm_t88rcucg1b7uk t285 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l53 = t285;
        } else {
            gm_tkn42u7na3t81 t286;
            memset(&t286, 0, sizeof t286);
            F.ret = t286;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t287 = gm_l53;
        gm_l32 = t287;
        {
            {
                void * t288 = gm_l2;
                {
                    {
                        uint8_t t289 = GM_LD(uint8_t, ((((uint8_t *)t288) + 8) + 0));
                        uint8_t t290 = (uint8_t)(t289 == ((uint8_t)6ULL));
                        if (!t290) {
                            goto gm_lb50;
                        }
                        gm_l54 = ((uint8_t)1);
                        goto gm_lb49;
                    }
gm_lb50:;
                    {
                        uint8_t t291 = GM_LD(uint8_t, ((((uint8_t *)t288) + 8) + 0));
                        uint8_t t292 = (uint8_t)(t291 == ((uint8_t)22ULL));
                        if (!t292) {
                            goto gm_lb51;
                        }
                        gm_l54 = ((uint8_t)1);
                        goto gm_lb49;
                    }
gm_lb51:;
                    {
                        gm_l54 = ((uint8_t)0);
                        goto gm_lb49;
                    }
gm_lb52:;
                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 105, 8);
                }
gm_lb49:;
                uint8_t t293 = gm_l54;
                if (!t293) {
                    goto gm_lb48;
                }
                void * t294 = gm_l0;
                uint64_t t295 = gm_l1;
                void * t296 = gm_l2;
                gm_t88rcucg1b7uk t297 = gm_l32;
                gm_t9805qtonbsag t298 = gm_f_fn_ref_7alnaqh((void *)NULL, cx, NULL, t294, t295, t296, t297);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l56 = t298;
                {
                    {
                        uint8_t t299 = (uint8_t)((((uint8_t *)&gm_l56))[4] != 0);
                        if (t299) {
                            goto gm_lb54;
                        }
                        gm_l55 = ((uint8_t)0);
                        goto gm_lb53;
                    }
gm_lb54:;
                    {
                        gm_l55 = ((uint8_t)1);
                        goto gm_lb53;
                    }
gm_lb55:;
                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 105, 45);
                }
gm_lb53:;
                uint8_t t300 = gm_l55;
                if (!t300) {
                    goto gm_lb48;
                }
                gm_t88rcucg1b7uk t301 = gm_l32;
                gm_tkn42u7na3t81 t302;
                GM_ST(uint8_t, (uint8_t *)&t302 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t302 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t302), t301);
                ((uint8_t *)&t302)[4] = 1;
                F.ret = t302;
                goto gm_fret;
            }
gm_lb48:;
        }
gm_lb47:;
        void * t303 = gm_l0;
        uint64_t t304 = gm_l1;
        void * t305 = gm_l2;
        gm_t88rcucg1b7uk t306 = gm_l32;
        uint8_t t307 = gm_f_untyped_fits_tp6gphv((void *)NULL, cx, NULL, t303, t304, t305, t306);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t307) {
            gm_t88rcucg1b7uk t308 = gm_l32;
            gm_tkn42u7na3t81 t309;
            GM_ST(uint8_t, (uint8_t *)&t309 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t309 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t309), t308);
            ((uint8_t *)&t309)[4] = 1;
            gm_l57 = t309;
        } else {
            gm_tkn42u7na3t81 t310;
            memset(&t310, 0, sizeof t310);
            gm_l57 = t310;
        }
        gm_tkn42u7na3t81 t311 = gm_l57;
        F.ret = t311;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_construction_61mg03r(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3, gm_tkn42u7na3t81 a4) {
    struct gm_fr_const_construction_61mg03r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    gm_tkn42u7na3t81 gm_l4; /* expected */
    uint64_t gm_l5; /* errors */
    gm_t88rcucg1b7uk gm_l6; /* t */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l8; /* gargs */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t08olq97rscn3 gm_l16; /* n */
    gm_tf5srh4mp4b3i gm_l18; /* found */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    uint64_t gm_l23; /* i */
    gm_b16_8 gm_l24; /* defaults */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    uint8_t gm_l28; /* g */
    GM_STACK_CHECK(&F, 1702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l29;
    uint8_t gm_l30;
    gm_t53at80pfdus3 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_t8ta482bl6be0 gm_l33;
    uint64_t gm_l34;
    uint8_t gm_l35;
    uint8_t gm_l36;
    gm_tkn42u7na3t81 gm_l37;
    gm_tf5srh4mp4b3i gm_l38;
    uint8_t gm_l39;
    {
        void * t33 = gm_l2;
        uint8_t t34 = gm_f_calls_on_value_q0pa6h5(t33);
        if (t34) {
            gm_l29 = ((uint8_t)1);
        } else {
            void * t35 = gm_l0;
            uint64_t t36 = gm_l1;
            void * t37 = gm_l2;
            uint8_t t38 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t35, t36, t37, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = (uint8_t)!t38;
            gm_l29 = t39;
        }
        uint8_t t40 = gm_l29;
        if (t40) {
            gm_tkn42u7na3t81 t41;
            memset(&t41, 0, sizeof t41);
            F.ret = t41;
            goto gm_fret;
        }
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, ((uint8_t *)t42));
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 8));
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 8));
        gm_l5 = t45;
        void * t46 = gm_l0;
        uint64_t t47 = gm_l1;
        void * t48 = gm_l2;
        gm_t88rcucg1b7uk t49 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t46, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t49;
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, ((uint8_t *)t50));
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 8));
        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 8));
        uint64_t t54 = gm_l5;
        uint8_t t55 = (!(t53 == t54));
        if (t55) {
            gm_l30 = ((uint8_t)1);
        } else {
            void * t56 = gm_l0;
            void * t57 = (void *)(((uint8_t *)t56) + 8);
            gm_t88rcucg1b7uk t58 = gm_l6;
            uint8_t t59 = gm_f_is_error_8k27gd1(t57, GM_CONV(gm_csguankko4lmhd, t58));
            gm_l30 = t59;
        }
        uint8_t t60 = gm_l30;
        if (t60) {
            gm_tkn42u7na3t81 t61;
            memset(&t61, 0, sizeof t61);
            F.ret = t61;
            goto gm_fret;
        }
        {
            {
                {
                    {
                        void * t62 = gm_l0;
                        void * t63 = (void *)(((uint8_t *)t62) + 8);
                        gm_t88rcucg1b7uk t64 = gm_l6;
                        gm_f_kind_jcnt4hm((void *)&gm_l31, t63, GM_CONV(gm_csguankko4lmhd, t64));
                        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                        uint8_t t66 = (uint8_t)(t65 == ((uint8_t)17ULL));
                        if (!t66) {
                            goto gm_lb2;
                        }
                        uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 8));
                        gm_l7 = t67;
                        gm_b16_8 t68 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l31) + 8) + 8));
                        gm_l8 = t68;
                        uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint8_t t70 = (t69 > ((uint64_t)0ULL));
                        if (t70) {
                            gm_tkn42u7na3t81 t71;
                            memset(&t71, 0, sizeof t71);
                            F.ret = t71;
                            goto gm_fret;
                        }
                        {
                            {
                                void * t72 = gm_l0;
                                void * t73 = GM_LD(void *, (((uint8_t *)t72) + 1088));
                                uint64_t t74 = GM_LD(uint64_t, ((((uint8_t *)t72) + 1088) + 8));
                                uint32_t t75 = gm_l7;
                                uint64_t t76 = (uint64_t)t75;
                                if ((uintptr_t)t76 >= (uintptr_t)(t74)) gm_trap_index((uintptr_t)t76, t74, gm_src2eo2k3vc53po, 189, 21);
                                uint8_t t77 = GM_LD(uint8_t, (((((uint8_t *)t73) + (uintptr_t)t76 * 184) + 24) + 0));
                                uint8_t t78 = (uint8_t)(t77 == ((uint8_t)0ULL));
                                if (!t78) {
                                    goto gm_lb4;
                                }
                                memmove(((uint8_t *)&F.l9), (((((uint8_t *)t73) + (uintptr_t)t76 * 184) + 24) + 8), 48);
                                void * t79 = gm_l0;
                                void * t80 = GM_LD(void *, (((uint8_t *)t79) + 1088));
                                uint64_t t81 = GM_LD(uint64_t, ((((uint8_t *)t79) + 1088) + 8));
                                uint32_t t82 = gm_l7;
                                uint64_t t83 = (uint64_t)t82;
                                if ((uintptr_t)t83 >= (uintptr_t)(t81)) gm_trap_index((uintptr_t)t83, t81, gm_src2eo2k3vc53po, 190, 8);
                                uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t80) + (uintptr_t)t83 * 184) + 112));
                                if (t84) {
                                    gm_tkn42u7na3t81 t85;
                                    memset(&t85, 0, sizeof t85);
                                    F.ret = t85;
                                    goto gm_fret;
                                }
                                gm_trqfvmjtt4sij t86;
                                gm_f_list_s83jn2q((void *)&t86, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l10 = t86;
                                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                gm_l11 = t87;
                                gm_l12 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t88 = gm_l12;
                                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                        uint8_t t90 = (t88 < t89);
                                        if (!t90) {
                                            goto gm_lb5;
                                        }
                                        {
                                            void * t91 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                            uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                            uint64_t t93 = gm_l12;
                                            {
                                                {
                                                    void * t94 = (void *)((uint8_t *)&F.l10);
                                                    gm_tftb1odc739t5 t95 = gm_f_push_vd1290p(t94, ((uint8_t)0));
                                                    gm_l32 = t95;
                                                    uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                    if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src2eo2k3vc53po, 193, 27);
                                                    goto gm_lb8;
                                                }
gm_lb9:;
                                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 193, 5);
                                            }
gm_lb8:;
                                        }
gm_lb7:;
                                        uint64_t t97 = gm_l12;
                                        uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                                        uint64_t t100 = r98;
                                        gm_l12 = t100;
                                    }
                                }
gm_lb5:;
                                gm_b16_8 t101 = gm_l3;
                                gm_l13 = t101;
                                gm_l14 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t102 = gm_l14;
                                        uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                        uint8_t t104 = (t102 < t103);
                                        if (!t104) {
                                            goto gm_lb10;
                                        }
                                        {
                                            void * t105 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                            uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                            uint64_t t107 = gm_l14;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l15), (((uint8_t *)t105) + (uintptr_t)t107 * 144), 144);
                                                    {
                                                        {
                                                            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                                                            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)1ULL));
                                                            if (!t109) {
                                                                goto gm_lb16;
                                                            }
                                                            memmove(((uint8_t *)&gm_l16), (((uint8_t *)&F.l15) + 8), 32);
                                                            memmove(((uint8_t *)&F.l17), ((((uint8_t *)&F.l15) + 8) + 32), 88);
                                                            gm_tf5srh4mp4b3i t110;
                                                            memset(&t110, 0, sizeof t110);
                                                            gm_l18 = t110;
                                                            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                                            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                                            gm_l19 = t112;
                                                            gm_l20 = ((uint64_t)0ULL);
                                                            {
                                                                for (;;) {
                                                                    uint64_t t113 = gm_l20;
                                                                    uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                                    uint8_t t115 = (t113 < t114);
                                                                    if (!t115) {
                                                                        goto gm_lb17;
                                                                    }
                                                                    {
                                                                        uint64_t t116 = gm_l20;
                                                                        void * t117 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                                                        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                                        uint64_t t119 = gm_l20;
                                                                        gm_t0mglksrkfa5e t120 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t117) + (uintptr_t)t119 * 40));
                                                                        gm_t8ta482bl6be0 t121;
                                                                        GM_ST(uint64_t, ((uint8_t *)&t121 + 0), t116);
                                                                        GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t121 + 8), t120);
                                                                        gm_l33 = t121;
                                                                        {
                                                                            {
                                                                                uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&gm_l33));
                                                                                gm_l21 = t122;
                                                                                memmove(((uint8_t *)&F.l22), (((uint8_t *)&gm_l33) + 8), 40);
                                                                                gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                                                                                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                                                                uint8_t t125 = (gm_eq0iqiubfm7e(((uint8_t *)&t123), ((uint8_t *)&t124)));
                                                                                if (t125) {
                                                                                    uint64_t t126 = gm_l21;
                                                                                    gm_tf5srh4mp4b3i t127;
                                                                                    GM_ST(uint8_t, (uint8_t *)&t127 + 9, 0);
                                                                                    GM_ST(uint16_t, (uint8_t *)&t127 + 10, 0);
                                                                                    GM_ST(uint32_t, (uint8_t *)&t127 + 12, 0);
                                                                                    GM_ST(uint64_t, ((uint8_t *)&t127), t126);
                                                                                    ((uint8_t *)&t127)[8] = 1;
                                                                                    gm_l18 = t127;
                                                                                    goto gm_lb17;
                                                                                }
                                                                                goto gm_lb20;
                                                                            }
gm_lb21:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 197, 9);
                                                                        }
gm_lb20:;
                                                                    }
gm_lb19:;
                                                                    uint64_t t128 = gm_l20;
                                                                    uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                                                                    uint64_t t131 = r129;
                                                                    gm_l20 = t131;
                                                                }
                                                            }
gm_lb17:;
                                                            uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
                                                            if (t132) {
                                                                uint64_t t133 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                                                                gm_l34 = t133;
                                                            } else {
                                                                gm_tkn42u7na3t81 t134;
                                                                memset(&t134, 0, sizeof t134);
                                                                F.ret = t134;
                                                                goto gm_fret;
                                                            }
                                                            uint64_t t135 = gm_l34;
                                                            gm_l23 = t135;
                                                            void * t136 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                            uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                            uint64_t t138 = gm_l23;
                                                            if ((uintptr_t)t138 >= (uintptr_t)(t137)) gm_trap_index((uintptr_t)t138, t137, gm_src2eo2k3vc53po, 204, 12);
                                                            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)t136) + (uintptr_t)t138 * 1));
                                                            if (t139) {
                                                                gm_l35 = ((uint8_t)1);
                                                            } else {
                                                                void * t140 = gm_l0;
                                                                uint64_t t141 = gm_l1;
                                                                uint32_t t142 = gm_l7;
                                                                void * t143 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                                uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                                uint64_t t145 = gm_l23;
                                                                if ((uintptr_t)t145 >= (uintptr_t)(t144)) gm_trap_index((uintptr_t)t145, t144, gm_src2eo2k3vc53po, 204, 63);
                                                                void * t146 = (void *)(((uint8_t *)t143) + (uintptr_t)t145 * 40);
                                                                uint8_t t147 = gm_f_field_visible_7kr8oue(t140, t141, t142, t146);
                                                                uint8_t t148 = (uint8_t)!t147;
                                                                gm_l35 = t148;
                                                            }
                                                            uint8_t t149 = gm_l35;
                                                            if (t149) {
                                                                gm_tkn42u7na3t81 t150;
                                                                memset(&t150, 0, sizeof t150);
                                                                F.ret = t150;
                                                                goto gm_fret;
                                                            }
                                                            void * t151 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                            uint64_t t153 = gm_l23;
                                                            if ((uintptr_t)t153 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t153, t152, gm_src2eo2k3vc53po, 205, 9);
                                                            GM_ST(uint8_t, (((uint8_t *)t151) + (uintptr_t)t153 * 1), ((uint8_t)1));
                                                            void * t154 = gm_l0;
                                                            uint64_t t155 = gm_l1;
                                                            void * t156 = (void *)((uint8_t *)&F.l17);
                                                            void * t157 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                            uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                            uint64_t t159 = gm_l23;
                                                            if ((uintptr_t)t159 >= (uintptr_t)(t158)) gm_trap_index((uintptr_t)t159, t158, gm_src2eo2k3vc53po, 206, 38);
                                                            gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t157) + (uintptr_t)t159 * 40) + 16));
                                                            gm_tkn42u7na3t81 t161;
                                                            GM_ST(uint8_t, (uint8_t *)&t161 + 5, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t161 + 6, 0);
                                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t161), t160);
                                                            ((uint8_t *)&t161)[4] = 1;
                                                            gm_tkn42u7na3t81 t162 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t154, t155, t156, t161);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_l37 = t162;
                                                            {
                                                                {
                                                                    uint8_t t163 = (uint8_t)((((uint8_t *)&gm_l37))[4] != 0);
                                                                    if (t163) {
                                                                        goto gm_lb23;
                                                                    }
                                                                    gm_l36 = ((uint8_t)1);
                                                                    goto gm_lb22;
                                                                }
gm_lb23:;
                                                                {
                                                                    gm_l36 = ((uint8_t)0);
                                                                    goto gm_lb22;
                                                                }
gm_lb24:;
                                                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 206, 12);
                                                            }
gm_lb22:;
                                                            uint8_t t164 = gm_l36;
                                                            if (t164) {
                                                                gm_tkn42u7na3t81 t165;
                                                                memset(&t165, 0, sizeof t165);
                                                                F.ret = t165;
                                                                goto gm_fret;
                                                            }
                                                            goto gm_lb15;
                                                        }
gm_lb16:;
                                                        gm_tkn42u7na3t81 t166;
                                                        memset(&t166, 0, sizeof t166);
                                                        F.ret = t166;
                                                        goto gm_fret;
                                                    }
gm_lb15:;
                                                    goto gm_lb13;
                                                }
gm_lb14:;
                                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 194, 5);
                                            }
gm_lb13:;
                                        }
gm_lb12:;
                                        uint64_t t167 = gm_l14;
                                        uint64_t r168; bool o169 = __builtin_add_overflow(t167, ((uint64_t)1ULL), &r168);
                                        uint64_t t170 = r168;
                                        gm_l14 = t170;
                                    }
                                }
gm_lb10:;
                                void * t171 = gm_l0;
                                void * t172 = GM_LD(void *, (((uint8_t *)t171) + 1088));
                                uint64_t t173 = GM_LD(uint64_t, ((((uint8_t *)t171) + 1088) + 8));
                                uint32_t t174 = gm_l7;
                                uint64_t t175 = (uint64_t)t174;
                                if ((uintptr_t)t175 >= (uintptr_t)(t173)) gm_trap_index((uintptr_t)t175, t173, gm_src2eo2k3vc53po, 208, 17);
                                gm_b16_8 t176 = GM_LD(gm_b16_8, ((((uint8_t *)t172) + (uintptr_t)t175 * 184) + 80));
                                gm_l24 = t176;
                                gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                                gm_l25 = t178;
                                gm_l26 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t179 = gm_l26;
                                        uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                        uint8_t t181 = (t179 < t180);
                                        if (!t181) {
                                            goto gm_lb25;
                                        }
                                        {
                                            uint64_t t182 = gm_l26;
                                            void * t183 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                            uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                            uint64_t t185 = gm_l26;
                                            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)t183) + (uintptr_t)t185 * 1));
                                            gm_tf5srh4mp4b3i t187;
                                            GM_ST(uint8_t, (uint8_t *)&t187 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t187 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t187 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t187 + 0), t182);
                                            GM_ST(uint8_t, ((uint8_t *)&t187 + 8), t186);
                                            gm_l38 = t187;
                                            {
                                                {
                                                    uint64_t t188 = GM_LD(uint64_t, ((uint8_t *)&gm_l38));
                                                    gm_l27 = t188;
                                                    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 8));
                                                    gm_l28 = t189;
                                                    {
                                                        {
                                                            uint8_t t190 = gm_l28;
                                                            uint8_t t191 = (uint8_t)!t190;
                                                            if (!t191) {
                                                                goto gm_lb31;
                                                            }
                                                            uint64_t t192 = gm_l27;
                                                            uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                            uint8_t t194 = (t192 < t193);
                                                            if (t194) {
                                                                void * t195 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                                uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                                uint64_t t197 = gm_l27;
                                                                uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)t195) + (uintptr_t)t197 * 1));
                                                                gm_l39 = t198;
                                                            } else {
                                                                gm_l39 = ((uint8_t)0);
                                                            }
                                                            uint8_t t199 = gm_l39;
                                                            uint8_t t200 = (uint8_t)!t199;
                                                            if (!t200) {
                                                                goto gm_lb31;
                                                            }
                                                            gm_tkn42u7na3t81 t201;
                                                            memset(&t201, 0, sizeof t201);
                                                            F.ret = t201;
                                                            goto gm_fret;
                                                        }
gm_lb31:;
                                                    }
gm_lb30:;
                                                    goto gm_lb28;
                                                }
gm_lb29:;
                                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 209, 5);
                                            }
gm_lb28:;
                                        }
gm_lb27:;
                                        uint64_t t202 = gm_l26;
                                        uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                                        uint64_t t205 = r203;
                                        gm_l26 = t205;
                                    }
                                }
gm_lb25:;
                                void * t206 = gm_l0;
                                gm_t88rcucg1b7uk t207 = gm_l6;
                                gm_tkn42u7na3t81 t208 = gm_l4;
                                gm_tkn42u7na3t81 t209 = gm_f_expect_type_uld3786(t206, GM_CONV(gm_csguankko4lmhd, t207), t208);
                                F.ret = t209;
                                goto gm_fret;
                            }
gm_lb4:;
                            gm_tkn42u7na3t81 t210;
                            memset(&t210, 0, sizeof t210);
                            F.ret = t210;
                            goto gm_fret;
                        }
gm_lb3:;
                    }
                }
gm_lb2:;
            }
            {
                gm_tkn42u7na3t81 t211;
                memset(&t211, 0, sizeof t211);
                F.ret = t211;
                goto gm_fret;
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_const_type_7rijs95 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l6; /* t */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tkn42u7na3t81 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l4 = t9;
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_l4;
            uint8_t t13 = gm_f_is_error_8k27gd1(t11, GM_CONV(gm_csguankko4lmhd, t12));
            if (t13) {
                gm_tkn42u7na3t81 t14;
                memset(&t14, 0, sizeof t14);
                F.ret = t14;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            void * t17 = gm_l2;
            gm_tkn42u7na3t81 t18 = gm_l3;
            gm_tkn42u7na3t81 t19 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t15, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t19;
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
            if (!t20) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l7));
            gm_l5 = t21;
            gm_t88rcucg1b7uk t22 = gm_l5;
            gm_tkn42u7na3t81 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[4] = 1;
            F.ret = t23;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
            if (!t24) {
                goto gm_lb6;
            }
            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l6 = t25;
            void * t26 = gm_l0;
            void * t27 = (void *)(((uint8_t *)t26) + 464);
            uint64_t t28 = gm_l1;
            void * t29 = gm_l2;
            gm_t88rcucg1b7uk t30 = gm_l6;
            gm_tkmj998anon0r t31;
            GM_ST(uint32_t, (uint8_t *)&t31 + 20, 0);
            GM_ST(uint64_t, ((uint8_t *)&t31 + 0), t28);
            GM_ST(void *, ((uint8_t *)&t31 + 8), t29);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t31 + 16), t30);
            gm_tftb1odc739t5 t32 = gm_f_push_7jrb8if(t27, &t31);
            gm_l8 = t32;
            uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src2eo2k3vc53po, 21, 9);
            gm_t88rcucg1b7uk t34 = gm_l6;
            gm_tkn42u7na3t81 t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35), t34);
            ((uint8_t *)&t35)[4] = 1;
            F.ret = t35;
            goto gm_fret;
        }
gm_lb6:;
    }
gm_lb5:;
    void * t36 = gm_l0;
    void * t37 = gm_l2;
    gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s14frn0dagfs0));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(24));
    gm_b0_1 t40 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t36, t38, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tkn42u7na3t81 t41;
    memset(&t41, 0, sizeof t41);
    F.ret = t41;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_type_pk33veh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint64_t a3) {
    struct gm_fr_construction_type_pk33veh F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* n */
    void * gm_l4; /* callee */
    uint64_t gm_l5; /* errors */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t3mf58gv9kf3n gm_l7; /* a */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    uint64_t gm_l9; /* len */
    void * gm_l10; /* l */
    uint64_t gm_l11; /* errors */
    uint64_t gm_l12; /* k */
    GM_STACK_CHECK(&F, 682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l13;
    gm_tkn42u7na3t81 gm_l14;
    gm_t88rcucg1b7uk gm_l15;
    gm_t5e6kkuq21nig gm_l16;
    void * t13 = gm_l2;
    {
        {
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)19ULL));
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l4 = t16;
            void * t17 = gm_l0;
            uint64_t t18 = gm_l1;
            void * t19 = gm_l4;
            uint8_t t20 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t17, t18, t19, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = (uint8_t)!t20;
            if (t21) {
                gm_tkn42u7na3t81 t22;
                memset(&t22, 0, sizeof t22);
                F.ret = t22;
                goto gm_fret;
            }
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, ((uint8_t *)t23));
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 8));
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
            gm_l5 = t26;
            void * t27 = gm_l0;
            uint64_t t28 = gm_l1;
            void * t29 = gm_l4;
            gm_t88rcucg1b7uk t30 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t30;
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, ((uint8_t *)t31));
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 8));
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
            uint64_t t35 = gm_l5;
            uint8_t t36 = (!(t34 == t35));
            if (t36) {
                gm_l13 = ((uint8_t)1);
            } else {
                void * t37 = gm_l0;
                void * t38 = (void *)(((uint8_t *)t37) + 8);
                gm_t88rcucg1b7uk t39 = gm_l6;
                uint8_t t40 = gm_f_is_error_8k27gd1(t38, GM_CONV(gm_csguankko4lmhd, t39));
                gm_l13 = t40;
            }
            uint8_t t41 = gm_l13;
            if (t41) {
                gm_tkn42u7na3t81 t42;
                memset(&t42, 0, sizeof t42);
                F.ret = t42;
                goto gm_fret;
            }
            gm_t88rcucg1b7uk t43 = gm_l6;
            gm_tkn42u7na3t81 t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44), t43);
            ((uint8_t *)&t44)[4] = 1;
            F.ret = t44;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)28ULL));
            if (!t46) {
                goto gm_lb3;
            }
            gm_t3mf58gv9kf3n t47 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t13) + 8) + 8));
            gm_l7 = t47;
            void * t48 = gm_l0;
            uint64_t t49 = gm_l1;
            void * t50 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
            uint64_t t51 = gm_l3;
            gm_tkn42u7na3t81 t52 = gm_f_construction_type_pk33veh((void *)NULL, cx, NULL, t48, t49, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t52;
            uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
            if (t53) {
                gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l14));
                gm_l15 = t54;
            } else {
                gm_tkn42u7na3t81 t55;
                memset(&t55, 0, sizeof t55);
                F.ret = t55;
                goto gm_fret;
            }
            gm_t88rcucg1b7uk t56 = gm_l15;
            gm_l8 = t56;
            uint64_t t57 = gm_l3;
            uint64_t t58 = (uint64_t)t57;
            gm_l9 = t58;
            {
                {
                    uint8_t t59 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l7)) != 0);
                    if (!t59) {
                        goto gm_lb5;
                    }
                    void * t60 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    gm_l10 = t60;
                    {
                        {
                            void * t61 = gm_l10;
                            uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t61) + 8) + 0));
                            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)6ULL));
                            if (!t63) {
                                goto gm_lb7;
                            }
                            void * t64 = gm_l0;
                            uint64_t t65 = gm_l1;
                            void * t66 = gm_l10;
                            uint8_t t67 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t64, t65, t66);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (!t67) {
                                goto gm_lb7;
                            }
                            gm_tkn42u7na3t81 t68;
                            memset(&t68, 0, sizeof t68);
                            F.ret = t68;
                            goto gm_fret;
                        }
gm_lb7:;
                    }
gm_lb6:;
                    void * t69 = gm_l0;
                    void * t70 = GM_LD(void *, ((uint8_t *)t69));
                    void * t71 = GM_LD(void *, (((uint8_t *)t70) + 8));
                    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
                    gm_l11 = t72;
                    void * t73 = gm_l0;
                    uint64_t t74 = gm_l1;
                    void * t75 = gm_l10;
                    gm_t5e6kkuq21nig t76 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t73, t74, t75);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t76;
                    {
                        {
                            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)0ULL));
                            if (!t78) {
                                goto gm_lb9;
                            }
                            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            gm_l12 = t79;
                            uint64_t t80 = gm_l12;
                            gm_l9 = t80;
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            gm_tkn42u7na3t81 t81;
                            memset(&t81, 0, sizeof t81);
                            F.ret = t81;
                            goto gm_fret;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 169, 17);
                    }
gm_lb8:;
                    void * t82 = gm_l0;
                    void * t83 = GM_LD(void *, ((uint8_t *)t82));
                    void * t84 = GM_LD(void *, (((uint8_t *)t83) + 8));
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t84) + 8));
                    uint64_t t86 = gm_l11;
                    uint8_t t87 = (!(t85 == t86));
                    if (t87) {
                        gm_tkn42u7na3t81 t88;
                        memset(&t88, 0, sizeof t88);
                        F.ret = t88;
                        goto gm_fret;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            void * t89 = gm_l0;
            void * t90 = (void *)(((uint8_t *)t89) + 8);
            uint64_t t91 = gm_l9;
            gm_t88rcucg1b7uk t92 = gm_l8;
            gm_t88rcucg1b7uk t93 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94), t93);
            ((uint8_t *)&t94)[4] = 1;
            F.ret = t94;
            goto gm_fret;
        }
gm_lb3:;
        {
            gm_tkn42u7na3t81 t95;
            memset(&t95, 0, sizeof t95);
            F.ret = t95;
            goto gm_fret;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 155, 5);
    }
gm_lb1:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_bool_edv2577(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1) {
    struct gm_fr_expect_bool_edv2577 F;
    void * gm_l0; /* s */
    gm_tkn42u7na3t81 gm_l1; /* expected */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l3; /* x */
    GM_STACK_CHECK(&F, 259u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk gm_l4;
    uint8_t gm_l5;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
    if (t5) {
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l1));
        gm_l4 = t6;
    } else {
        gm_t88rcucg1b7uk t7 = gm_l2;
        gm_tkn42u7na3t81 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[4] = 1;
        F.ret = t8;
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t9 = gm_l4;
    gm_l3 = t9;
    gm_t88rcucg1b7uk t10 = gm_l3;
    gm_t88rcucg1b7uk t11 = gm_l2;
    uint8_t t12 = (gm_eq9m6mvtmgde(((uint8_t *)&t10), ((uint8_t *)&t11)));
    if (t12) {
        gm_l5 = ((uint8_t)1);
    } else {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        gm_t88rcucg1b7uk t15 = gm_l3;
        gm_t53at80pfdus3 t16;
        gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
        gm_t88rcucg1b7uk t17 = gm_l2;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        uint8_t t19 = (uint8_t)(t18 == ((uint8_t)13ULL));
        if (t19) {
            gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t16) + 8));
            t19 = (uint8_t)gm_eq9m6mvtmgde(((uint8_t *)&t20), ((uint8_t *)&t17));
        }
        uint8_t t21 = (uint8_t)(t19);
        gm_t53at80pfdus3 t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)13ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22 + 8), t17);
        gm_l5 = t21;
    }
    uint8_t t23 = gm_l5;
    if (t23) {
        gm_t88rcucg1b7uk t24 = gm_l3;
        gm_tkn42u7na3t81 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25), t24);
        ((uint8_t *)&t25)[4] = 1;
        F.ret = t25;
        goto gm_fret;
    }
    gm_tkn42u7na3t81 t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_type_uld3786(void * a0, gm_csguankko4lmhd a1, gm_tkn42u7na3t81 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_expect_type_uld3786 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tkn42u7na3t81 gm_l2; /* expected */
    gm_t88rcucg1b7uk gm_l3; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_t88rcucg1b7uk gm_l4;
    gm_tkn42u7na3t81 gm_l5;
    uint8_t t2 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
    if (t2) {
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l4 = t3;
    } else {
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tkn42u7na3t81 t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[4] = 1;
        F.ret = t5;
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t6 = gm_l4;
    gm_l3 = t6;
    gm_t88rcucg1b7uk t7 = gm_l3;
    gm_t88rcucg1b7uk t8 = gm_l1;
    uint8_t t9 = (gm_eq9m6mvtmgde(((uint8_t *)&t7), ((uint8_t *)&t8)));
    if (t9) {
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_tkn42u7na3t81 t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[4] = 1;
        gm_l5 = t11;
    } else {
        gm_tkn42u7na3t81 t12;
        memset(&t12, 0, sizeof t12);
        gm_l5 = t12;
    }
    gm_tkn42u7na3t81 t13 = gm_l5;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_field_visible_7kr8oue(void * a0, uint64_t a1, uint32_t a2, void * a3);
inline __attribute__((aligned(4))) uint8_t gm_f_float_finite_ak3diof(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_float_finite_ak3diof F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint16_t gm_l3; /* bits */
    double gm_l4; /* x */
    float gm_l5; /* x */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l6;
    {
        {
            void * t4 = gm_l0;
            void * t5 = (void *)(((uint8_t *)t4) + 8);
            gm_t88rcucg1b7uk t6 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l6, t5, GM_CONV(gm_csguankko4lmhd, t6));
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)3ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint16_t t9 = GM_LD(uint16_t, (((uint8_t *)&gm_l6) + 8));
            gm_l3 = t9;
            uint16_t t10 = gm_l3;
            uint8_t t11 = ((t10 == ((uint16_t)64ULL)));
            if (t11) {
                double t12 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t12;
                double t13 = gm_l4;
                double t14 = gm_l4;
                double t15 = (t13 - t14);
                uint8_t t16 = ((t15 == ((double)0e0)));
                F.ret = t16;
                goto gm_fret;
            }
            uint16_t t17 = gm_l3;
            uint8_t t18 = ((t17 == ((uint16_t)32ULL)));
            if (t18) {
                float t19 = gm_f_to_f32_8bnhsji((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t19;
                float t20 = gm_l5;
                float t21 = gm_l5;
                float t22 = (t20 - t21);
                uint8_t t23 = ((t22 == ((float)0e0)));
                F.ret = t23;
                goto gm_fret;
            }
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_suffix_type_icfvn23(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tj2m9dbh1ekv6 a1) {
    struct gm_fr_suffix_type_icfvn23 F;
    void * gm_l0; /* s */
    gm_tj2m9dbh1ekv6 gm_l1; /* suffix */
    uint32_t gm_l2; /* bits */
    uint32_t gm_l3; /* bits */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk gm_l4;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_l2 = t9;
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            uint32_t t12 = gm_l2;
            uint16_t t13 = (uint16_t)t12;
            gm_t88rcucg1b7uk t14 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t11, ((uint8_t)0), t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
            if (!t16) {
                goto gm_lb3;
            }
            uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_l3 = t17;
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 8);
            uint32_t t20 = gm_l3;
            uint16_t t21 = (uint16_t)t20;
            gm_t88rcucg1b7uk t22 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t19, ((uint8_t)1), t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t22;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
            if (!t24) {
                goto gm_lb4;
            }
            void * t25 = gm_l0;
            void * t26 = (void *)(((uint8_t *)t25) + 8);
            gm_t88rcucg1b7uk t27 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t27;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)3ULL));
            if (!t29) {
                goto gm_lb5;
            }
            void * t30 = gm_l0;
            void * t31 = (void *)(((uint8_t *)t30) + 8);
            gm_t53at80pfdus3 t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t33 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t31, &t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t33;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 123, 89);
    }
gm_lb1:;
    gm_t88rcucg1b7uk t34 = gm_l4;
    F.ret = t34;
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
__attribute__((aligned(4))) uint8_t gm_f_untyped_fits_tp6gphv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_untyped_fits_tp6gphv F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_t88rcucg1b7uk gm_l5; /* x */
    uint16_t gm_l9; /* bits */
    GM_STACK_CHECK(&F, 1184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l12;
    gm_tkv1vap90vht2 gm_l13;
    gm_t5vbkdq81qfs7 gm_l14;
    gm_t53at80pfdus3 gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_t53at80pfdus3 gm_l19;
    uint8_t gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_t88rcucg1b7uk t20 = gm_l3;
    gm_l4 = t20;
    {
        {
            void * t21 = gm_l0;
            void * t22 = (void *)(((uint8_t *)t21) + 8);
            gm_t88rcucg1b7uk t23 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l12, t22, GM_CONV(gm_csguankko4lmhd, t23));
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)13ULL));
            if (!t25) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 8));
            gm_l5 = t26;
            gm_t88rcucg1b7uk t27 = gm_l5;
            gm_l4 = t27;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t28 = gm_l0;
    uint64_t t29 = gm_l1;
    void * t30 = gm_l2;
    gm_f_untyped_if_plain_lbrp24p((void *)&gm_l13, (void *)NULL, cx, NULL, t28, t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l13))[208] != 0);
    if (t31) {
        memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l13), 208);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l14), 208);
    {
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
            if (!t33) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 200);
            {
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)0ULL));
                    if (!t35) {
                        goto gm_lb6;
                    }
                    memmove(((uint8_t *)&F.l8), (((uint8_t *)&F.l7) + 8), 48);
                    void * t36 = gm_l0;
                    void * t37 = (void *)(((uint8_t *)t36) + 8);
                    gm_t88rcucg1b7uk t38 = gm_l4;
                    uint8_t t39 = gm_f_is_integer_8d4f9ek(t37, GM_CONV(gm_csguankko4lmhd, t38));
                    if (t39) {
                        void * t40 = gm_l0;
                        gm_t88rcucg1b7uk t41 = gm_l4;
                        uint8_t t42 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t40, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), t41);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t42;
                        goto gm_fret;
                    }
                    {
                        {
                            void * t43 = gm_l0;
                            void * t44 = (void *)(((uint8_t *)t43) + 8);
                            gm_t88rcucg1b7uk t45 = gm_l4;
                            gm_f_kind_jcnt4hm((void *)&gm_l15, t44, GM_CONV(gm_csguankko4lmhd, t45));
                            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)3ULL));
                            if (!t47) {
                                goto gm_lb8;
                            }
                            uint16_t t48 = GM_LD(uint16_t, (((uint8_t *)&gm_l15) + 8));
                            gm_l9 = t48;
                            gm_tbci5ospk5nnn t49 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l8));
                            gm_f_from_int_ofjuoqh((void *)&F.l10, (void *)NULL, cx, NULL, &t49);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t50 = gm_l0;
                            gm_t88rcucg1b7uk t51 = gm_l4;
                            uint8_t t52 = gm_f_float_finite_ak3diof((void *)NULL, cx, NULL, t50, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), t51);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (t52) {
                                uint16_t t53 = gm_l9;
                                double t54 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), t53);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tngtuiuskn938 t55;
                                gm_f_from_f64_1parp9j((void *)&t55, (void *)NULL, cx, NULL, t54);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t56 = gm_f_eq_ceufbo4(&t55, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)));
                                gm_l16 = t56;
                            } else {
                                gm_l16 = ((uint8_t)0);
                            }
                            uint8_t t57 = gm_l16;
                            F.ret = t57;
                            goto gm_fret;
                        }
gm_lb8:;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
gm_lb7:;
                }
gm_lb6:;
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)1ULL));
                    if (!t59) {
                        goto gm_lb9;
                    }
                    memmove(((uint8_t *)&F.l11), (((uint8_t *)&F.l7) + 8), 96);
                    void * t60 = gm_l0;
                    void * t61 = (void *)(((uint8_t *)t60) + 8);
                    gm_t88rcucg1b7uk t62 = gm_l4;
                    gm_f_kind_jcnt4hm((void *)&gm_l19, t61, GM_CONV(gm_csguankko4lmhd, t62));
                    {
                        {
                            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)3ULL));
                            if (!t64) {
                                goto gm_lb11;
                            }
                            gm_l18 = ((uint8_t)1);
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            gm_l18 = ((uint8_t)0);
                            goto gm_lb10;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 233, 29);
                    }
gm_lb10:;
                    uint8_t t65 = gm_l18;
                    if (t65) {
                        void * t66 = gm_l0;
                        gm_t88rcucg1b7uk t67 = gm_l4;
                        uint8_t t68 = gm_f_float_finite_ak3diof((void *)NULL, cx, NULL, t66, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)), t67);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l17 = t68;
                    } else {
                        gm_l17 = ((uint8_t)0);
                    }
                    uint8_t t69 = gm_l17;
                    F.ret = t69;
                    goto gm_fret;
                }
gm_lb9:;
                {
                    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                    uint8_t t71 = (uint8_t)(t70 == ((uint8_t)2ULL));
                    if (!t71) {
                        goto gm_lb13;
                    }
                    void * t72 = gm_l0;
                    void * t73 = (void *)(((uint8_t *)t72) + 8);
                    gm_t88rcucg1b7uk t74 = gm_l4;
                    gm_f_kind_jcnt4hm((void *)&gm_l21, t73, GM_CONV(gm_csguankko4lmhd, t74));
                    {
                        {
                            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)3ULL));
                            if (!t76) {
                                goto gm_lb15;
                            }
                            gm_l20 = ((uint8_t)1);
                            goto gm_lb14;
                        }
gm_lb15:;
                        {
                            gm_l20 = ((uint8_t)0);
                            goto gm_lb14;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 234, 28);
                    }
gm_lb14:;
                    uint8_t t77 = gm_l20;
                    F.ret = t77;
                    goto gm_fret;
                }
gm_lb13:;
                {
                    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                    uint8_t t79 = (uint8_t)(t78 == ((uint8_t)3ULL));
                    if (!t79) {
                        goto gm_lb17;
                    }
                    void * t80 = gm_l0;
                    void * t81 = (void *)(((uint8_t *)t80) + 8);
                    gm_t88rcucg1b7uk t82 = gm_l4;
                    gm_t53at80pfdus3 t83;
                    gm_f_kind_jcnt4hm((void *)&t83, t81, GM_CONV(gm_csguankko4lmhd, t82));
                    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t83) + 0));
                    uint8_t t85 = (uint8_t)((t84 == ((uint8_t)4ULL)));
                    gm_t53at80pfdus3 t86;
                    memset(&t86, 0, sizeof t86);
                    GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)4ULL));
                    F.ret = t85;
                    goto gm_fret;
                }
gm_lb17:;
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)4ULL));
                    if (!t88) {
                        goto gm_lb18;
                    }
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb18:;
                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 225, 5);
            }
gm_lb5:;
        }
gm_lb4:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb3:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_untyped_if_plain_lbrp24p F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* errors */
    uint8_t gm_l4; /* was */
    GM_STACK_CHECK(&F, 1015u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l6;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    gm_l3 = t5;
    void * t6 = gm_l0;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 2656));
    gm_l4 = t7;
    void * t8 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t8) + 2656), ((uint8_t)1));
    void * t9 = gm_l0;
    uint64_t t10 = gm_l1;
    void * t11 = gm_l2;
    gm_f_try_untyped_fj0h8i5((void *)&F.l5, (void *)NULL, cx, NULL, t9, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = gm_l0;
    uint8_t t13 = gm_l4;
    GM_ST(uint8_t, (((uint8_t *)t12) + 2656), t13);
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 8));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t18 = gm_l3;
    uint8_t t19 = (!(t17 == t18));
    if (t19) {
        gm_l6 = ((uint8_t)1);
    } else {
        gm_t5vbkdq81qfs7 t20;
        memset(&t20, 0, sizeof t20);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)1ULL));
        gm_tkv1vap90vht2 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 209, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 210, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 212, 0);
        GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[208] = 1;
        gm_tkv1vap90vht2 t22 = GM_LD(gm_tkv1vap90vht2, ((uint8_t *)&F.l5));
        uint8_t t23 = (gm_eq0uoi35f1be(((uint8_t *)&t22), ((uint8_t *)&t21)));
        gm_l6 = t23;
    }
    uint8_t t24 = gm_l6;
    if (t24) {
        gm_tkv1vap90vht2 t25;
        memset(&t25, 0, sizeof t25);
        memmove(gm_out, ((uint8_t *)&t25), 216);
        goto gm_fdone;
    }
    memmove(gm_out, ((uint8_t *)&F.l5), 216);
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
