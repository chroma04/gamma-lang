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
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_t8er5olt969ip;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_b16_8 f11; } gm_th4d8o29g5urm;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { uint64_t f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tkb8hpcqf70t1;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_tjcj4j2gbh6c8 f0; uint8_t f1; uint8_t pe[1]; } gm_tp8mv052i18rg;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8er5olt969ip) == 28 && _Alignof(gm_t8er5olt969ip) == 4 && offsetof(gm_t8er5olt969ip, f0) == 0 && offsetof(gm_t8er5olt969ip, f1) == 4 && offsetof(gm_t8er5olt969ip, f2) == 8 && offsetof(gm_t8er5olt969ip, f3) == 12 && offsetof(gm_t8er5olt969ip, f4) == 16, "layout of `Deferred`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th4d8o29g5urm) == 192 && _Alignof(gm_th4d8o29g5urm) == 8 && offsetof(gm_th4d8o29g5urm, f0) == 0 && offsetof(gm_th4d8o29g5urm, f1) == 16 && offsetof(gm_th4d8o29g5urm, f2) == 32 && offsetof(gm_th4d8o29g5urm, f3) == 48 && offsetof(gm_th4d8o29g5urm, f4) == 64 && offsetof(gm_th4d8o29g5urm, f5) == 80 && offsetof(gm_th4d8o29g5urm, f6) == 96 && offsetof(gm_th4d8o29g5urm, f7) == 112 && offsetof(gm_th4d8o29g5urm, f8) == 128 && offsetof(gm_th4d8o29g5urm, f9) == 144 && offsetof(gm_th4d8o29g5urm, f10) == 160 && offsetof(gm_th4d8o29g5urm, f11) == 176, "layout of `(string, string, string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkb8hpcqf70t1) == 24 && _Alignof(gm_tkb8hpcqf70t1) == 8 && offsetof(gm_tkb8hpcqf70t1, f0) == 0 && offsetof(gm_tkb8hpcqf70t1, f1) == 8, "layout of `NamedDecl`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
typedef struct { gm_tkb8hpcqf70t1 f0; uint8_t f1; uint8_t pe[7]; } gm_toblvhepnjqkh;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tp8mv052i18rg) == 6 && _Alignof(gm_tp8mv052i18rg) == 2 && offsetof(gm_tp8mv052i18rg, f0) == 0 && offsetof(gm_tp8mv052i18rg, f1) == 4, "layout of `?(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_toblvhepnjqkh) == 32 && _Alignof(gm_toblvhepnjqkh) == 8 && offsetof(gm_toblvhepnjqkh, f0) == 0 && offsetof(gm_toblvhepnjqkh, f1) == 24, "layout of `?NamedDecl`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
typedef struct { gm_t428ue2u3hf5g f0; uint8_t f1; uint8_t pe[7]; } gm_trgmlgt2gmmjs;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_trgmlgt2gmmjs) == 40 && _Alignof(gm_trgmlgt2gmmjs) == 8 && offsetof(gm_trgmlgt2gmmjs, f0) == 0 && offsetof(gm_trgmlgt2gmmjs, f1) == 32, "layout of `?ConstKey`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
typedef struct { gm_tb07pp6njg0bk f0; uint8_t f1; uint8_t pe[7]; } gm_tk1gen3hgkm51;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
_Static_assert(sizeof(gm_tk1gen3hgkm51) == 232 && _Alignof(gm_tk1gen3hgkm51) == 8 && offsetof(gm_tk1gen3hgkm51, f0) == 0 && offsetof(gm_tk1gen3hgkm51, f1) == 224, "layout of `?LocalConst`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_array_len_eielif2 {
    gm_frame h;
    gm_t0ialelq3ql1k l6; /* v */
    gm_tbci5ospk5nnn l7; /* i */
    gm_t5e6kkuq21nig ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_type_64h714c {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_calls_on_value_q0pa6h5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_len_exprs_m8s6m0h {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* names */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_component_type_q9ge5lo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_comptime_value_l5orc93 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_key_of_q36m6nm {
    gm_frame h;
    gm_t5vbkdq81qfs7 l9; /* r */
    gm_t0ialelq3ql1k l10; /* v */
    gm_tbci5ospk5nnn l11; /* i */
    gm_trqfvmjtt4sij l13; /* bytes */
    gm_trgmlgt2gmmjs ret;
};
struct gm_fr_construction_call_ktqtrpo {
    gm_frame h;
    void * ret;
};
struct gm_fr_decl_is_type_oh0cpn9 {
    gm_frame h;
    gm_t6qooom8qa2do l3; /* v */
    gm_tv2uf4leil5hq l5; /* b */
    uint8_t ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_1l2vi9a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3uqusp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_osbc35j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_enum_array_type_0sic6t0 {
    gm_frame h;
    gm_tckdobunaulee l8; /* v */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fn_ref_7alnaqh {
    gm_frame h;
    gm_tddtjfggdat0d l6; /* sig */
    gm_tddtjfggdat0d l17; /* have */
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_ty_qaih7ts {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_format_6nmb1bf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bnsf58k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bunqlnl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c3isg16 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_il5kbp4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0aimgq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6tblha {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nbjjmv4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o2s8o9q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o5o5km2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_oq3rvor {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pplcoh0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qmamljh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rd9dflp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sv91363 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uhvct7c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_um7orcc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_up1v1ae {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vom8db6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_generic_decl_oir9hm6 {
    gm_frame h;
    gm_toblvhepnjqkh ret;
};
struct gm_fr_generic_type_args_5b8bvov {
    gm_frame h;
    gm_t6art7bprjiqb l5; /* info */
    gm_trqfvmjtt4sij l8; /* given */
    gm_t7te2tdpebbt4 l14; /* a */
    gm_t0mfir12leg2g l16; /* ep */
    gm_tc3oht4ksth9d l17; /* x */
    gm_tc3oht4ksth9d l19; /* x */
    gm_trqfvmjtt4sij l25; /* out */
    gm_t0mfir12leg2g l30; /* ep */
    gm_tc3oht4ksth9d l32; /* g */
    gm_tc3oht4ksth9d l34; /* ex */
    gm_trqfvmjtt4sij l37; /* map */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_hoists_13k2a2s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_type_name_a77a0kf {
    gm_frame h;
    gm_tp8mv052i18rg ret;
};
struct gm_fr_is_builtin_type_name_v5ttoka {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_oversized_int_name_nr25haf {
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
struct gm_fr_leave_module_scope_4hp5mc4 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_len_param_names_cilm68f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_len_params_bound_f7mjvn9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_length_is_type_soi53ah {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9tqbks7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_const_idpokjh {
    gm_frame h;
    gm_tk1gen3hgkm51 ret;
};
struct gm_fr_local_fn_aji8qnd {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_looks_like_type_ho5qv1k {
    gm_frame h;
    gm_tv1o1j44ii4db l11; /* el */
    uint8_t ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_names_error_oet8t6b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_names_type_rjdgib9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_prefix_operand_3n5jchh {
    gm_frame h;
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_afk8dfq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_down_hge098p {
    gm_frame h;
    gm_t2gtbh39ptekd l21; /* kind */
    void * ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hv483nm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k0n7eot {
    gm_frame h;
    gm_t0mfir12leg2g l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pushes_down_0djaflp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0ui7njc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_12ta4fr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_lhe5gv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_type_jjodbei {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_resolve_type_unchecked_psme7dn {
    gm_frame h;
    gm_trqfvmjtt4sij l13; /* ts */
    gm_tv1o1j44ii4db l16; /* el */
    gm_trqfvmjtt4sij l17; /* names */
    gm_tv1o1j44ii4db l20; /* el */
    gm_tqqhoivahirr0 l30; /* f */
    gm_tqqhoivahirr0 l48; /* f */
    gm_tkacitepg5fk0 l78; /* a */
    gm_tet9guhev5r4u l80; /* i */
    gm_tc3oht4ksth9d l82; /* x */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_syntax_is_type_pe6ugv2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_syntax_module_vho1001 {
    gm_frame h;
    gm_t6qooom8qa2do l9; /* v */
    gm_tv2uf4leil5hq l13; /* b */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
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
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_param_3av4r8i {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unknown_type_beat5io {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_trqfvmjtt4sij l7; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_value_param_tof1h6t {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_with_operand_ajbkgld {
    gm_frame h;
    gm_t2gtbh39ptekd l10; /* kind */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_6nmb1bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_il5kbp4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_o2s8o9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_o5o5km2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_oq3rvor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pplcoh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uhvct7c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_um7orcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_up1v1ae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_afk8dfq(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k0n7eot(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0ui7njc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lhe5gv6(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unknown_type_beat5io(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k027ibn62fv[40] __attribute__((aligned(8)));
extern uint8_t gm_k647t05rm86[40] __attribute__((aligned(8)));
extern uint8_t gm_kcodas3apkm[40] __attribute__((aligned(8)));
extern uint8_t gm_knjqrvnblc8[40] __attribute__((aligned(8)));
extern uint8_t gm_ks263t40fiu[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0hcm1974rbe7[34];
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s2328gjukbu28[16];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s33hvuvcdjs1b[11];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s43j3q24pq7de[26];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s5veo86cvpdae[86];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6hgf366ivqrg[34];
extern const uint8_t gm_s6i2dst792b3t[54];
extern const uint8_t gm_s6rj805fm39qm[65];
extern const uint8_t gm_s783a88c5t0ve[67];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9rkpo2ljmui9[50];
extern const uint8_t gm_sbfpg6hac36v9[19];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_se5118fq98e06[88];
extern const uint8_t gm_sencmbgnvrve5[18];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfpopfqbubj89[35];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgrpmaenv85i1[73];
extern const uint8_t gm_sgva3apqq9jo2[68];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sjdn5gs7lu39i[57];
extern const uint8_t gm_sjik6bsmdi5g2[83];
extern const uint8_t gm_sk5d5fbcb3pir[10];
extern const uint8_t gm_skpsvuvlfj0ep[68];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_sldikqa9i70sm[52];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm7179tmr0nhi[27];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_sm9funvq1222p[44];
extern const uint8_t gm_smi7k9v6u7cmo[11];
extern const uint8_t gm_sn790bmojc3gl[33];
extern const uint8_t gm_sobb0dbnbb0kc[56];
extern const uint8_t gm_spdcqn4vu2cam[45];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_ss8fufid8t1o1[32];
extern const uint8_t gm_ssj1a8k9nspq4[82];
extern const uint8_t gm_ssv8t6tsqrjjf[97];
extern const uint8_t gm_stevggi5tvq7c[18];
extern const uint8_t gm_su6il7ugmnq2g[48];
extern const gm_tmpl gm_tpl0vock46lp3ec;
extern const gm_tmpl gm_tpl2fcbm03ka11j;
extern const gm_tmpl gm_tpl5cnv021ebod7;
extern const gm_tmpl gm_tpl8v9uhia7uc1b;
extern const gm_tmpl gm_tpla0ci4vs242iu;
extern const gm_tmpl gm_tplc1tlvqjnir84;
extern const gm_tmpl gm_tpld3o5iqghffqq;
extern const gm_tmpl gm_tpleeuqgcr11ngn;
extern const gm_tmpl gm_tpleo7934k1qvrc;
extern const gm_tmpl gm_tplg21ilonldhva;
extern const gm_tmpl gm_tplhfdokrq767sh;
extern const gm_tmpl gm_tplj4gqj9p0fguq;
extern const gm_tmpl gm_tpljt2av4h7c132;
extern const gm_tmpl gm_tplk0jab62rgl3v;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_tmpl gm_tplmf8455dp92a3;
extern const gm_tmpl gm_tpln92q8vfbc4aq;
extern const gm_tmpl gm_tplup0bf5d2eopf;
extern const gm_tmpl gm_tplv1cj36rb6m0h;
extern const gm_tmpl gm_tplv23k7silrvnp;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 40);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_tbgd14esf0jvi t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcgfglnv8p059r, 92, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 80);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgfglnv8p059r, 96, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 240));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 240) + 8));
    uint64_t t6 = gm_l1;
    if ((uintptr_t)t6 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t6, t5, gm_src6lm8esmv16p1, 190, 14);
    void * t7 = (void *)(((uint8_t *)t4) + (uintptr_t)t6 * 216);
    gm_l3 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l3;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
    void * t11 = gm_l3;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
    uint64_t t13 = gm_l2;
    uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_tf5srh4mp4b3i t4 = gm_l2;
    gm_b16_8 t5 = gm_l3;
    void * t6 = gm_l4;
    gm_t88rcucg1b7uk t7 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t2, t3, t4, t5, t6, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1l2vi9a F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 96)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 56)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 28)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 224)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_osbc35j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_module_scope_011vo5a F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_trqfvmjtt4sij t4 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 1688));
    gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t3) + 1728));
    gm_td7pkamr64l0c t6;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t6 + 40), t5);
    F.l1 = t6;
    void * t7 = gm_l0;
    gm_trqfvmjtt4sij t8;
    gm_f_list_1jabh22((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t7) + 1688), t8);
    void * t9 = gm_l0;
    gm_trqfvmjtt4sij t10;
    gm_f_list_0bj26n7((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t9) + 1728), t10);
    memmove(gm_out, ((uint8_t *)&F.l1), 80);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)26ULL));
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
inline __attribute__((aligned(4))) void gm_f_format_6nmb1bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6nmb1bf F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6hgf366ivqrg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5cnv021ebod7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bnsf58k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn790bmojc3gl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleo7934k1qvrc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bunqlnl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sobb0dbnbb0kc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljt2av4h7c132), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c3isg16 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6i2dst792b3t));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(53));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleeuqgcr11ngn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_il5kbp4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_il5kbp4 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5veo86cvpdae));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(85));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0vock46lp3ec), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l0aimgq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_su6il7ugmnq2g));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpld3o5iqghffqq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6tblha F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s43j3q24pq7de));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj4gqj9p0fguq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nbjjmv4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0hcm1974rbe7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplup0bf5d2eopf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o2s8o9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o2s8o9q F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjik6bsmdi5g2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(82));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv1cj36rb6m0h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o5o5km2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o5o5km2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6rj805fm39qm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8v9uhia7uc1b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ooibcq2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf50oc24o4dhk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk6g6bjh5rkat), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_oq3rvor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_oq3rvor F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s783a88c5t0ve));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv23k7silrvnp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pplcoh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pplcoh0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sencmbgnvrve5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhfdokrq767sh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qmamljh F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se5118fq98e06));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(87));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmf8455dp92a3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rd9dflp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_skpsvuvlfj0ep));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla0ci4vs242iu), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sv91363 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbfpg6hac36v9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln92q8vfbc4aq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uhvct7c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uhvct7c F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss8fufid8t1o1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk0jab62rgl3v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_um7orcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_um7orcc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssv8t6tsqrjjf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(96));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2fcbm03ka11j), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_up1v1ae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_up1v1ae F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spdcqn4vu2cam));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc1tlvqjnir84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vom8db6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sk5d5fbcb3pir));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg21ilonldhva), t2);
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
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = gm_f_hoists_13k2a2s(t2);
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t5 = gm_l0;
    void * t6 = gm_l0;
    void * t7 = gm_f_prefix_operand_3n5jchh(t6);
    void * t8 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t5, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_module_scope_4hp5mc4 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    memmove((((uint8_t *)t2) + 1688), ((uint8_t *)a1), 40);
    void * t3 = gm_l0;
    memmove((((uint8_t *)t3) + 1728), (((uint8_t *)a1) + 40), 40);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9tqbks7 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1l2vi9a();
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
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_epvducg F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_5nn7ph0();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 547, 78);
    void * t6 = (void *)((((uint8_t *)t3) + (uintptr_t)t5 * 184) + 56);
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_get_espvf96(t6, t7);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
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
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)9ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)5ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
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
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0jmnnpk(t8, ((uint64_t)1ULL), t10);
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
    gm_b16_8 t18 = gm_l1;
    GM_ST(gm_b16_8, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_afk8dfq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_afk8dfq F;
    void * gm_l0; /* l */
    gm_t8er5olt969ip gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d3uqusp();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0ui7njc(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 28), ((uint8_t *)a1), 28);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 28)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h69n5fi F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_8fnsr5h();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_1sqb3ir(t8, ((uint64_t)1ULL), t10);
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
    gm_tplpbqf3duvn3 t18 = gm_l1;
    GM_ST(gm_tplpbqf3duvn3, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_hv483nm F;
    void * gm_l0; /* l */
    gm_tep2drsl9408b gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_osbc35j();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_12ta4fr(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k0n7eot(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k0n7eot F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1l2vi9a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_lhe5gv6(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 96), ((uint8_t *)&F.l1), 96);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 96)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j4iu2lk(t8, ((uint64_t)1ULL), t10);
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
    gm_t88rcucg1b7uk t18 = gm_l1;
    GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t16) + (uintptr_t)t17 * 4), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jbu9fo3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8skjmj4(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)a1), 32);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    gm_b16_8 t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_tep2drsl9408b t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_hv483nm(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcsffver18si54, 63, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0ui7njc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0ui7njc F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)28ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_12ta4fr F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1sqb3ir F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
    F.ret = t8;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lhe5gv6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_lhe5gv6 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)96ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)a1));
    gm_f_truncate_7r782uc(t3, t4);
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 1648);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    gm_f_truncate_daelje0(t6, t7);
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 1728);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    gm_f_truncate_dsb08pk(t9, t10);
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 1688);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    gm_f_truncate_upeumbt(t12, t13);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tgglga8ro3isc t5;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t6 + 8), t5);
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1608) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 1648) + 8));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 1728) + 8));
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1688) + 8));
    gm_t66j1im9ip128 t10;
    GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t5);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 16), t7);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 24), t9);
    memmove(gm_out, ((uint8_t *)&t10), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t4 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t5 = (void *)((uint8_t *)&F.l1);
    gm_f_into_slice_hq0te56((void *)&gm_l2, t5);
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srclv3pj5es0f8o, 624, 12);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 224)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)6ULL));
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
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_array_len_eielif2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint32_t gm_l4; /* pid */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint64_t gm_l8; /* n */
    void * gm_l9; /* hook */
    GM_STACK_CHECK(&F, 1173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9805qtonbsag gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tkv1vap90vht2 gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tabb710d9mq2e gm_l15;
    void * gm_l16;
    {
        {
            void * t16 = gm_l2;
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)6ULL));
            if (!t18) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t16) + 8) + 8), 32);
            {
                {
                    void * t19 = gm_l0;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_t9805qtonbsag t21 = gm_f_value_param_tof1h6t(t19, t20);
                    gm_l10 = t21;
                    uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
                    if (!t22) {
                        goto gm_lb4;
                    }
                    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                    gm_l4 = t23;
                    {
                        {
                            void * t24 = gm_l0;
                            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 1128));
                            uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 1128) + 8));
                            uint32_t t27 = gm_l4;
                            uint64_t t28 = (uint64_t)t27;
                            if ((uintptr_t)t28 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t28, t26, gm_srcsffver18si54, 626, 26);
                            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t25) + (uintptr_t)t28 * 48) + 0));
                            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)2ULL));
                            if (!t30) {
                                goto gm_lb6;
                            }
                            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t25) + (uintptr_t)t28 * 48) + 4));
                            gm_l5 = t31;
                            void * t32 = gm_l0;
                            void * t33 = (void *)(((uint8_t *)t32) + 8);
                            gm_t88rcucg1b7uk t34 = gm_l5;
                            uint8_t t35 = gm_f_is_integer_8d4f9ek(t33, GM_CONV(gm_csguankko4lmhd, t34));
                            uint8_t t36 = (uint8_t)!t35;
                            if (t36) {
                                void * t37 = gm_l0;
                                void * t38 = gm_l2;
                                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
                                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                                gm_tnpifv20irnph t41;
                                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                                gm_f_format_bunqlnl((void *)&gm_l11, (void *)NULL, cx, NULL, t41);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcsffver18si54, 628, 33);
                                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                                gm_b0_1 t45 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t37, t39, t44);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t5e6kkuq21nig t46;
                                GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t46 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)2ULL));
                                F.ret = t46;
                                goto gm_fret;
                            }
                            void * t47 = gm_l0;
                            void * t48 = (void *)(((uint8_t *)t47) + 8);
                            uint32_t t49 = gm_l4;
                            gm_t88rcucg1b7uk t50 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t5e6kkuq21nig t51;
                            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t51 + 8), t50);
                            F.ret = t51;
                            goto gm_fret;
                        }
gm_lb6:;
                        gm_t5e6kkuq21nig t52;
                        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t52 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)2ULL));
                        F.ret = t52;
                        goto gm_fret;
                    }
gm_lb5:;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t53 = gm_l0;
    uint64_t t54 = gm_l1;
    void * t55 = gm_l2;
    gm_f_try_untyped_fj0h8i5((void *)&gm_l12, (void *)NULL, cx, NULL, t53, t54, t55);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l12))[208] != 0);
            if (!t56) {
                goto gm_lb8;
            }
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
            if (!t58) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l12) + 8), 200);
            {
                {
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)0ULL));
                    if (!t60) {
                        goto gm_lb10;
                    }
                    memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 48);
                    {
                        {
                            gm_tf5srh4mp4b3i t61 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                            gm_l13 = t61;
                            uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
                            if (!t62) {
                                goto gm_lb12;
                            }
                            uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                            gm_l8 = t63;
                            uint64_t t64 = gm_l8;
                            gm_t5e6kkuq21nig t65;
                            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t65 + 8), t64);
                            F.ret = t65;
                            goto gm_fret;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    void * t66 = gm_l0;
                    void * t67 = gm_l2;
                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
                    gm_b16_8 t69 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t70;
                    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
                    gm_f_format_uhvct7c((void *)&gm_l14, (void *)NULL, cx, NULL, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcsffver18si54, 638, 33);
                    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                    gm_b0_1 t74 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t66, t68, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5e6kkuq21nig t75;
                    GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
                    F.ret = t75;
                    goto gm_fret;
                }
gm_lb10:;
            }
gm_lb9:;
            void * t76 = gm_l0;
            void * t77 = gm_l2;
            gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
            void * t79 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t80 = gm_f_describe_bm129vh(t79);
            gm_tnpifv20irnph t81;
            GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
            gm_f_format_up1v1ae((void *)&gm_l15, (void *)NULL, cx, NULL, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcsffver18si54, 641, 29);
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            gm_b0_1 t85 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t76, t78, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5e6kkuq21nig t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t86 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)2ULL));
            F.ret = t86;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t87 = (uint8_t)((((uint8_t *)&gm_l12))[208] != 0);
            if (!t87) {
                goto gm_lb13;
            }
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)1ULL));
            if (!t89) {
                goto gm_lb13;
            }
            gm_t5e6kkuq21nig t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)2ULL));
            F.ret = t90;
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l12))[208] != 0);
            if (t91) {
                goto gm_lb14;
            }
            void * t92 = gm_l0;
            uint8_t t93 = (uint8_t)(GM_LD(void *, (((uint8_t *)t92) + 2296)) != 0);
            if (t93) {
                void * t94 = GM_LD(void *, (((uint8_t *)t92) + 2296));
                gm_l16 = t94;
            } else {
                void * t95 = gm_l0;
                void * t96 = gm_l2;
                gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
                gm_b16_8 t98;
                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s9rkpo2ljmui9));
                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(49));
                gm_b0_1 t99 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t95, t97, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5e6kkuq21nig t100;
                GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t100 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)2ULL));
                F.ret = t100;
                goto gm_fret;
            }
            void * t101 = gm_l16;
            gm_l9 = t101;
            void * t102 = gm_l9;
            void * t103 = gm_l0;
            uint64_t t104 = gm_l1;
            void * t105 = gm_l2;
            void * t106 = t102;
            gm_t5e6kkuq21nig t107;
            if (GM_PTR_TAGGED(t106)) {
                t107 = ((gm_t5e6kkuq21nig (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t106))(t103, t104, t105);
            } else {
                t107 = ((gm_t5e6kkuq21nig (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t106)(NULL, cx, NULL, t103, t104, t105);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t107;
            goto gm_fret;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 634, 5);
    }
gm_lb7:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_builtin_type_64h714c F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tjcj4j2gbh6c8 gm_l2; /* i */
    GM_STACK_CHECK(&F, 1164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tp8mv052i18rg gm_l3;
    gm_tkn42u7na3t81 gm_l4;
    {
        {
            gm_b16_8 t18 = gm_l1;
            gm_tp8mv052i18rg t19 = gm_f_int_type_name_a77a0kf(t18);
            gm_l3 = t19;
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            gm_tjcj4j2gbh6c8 t21 = GM_LD(gm_tjcj4j2gbh6c8, ((uint8_t *)&gm_l3));
            gm_l2 = t21;
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
            uint16_t t25 = GM_LD(uint16_t, (((uint8_t *)&gm_l2) + 2));
            gm_t88rcucg1b7uk t26 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t27), t26);
            ((uint8_t *)&t27)[4] = 1;
            F.ret = t27;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            gm_b16_8 t28 = gm_l1;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s6groq37ram3h));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(5));
            uint8_t t30 = (gm_eq0iqiubfm7e(((uint8_t *)&t28), ((uint8_t *)&t29)));
            if (!t30) {
                goto gm_lb4;
            }
            void * t31 = gm_l0;
            void * t32 = (void *)(((uint8_t *)t31) + 8);
            gm_t88rcucg1b7uk t33 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34), t33);
            ((uint8_t *)&t34)[4] = 1;
            gm_l4 = t34;
            goto gm_lb3;
        }
gm_lb4:;
        {
            gm_b16_8 t35 = gm_l1;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s0i3i86nphpsh));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(5));
            uint8_t t37 = (gm_eq0iqiubfm7e(((uint8_t *)&t35), ((uint8_t *)&t36)));
            if (!t37) {
                goto gm_lb5;
            }
            void * t38 = gm_l0;
            void * t39 = (void *)(((uint8_t *)t38) + 8);
            gm_t53at80pfdus3 t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t41 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t39, &t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42), t41);
            ((uint8_t *)&t42)[4] = 1;
            gm_l4 = t42;
            goto gm_lb3;
        }
gm_lb5:;
        {
            gm_b16_8 t43 = gm_l1;
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_shnig5tqtdedi));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(3));
            uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
            if (!t45) {
                goto gm_lb6;
            }
            void * t46 = gm_l0;
            void * t47 = (void *)(((uint8_t *)t46) + 8);
            gm_t53at80pfdus3 t48;
            memset(&t48, 0, sizeof t48);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t48 + 8), ((uint16_t)16ULL));
            gm_t88rcucg1b7uk t49 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t47, &t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t50), t49);
            ((uint8_t *)&t50)[4] = 1;
            gm_l4 = t50;
            goto gm_lb3;
        }
gm_lb6:;
        {
            gm_b16_8 t51 = gm_l1;
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sm9e81djuncnu));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(3));
            uint8_t t53 = (gm_eq0iqiubfm7e(((uint8_t *)&t51), ((uint8_t *)&t52)));
            if (!t53) {
                goto gm_lb7;
            }
            void * t54 = gm_l0;
            void * t55 = (void *)(((uint8_t *)t54) + 8);
            gm_t53at80pfdus3 t56;
            memset(&t56, 0, sizeof t56);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t56 + 8), ((uint16_t)32ULL));
            gm_t88rcucg1b7uk t57 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t55, &t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t58 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t58), t57);
            ((uint8_t *)&t58)[4] = 1;
            gm_l4 = t58;
            goto gm_lb3;
        }
gm_lb7:;
        {
            gm_b16_8 t59 = gm_l1;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sqqajgod2bb4o));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
            uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
            if (!t61) {
                goto gm_lb8;
            }
            void * t62 = gm_l0;
            void * t63 = (void *)(((uint8_t *)t62) + 8);
            gm_t53at80pfdus3 t64;
            memset(&t64, 0, sizeof t64);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t64 + 8), ((uint16_t)64ULL));
            gm_t88rcucg1b7uk t65 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t63, &t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t66), t65);
            ((uint8_t *)&t66)[4] = 1;
            gm_l4 = t66;
            goto gm_lb3;
        }
gm_lb8:;
        {
            gm_b16_8 t67 = gm_l1;
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s929lennfd3g6));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(4));
            uint8_t t69 = (gm_eq0iqiubfm7e(((uint8_t *)&t67), ((uint8_t *)&t68)));
            if (!t69) {
                goto gm_lb9;
            }
            void * t70 = gm_l0;
            void * t71 = (void *)(((uint8_t *)t70) + 8);
            gm_t88rcucg1b7uk t72 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t73), t72);
            ((uint8_t *)&t73)[4] = 1;
            gm_l4 = t73;
            goto gm_lb3;
        }
gm_lb9:;
        {
            gm_b16_8 t74 = gm_l1;
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s3cv29r83pgn4));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(5));
            uint8_t t76 = (gm_eq0iqiubfm7e(((uint8_t *)&t74), ((uint8_t *)&t75)));
            if (!t76) {
                goto gm_lb10;
            }
            void * t77 = gm_l0;
            void * t78 = (void *)(((uint8_t *)t77) + 8);
            gm_t88rcucg1b7uk t79 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t80), t79);
            ((uint8_t *)&t80)[4] = 1;
            gm_l4 = t80;
            goto gm_lb3;
        }
gm_lb10:;
        {
            gm_b16_8 t81 = gm_l1;
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s2737i1iq57nf));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(4));
            uint8_t t83 = (gm_eq0iqiubfm7e(((uint8_t *)&t81), ((uint8_t *)&t82)));
            if (!t83) {
                goto gm_lb11;
            }
            void * t84 = gm_l0;
            void * t85 = (void *)(((uint8_t *)t84) + 8);
            gm_t88rcucg1b7uk t86 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t87), t86);
            ((uint8_t *)&t87)[4] = 1;
            gm_l4 = t87;
            goto gm_lb3;
        }
gm_lb11:;
        {
            gm_b16_8 t88 = gm_l1;
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_slolcr6ikvg33));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(6));
            uint8_t t90 = (gm_eq0iqiubfm7e(((uint8_t *)&t88), ((uint8_t *)&t89)));
            if (!t90) {
                goto gm_lb12;
            }
            void * t91 = gm_l0;
            void * t92 = (void *)(((uint8_t *)t91) + 8);
            gm_t53at80pfdus3 t93;
            memset(&t93, 0, sizeof t93);
            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)7ULL));
            gm_t88rcucg1b7uk t94 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t92, &t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t95), t94);
            ((uint8_t *)&t95)[4] = 1;
            gm_l4 = t95;
            goto gm_lb3;
        }
gm_lb12:;
        {
            gm_b16_8 t96 = gm_l1;
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s0iqiubfm7e7e));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(6));
            uint8_t t98 = (gm_eq0iqiubfm7e(((uint8_t *)&t96), ((uint8_t *)&t97)));
            if (!t98) {
                goto gm_lb13;
            }
            void * t99 = gm_l0;
            void * t100 = (void *)(((uint8_t *)t99) + 8);
            gm_t88rcucg1b7uk t101 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t102), t101);
            ((uint8_t *)&t102)[4] = 1;
            gm_l4 = t102;
            goto gm_lb3;
        }
gm_lb13:;
        {
            gm_b16_8 t103 = gm_l1;
            gm_b16_8 t104;
            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_s47llp68ptjgu));
            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(7));
            uint8_t t105 = (gm_eq0iqiubfm7e(((uint8_t *)&t103), ((uint8_t *)&t104)));
            if (!t105) {
                goto gm_lb14;
            }
            void * t106 = gm_l0;
            void * t107 = (void *)(((uint8_t *)t106) + 8);
            void * t108 = gm_l0;
            void * t109 = (void *)(((uint8_t *)t108) + 8);
            gm_t88rcucg1b7uk t110 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t111 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t107, ((uint8_t)1), t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112), t111);
            ((uint8_t *)&t112)[4] = 1;
            gm_l4 = t112;
            goto gm_lb3;
        }
gm_lb14:;
        {
            gm_b16_8 t113 = gm_l1;
            gm_b16_8 t114;
            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sflee3fpg0btv));
            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(4));
            uint8_t t115 = (gm_eq0iqiubfm7e(((uint8_t *)&t113), ((uint8_t *)&t114)));
            if (!t115) {
                goto gm_lb15;
            }
            void * t116 = gm_l0;
            void * t117 = (void *)(((uint8_t *)t116) + 8);
            gm_t88rcucg1b7uk t118 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t117, ((uint8_t)0), ((uint16_t)32ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t119), t118);
            ((uint8_t *)&t119)[4] = 1;
            gm_l4 = t119;
            goto gm_lb3;
        }
gm_lb15:;
        {
            gm_tkn42u7na3t81 t120;
            memset(&t120, 0, sizeof t120);
            gm_l4 = t120;
            goto gm_lb3;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 31, 12);
    }
gm_lb3:;
    gm_tkn42u7na3t81 t121 = gm_l4;
    F.ret = t121;
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
extern __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2) {
    struct gm_fr_check_len_exprs_m8s6m0h F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* map */
    gm_toceffb697p0u gm_l2; /* at */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t2i8ne5ibd7v4 gm_l6; /* le */
    gm_tl874t2atnlh3 gm_l9; /* a */
    GM_STACK_CHECK(&F, 814u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_l4 = ((uint64_t)0ULL);
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 2312) + 8));
    gm_l3 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = gm_l3;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l4;
                        gm_l5 = t14;
                        void * t15 = gm_l0;
                        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 2312));
                        uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 2312) + 8));
                        uint64_t t18 = gm_l5;
                        if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcsffver18si54, 660, 15);
                        gm_t2i8ne5ibd7v4 t19 = GM_LD(gm_t2i8ne5ibd7v4, (((uint8_t *)t16) + (uintptr_t)t18 * 16));
                        gm_l6 = t19;
                        void * t20 = gm_l0;
                        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                        gm_b16_8 t22 = gm_l1;
                        uint8_t t23 = gm_f_len_params_bound_f7mjvn9(t20, GM_CONV(gm_csguankko4lmhd, t21), t22);
                        uint8_t t24 = (uint8_t)!t23;
                        if (t24) {
                            gm_l10 = ((uint8_t)1);
                        } else {
                            void * t25 = gm_l0;
                            void * t26 = (void *)(((uint8_t *)t25) + 8);
                            void * t27 = gm_l0;
                            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                            gm_b16_8 t29 = gm_l1;
                            gm_t88rcucg1b7uk t30 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t27, t28, t29);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t31 = gm_f_is_error_8k27gd1(t26, GM_CONV(gm_csguankko4lmhd, t30));
                            uint8_t t32 = (uint8_t)!t31;
                            gm_l10 = t32;
                        }
                        uint8_t t33 = gm_l10;
                        if (t33) {
                            goto gm_lb3;
                        }
                        gm_trqfvmjtt4sij t34;
                        gm_f_list_epvducg((void *)&t34, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l7 = t34;
                        void * t35 = gm_l0;
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                        gm_b16_8 t37 = gm_l1;
                        void * t38 = (void *)((uint8_t *)&F.l7);
                        gm_b0_1 t39 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t35, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
                        void * t41 = gm_l0;
                        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                        gm_b16_8 t43 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                        gm_b16_8 t46;
                        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(2));
                        gm_f_join_sfknljb((void *)&gm_l11, (void *)NULL, cx, NULL, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcsffver18si54, 664, 129);
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                        gm_t6j6smj7qoctu t49;
                        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t43);
                        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t48);
                        gm_f_format_rd9dflp((void *)&gm_l12, (void *)NULL, cx, NULL, &t49);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 664, 34);
                        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                        gm_t4gut2c9fj1kt t53;
                        gm_f_error_s6hn871((void *)&t53, (void *)NULL, cx, NULL, t40, t52);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l8 = t53;
                        {
                            {
                                uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l2))[12] != 0);
                                if (!t54) {
                                    goto gm_lb7;
                                }
                                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l2));
                                gm_l9 = t55;
                                void * t56 = (void *)((uint8_t *)&F.l8);
                                gm_tl874t2atnlh3 t57 = gm_l9;
                                gm_b16_8 t58;
                                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_stevggi5tvq7c));
                                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(17));
                                gm_f_add_label_50fj7vv(t56, GM_CONV(gm_cseahvg8v7p0vs, t57), t58);
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t59 = gm_l0;
                        gm_b0_1 t60 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t59, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 659, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t61 = gm_l4;
            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
            uint64_t t64 = r62;
            gm_l4 = t64;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_component_type_q9ge5lo F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* text */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l5;
    gm_t53at80pfdus3 gm_l6;
    gm_tabb710d9mq2e gm_l7;
    void * t5 = gm_l0;
    uint64_t t6 = gm_l1;
    void * t7 = gm_l2;
    gm_t88rcucg1b7uk t8 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t8;
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_t88rcucg1b7uk t11 = gm_l3;
    gm_f_kind_jcnt4hm((void *)&gm_l6, t10, GM_CONV(gm_csguankko4lmhd, t11));
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)15ULL));
            if (!t13) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 538, 8);
    }
gm_lb1:;
    uint8_t t14 = gm_l5;
    if (t14) {
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l3;
        gm_b16_8 t17 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t17;
        void * t18 = gm_l0;
        void * t19 = gm_l2;
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 72));
        gm_b16_8 t21 = gm_l4;
        gm_b16_8 t22 = gm_l4;
        gm_t6j6smj7qoctu t23;
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t22);
        gm_f_format_um7orcc((void *)&gm_l7, (void *)NULL, cx, NULL, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcsffver18si54, 540, 25);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
        gm_b0_1 t27 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t18, t20, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_t88rcucg1b7uk t28 = gm_l3;
    F.ret = t28;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_comptime_value_l5orc93 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* e */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint32_t gm_l5; /* pid */
    gm_t88rcucg1b7uk gm_l6; /* have */
    gm_t428ue2u3hf5g gm_l7; /* key */
    void * gm_l8; /* hook */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* cell */
    gm_t428ue2u3hf5g gm_l11; /* key */
    GM_STACK_CHECK(&F, 1311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_trgmlgt2gmmjs gm_l14;
    gm_t9805qtonbsag gm_l15;
    uint32_t gm_l16;
    void * gm_l17;
    gm_trgmlgt2gmmjs gm_l18;
    gm_t428ue2u3hf5g gm_l19;
    {
        {
            void * t14 = gm_l3;
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)6ULL));
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t14) + 8) + 8), 32);
            {
                {
                    void * t17 = gm_l0;
                    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_t9805qtonbsag t19 = gm_f_value_param_tof1h6t(t17, t18);
                    gm_l12 = t19;
                    uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
                    if (!t20) {
                        goto gm_lb4;
                    }
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                    gm_l5 = t21;
                    {
                        {
                            void * t22 = gm_l0;
                            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 1128));
                            uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t22) + 1128) + 8));
                            uint32_t t25 = gm_l5;
                            uint64_t t26 = (uint64_t)t25;
                            if ((uintptr_t)t26 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t26, t24, gm_srcsffver18si54, 792, 29);
                            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t23) + (uintptr_t)t26 * 48) + 0));
                            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)2ULL));
                            if (!t28) {
                                goto gm_lb6;
                            }
                            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t23) + (uintptr_t)t26 * 48) + 4));
                            gm_l6 = t29;
                            {
                                {
                                    gm_t88rcucg1b7uk t30 = gm_l6;
                                    gm_t88rcucg1b7uk t31 = gm_l2;
                                    uint8_t t32 = (!gm_eq9m6mvtmgde(((uint8_t *)&t30), ((uint8_t *)&t31)));
                                    if (!t32) {
                                        goto gm_lb8;
                                    }
                                    gm_t88rcucg1b7uk t33 = gm_l2;
                                    void * t34 = gm_l0;
                                    void * t35 = (void *)(((uint8_t *)t34) + 8);
                                    gm_t88rcucg1b7uk t36 = gm_l6;
                                    gm_t88rcucg1b7uk t37 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t35, t36);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t38 = (!gm_eq9m6mvtmgde(((uint8_t *)&t33), ((uint8_t *)&t37)));
                                    if (!t38) {
                                        goto gm_lb8;
                                    }
                                    void * t39 = gm_l0;
                                    void * t40 = gm_l3;
                                    gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                                    void * t42 = gm_l0;
                                    gm_t88rcucg1b7uk t43 = gm_l2;
                                    gm_b16_8 t44 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t42, t43);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                                    void * t46 = gm_l0;
                                    gm_t88rcucg1b7uk t47 = gm_l6;
                                    gm_b16_8 t48 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t46, t47);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tfeab279c16rr t49;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t44);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t45);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 32), t48);
                                    gm_f_format_c3isg16((void *)&gm_l13, (void *)NULL, cx, NULL, &t49);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                    if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcsffver18si54, 794, 33);
                                    gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                                    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                                    gm_b0_1 t53 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t39, t41, t52);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tkn42u7na3t81 t54;
                                    memset(&t54, 0, sizeof t54);
                                    F.ret = t54;
                                    goto gm_fret;
                                }
gm_lb8:;
                            }
gm_lb7:;
                            void * t55 = gm_l0;
                            void * t56 = (void *)(((uint8_t *)t55) + 8);
                            uint32_t t57 = gm_l5;
                            gm_t88rcucg1b7uk t58 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t56, t57);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tkn42u7na3t81 t59;
                            GM_ST(uint8_t, (uint8_t *)&t59 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t59 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t59), t58);
                            ((uint8_t *)&t59)[4] = 1;
                            F.ret = t59;
                            goto gm_fret;
                        }
gm_lb6:;
                        gm_tkn42u7na3t81 t60;
                        memset(&t60, 0, sizeof t60);
                        F.ret = t60;
                        goto gm_fret;
                    }
gm_lb5:;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t61 = gm_l0;
            uint64_t t62 = gm_l1;
            void * t63 = gm_l3;
            gm_t88rcucg1b7uk t64 = gm_l2;
            gm_f_const_key_of_q36m6nm((void *)&gm_l14, (void *)NULL, cx, NULL, t61, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l14))[32] != 0);
            if (!t65) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l14), 32);
            void * t66 = gm_l0;
            void * t67 = (void *)(((uint8_t *)t66) + 8);
            gm_t428ue2u3hf5g t68 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l7));
            gm_t53at80pfdus3 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t69 + 8), t68);
            gm_t88rcucg1b7uk t70 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t67, &t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t71), t70);
            ((uint8_t *)&t71)[4] = 1;
            F.ret = t71;
            goto gm_fret;
        }
gm_lb10:;
    }
gm_lb9:;
    {
        {
            void * t72 = gm_l0;
            uint8_t t73 = (uint8_t)(GM_LD(void *, (((uint8_t *)t72) + 2288)) != 0);
            if (!t73) {
                goto gm_lb12;
            }
            void * t74 = GM_LD(void *, (((uint8_t *)t72) + 2288));
            gm_l8 = t74;
            void * t75 = gm_l0;
            void * t76 = (void *)(((uint8_t *)t75) + 8);
            gm_t88rcucg1b7uk t77 = gm_l2;
            uint8_t t78 = gm_f_has_param_u9t0efs(t76, GM_CONV(gm_csguankko4lmhd, t77));
            uint8_t t79 = (uint8_t)!t78;
            if (!t79) {
                goto gm_lb12;
            }
            void * t80 = gm_l8;
            void * t81 = gm_l0;
            uint64_t t82 = gm_l1;
            void * t83 = gm_l3;
            gm_t88rcucg1b7uk t84 = gm_l2;
            gm_tkn42u7na3t81 t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t85), t84);
            ((uint8_t *)&t85)[4] = 1;
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s33hvuvcdjs1b));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(10));
            void * t87 = gm_l3;
            gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
            void * t89 = t80;
            gm_t9805qtonbsag t90;
            if (GM_PTR_TAGGED(t89)) {
                t90 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t89))(t81, t82, t83, t85, t86, GM_CONV(gm_cseahvg8v7p0vs, t88));
            } else {
                t90 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t89)(NULL, cx, NULL, t81, t82, t83, t85, t86, t88);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t90;
            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l15))[4] != 0);
            if (t91) {
                uint32_t t92 = GM_LD(uint32_t, ((uint8_t *)&gm_l15));
                gm_l16 = t92;
            } else {
                gm_tkn42u7na3t81 t93;
                memset(&t93, 0, sizeof t93);
                F.ret = t93;
                goto gm_fret;
            }
            uint32_t t94 = gm_l16;
            gm_l9 = t94;
            void * t95 = gm_l0;
            void * t96 = GM_LD(void *, (((uint8_t *)t95) + 2088));
            uint64_t t97 = GM_LD(uint64_t, ((((uint8_t *)t95) + 2088) + 8));
            uint32_t t98 = gm_l9;
            uint64_t t99 = (uint64_t)t98;
            if ((uintptr_t)t99 >= (uintptr_t)(t97)) gm_trap_index((uintptr_t)t99, t97, gm_srcsffver18si54, 803, 17);
            uint8_t t100 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t96) + (uintptr_t)t99 * 88) + 80)) != 0);
            if (t100) {
                void * t101 = GM_LD(void *, ((((uint8_t *)t96) + (uintptr_t)t99 * 88) + 80));
                gm_l17 = t101;
            } else {
                gm_tkn42u7na3t81 t102;
                memset(&t102, 0, sizeof t102);
                F.ret = t102;
                goto gm_fret;
            }
            void * t103 = gm_l17;
            gm_l10 = t103;
            void * t104 = gm_l0;
            void * t105 = gm_l10;
            gm_t88rcucg1b7uk t106 = gm_l2;
            void * t107 = gm_l3;
            gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 72));
            gm_t6evimt84lndq t109 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t105));
            gm_f_value_key_vki574i((void *)&gm_l18, (void *)NULL, cx, NULL, t104, &t109, t106, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t110 = (uint8_t)((((uint8_t *)&gm_l18))[32] != 0);
            if (t110) {
                memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 32);
            } else {
                gm_tkn42u7na3t81 t111;
                memset(&t111, 0, sizeof t111);
                F.ret = t111;
                goto gm_fret;
            }
            memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l19), 32);
            void * t112 = gm_l0;
            void * t113 = (void *)(((uint8_t *)t112) + 8);
            gm_t428ue2u3hf5g t114 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l11));
            gm_t53at80pfdus3 t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t115 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t115 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t115 + 8), t114);
            gm_t88rcucg1b7uk t116 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t113, &t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t117;
            GM_ST(uint8_t, (uint8_t *)&t117 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t117 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t117), t116);
            ((uint8_t *)&t117)[4] = 1;
            F.ret = t117;
            goto gm_fret;
        }
gm_lb12:;
    }
gm_lb11:;
    void * t118 = gm_l0;
    void * t119 = gm_l3;
    gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t119) + 72));
    gm_b16_8 t121;
    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sm7179tmr0nhi));
    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(26));
    gm_b0_1 t122 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t118, t120, t121);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tkn42u7na3t81 t123;
    memset(&t123, 0, sizeof t123);
    F.ret = t123;
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
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_const_key_of_q36m6nm F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    uint32_t gm_l5; /* f */
    void * gm_l6; /* x */
    void * gm_l7; /* inner */
    gm_tibupt4dmcqai gm_l8; /* lit */
    uint8_t gm_l12; /* b */
    GM_STACK_CHECK(&F, 1961u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l14;
    gm_t9805qtonbsag gm_l15;
    gm_tkv1vap90vht2 gm_l16;
    gm_t5vbkdq81qfs7 gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    gm_tftb1odc739t5 gm_l20;
    void * t17 = gm_l2;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
    uint8_t t19 = (uint8_t)((t18 == ((uint8_t)4ULL)));
    gm_t2gtbh39ptekd t20 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t17) + 8));
    gm_t2gtbh39ptekd t21;
    memset(&t21, 0, sizeof t21);
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)4ULL));
    if (t19) {
        {
            {
                void * t22 = gm_l0;
                void * t23 = (void *)(((uint8_t *)t22) + 8);
                gm_t88rcucg1b7uk t24 = gm_l3;
                gm_f_kind_jcnt4hm((void *)&gm_l14, t23, GM_CONV(gm_csguankko4lmhd, t24));
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                uint8_t t26 = (uint8_t)(t25 == ((uint8_t)13ULL));
                if (!t26) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 8));
                gm_l4 = t27;
                void * t28 = gm_l0;
                gm_t88rcucg1b7uk t29 = gm_l4;
                uint8_t t30 = gm_f_is_nullable_pointer_nkie06s(t28, GM_CONV(gm_csguankko4lmhd, t29));
                if (!t30) {
                    goto gm_lb2;
                }
                gm_t428ue2u3hf5g t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t31 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)2ULL));
                gm_trgmlgt2gmmjs t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t32), t31);
                ((uint8_t *)&t32)[32] = 1;
                memmove(gm_out, ((uint8_t *)&t32), 40);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_trgmlgt2gmmjs t33;
        memset(&t33, 0, sizeof t33);
        memmove(gm_out, ((uint8_t *)&t33), 40);
        goto gm_fdone;
    }
    {
        {
            void * t34 = gm_l0;
            uint64_t t35 = gm_l1;
            void * t36 = gm_l2;
            gm_t88rcucg1b7uk t37 = gm_l3;
            gm_t9805qtonbsag t38 = gm_f_fn_ref_7alnaqh((void *)NULL, cx, NULL, t34, t35, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t38;
            uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l15))[4] != 0);
            if (!t39) {
                goto gm_lb4;
            }
            uint32_t t40 = GM_LD(uint32_t, ((uint8_t *)&gm_l15));
            gm_l5 = t40;
            uint32_t t41 = gm_l5;
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 344));
            uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)t42) + 344) + 8));
            uint32_t t45 = gm_l5;
            uint64_t t46 = (uint64_t)t45;
            if ((uintptr_t)t46 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t46, t44, gm_srcsffver18si54, 818, 53);
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t43) + (uintptr_t)t46 * 136));
            gm_tiilmfd16v3fc t48;
            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t41);
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 8), t47);
            gm_t428ue2u3hf5g t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t49 + 8), t48);
            gm_trgmlgt2gmmjs t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 36, 0);
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t50), t49);
            ((uint8_t *)&t50)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t50), 40);
            goto gm_fdone;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t51 = gm_l0;
    void * t52 = (void *)(((uint8_t *)t51) + 8);
    gm_t88rcucg1b7uk t53 = gm_l3;
    uint8_t t54 = gm_f_has_param_u9t0efs(t52, GM_CONV(gm_csguankko4lmhd, t53));
    if (t54) {
        gm_trgmlgt2gmmjs t55;
        memset(&t55, 0, sizeof t55);
        memmove(gm_out, ((uint8_t *)&t55), 40);
        goto gm_fdone;
    }
    void * t56 = gm_l2;
    gm_l6 = t56;
    {
        for (;;) {
            void * t57 = gm_l6;
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)9ULL));
            if (!t59) {
                goto gm_lb5;
            }
            void * t60 = GM_LD(void *, ((((uint8_t *)t57) + 8) + 8));
            gm_l7 = t60;
            {
                void * t61 = gm_l7;
                gm_l6 = t61;
            }
gm_lb7:;
        }
    }
gm_lb5:;
    {
        {
            void * t62 = gm_l6;
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t62) + 8) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)2ULL));
            if (!t64) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t62) + 8) + 8), 24);
            gm_t9qkh0rsooc8d t65 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l8) + 16));
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
            uint8_t t67 = (uint8_t)(!(t66 == ((uint8_t)2ULL)));
            if (!t67) {
                goto gm_lb9;
            }
            gm_t88rcucg1b7uk t68 = gm_l3;
            void * t69 = gm_l0;
            void * t70 = (void *)(((uint8_t *)t69) + 8);
            gm_t9qkh0rsooc8d t71;
            GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t72 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t73 = (gm_eq9m6mvtmgde(((uint8_t *)&t68), ((uint8_t *)&t72)));
            if (!t73) {
                goto gm_lb9;
            }
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            gm_t428ue2u3hf5g t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)3ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 8), t75);
            gm_trgmlgt2gmmjs t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 36, 0);
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t77), t76);
            ((uint8_t *)&t77)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t77), 40);
            goto gm_fdone;
        }
gm_lb9:;
    }
gm_lb8:;
    void * t78 = gm_l0;
    uint64_t t79 = gm_l1;
    void * t80 = gm_l2;
    gm_f_untyped_if_plain_lbrp24p((void *)&gm_l16, (void *)NULL, cx, NULL, t78, t79, t80);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t81 = (uint8_t)((((uint8_t *)&gm_l16))[208] != 0);
    if (t81) {
        memmove(((uint8_t *)&gm_l17), ((uint8_t *)&gm_l16), 208);
    } else {
        gm_trgmlgt2gmmjs t82;
        memset(&t82, 0, sizeof t82);
        memmove(gm_out, ((uint8_t *)&t82), 40);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l17), 208);
    {
        {
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)0ULL));
            if (!t84) {
                goto gm_lb11;
            }
            memmove(((uint8_t *)&F.l10), (((uint8_t *)&F.l9) + 8), 200);
            {
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)0ULL));
                    if (!t86) {
                        goto gm_lb13;
                    }
                    memmove(((uint8_t *)&F.l11), (((uint8_t *)&F.l10) + 8), 48);
                    void * t87 = gm_l0;
                    void * t88 = (void *)(((uint8_t *)t87) + 8);
                    gm_t88rcucg1b7uk t89 = gm_l3;
                    uint8_t t90 = gm_f_is_integer_8d4f9ek(t88, GM_CONV(gm_csguankko4lmhd, t89));
                    uint8_t t91 = (uint8_t)!t90;
                    if (t91) {
                        gm_l18 = ((uint8_t)1);
                    } else {
                        void * t92 = gm_l0;
                        gm_t88rcucg1b7uk t93 = gm_l3;
                        uint8_t t94 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t92, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), t93);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t95 = (uint8_t)!t94;
                        gm_l18 = t95;
                    }
                    uint8_t t96 = gm_l18;
                    if (t96) {
                        gm_trgmlgt2gmmjs t97;
                        memset(&t97, 0, sizeof t97);
                        memmove(gm_out, ((uint8_t *)&t97), 40);
                        goto gm_fdone;
                    }
                    void * t98 = gm_l0;
                    gm_t88rcucg1b7uk t99 = gm_l3;
                    gm_t428ue2u3hf5g t100;
                    gm_f_int_key_dmcraup((void *)&t100, (void *)NULL, cx, NULL, t98, t99, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_trgmlgt2gmmjs t101;
                    GM_ST(uint8_t, (uint8_t *)&t101 + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&t101 + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&t101 + 36, 0);
                    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t101), t100);
                    ((uint8_t *)&t101)[32] = 1;
                    memmove(gm_out, ((uint8_t *)&t101), 40);
                    goto gm_fdone;
                }
gm_lb13:;
                {
                    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                    uint8_t t103 = (uint8_t)(t102 == ((uint8_t)3ULL));
                    if (!t103) {
                        goto gm_lb14;
                    }
                    uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 8));
                    gm_l12 = t104;
                    void * t105 = gm_l0;
                    void * t106 = (void *)(((uint8_t *)t105) + 8);
                    gm_t88rcucg1b7uk t107 = gm_l3;
                    gm_t53at80pfdus3 t108;
                    gm_f_kind_jcnt4hm((void *)&t108, t106, GM_CONV(gm_csguankko4lmhd, t107));
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&t108) + 0));
                    uint8_t t110 = (uint8_t)(!(t109 == ((uint8_t)4ULL)));
                    gm_t53at80pfdus3 t111;
                    memset(&t111, 0, sizeof t111);
                    GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)4ULL));
                    if (t110) {
                        gm_trgmlgt2gmmjs t112;
                        memset(&t112, 0, sizeof t112);
                        memmove(gm_out, ((uint8_t *)&t112), 40);
                        goto gm_fdone;
                    }
                    gm_trqfvmjtt4sij t113;
                    gm_f_list_uvfu1k1((void *)&t113, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l13 = t113;
                    void * t114 = (void *)((uint8_t *)&F.l13);
                    uint8_t t115 = gm_l12;
                    if (t115) {
                        gm_l19 = ((uint8_t)1ULL);
                    } else {
                        gm_l19 = ((uint8_t)0ULL);
                    }
                    uint8_t t116 = gm_l19;
                    gm_tftb1odc739t5 t117 = gm_f_push_t83rg2m(t114, t116);
                    gm_l20 = t117;
                    uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcsffver18si54, 834, 13);
                    gm_t88rcucg1b7uk t119 = gm_l3;
                    gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                    gm_tcmkompbl09il t122;
                    GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t122 + 0), t119);
                    GM_ST(gm_b16_8, ((uint8_t *)&t122 + 8), t121);
                    gm_t428ue2u3hf5g t123;
                    GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t123 + 8), t122);
                    gm_trgmlgt2gmmjs t124;
                    GM_ST(uint8_t, (uint8_t *)&t124 + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&t124 + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&t124 + 36, 0);
                    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t124), t123);
                    ((uint8_t *)&t124)[32] = 1;
                    memmove(gm_out, ((uint8_t *)&t124), 40);
                    goto gm_fdone;
                }
gm_lb14:;
                {
                    gm_trgmlgt2gmmjs t125;
                    memset(&t125, 0, sizeof t125);
                    memmove(gm_out, ((uint8_t *)&t125), 40);
                    goto gm_fdone;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 826, 5);
            }
gm_lb12:;
        }
gm_lb11:;
        gm_trgmlgt2gmmjs t126;
        memset(&t126, 0, sizeof t126);
        memmove(gm_out, ((uint8_t *)&t126), 40);
        goto gm_fdone;
    }
gm_lb10:;
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
inline __attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_construction_call_ktqtrpo F;
    void * gm_l0; /* e */
    void * gm_l1; /* callee */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    void * gm_l6; /* inner */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l7;
    void * t12 = gm_l0;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)19ULL));
            if (!t14) {
                goto gm_lb2;
            }
            void * t15 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
            gm_l1 = t15;
            void * t16 = gm_l1;
            uint8_t t17 = gm_f_calls_on_value_q0pa6h5(t16);
            if (t17) {
                gm_l7 = ((void *)0);
            } else {
                void * t18 = gm_l0;
                gm_l7 = t18;
            }
            void * t19 = gm_l7;
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)28ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_t3mf58gv9kf3n t22 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t12) + 8) + 8));
            gm_l2 = t22;
            void * t23 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
            void * t24 = gm_f_construction_call_ktqtrpo(t23);
            F.ret = t24;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)24ULL));
            if (!t26) {
                goto gm_lb4;
            }
            void * t27 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
            gm_l3 = t27;
            void * t28 = gm_l3;
            void * t29 = gm_f_construction_call_ktqtrpo(t28);
            F.ret = t29;
            goto gm_fret;
        }
gm_lb4:;
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)25ULL));
                    if (!t31) {
                        goto gm_lb7;
                    }
                    void * t32 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
                    gm_l4 = t32;
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)26ULL));
                    if (!t34) {
                        goto gm_lb8;
                    }
                    void * t35 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
                    gm_l4 = t35;
                    goto gm_lb6;
                }
gm_lb8:;
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)27ULL));
                    if (!t37) {
                        goto gm_lb9;
                    }
                    void * t38 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
                    gm_l4 = t38;
                    goto gm_lb6;
                }
gm_lb9:;
                goto gm_lb5;
            }
gm_lb6:;
            void * t39 = gm_l4;
            void * t40 = gm_f_construction_call_ktqtrpo(t39);
            F.ret = t40;
            goto gm_fret;
        }
gm_lb5:;
        {
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 258, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_decl_is_type_oh0cpn9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint32_t gm_l2; /* depth */
    void * gm_l4; /* decl */
    GM_STACK_CHECK(&F, 384u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tagrkbqh054h7 gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    {
        {
            void * t13 = gm_l0;
            uint64_t t14 = gm_l1;
            gm_f_state_a5bnsjb((void *)&gm_l6, (void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l6) + 8), 208);
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
                    if (!t20) {
                        goto gm_lb5;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 144, 40);
            }
gm_lb3:;
            uint8_t t21 = gm_l7;
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    uint32_t t22 = gm_l2;
    uint8_t t23 = (t22 > ((uint32_t)16ULL));
    if (t23) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    void * t26 = GM_LD(void *, (((uint8_t *)t25) + 240));
    uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t25) + 240) + 8));
    uint64_t t28 = gm_l1;
    if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_srcsffver18si54, 146, 14);
    void * t29 = (void *)(((uint8_t *)t26) + (uintptr_t)t28 * 216);
    gm_l4 = t29;
    {
        {
            void * t30 = gm_l4;
            uint8_t t31 = GM_LD(uint8_t, (((((uint8_t *)t30) + 40) + 64) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
            if (!t32) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&F.l5), (((((uint8_t *)t30) + 40) + 64) + 8), 64);
            {
                {
                    uint8_t t33 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 24)) != 0);
                    if (t33) {
                        goto gm_lb10;
                    }
                    gm_l9 = ((uint8_t)0);
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb9;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 148, 8);
            }
gm_lb9:;
            uint8_t t34 = gm_l9;
            if (t34) {
                gm_l8 = ((uint8_t)1);
            } else {
                uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 8) + 8));
                uint8_t t36 = (!(t35 == ((uint64_t)1ULL)));
                gm_l8 = t36;
            }
            uint8_t t37 = gm_l8;
            if (t37) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t38 = gm_l0;
            void * t39 = gm_l4;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 32));
            void * t41 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            uint32_t t42 = gm_l2;
            uint8_t t43 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t38, t40, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t43;
            goto gm_fret;
        }
gm_lb8:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb7:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_enum_array_type_0sic6t0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* index */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* u */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 920u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_t53at80pfdus3 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tdlpv0q69lov1 gm_l13;
    gm_tep2drsl9408b gm_l14;
    gm_tabb710d9mq2e gm_l15;
    uint8_t gm_l16;
    gm_tabb710d9mq2e gm_l17;
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 8);
    gm_l4 = t14;
    void * t15 = gm_l4;
    gm_t88rcucg1b7uk t16 = gm_l1;
    uint8_t t17 = gm_f_is_error_8k27gd1(t15, GM_CONV(gm_csguankko4lmhd, t16));
    if (t17) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t18 = gm_l4;
        gm_t88rcucg1b7uk t19 = gm_l2;
        uint8_t t20 = gm_f_is_error_8k27gd1(t18, GM_CONV(gm_csguankko4lmhd, t19));
        gm_l9 = t20;
    }
    uint8_t t21 = gm_l9;
    if (t21) {
        void * t22 = gm_l4;
        gm_t88rcucg1b7uk t23 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t23;
        goto gm_fret;
    }
    void * t24 = gm_l4;
    gm_t88rcucg1b7uk t25 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l11, t24, GM_CONV(gm_csguankko4lmhd, t25));
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)19ULL));
            if (!t27) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l10 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 595, 8);
    }
gm_lb1:;
    uint8_t t28 = gm_l10;
    if (t28) {
        void * t29 = gm_l0;
        gm_tl874t2atnlh3 t30 = gm_l3;
        void * t31 = gm_l0;
        gm_t88rcucg1b7uk t32 = gm_l1;
        gm_b16_8 t33 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t34;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
        gm_f_format_o2s8o9q((void *)&gm_l12, (void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 596, 23);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_b0_1 t38 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t29, t30, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t39 = gm_l4;
        gm_t88rcucg1b7uk t40 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t40;
        goto gm_fret;
    }
    void * t41 = gm_l0;
    gm_t88rcucg1b7uk t42 = gm_l1;
    gm_f_union_variants_a7khaqp((void *)&gm_l13, t41, GM_CONV(gm_csguankko4lmhd, t42));
    uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
    if (t43) {
        memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l13), 24);
    } else {
        void * t44 = gm_l0;
        gm_tl874t2atnlh3 t45 = gm_l3;
        void * t46 = gm_l0;
        gm_t88rcucg1b7uk t47 = gm_l1;
        gm_b16_8 t48 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t49;
        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
        gm_f_format_qmamljh((void *)&gm_l15, (void *)NULL, cx, NULL, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcsffver18si54, 600, 23);
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        gm_b0_1 t53 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t44, t45, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t54 = gm_l4;
        gm_t88rcucg1b7uk t55 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t55;
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l14), 24);
    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
    gm_l6 = t56;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t57 = gm_l7;
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t59 = (t57 < t58);
            if (!t59) {
                goto gm_lb4;
            }
            {
                void * t60 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t62 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t60) + (uintptr_t)t62 * 72), 72);
                        {
                            {
                                uint8_t t63 = (uint8_t)(((((uint8_t *)&F.l8) + 16))[4] != 0);
                                if (t63) {
                                    goto gm_lb10;
                                }
                                gm_l16 = ((uint8_t)0);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                gm_l16 = ((uint8_t)1);
                                goto gm_lb9;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 604, 12);
                        }
gm_lb9:;
                        uint8_t t64 = gm_l16;
                        if (t64) {
                            void * t65 = gm_l0;
                            gm_tl874t2atnlh3 t66 = gm_l3;
                            void * t67 = gm_l0;
                            gm_t88rcucg1b7uk t68 = gm_l1;
                            gm_b16_8 t69 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t67, t68);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                            gm_t6j6smj7qoctu t71;
                            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t69);
                            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t70);
                            gm_f_format_il5kbp4((void *)&gm_l17, (void *)NULL, cx, NULL, &t71);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                            if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcsffver18si54, 605, 27);
                            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                            gm_b0_1 t75 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t65, t66, t74);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t76 = gm_l4;
                            gm_t88rcucg1b7uk t77 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t76);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t77;
                            goto gm_fret;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 603, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t78 = gm_l7;
            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
            uint64_t t81 = r79;
            gm_l7 = t81;
        }
    }
gm_lb4:;
    void * t82 = gm_l4;
    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t84 = (uint64_t)t83;
    gm_t88rcucg1b7uk t85 = gm_l2;
    gm_t88rcucg1b7uk t86 = gm_l1;
    gm_tnnjeerolos0a t87;
    GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t87 + 4), t86);
    gm_t8nckeqfenl8a t88;
    GM_ST(uint32_t, (uint8_t *)&t88 + 20, 0);
    GM_ST(uint64_t, ((uint8_t *)&t88 + 0), t84);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t88 + 8), t85);
    GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t88 + 12), t87);
    gm_t53at80pfdus3 t89;
    GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t89 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t89 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t89 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)11ULL));
    GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t89 + 8), t88);
    gm_t88rcucg1b7uk t90 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t82, &t89);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t90;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_fn_ref_7alnaqh F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* want */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint32_t gm_l10; /* g */
    uint32_t gm_l11; /* g */
    void * gm_l12; /* base */
    gm_t08olq97rscn3 gm_l13; /* name */
    uint64_t gm_l14; /* m */
    uint32_t gm_l15; /* g */
    void * gm_l16; /* info */
    GM_STACK_CHECK(&F, 1580u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l18;
    gm_t53at80pfdus3 gm_l19;
    gm_t9805qtonbsag gm_l20;
    gm_tf5srh4mp4b3i gm_l21;
    uint64_t gm_l22;
    gm_t6qooom8qa2do gm_l23;
    gm_tf5srh4mp4b3i gm_l24;
    uint64_t gm_l25;
    gm_tf5srh4mp4b3i gm_l26;
    uint64_t gm_l27;
    gm_t6qooom8qa2do gm_l28;
    gm_t88rcucg1b7uk t16 = gm_l3;
    gm_l4 = t16;
    {
        {
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 8);
            gm_t88rcucg1b7uk t19 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l18, t18, GM_CONV(gm_csguankko4lmhd, t19));
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)13ULL));
            if (!t21) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 8));
            gm_l5 = t22;
            gm_t88rcucg1b7uk t23 = gm_l5;
            gm_l4 = t23;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t24 = gm_l0;
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            gm_t88rcucg1b7uk t26 = gm_l4;
            gm_f_kind_jcnt4hm((void *)&gm_l19, t25, GM_CONV(gm_csguankko4lmhd, t26));
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)16ULL));
            if (!t28) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l19) + 8), 48);
            gm_l7 = ((uint64_t)0ULL);
            gm_l8 = ((uint32_t)0ULL);
            void * t29 = gm_l2;
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)6ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t29) + 8) + 8), 32);
                    {
                        {
                            void * t32 = gm_l0;
                            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                            gm_t9805qtonbsag t34 = gm_f_local_fn_aji8qnd(t32, t33);
                            gm_l20 = t34;
                            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l20))[4] != 0);
                            if (!t35) {
                                goto gm_lb8;
                            }
                            uint32_t t36 = GM_LD(uint32_t, ((uint8_t *)&gm_l20));
                            gm_l10 = t36;
                            uint32_t t37 = gm_l10;
                            gm_l8 = t37;
                            goto gm_lb7;
                        }
gm_lb8:;
                        void * t38 = gm_l0;
                        uint64_t t39 = gm_l1;
                        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                        gm_tf5srh4mp4b3i t41 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t38, t39, t40);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l21 = t41;
                        uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l21))[8] != 0);
                        if (t42) {
                            uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                            gm_l22 = t43;
                        } else {
                            gm_t9805qtonbsag t44;
                            memset(&t44, 0, sizeof t44);
                            F.ret = t44;
                            goto gm_fret;
                        }
                        uint64_t t45 = gm_l22;
                        gm_l7 = t45;
                        {
                            {
                                void * t46 = gm_l0;
                                uint64_t t47 = gm_l7;
                                gm_f_resolve_decl_pus042v((void *)&gm_l23, (void *)NULL, cx, NULL, t46, t47);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                                uint8_t t49 = (uint8_t)(t48 == ((uint8_t)5ULL));
                                if (!t49) {
                                    goto gm_lb10;
                                }
                                uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 8));
                                gm_l11 = t50;
                                uint32_t t51 = gm_l11;
                                gm_l8 = t51;
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_t9805qtonbsag t52;
                            memset(&t52, 0, sizeof t52);
                            F.ret = t52;
                            goto gm_fret;
                        }
gm_lb9:;
                    }
gm_lb7:;
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)22ULL));
                    if (!t54) {
                        goto gm_lb11;
                    }
                    void * t55 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
                    gm_l12 = t55;
                    memmove(((uint8_t *)&gm_l13), (((((uint8_t *)t29) + 8) + 8) + 8), 32);
                    void * t56 = gm_l0;
                    uint64_t t57 = gm_l1;
                    void * t58 = gm_l12;
                    gm_tf5srh4mp4b3i t59 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t56, t57, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t59;
                    uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l24))[8] != 0);
                    if (t60) {
                        uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
                        gm_l25 = t61;
                    } else {
                        gm_t9805qtonbsag t62;
                        memset(&t62, 0, sizeof t62);
                        F.ret = t62;
                        goto gm_fret;
                    }
                    uint64_t t63 = gm_l25;
                    gm_l14 = t63;
                    void * t64 = gm_l0;
                    void * t65 = GM_LD(void *, ((uint8_t *)t64));
                    uint64_t t66 = gm_l14;
                    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                    gm_tf5srh4mp4b3i t68 = gm_f_lookup_59juqoo(t65, t66, t67);
                    gm_l26 = t68;
                    uint8_t t69 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
                    if (t69) {
                        uint64_t t70 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                        gm_l27 = t70;
                    } else {
                        gm_t9805qtonbsag t71;
                        memset(&t71, 0, sizeof t71);
                        F.ret = t71;
                        goto gm_fret;
                    }
                    uint64_t t72 = gm_l27;
                    gm_l7 = t72;
                    void * t73 = gm_l0;
                    uint64_t t74 = gm_l7;
                    uint64_t t75 = gm_l1;
                    uint8_t t76 = gm_f_decl_visible_bd168ti(t73, t74, t75);
                    uint8_t t77 = (uint8_t)!t76;
                    if (t77) {
                        gm_t9805qtonbsag t78;
                        memset(&t78, 0, sizeof t78);
                        F.ret = t78;
                        goto gm_fret;
                    }
                    {
                        {
                            void * t79 = gm_l0;
                            uint64_t t80 = gm_l7;
                            gm_f_resolve_decl_pus042v((void *)&gm_l28, (void *)NULL, cx, NULL, t79, t80);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t82 = (uint8_t)(t81 == ((uint8_t)5ULL));
                            if (!t82) {
                                goto gm_lb13;
                            }
                            uint32_t t83 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 8));
                            gm_l15 = t83;
                            uint32_t t84 = gm_l15;
                            gm_l8 = t84;
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_t9805qtonbsag t85;
                        memset(&t85, 0, sizeof t85);
                        F.ret = t85;
                        goto gm_fret;
                    }
gm_lb12:;
                    goto gm_lb5;
                }
gm_lb11:;
                {
                    gm_t9805qtonbsag t86;
                    memset(&t86, 0, sizeof t86);
                    F.ret = t86;
                    goto gm_fret;
                }
gm_lb14:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 849, 5);
            }
gm_lb5:;
            void * t87 = gm_l0;
            void * t88 = GM_LD(void *, (((uint8_t *)t87) + 344));
            uint64_t t89 = GM_LD(uint64_t, ((((uint8_t *)t87) + 344) + 8));
            uint32_t t90 = gm_l8;
            uint64_t t91 = (uint64_t)t90;
            if ((uintptr_t)t91 >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)t91, t89, gm_srcsffver18si54, 868, 14);
            void * t92 = (void *)(((uint8_t *)t88) + (uintptr_t)t91 * 136);
            gm_l16 = t92;
            void * t93 = gm_l16;
            uint64_t t94 = GM_LD(uint64_t, ((((uint8_t *)t93) + 80) + 8));
            uint8_t t95 = (t94 > ((uint64_t)0ULL));
            if (t95) {
                gm_t9805qtonbsag t96;
                memset(&t96, 0, sizeof t96);
                F.ret = t96;
                goto gm_fret;
            }
            void * t97 = gm_l0;
            void * t98 = gm_l16;
            gm_f_fn_ty_qaih7ts((void *)&F.l17, (void *)NULL, cx, NULL, t97, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t99 = gm_l0;
            void * t100 = (void *)(((uint8_t *)t99) + 8);
            gm_tddtjfggdat0d t101 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l17));
            gm_t53at80pfdus3 t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t102 + 8), t101);
            gm_t88rcucg1b7uk t103 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t100, &t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t104 = gm_l0;
            void * t105 = (void *)(((uint8_t *)t104) + 8);
            gm_tddtjfggdat0d t106 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l6));
            gm_t53at80pfdus3 t107;
            GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t107 + 8), t106);
            gm_t88rcucg1b7uk t108 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t105, &t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t109 = (!gm_eq9m6mvtmgde(((uint8_t *)&t103), ((uint8_t *)&t108)));
            if (t109) {
                gm_t9805qtonbsag t110;
                memset(&t110, 0, sizeof t110);
                F.ret = t110;
                goto gm_fret;
            }
            uint32_t t111 = gm_l8;
            gm_t9805qtonbsag t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t112), t111);
            ((uint8_t *)&t112)[4] = 1;
            F.ret = t112;
            goto gm_fret;
        }
gm_lb4:;
        gm_t9805qtonbsag t113;
        memset(&t113, 0, sizeof t113);
        F.ret = t113;
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
inline __attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_ty_qaih7ts F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_trqfvmjtt4sij t7;
    gm_f_list_nof7vfj((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    void * t8 = gm_l1;
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 40));
    gm_l3 = t9;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l4;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t15 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t13) + (uintptr_t)t15 * 56), 56);
                        void * t16 = (void *)((uint8_t *)&F.l2);
                        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        gm_tftb1odc739t5 t18 = gm_f_push_k3l9nic(t16, GM_CONV(gm_csguankko4lmhd, t17));
                        gm_l6 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcsffver18si54, 530, 25);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 530, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l4;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l4 = t23;
        }
    }
gm_lb1:;
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
    void * t26 = gm_l1;
    gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 56));
    void * t28 = gm_l1;
    gm_tnpifv20irnph t29 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t28) + 64));
    void * t30 = gm_l1;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t30) + 128));
    gm_tddtjfggdat0d t32;
    GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
    GM_ST(uint8_t, (uint8_t *)&t32 + 41, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 42, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 44, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t25);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32 + 16), t27);
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t32 + 24), t29);
    GM_ST(uint8_t, ((uint8_t *)&t32 + 40), t31);
    memmove(gm_out, ((uint8_t *)&t32), 48);
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
__attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3) {
    struct gm_fr_generic_decl_oir9hm6 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* callee */
    uint8_t gm_l3; /* report */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l9; /* d */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 968u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l11;
    uint64_t gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    uint64_t gm_l14;
    gm_tf5srh4mp4b3i gm_l15;
    uint64_t gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    uint64_t gm_l18;
    void * t6 = gm_l2;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)6ULL));
            if (!t8) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t6) + 8) + 8), 32);
            void * t9 = gm_l0;
            uint64_t t10 = gm_l1;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_tf5srh4mp4b3i t12 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t12;
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
            if (t13) {
                uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                gm_l12 = t14;
            } else {
                gm_toblvhepnjqkh t15;
                memset(&t15, 0, sizeof t15);
                memmove(gm_out, ((uint8_t *)&t15), 32);
                goto gm_fdone;
            }
            uint64_t t16 = gm_l12;
            gm_l5 = t16;
            uint64_t t17 = gm_l5;
            gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
            gm_tkb8hpcqf70t1 t19;
            GM_ST(uint32_t, (uint8_t *)&t19 + 20, 0);
            GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t17);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t19 + 8), t18);
            gm_toblvhepnjqkh t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 28, 0);
            GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&t20), t19);
            ((uint8_t *)&t20)[24] = 1;
            memmove(gm_out, ((uint8_t *)&t20), 32);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)22ULL));
            if (!t22) {
                goto gm_lb3;
            }
            void * t23 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l6 = t23;
            memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t6) + 8) + 8) + 8), 32);
            void * t24 = gm_l0;
            uint64_t t25 = gm_l1;
            void * t26 = gm_l6;
            gm_tf5srh4mp4b3i t27 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t24, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t27;
            uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
            if (t28) {
                uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                gm_l14 = t29;
            } else {
                gm_toblvhepnjqkh t30;
                memset(&t30, 0, sizeof t30);
                memmove(gm_out, ((uint8_t *)&t30), 32);
                goto gm_fdone;
            }
            uint64_t t31 = gm_l14;
            gm_l8 = t31;
            uint8_t t32 = gm_l3;
            if (t32) {
                void * t33 = gm_l0;
                uint64_t t34 = gm_l8;
                uint64_t t35 = gm_l1;
                gm_tf5srh4mp4b3i t36 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t33, t34, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t36;
                uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                if (t37) {
                    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                    gm_l16 = t38;
                } else {
                    gm_toblvhepnjqkh t39;
                    memset(&t39, 0, sizeof t39);
                    memmove(gm_out, ((uint8_t *)&t39), 32);
                    goto gm_fdone;
                }
                uint64_t t40 = gm_l16;
                gm_l9 = t40;
                uint64_t t41 = gm_l9;
                gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
                gm_tkb8hpcqf70t1 t43;
                GM_ST(uint32_t, (uint8_t *)&t43 + 20, 0);
                GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t41);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t43 + 8), t42);
                gm_toblvhepnjqkh t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 28, 0);
                GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&t44), t43);
                ((uint8_t *)&t44)[24] = 1;
                memmove(gm_out, ((uint8_t *)&t44), 32);
                goto gm_fdone;
            }
            void * t45 = gm_l0;
            void * t46 = GM_LD(void *, ((uint8_t *)t45));
            uint64_t t47 = gm_l8;
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_tf5srh4mp4b3i t49 = gm_f_lookup_59juqoo(t46, t47, t48);
            gm_l17 = t49;
            uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
            if (t50) {
                uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                gm_l18 = t51;
            } else {
                gm_toblvhepnjqkh t52;
                memset(&t52, 0, sizeof t52);
                memmove(gm_out, ((uint8_t *)&t52), 32);
                goto gm_fdone;
            }
            uint64_t t53 = gm_l18;
            gm_l10 = t53;
            void * t54 = gm_l0;
            uint64_t t55 = gm_l10;
            uint64_t t56 = gm_l1;
            uint8_t t57 = gm_f_decl_visible_bd168ti(t54, t55, t56);
            uint8_t t58 = (uint8_t)!t57;
            if (t58) {
                gm_toblvhepnjqkh t59;
                memset(&t59, 0, sizeof t59);
                memmove(gm_out, ((uint8_t *)&t59), 32);
                goto gm_fdone;
            }
            void * t60 = gm_l0;
            uint64_t t61 = gm_l10;
            uint64_t t62 = gm_l1;
            gm_b0_1 t63 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t60, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t64 = gm_l10;
            gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
            gm_tkb8hpcqf70t1 t66;
            GM_ST(uint32_t, (uint8_t *)&t66 + 20, 0);
            GM_ST(uint64_t, ((uint8_t *)&t66 + 0), t64);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t66 + 8), t65);
            gm_toblvhepnjqkh t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 28, 0);
            GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&t67), t66);
            ((uint8_t *)&t67)[24] = 1;
            memmove(gm_out, ((uint8_t *)&t67), 32);
            goto gm_fdone;
        }
gm_lb3:;
        {
            gm_toblvhepnjqkh t68;
            memset(&t68, 0, sizeof t68);
            memmove(gm_out, ((uint8_t *)&t68), 32);
            goto gm_fdone;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 554, 5);
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
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_type_args_5b8bvov F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* template */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* params */
    gm_b16_8 gm_l7; /* name */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* next */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t08olq97rscn3 gm_l18; /* n */
    gm_tf5srh4mp4b3i gm_l20; /* found */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* j */
    uint32_t gm_l24; /* p */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    uint32_t gm_l29; /* pid */
    uint64_t gm_l31; /* m */
    void * gm_l33; /* d */
    void * gm_l35; /* e */
    gm_t88rcucg1b7uk gm_l36; /* vt */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* j */
    uint32_t gm_l41; /* q */
    gm_t88rcucg1b7uk gm_l42; /* t */
    GM_STACK_CHECK(&F, 3890u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l43;
    gm_tn68el8d14vku gm_l44;
    uint64_t gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tabb710d9mq2e gm_l47;
    uint8_t gm_l48;
    gm_tabb710d9mq2e gm_l49;
    gm_tn68el8d14vku gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tc3oht4ksth9d gm_l53;
    gm_t88rcucg1b7uk gm_l54;
    gm_tn68el8d14vku gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tkn42u7na3t81 gm_l57;
    gm_t88rcucg1b7uk gm_l58;
    gm_tftb1odc739t5 gm_l59;
    {
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 1088));
        uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)t42) + 1088) + 8));
        uint32_t t45 = gm_l2;
        uint64_t t46 = (uint64_t)t45;
        if ((uintptr_t)t46 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t46, t44, gm_srcsffver18si54, 707, 13);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t43) + (uintptr_t)t46 * 184), 184);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 120));
        gm_l6 = t47;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_l7 = t48;
        gm_trqfvmjtt4sij t49;
        gm_f_list_9tqbks7((void *)&t49, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t49;
        gm_b16_8 t50 = gm_l6;
        gm_l9 = t50;
        gm_l10 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t51 = gm_l10;
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t53 = (t51 < t52);
                if (!t53) {
                    goto gm_lb1;
                }
                {
                    void * t54 = GM_LD(void *, ((uint8_t *)&gm_l9));
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint64_t t56 = gm_l10;
                    {
                        {
                            void * t57 = (void *)((uint8_t *)&F.l8);
                            gm_t0mfir12leg2g t58;
                            memset(&t58, 0, sizeof t58);
                            gm_tftb1odc739t5 t59 = gm_f_push_k0n7eot(t57, &t58);
                            gm_l43 = t59;
                            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                            if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srcsffver18si54, 712, 23);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 712, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t61 = gm_l10;
                uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                uint64_t t64 = r62;
                gm_l10 = t64;
            }
        }
gm_lb1:;
        gm_l11 = ((uint64_t)0ULL);
        gm_b16_8 t65 = gm_l3;
        gm_l12 = t65;
        gm_l13 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t66 = gm_l13;
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint8_t t68 = (t66 < t67);
                if (!t68) {
                    goto gm_lb6;
                }
                {
                    void * t69 = GM_LD(void *, ((uint8_t *)&gm_l12));
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint64_t t71 = gm_l13;
                    {
                        {
                            memmove(((uint8_t *)&F.l14), (((uint8_t *)t69) + (uintptr_t)t71 * 144), 144);
                            gm_l15 = ((uint64_t)0ULL);
                            gm_t0mfir12leg2g t72;
                            memset(&t72, 0, sizeof t72);
                            F.l16 = t72;
                            {
                                {
                                    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)0ULL));
                                    if (!t74) {
                                        goto gm_lb12;
                                    }
                                    memmove(((uint8_t *)&F.l17), (((uint8_t *)&F.l14) + 8), 88);
                                    uint64_t t75 = gm_l11;
                                    gm_l15 = t75;
                                    uint64_t t76 = gm_l11;
                                    uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                                    if (o78) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 720, 17);
                                    uint64_t t79 = r77;
                                    gm_l11 = t79;
                                    gm_tc3oht4ksth9d t80 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l17));
                                    gm_t0mfir12leg2g t81;
                                    GM_ST(uint8_t, (uint8_t *)&t81 + 89, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t81 + 90, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t81 + 92, 0);
                                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t81), t80);
                                    ((uint8_t *)&t81)[88] = 1;
                                    F.l16 = t81;
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)1ULL));
                                    if (!t83) {
                                        goto gm_lb13;
                                    }
                                    memmove(((uint8_t *)&gm_l18), (((uint8_t *)&F.l14) + 8), 32);
                                    memmove(((uint8_t *)&F.l19), ((((uint8_t *)&F.l14) + 8) + 32), 88);
                                    gm_tf5srh4mp4b3i t84;
                                    memset(&t84, 0, sizeof t84);
                                    gm_l20 = t84;
                                    gm_b16_8 t85 = gm_l6;
                                    gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                                    gm_l21 = t86;
                                    gm_l22 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t87 = gm_l22;
                                            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                            uint8_t t89 = (t87 < t88);
                                            if (!t89) {
                                                goto gm_lb14;
                                            }
                                            {
                                                uint64_t t90 = gm_l22;
                                                void * t91 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                uint64_t t93 = gm_l22;
                                                uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)t91) + (uintptr_t)t93 * 4));
                                                gm_tn68el8d14vku t95;
                                                GM_ST(uint32_t, (uint8_t *)&t95 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t95 + 0), t90);
                                                GM_ST(uint32_t, ((uint8_t *)&t95 + 8), t94);
                                                gm_l44 = t95;
                                                {
                                                    {
                                                        uint64_t t96 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                                                        gm_l23 = t96;
                                                        uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l44) + 8));
                                                        gm_l24 = t97;
                                                        void * t98 = gm_l0;
                                                        void * t99 = (void *)(((uint8_t *)t98) + 8);
                                                        uint32_t t100 = gm_l24;
                                                        gm_b16_8 t101 = gm_f_param_name_rbnop5l(t99, t100);
                                                        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                                        uint8_t t103 = (gm_eq0iqiubfm7e(((uint8_t *)&t101), ((uint8_t *)&t102)));
                                                        if (t103) {
                                                            uint64_t t104 = gm_l23;
                                                            gm_tf5srh4mp4b3i t105;
                                                            GM_ST(uint8_t, (uint8_t *)&t105 + 9, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t105 + 10, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t105 + 12, 0);
                                                            GM_ST(uint64_t, ((uint8_t *)&t105), t104);
                                                            ((uint8_t *)&t105)[8] = 1;
                                                            gm_l20 = t105;
                                                            goto gm_lb14;
                                                        }
                                                        goto gm_lb17;
                                                    }
gm_lb18:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 725, 17);
                                                }
gm_lb17:;
                                            }
gm_lb16:;
                                            uint64_t t106 = gm_l22;
                                            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                                            uint64_t t109 = r107;
                                            gm_l22 = t109;
                                        }
                                    }
gm_lb14:;
                                    uint8_t t110 = (uint8_t)((((uint8_t *)&gm_l20))[8] != 0);
                                    if (t110) {
                                        uint64_t t111 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                        gm_l45 = t111;
                                    } else {
                                        void * t112 = gm_l0;
                                        gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l18) + 16));
                                        gm_b16_8 t114 = gm_l7;
                                        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                        gm_t6j6smj7qoctu t116;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t116 + 0), t114);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t116 + 16), t115);
                                        gm_f_format_bnsf58k((void *)&gm_l46, (void *)NULL, cx, NULL, &t116);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                        if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srcsffver18si54, 732, 37);
                                        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                                        gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                                        gm_b0_1 t120 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t112, t113, t119);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_th2lqbo77pvsc t121;
                                        memset(&t121, 0, sizeof t121);
                                        memmove(gm_out, ((uint8_t *)&t121), 24);
                                        goto gm_fdone;
                                    }
                                    uint64_t t122 = gm_l45;
                                    gm_l15 = t122;
                                    gm_tc3oht4ksth9d t123 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l19));
                                    gm_t0mfir12leg2g t124;
                                    GM_ST(uint8_t, (uint8_t *)&t124 + 89, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t124 + 90, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t124 + 92, 0);
                                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t124), t123);
                                    ((uint8_t *)&t124)[88] = 1;
                                    F.l16 = t124;
                                    goto gm_lb11;
                                }
gm_lb13:;
                                {
                                    uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                    uint8_t t126 = (uint8_t)(t125 == ((uint8_t)2ULL));
                                    if (!t126) {
                                        goto gm_lb19;
                                    }
                                    void * t127 = gm_l0;
                                    gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                                    gm_b16_8 t129;
                                    GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sldikqa9i70sm));
                                    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(51));
                                    gm_b0_1 t130 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t127, t128, t129);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_th2lqbo77pvsc t131;
                                    memset(&t131, 0, sizeof t131);
                                    memmove(gm_out, ((uint8_t *)&t131), 24);
                                    goto gm_fdone;
                                }
gm_lb19:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 717, 9);
                            }
gm_lb11:;
                            uint64_t t132 = gm_l15;
                            uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t134 = (t132 >= t133);
                            if (t134) {
                                void * t135 = gm_l0;
                                gm_tl874t2atnlh3 t136 = gm_l4;
                                gm_b16_8 t137 = gm_l7;
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                gm_t3877pk7lv748 t139;
                                GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t137);
                                GM_ST(uint64_t, ((uint8_t *)&t139 + 16), t138);
                                gm_f_format_n6tblha((void *)&gm_l47, (void *)NULL, cx, NULL, &t139);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srcsffver18si54, 743, 27);
                                gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                                gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                                gm_b0_1 t143 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t135, t136, t142);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_th2lqbo77pvsc t144;
                                memset(&t144, 0, sizeof t144);
                                memmove(gm_out, ((uint8_t *)&t144), 24);
                                goto gm_fdone;
                            }
                            void * t145 = GM_LD(void *, ((uint8_t *)&F.l8));
                            uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                            uint64_t t147 = gm_l15;
                            if ((uintptr_t)t147 >= (uintptr_t)(t146)) gm_trap_index((uintptr_t)t147, t146, gm_srcsffver18si54, 746, 12);
                            {
                                {
                                    uint8_t t148 = (uint8_t)(((((uint8_t *)t145) + (uintptr_t)t147 * 96))[88] != 0);
                                    if (t148) {
                                        goto gm_lb21;
                                    }
                                    gm_l48 = ((uint8_t)0);
                                    goto gm_lb20;
                                }
gm_lb21:;
                                {
                                    gm_l48 = ((uint8_t)1);
                                    goto gm_lb20;
                                }
gm_lb22:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 746, 12);
                            }
gm_lb20:;
                            uint8_t t149 = gm_l48;
                            if (t149) {
                                void * t150 = gm_l0;
                                gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                                void * t152 = gm_l0;
                                void * t153 = (void *)(((uint8_t *)t152) + 8);
                                void * t154 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t156 = gm_l15;
                                uint32_t t157 = GM_LD(uint32_t, (((uint8_t *)t154) + (uintptr_t)t156 * 4));
                                gm_b16_8 t158 = gm_f_param_name_rbnop5l(t153, t157);
                                gm_tnpifv20irnph t159;
                                GM_ST(gm_b16_8, ((uint8_t *)&t159 + 0), t158);
                                gm_f_format_nbjjmv4((void *)&gm_l49, (void *)NULL, cx, NULL, t159);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 8, gm_srcsffver18si54, 747, 29);
                                gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                                gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                                gm_b0_1 t163 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t150, t151, t162);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_th2lqbo77pvsc t164;
                                memset(&t164, 0, sizeof t164);
                                memmove(gm_out, ((uint8_t *)&t164), 24);
                                goto gm_fdone;
                            }
                            void * t165 = GM_LD(void *, ((uint8_t *)&F.l8));
                            uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                            uint64_t t167 = gm_l15;
                            if ((uintptr_t)t167 >= (uintptr_t)(t166)) gm_trap_index((uintptr_t)t167, t166, gm_srcsffver18si54, 750, 9);
                            memmove((((uint8_t *)t165) + (uintptr_t)t167 * 96), ((uint8_t *)&F.l16), 96);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 714, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t168 = gm_l13;
                uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
                uint64_t t171 = r169;
                gm_l13 = t171;
            }
        }
gm_lb6:;
    }
    {
        gm_trqfvmjtt4sij t172;
        gm_f_list_nof7vfj((void *)&t172, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l25 = t172;
        gm_b16_8 t173 = gm_l6;
        gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
        gm_l26 = t174;
        gm_l27 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t175 = gm_l27;
                uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                uint8_t t177 = (t175 < t176);
                if (!t177) {
                    goto gm_lb23;
                }
                {
                    uint64_t t178 = gm_l27;
                    void * t179 = GM_LD(void *, ((uint8_t *)&gm_l26));
                    uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint64_t t181 = gm_l27;
                    uint32_t t182 = GM_LD(uint32_t, (((uint8_t *)t179) + (uintptr_t)t181 * 4));
                    gm_tn68el8d14vku t183;
                    GM_ST(uint32_t, (uint8_t *)&t183 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t183 + 0), t178);
                    GM_ST(uint32_t, ((uint8_t *)&t183 + 8), t182);
                    gm_l50 = t183;
                    {
                        {
                            uint64_t t184 = GM_LD(uint64_t, ((uint8_t *)&gm_l50));
                            gm_l28 = t184;
                            uint32_t t185 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 8));
                            gm_l29 = t185;
                            gm_t0mfir12leg2g t186;
                            memset(&t186, 0, sizeof t186);
                            F.l30 = t186;
                            uint64_t t187 = gm_l1;
                            gm_l31 = t187;
                            {
                                {
                                    void * t188 = GM_LD(void *, ((uint8_t *)&F.l8));
                                    uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                    uint64_t t190 = gm_l28;
                                    if ((uintptr_t)t190 >= (uintptr_t)(t189)) gm_trap_index((uintptr_t)t190, t189, gm_srcsffver18si54, 756, 17);
                                    uint8_t t191 = (uint8_t)(((((uint8_t *)t188) + (uintptr_t)t190 * 96))[88] != 0);
                                    if (!t191) {
                                        goto gm_lb29;
                                    }
                                    memmove(((uint8_t *)&F.l32), (((uint8_t *)t188) + (uintptr_t)t190 * 96), 88);
                                    gm_tc3oht4ksth9d t192 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l32));
                                    gm_t0mfir12leg2g t193;
                                    GM_ST(uint8_t, (uint8_t *)&t193 + 89, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t193 + 90, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t193 + 92, 0);
                                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t193), t192);
                                    ((uint8_t *)&t193)[88] = 1;
                                    F.l30 = t193;
                                    goto gm_lb28;
                                }
gm_lb29:;
                                {
                                    {
                                        void * t194 = GM_LD(void *, (((uint8_t *)&F.l5) + 136));
                                        uint64_t t195 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 136) + 8));
                                        uint64_t t196 = gm_l28;
                                        if ((uintptr_t)t196 >= (uintptr_t)(t195)) gm_trap_index((uintptr_t)t196, t195, gm_srcsffver18si54, 758, 24);
                                        uint8_t t197 = (uint8_t)(GM_LD(void *, (((uint8_t *)t194) + (uintptr_t)t196 * 8)) != 0);
                                        if (!t197) {
                                            goto gm_lb31;
                                        }
                                        void * t198 = GM_LD(void *, (((uint8_t *)t194) + (uintptr_t)t196 * 8));
                                        gm_l33 = t198;
                                        void * t199 = gm_l33;
                                        gm_tc3oht4ksth9d t200 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)t199));
                                        gm_t0mfir12leg2g t201;
                                        GM_ST(uint8_t, (uint8_t *)&t201 + 89, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t201 + 90, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t201 + 92, 0);
                                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t201), t200);
                                        ((uint8_t *)&t201)[88] = 1;
                                        F.l30 = t201;
                                        uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 16));
                                        gm_l31 = t202;
                                        goto gm_lb30;
                                    }
gm_lb31:;
                                    void * t203 = gm_l0;
                                    void * t204 = GM_LD(void *, (((uint8_t *)t203) + 1128));
                                    uint64_t t205 = GM_LD(uint64_t, ((((uint8_t *)t203) + 1128) + 8));
                                    uint32_t t206 = gm_l29;
                                    uint64_t t207 = (uint64_t)t206;
                                    if ((uintptr_t)t207 >= (uintptr_t)(t205)) gm_trap_index((uintptr_t)t207, t205, gm_srcsffver18si54, 762, 19);
                                    gm_tnnjeerolos0a t208 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t204) + (uintptr_t)t207 * 48));
                                    uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&t208) + 0));
                                    uint8_t t210 = (uint8_t)((t209 == ((uint8_t)1ULL)));
                                    gm_tnnjeerolos0a t211;
                                    GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)1ULL));
                                    if (t210) {
                                        void * t212 = (void *)((uint8_t *)&F.l25);
                                        void * t213 = gm_l0;
                                        void * t214 = (void *)(((uint8_t *)t213) + 8);
                                        gm_tnpifv20irnph t215 = gm_f_empty_effects_r0iv3pv();
                                        gm_t53at80pfdus3 t216;
                                        memset(&t216, 0, sizeof t216);
                                        GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)24ULL));
                                        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t216 + 8), t215);
                                        gm_t88rcucg1b7uk t217 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t214, &t216);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t218 = gm_f_push_k3l9nic(t212, GM_CONV(gm_csguankko4lmhd, t217));
                                        gm_l51 = t218;
                                        uint8_t t219 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                        if (!t219) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcsffver18si54, 764, 13);
                                        goto gm_lb25;
                                    } else {
                                        void * t220 = gm_l0;
                                        gm_tl874t2atnlh3 t221 = gm_l4;
                                        void * t222 = gm_l0;
                                        void * t223 = (void *)(((uint8_t *)t222) + 8);
                                        uint32_t t224 = gm_l29;
                                        gm_b16_8 t225 = gm_f_param_name_rbnop5l(t223, t224);
                                        gm_b16_8 t226 = gm_l7;
                                        gm_t6j6smj7qoctu t227;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t227 + 0), t225);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t227 + 16), t226);
                                        gm_f_format_l0aimgq((void *)&gm_l52, (void *)NULL, cx, NULL, &t227);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                        if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_srcsffver18si54, 767, 27);
                                        gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                                        gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                                        gm_b0_1 t231 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t220, t221, t230);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_th2lqbo77pvsc t232;
                                        memset(&t232, 0, sizeof t232);
                                        memmove(gm_out, ((uint8_t *)&t232), 24);
                                        goto gm_fdone;
                                    }
                                }
gm_lb30:;
                            }
gm_lb28:;
                            uint8_t t233 = (uint8_t)((((uint8_t *)&F.l30))[88] != 0);
                            if (t233) {
                                memmove(((uint8_t *)&gm_l53), ((uint8_t *)&F.l30), 88);
                            } else {
                                gm_th2lqbo77pvsc t234;
                                memset(&t234, 0, sizeof t234);
                                memmove(gm_out, ((uint8_t *)&t234), 24);
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&F.l34), ((uint8_t *)&gm_l53), 88);
                            void * t235 = (void *)((uint8_t *)&F.l34);
                            gm_l35 = t235;
                            void * t236 = gm_l0;
                            void * t237 = GM_LD(void *, (((uint8_t *)t236) + 1128));
                            uint64_t t238 = GM_LD(uint64_t, ((((uint8_t *)t236) + 1128) + 8));
                            uint32_t t239 = gm_l29;
                            uint64_t t240 = (uint64_t)t239;
                            if ((uintptr_t)t240 >= (uintptr_t)(t238)) gm_trap_index((uintptr_t)t240, t238, gm_srcsffver18si54, 772, 20);
                            {
                                {
                                    uint8_t t241 = GM_LD(uint8_t, ((((uint8_t *)t237) + (uintptr_t)t240 * 48) + 0));
                                    uint8_t t242 = (uint8_t)(t241 == ((uint8_t)2ULL));
                                    if (!t242) {
                                        goto gm_lb33;
                                    }
                                    gm_t88rcucg1b7uk t243 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t237) + (uintptr_t)t240 * 48) + 4));
                                    gm_l36 = t243;
                                    gm_trqfvmjtt4sij t244;
                                    gm_f_list_oalq03d((void *)&t244, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l37 = t244;
                                    void * t245 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                    uint64_t t248 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t249 = (uintptr_t)t247;
                                    if (t248 > t249 || t249 > (uintptr_t)t246) gm_trap_slice(t248, t249, t246, gm_srcsffver18si54, 775, 39);
                                    uint64_t t250 = ((uintptr_t)t247) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t251;
                                    GM_ST(void *, (uint8_t *)&t251, (void *)(((uint8_t *)t245 + (uintptr_t)((uint64_t)0ULL) * 4)));
                                    GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(t250));
                                    gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                                    gm_l38 = t252;
                                    gm_l39 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t253 = gm_l39;
                                            uint64_t t254 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                            uint8_t t255 = (t253 < t254);
                                            if (!t255) {
                                                goto gm_lb34;
                                            }
                                            {
                                                uint64_t t256 = gm_l39;
                                                void * t257 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                                uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                uint64_t t259 = gm_l39;
                                                uint32_t t260 = GM_LD(uint32_t, (((uint8_t *)t257) + (uintptr_t)t259 * 4));
                                                gm_tn68el8d14vku t261;
                                                GM_ST(uint32_t, (uint8_t *)&t261 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t261 + 0), t256);
                                                GM_ST(uint32_t, ((uint8_t *)&t261 + 8), t260);
                                                gm_l55 = t261;
                                                {
                                                    {
                                                        uint64_t t262 = GM_LD(uint64_t, ((uint8_t *)&gm_l55));
                                                        gm_l40 = t262;
                                                        uint32_t t263 = GM_LD(uint32_t, (((uint8_t *)&gm_l55) + 8));
                                                        gm_l41 = t263;
                                                        void * t264 = (void *)((uint8_t *)&F.l37);
                                                        uint32_t t265 = gm_l41;
                                                        void * t266 = GM_LD(void *, ((uint8_t *)&F.l25));
                                                        uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                                        uint64_t t268 = gm_l40;
                                                        if ((uintptr_t)t268 >= (uintptr_t)(t267)) gm_trap_index((uintptr_t)t268, t267, gm_srcsffver18si54, 775, 96);
                                                        gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t266) + (uintptr_t)t268 * 4));
                                                        gm_tplpbqf3duvn3 t270;
                                                        GM_ST(uint32_t, ((uint8_t *)&t270 + 0), t265);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t270 + 4), t269);
                                                        gm_tftb1odc739t5 t271 = gm_f_push_h69n5fi(t264, GM_CONV(gm_cs7e9ppsd384bj, t270));
                                                        gm_l56 = t271;
                                                        uint8_t t272 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                        if (!t272) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcsffver18si54, 775, 67);
                                                        goto gm_lb37;
                                                    }
gm_lb38:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 775, 17);
                                                }
gm_lb37:;
                                            }
gm_lb36:;
                                            uint64_t t273 = gm_l39;
                                            uint64_t r274; bool o275 = __builtin_add_overflow(t273, ((uint64_t)1ULL), &r274);
                                            uint64_t t276 = r274;
                                            gm_l39 = t276;
                                        }
                                    }
gm_lb34:;
                                    void * t277 = gm_l0;
                                    uint64_t t278 = gm_l31;
                                    void * t279 = gm_l0;
                                    gm_t88rcucg1b7uk t280 = gm_l36;
                                    gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
                                    gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                                    gm_t88rcucg1b7uk t283 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t279, t280, t282);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t284 = gm_l35;
                                    gm_tkn42u7na3t81 t285 = gm_f_comptime_value_l5orc93((void *)NULL, cx, NULL, t277, t278, t283, t284);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l57 = t285;
                                    uint8_t t286 = (uint8_t)((((uint8_t *)&gm_l57))[4] != 0);
                                    if (t286) {
                                        gm_t88rcucg1b7uk t287 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l57));
                                        gm_l58 = t287;
                                    } else {
                                        gm_th2lqbo77pvsc t288;
                                        memset(&t288, 0, sizeof t288);
                                        memmove(gm_out, ((uint8_t *)&t288), 24);
                                        goto gm_fdone;
                                    }
                                    gm_t88rcucg1b7uk t289 = gm_l58;
                                    gm_l54 = t289;
                                    goto gm_lb32;
                                }
gm_lb33:;
                                {
                                    uint8_t t290 = GM_LD(uint8_t, ((((uint8_t *)t237) + (uintptr_t)t240 * 48) + 0));
                                    uint8_t t291 = (uint8_t)(t290 == ((uint8_t)1ULL));
                                    if (!t291) {
                                        goto gm_lb39;
                                    }
                                    void * t292 = gm_l0;
                                    uint64_t t293 = gm_l31;
                                    void * t294 = gm_l35;
                                    gm_t88rcucg1b7uk t295 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t292, t293, t294);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l54 = t295;
                                    goto gm_lb32;
                                }
gm_lb39:;
                                {
                                    uint8_t t296 = GM_LD(uint8_t, ((((uint8_t *)t237) + (uintptr_t)t240 * 48) + 0));
                                    uint8_t t297 = (uint8_t)(t296 == ((uint8_t)0ULL));
                                    if (!t297) {
                                        goto gm_lb40;
                                    }
                                    void * t298 = gm_l0;
                                    uint64_t t299 = gm_l31;
                                    void * t300 = gm_l35;
                                    gm_t88rcucg1b7uk t301 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t298, t299, t300);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l54 = t301;
                                    goto gm_lb32;
                                }
gm_lb40:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 772, 14);
                            }
gm_lb32:;
                            gm_t88rcucg1b7uk t302 = gm_l54;
                            gm_l42 = t302;
                            void * t303 = gm_l0;
                            void * t304 = (void *)(((uint8_t *)t303) + 8);
                            gm_t88rcucg1b7uk t305 = gm_l42;
                            uint8_t t306 = gm_f_is_error_8k27gd1(t304, GM_CONV(gm_csguankko4lmhd, t305));
                            if (t306) {
                                gm_th2lqbo77pvsc t307;
                                memset(&t307, 0, sizeof t307);
                                memmove(gm_out, ((uint8_t *)&t307), 24);
                                goto gm_fdone;
                            }
                            void * t308 = (void *)((uint8_t *)&F.l25);
                            gm_t88rcucg1b7uk t309 = gm_l42;
                            gm_tftb1odc739t5 t310 = gm_f_push_k3l9nic(t308, GM_CONV(gm_csguankko4lmhd, t309));
                            gm_l59 = t310;
                            uint8_t t311 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                            if (!t311) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srcsffver18si54, 782, 9);
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 753, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t312 = gm_l27;
                uint64_t r313; bool o314 = __builtin_add_overflow(t312, ((uint64_t)1ULL), &r313);
                uint64_t t315 = r313;
                gm_l27 = t315;
            }
        }
gm_lb23:;
    }
    {
        gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
        gm_th2lqbo77pvsc t317;
        GM_ST(uint8_t, (uint8_t *)&t317 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t317 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t317 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t317), t316);
        ((uint8_t *)&t317)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t317), 24);
        goto gm_fdone;
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
extern __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hoists_13k2a2s F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* x */
    void * gm_l3; /* inner */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l4;
    uint8_t gm_l5;
    void * gm_l6;
    void * t9 = gm_l0;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)28ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t9) + 8) + 8));
            gm_l1 = t12;
            void * t13 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
            gm_l4 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)24ULL));
            if (!t15) {
                goto gm_lb3;
            }
            void * t16 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l2 = t16;
            void * t17 = gm_l2;
            gm_l4 = t17;
            goto gm_lb1;
        }
gm_lb3:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 285, 14);
    }
gm_lb1:;
    void * t18 = gm_l4;
    gm_l3 = t18;
    void * t19 = gm_l3;
    void * t20 = gm_f_construction_call_ktqtrpo(t19);
    gm_l6 = t20;
    {
        {
            uint8_t t21 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
            if (t21) {
                goto gm_lb6;
            }
            gm_l5 = ((uint8_t)0);
            goto gm_lb5;
        }
gm_lb6:;
        {
            gm_l5 = ((uint8_t)1);
            goto gm_lb5;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 290, 8);
    }
gm_lb5:;
    uint8_t t22 = gm_l5;
    if (t22) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t23 = gm_l0;
    void * t24 = gm_f_prefix_operand_3n5jchh(t23);
    uint8_t t25 = gm_f_pushes_down_0djaflp(t24);
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_type_name_a77a0kf F;
    gm_b16_8 gm_l0; /* name */
    uint32_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 334u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t8 = (t7 < ((uint64_t)2ULL));
    if (t8) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t10, gm_srcsffver18si54, 5, 24);
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t12 + 1), ((uint8_t)105ULL));
        uint8_t t13 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 0, ((uint8_t *)&t11))) t13 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 1, ((uint8_t *)&t11))) t13 = 1;
        uint8_t t14 = (uint8_t)(!t13);
        gm_l9 = t14;
    }
    uint8_t t15 = gm_l9;
    if (t15) {
        gm_l8 = ((uint8_t)1);
    } else {
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t17, gm_srcsffver18si54, 5, 53);
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)((uint64_t)1ULL) * 1));
        uint8_t t19 = ((t18 == ((uint8_t)48ULL)));
        gm_l8 = t19;
    }
    uint8_t t20 = gm_l8;
    if (t20) {
        gm_l7 = ((uint8_t)1);
    } else {
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t22 = (t21 > ((uint64_t)4ULL));
        gm_l7 = t22;
    }
    uint8_t t23 = gm_l7;
    if (t23) {
        gm_tp8mv052i18rg t24;
        memset(&t24, 0, sizeof t24);
        F.ret = t24;
        goto gm_fret;
    }
    gm_l1 = ((uint32_t)0ULL);
    void * t25 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t27 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t28 = (uintptr_t)t26;
    if (t27 > t28 || t28 > (uintptr_t)t26) gm_trap_slice(t27, t28, t26, gm_srcsffver18si54, 7, 14);
    uint64_t t29 = ((uintptr_t)t26) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t25 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29));
    gm_l2 = t30;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l3;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb1;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t36 = gm_l3;
                {
                    {
                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t34) + (uintptr_t)t36 * 1));
                        gm_l4 = t37;
                        uint8_t t38 = gm_l4;
                        gm_l5 = t38;
                        uint8_t t39 = gm_l5;
                        uint8_t t40 = (((uint8_t)48ULL) <= t39);
                        if (t40) {
                            uint8_t t41 = gm_l5;
                            uint8_t t42 = (t41 <= ((uint8_t)57ULL));
                            gm_l10 = t42;
                        } else {
                            gm_l10 = ((uint8_t)0);
                        }
                        uint8_t t43 = gm_l10;
                        uint8_t t44 = (uint8_t)!t43;
                        if (t44) {
                            gm_tp8mv052i18rg t45;
                            memset(&t45, 0, sizeof t45);
                            F.ret = t45;
                            goto gm_fret;
                        }
                        uint32_t t46 = gm_l1;
                        uint32_t r47; bool o48 = __builtin_mul_overflow(t46, ((uint32_t)10ULL), &r47);
                        if (o48) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 9, 16);
                        uint32_t t49 = r47;
                        uint8_t t50 = gm_l4;
                        uint8_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint8_t)48ULL), &r51);
                        if (o52) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 9, 32);
                        uint8_t t53 = r51;
                        uint32_t t54 = (uint32_t)t53;
                        uint32_t r55; bool o56 = __builtin_add_overflow(t49, t54, &r55);
                        if (o56) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 9, 16);
                        uint32_t t57 = r55;
                        gm_l1 = t57;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 7, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t58 = gm_l3;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l3 = t61;
        }
    }
gm_lb1:;
    uint32_t t62 = gm_l1;
    gm_l6 = t62;
    uint32_t t63 = gm_l6;
    uint8_t t64 = (((uint32_t)1ULL) <= t63);
    if (t64) {
        uint32_t t65 = gm_l6;
        uint8_t t66 = (t65 <= ((uint32_t)128ULL));
        gm_l11 = t66;
    } else {
        gm_l11 = ((uint8_t)0);
    }
    uint8_t t67 = gm_l11;
    uint8_t t68 = (uint8_t)!t67;
    if (t68) {
        gm_tp8mv052i18rg t69;
        memset(&t69, 0, sizeof t69);
        F.ret = t69;
        goto gm_fret;
    }
    void * t70 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t71, gm_srcsffver18si54, 12, 21);
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)t70) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint8_t t73 = ((t72 == ((uint8_t)105ULL)));
    uint32_t t74 = gm_l1;
    uint16_t t75 = (uint16_t)t74;
    gm_tjcj4j2gbh6c8 t76;
    GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t76 + 0), t73);
    GM_ST(uint16_t, ((uint8_t *)&t76 + 2), t75);
    gm_tp8mv052i18rg t77;
    GM_ST(uint8_t, (uint8_t *)&t77 + 5, 0);
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t77), t76);
    ((uint8_t *)&t77)[4] = 1;
    F.ret = t77;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_builtin_type_name_v5ttoka F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 453u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    gm_tp8mv052i18rg gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_tp8mv052i18rg t6 = gm_f_int_type_name_a77a0kf(t5);
    gm_l3 = t6;
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
            if (t7) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 49, 12);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    if (t8) {
        gm_l1 = ((uint8_t)1);
    } else {
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s6groq37ram3h));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(5));
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s0i3i86nphpsh));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_shnig5tqtdedi));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sm9e81djuncnu));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sqqajgod2bb4o));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s3cv29r83pgn4));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2737i1iq57nf));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_slolcr6ikvg33));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s0iqiubfm7e7e));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s47llp68ptjgu));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sflee3fpg0btv));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
        gm_th4d8o29g5urm t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 16), t11);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 32), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 48), t13);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 64), t14);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 80), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 96), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 112), t17);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 128), t18);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 144), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 160), t20);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 176), t21);
        uint8_t t23 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 0, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 16, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 32, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 48, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 64, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 80, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 96, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 112, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 128, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 144, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 160, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 176, ((uint8_t *)&t9))) t23 = 1;
        uint8_t t24 = (uint8_t)(t23);
        gm_l1 = t24;
    }
    uint8_t t25 = gm_l1;
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_oversized_int_name_nr25haf F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* bits */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 393u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t13 = (t12 < ((uint64_t)2ULL));
    if (t13) {
        gm_l10 = ((uint8_t)1);
    } else {
        void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t15, gm_srcsffver18si54, 17, 24);
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t14) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t17 + 1), ((uint8_t)105ULL));
        uint8_t t18 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t17) + 0, ((uint8_t *)&t16))) t18 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t17) + 1, ((uint8_t *)&t16))) t18 = 1;
        uint8_t t19 = (uint8_t)(!t18);
        gm_l10 = t19;
    }
    uint8_t t20 = gm_l10;
    if (t20) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t22, gm_srcsffver18si54, 17, 53);
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)((uint64_t)1ULL) * 1));
        uint8_t t24 = ((t23 == ((uint8_t)48ULL)));
        gm_l9 = t24;
    }
    uint8_t t25 = gm_l9;
    if (t25) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t26 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t28 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t29 = (uintptr_t)t27;
    if (t28 > t29 || t29 > (uintptr_t)t27) gm_trap_slice(t28, t29, t27, gm_srcsffver18si54, 18, 14);
    uint64_t t30 = ((uintptr_t)t27) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30));
    gm_l1 = t31;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l2;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t37 = gm_l2;
                {
                    {
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t35) + (uintptr_t)t37 * 1));
                        gm_l3 = t38;
                        uint8_t t39 = gm_l3;
                        gm_l4 = t39;
                        uint8_t t40 = gm_l4;
                        uint8_t t41 = (((uint8_t)48ULL) <= t40);
                        if (t41) {
                            uint8_t t42 = gm_l4;
                            uint8_t t43 = (t42 <= ((uint8_t)57ULL));
                            gm_l11 = t43;
                        } else {
                            gm_l11 = ((uint8_t)0);
                        }
                        uint8_t t44 = gm_l11;
                        uint8_t t45 = (uint8_t)!t44;
                        if (t45) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l2;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l2 = t49;
        }
    }
gm_lb1:;
    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t51 = (t50 > ((uint64_t)4ULL));
    if (t51) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    gm_l5 = ((uint32_t)0ULL);
    void * t52 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t54 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t55 = (uintptr_t)t53;
    if (t54 > t55 || t55 > (uintptr_t)t53) gm_trap_slice(t54, t55, t53, gm_srcsffver18si54, 23, 14);
    uint64_t t56 = ((uintptr_t)t53) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t52 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
    gm_l6 = t57;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t58 = gm_l7;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t60 = (t58 < t59);
            if (!t60) {
                goto gm_lb6;
            }
            {
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t63 = gm_l7;
                {
                    {
                        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t61) + (uintptr_t)t63 * 1));
                        gm_l8 = t64;
                        uint32_t t65 = gm_l5;
                        uint32_t r66; bool o67 = __builtin_mul_overflow(t65, ((uint32_t)10ULL), &r66);
                        if (o67) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 23, 33);
                        uint32_t t68 = r66;
                        uint8_t t69 = gm_l8;
                        uint8_t r70; bool o71 = __builtin_sub_overflow(t69, ((uint8_t)48ULL), &r70);
                        if (o71) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 23, 49);
                        uint8_t t72 = r70;
                        uint32_t t73 = (uint32_t)t72;
                        uint32_t r74; bool o75 = __builtin_add_overflow(t68, t73, &r74);
                        if (o75) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 23, 33);
                        uint32_t t76 = r74;
                        gm_l5 = t76;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 23, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t77 = gm_l7;
            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
            uint64_t t80 = r78;
            gm_l7 = t80;
        }
    }
gm_lb6:;
    uint32_t t81 = gm_l5;
    uint8_t t82 = (t81 > ((uint32_t)128ULL));
    F.ret = t82;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0);
extern __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1) { gm_f_leave_module_scope_4hp5mc4(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_len_param_names_cilm68f F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    void * gm_l3; /* out */
    uint32_t gm_l4; /* p */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l7; /* m */
    gm_b16_8 gm_l8; /* n */
    gm_t88rcucg1b7uk gm_l9; /* a */
    gm_t88rcucg1b7uk gm_l10; /* b */
    GM_STACK_CHECK(&F, 574u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 8);
    gm_t88rcucg1b7uk t13 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l11, t12, GM_CONV(gm_csguankko4lmhd, t13));
    {
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)19ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
            gm_l4 = t16;
            gm_b16_8 t17 = gm_l2;
            gm_l5 = t17;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t20 = (t18 < t19);
                    if (!t20) {
                        goto gm_lb3;
                    }
                    {
                        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t23 = gm_l6;
                        {
                            {
                                gm_tplpbqf3duvn3 t24 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t21) + (uintptr_t)t23 * 8));
                                gm_l7 = t24;
                                uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
                                uint32_t t26 = gm_l4;
                                uint8_t t27 = ((t25 == t26));
                                if (t27) {
                                    void * t28 = gm_l0;
                                    void * t29 = (void *)(((uint8_t *)t28) + 8);
                                    uint32_t t30 = gm_l4;
                                    gm_b16_8 t31 = gm_f_param_name_rbnop5l(t29, t30);
                                    void * t32 = gm_l0;
                                    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 4));
                                    gm_b16_8 t34 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t32, t33);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t6j6smj7qoctu t35;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t31);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 16), t34);
                                    gm_f_format_vom8db6((void *)&gm_l12, (void *)NULL, cx, NULL, &t35);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 692, 29);
                                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                                    gm_l8 = t38;
                                    gm_b16_8 t39 = gm_l8;
                                    void * t40 = gm_l3;
                                    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)t40));
                                    uint8_t t42 = 0;
                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t41) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t41))) + i * 16, ((uint8_t *)&t39))) { t42 = 1; break; }
                                    uint8_t t43 = (uint8_t)(!t42);
                                    if (t43) {
                                        void * t44 = gm_l3;
                                        gm_b16_8 t45 = gm_l8;
                                        gm_tftb1odc739t5 t46 = gm_f_push_3ef746b(t44, t45);
                                        gm_l13 = t46;
                                        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcsffver18si54, 693, 41);
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 690, 22);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t48 = gm_l6;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l6 = t51;
                }
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)22ULL));
            if (!t53) {
                goto gm_lb8;
            }
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l11) + 8) + 4));
            gm_l9 = t54;
            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l11) + 8) + 8));
            gm_l10 = t55;
            void * t56 = gm_l0;
            gm_t88rcucg1b7uk t57 = gm_l9;
            gm_b16_8 t58 = gm_l2;
            void * t59 = gm_l3;
            gm_b0_1 t60 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t56, t57, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l10;
            gm_b16_8 t63 = gm_l2;
            void * t64 = gm_l3;
            gm_b0_1 t65 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t61, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 689, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_len_params_bound_f7mjvn9 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    uint32_t gm_l3; /* p */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l6; /* m */
    gm_t428ue2u3hf5g gm_l7; /* k */
    gm_t88rcucg1b7uk gm_l8; /* a */
    gm_t88rcucg1b7uk gm_l9; /* b */
    GM_STACK_CHECK(&F, 461u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l10;
    gm_t53at80pfdus3 gm_l11;
    uint8_t gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    uint8_t gm_l14;
    void * t16 = gm_l0;
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_t88rcucg1b7uk t18 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l10, t17, GM_CONV(gm_csguankko4lmhd, t18));
    {
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)19ULL));
            if (!t20) {
                goto gm_lb2;
            }
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
            gm_l3 = t21;
            gm_b16_8 t22 = gm_l2;
            gm_l4 = t22;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t23 = gm_l5;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t25 = (t23 < t24);
                    if (!t25) {
                        goto gm_lb3;
                    }
                    {
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t28 = gm_l5;
                        {
                            {
                                gm_tplpbqf3duvn3 t29 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t26) + (uintptr_t)t28 * 8));
                                gm_l6 = t29;
                                uint32_t t30 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                                uint32_t t31 = gm_l3;
                                uint8_t t32 = ((t30 == t31));
                                if (t32) {
                                    {
                                        {
                                            void * t33 = gm_l0;
                                            void * t34 = (void *)(((uint8_t *)t33) + 8);
                                            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 4));
                                            gm_f_kind_jcnt4hm((void *)&gm_l11, t34, GM_CONV(gm_csguankko4lmhd, t35));
                                            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)21ULL));
                                            if (!t37) {
                                                goto gm_lb9;
                                            }
                                            memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l11) + 8), 32);
                                            gm_tf5srh4mp4b3i t38 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l7)));
                                            gm_l13 = t38;
                                            {
                                                {
                                                    uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
                                                    if (t39) {
                                                        goto gm_lb11;
                                                    }
                                                    gm_l12 = ((uint8_t)0);
                                                    goto gm_lb10;
                                                }
gm_lb11:;
                                                {
                                                    gm_l12 = ((uint8_t)1);
                                                    goto gm_lb10;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 676, 63);
                                            }
gm_lb10:;
                                            uint8_t t40 = gm_l12;
                                            F.ret = t40;
                                            goto gm_fret;
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 674, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t41 = gm_l5;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l5 = t44;
                }
            }
gm_lb3:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)22ULL));
            if (!t46) {
                goto gm_lb13;
            }
            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l10) + 8) + 4));
            gm_l8 = t47;
            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l10) + 8) + 8));
            gm_l9 = t48;
            void * t49 = gm_l0;
            gm_t88rcucg1b7uk t50 = gm_l8;
            gm_b16_8 t51 = gm_l2;
            uint8_t t52 = gm_f_len_params_bound_f7mjvn9(t49, GM_CONV(gm_csguankko4lmhd, t50), t51);
            if (t52) {
                void * t53 = gm_l0;
                gm_t88rcucg1b7uk t54 = gm_l9;
                gm_b16_8 t55 = gm_l2;
                uint8_t t56 = gm_f_len_params_bound_f7mjvn9(t53, GM_CONV(gm_csguankko4lmhd, t54), t55);
                gm_l14 = t56;
            } else {
                gm_l14 = ((uint8_t)0);
            }
            uint8_t t57 = gm_l14;
            F.ret = t57;
            goto gm_fret;
        }
gm_lb13:;
        {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 672, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_length_is_type_soi53ah F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* len */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    uint8_t gm_l5;
    gm_t9805qtonbsag gm_l6;
    void * t9 = gm_l2;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)6ULL));
            if (!t11) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t9) + 8) + 8), 32);
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_t9805qtonbsag t14 = gm_f_value_param_tof1h6t(t12, t13);
            gm_l6 = t14;
            {
                {
                    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l6))[4] != 0);
                    if (t15) {
                        goto gm_lb4;
                    }
                    gm_l5 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l5 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 585, 30);
            }
gm_lb3:;
            uint8_t t16 = gm_l5;
            if (t16) {
                void * t17 = gm_l0;
                uint64_t t18 = gm_l1;
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                uint8_t t20 = gm_f_names_type_rjdgib9((void *)NULL, cx, NULL, t17, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t20;
            } else {
                gm_l4 = ((uint8_t)0);
            }
            uint8_t t21 = gm_l4;
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)22ULL));
            if (!t23) {
                goto gm_lb6;
            }
            void * t24 = gm_l0;
            uint64_t t25 = gm_l1;
            void * t26 = gm_l2;
            uint8_t t27 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t24, t25, t26, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t27;
            goto gm_fret;
        }
gm_lb6:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 584, 5);
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
inline __attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_const_idpokjh F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 858u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1688) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 109, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1688));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1688) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 110, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 224));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1688));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1688) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 110, 58);
                    gm_tb07pp6njg0bk t24 = GM_LD(gm_tb07pp6njg0bk, (((uint8_t *)t21) + (uintptr_t)t23 * 224));
                    gm_tk1gen3hgkm51 t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 225, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 226, 0);
                    GM_ST(uint32_t, (uint8_t *)&t25 + 228, 0);
                    GM_ST(gm_tb07pp6njg0bk, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[224] = 1;
                    memmove(gm_out, ((uint8_t *)&t25), 232);
                    goto gm_fdone;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tk1gen3hgkm51 t26;
    memset(&t26, 0, sizeof t26);
    memmove(gm_out, ((uint8_t *)&t26), 232);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1) { gm_tk1gen3hgkm51 gm_r; gm_f_local_const_idpokjh(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_fn_aji8qnd F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1728) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 119, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1728));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1728) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 120, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 24));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1728));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1728) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 120, 55);
                    uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t21) + (uintptr_t)t23 * 24) + 16));
                    gm_t9805qtonbsag t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[4] = 1;
                    F.ret = t25;
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_t9805qtonbsag t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_looks_like_type_ho5qv1k F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    void * gm_l5; /* inner */
    gm_t3mf58gv9kf3n gm_l6; /* a */
    void * gm_l7; /* inner */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l12; /* lhs */
    void * gm_l13; /* callee */
    gm_tkb8hpcqf70t1 gm_l14; /* g */
    void * gm_l15; /* inner */
    GM_STACK_CHECK(&F, 1224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l16;
    uint8_t gm_l17;
    gm_t6qooom8qa2do gm_l18;
    uint8_t gm_l19;
    void * gm_l20;
    uint8_t gm_l21;
    void * gm_l22;
    uint8_t gm_l23;
    gm_toblvhepnjqkh gm_l24;
    gm_tkb8hpcqf70t1 gm_l25;
    uint8_t gm_l26;
    gm_t6qooom8qa2do gm_l27;
    void * t41 = gm_l2;
    {
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)6ULL));
            if (!t43) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t41) + 8) + 8), 32);
            {
                {
                    void * t44 = gm_l0;
                    uint64_t t45 = gm_l1;
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_tf5srh4mp4b3i t47 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t44, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t47;
                    uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                    if (!t48) {
                        goto gm_lb4;
                    }
                    uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
                    gm_l4 = t49;
                    void * t50 = gm_l0;
                    uint64_t t51 = gm_l4;
                    gm_f_resolve_decl_pus042v((void *)&gm_l18, (void *)NULL, cx, NULL, t50, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)0ULL));
                            if (!t53) {
                                goto gm_lb6;
                            }
                            gm_l17 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l17 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 221, 68);
                    }
gm_lb5:;
                    uint8_t t54 = gm_l17;
                    F.ret = t54;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            uint8_t t56 = gm_f_is_builtin_type_name_v5ttoka(t55);
            F.ret = t56;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)9ULL));
            if (!t58) {
                goto gm_lb8;
            }
            void * t59 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l5 = t59;
            void * t60 = gm_l0;
            uint64_t t61 = gm_l1;
            void * t62 = gm_l5;
            uint8_t t63 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t60, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t63;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)28ULL));
            if (!t65) {
                goto gm_lb9;
            }
            gm_t3mf58gv9kf3n t66 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t41) + 8) + 8));
            gm_l6 = t66;
            void * t67 = gm_l2;
            uint8_t t68 = gm_f_hoists_13k2a2s(t67);
            if (t68) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t69 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
            void * t70 = gm_f_construction_call_ktqtrpo(t69);
            gm_l20 = t70;
            {
                {
                    uint8_t t71 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l20)) != 0);
                    if (t71) {
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
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 230, 16);
            }
gm_lb10:;
            uint8_t t72 = gm_l19;
            if (t72) {
                void * t73 = gm_l0;
                uint64_t t74 = gm_l1;
                void * t75 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                uint8_t t76 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t73, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t76;
                goto gm_fret;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb9:;
        {
            uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)24ULL));
            if (!t78) {
                goto gm_lb13;
            }
            void * t79 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l7 = t79;
            void * t80 = gm_l2;
            uint8_t t81 = gm_f_hoists_13k2a2s(t80);
            if (t81) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t82 = gm_l7;
            void * t83 = gm_f_construction_call_ktqtrpo(t82);
            gm_l22 = t83;
            {
                {
                    uint8_t t84 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l22)) != 0);
                    if (t84) {
                        goto gm_lb15;
                    }
                    gm_l21 = ((uint8_t)0);
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    gm_l21 = ((uint8_t)1);
                    goto gm_lb14;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 235, 16);
            }
gm_lb14:;
            uint8_t t85 = gm_l21;
            if (t85) {
                void * t86 = gm_l0;
                uint64_t t87 = gm_l1;
                void * t88 = gm_l7;
                uint8_t t89 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t86, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t89;
                goto gm_fret;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)10ULL));
            if (!t91) {
                goto gm_lb17;
            }
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)t41) + 8) + 8));
            gm_l8 = t92;
            gm_b16_8 t93 = gm_l8;
            gm_l9 = t93;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t94 = gm_l10;
                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t96 = (t94 < t95);
                    if (!t96) {
                        goto gm_lb18;
                    }
                    {
                        void * t97 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t99 = gm_l10;
                        {
                            {
                                memmove(((uint8_t *)&F.l11), (((uint8_t *)t97) + (uintptr_t)t99 * 136), 136);
                                uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 128));
                                if (t100) {
                                    gm_l23 = ((uint8_t)1);
                                } else {
                                    void * t101 = gm_l0;
                                    uint64_t t102 = gm_l1;
                                    void * t103 = (void *)(((uint8_t *)&F.l11) + 40);
                                    uint8_t t104 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t101, t102, t103);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t105 = (uint8_t)!t104;
                                    gm_l23 = t105;
                                }
                                uint8_t t106 = gm_l23;
                                if (t106) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb21;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 239, 13);
                        }
gm_lb21:;
                    }
gm_lb20:;
                    uint64_t t107 = gm_l10;
                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                    uint64_t t110 = r108;
                    gm_l10 = t110;
                }
            }
gm_lb18:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb17:;
        {
            {
                {
                    uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t112 = (uint8_t)(t111 == ((uint8_t)25ULL));
                    if (!t112) {
                        goto gm_lb25;
                    }
                    goto gm_lb24;
                }
gm_lb25:;
                {
                    uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t114 = (uint8_t)(t113 == ((uint8_t)26ULL));
                    if (!t114) {
                        goto gm_lb26;
                    }
                    goto gm_lb24;
                }
gm_lb26:;
                {
                    uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)27ULL));
                    if (!t116) {
                        goto gm_lb27;
                    }
                    goto gm_lb24;
                }
gm_lb27:;
                {
                    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)29ULL));
                    if (!t118) {
                        goto gm_lb28;
                    }
                    goto gm_lb24;
                }
gm_lb28:;
                {
                    uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t120 = (uint8_t)(t119 == ((uint8_t)30ULL));
                    if (!t120) {
                        goto gm_lb29;
                    }
                    goto gm_lb24;
                }
gm_lb29:;
                {
                    uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)31ULL));
                    if (!t122) {
                        goto gm_lb30;
                    }
                    goto gm_lb24;
                }
gm_lb30:;
                {
                    uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)32ULL));
                    if (!t124) {
                        goto gm_lb31;
                    }
                    goto gm_lb24;
                }
gm_lb31:;
                {
                    uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t126 = (uint8_t)(t125 == ((uint8_t)33ULL));
                    if (!t126) {
                        goto gm_lb32;
                    }
                    goto gm_lb24;
                }
gm_lb32:;
                goto gm_lb23;
            }
gm_lb24:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb23:;
        {
            uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t128 = (uint8_t)(t127 == ((uint8_t)17ULL));
            if (!t128) {
                goto gm_lb33;
            }
            void * t129 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l12 = t129;
            void * t130 = gm_l0;
            uint64_t t131 = gm_l1;
            void * t132 = gm_l12;
            uint8_t t133 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t130, t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t133;
            goto gm_fret;
        }
gm_lb33:;
        {
            uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t135 = (uint8_t)(t134 == ((uint8_t)19ULL));
            if (!t135) {
                goto gm_lb34;
            }
            void * t136 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l13 = t136;
            void * t137 = gm_l0;
            uint64_t t138 = gm_l1;
            void * t139 = gm_l13;
            gm_f_generic_decl_oir9hm6((void *)&gm_l24, (void *)NULL, cx, NULL, t137, t138, t139, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t140 = (uint8_t)((((uint8_t *)&gm_l24))[24] != 0);
            if (t140) {
                memmove(((uint8_t *)&gm_l25), ((uint8_t *)&gm_l24), 24);
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l25), 24);
            void * t141 = gm_l0;
            uint64_t t142 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
            gm_f_resolve_decl_pus042v((void *)&gm_l27, (void *)NULL, cx, NULL, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                    uint8_t t144 = (uint8_t)(t143 == ((uint8_t)1ULL));
                    if (!t144) {
                        goto gm_lb36;
                    }
                    gm_l26 = ((uint8_t)1);
                    goto gm_lb35;
                }
gm_lb36:;
                {
                    gm_l26 = ((uint8_t)0);
                    goto gm_lb35;
                }
gm_lb37:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 248, 20);
            }
gm_lb35:;
            uint8_t t145 = gm_l26;
            F.ret = t145;
            goto gm_fret;
        }
gm_lb34:;
        {
            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t147 = (uint8_t)(t146 == ((uint8_t)35ULL));
            if (!t147) {
                goto gm_lb38;
            }
            void * t148 = GM_LD(void *, (((((uint8_t *)t41) + 8) + 8) + 16));
            gm_l15 = t148;
            void * t149 = gm_l0;
            uint64_t t150 = gm_l1;
            void * t151 = gm_l15;
            uint8_t t152 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t149, t150, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t152;
            goto gm_fret;
        }
gm_lb38:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb39:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 219, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_names_error_oet8t6b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 599u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l5;
    uint64_t gm_l6;
    {
        {
            void * t4 = gm_l2;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t4) + 8) + 8), 32);
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_tf5srh4mp4b3i t10 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t10;
            uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
            if (t11) {
                uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                gm_l6 = t12;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            uint64_t t13 = gm_l6;
            gm_l4 = t13;
            void * t14 = gm_l0;
            uint64_t t15 = gm_l4;
            gm_t6qooom8qa2do t16;
            gm_f_resolve_decl_pus042v((void *)&t16, (void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
            uint8_t t18 = (uint8_t)((t17 == ((uint8_t)11ULL)));
            gm_t6qooom8qa2do t19;
            memset(&t19, 0, sizeof t19);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)11ULL));
            F.ret = t18;
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_names_type_rjdgib9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    gm_tkn42u7na3t81 gm_l5;
    gm_tf5srh4mp4b3i gm_l6;
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l2;
    gm_tkn42u7na3t81 t9 = gm_f_type_param_3av4r8i(t7, t8);
    gm_l5 = t9;
    {
        {
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
            if (t10) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 138, 8);
    }
gm_lb1:;
    uint8_t t11 = gm_l4;
    if (t11) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    {
        {
            void * t12 = gm_l0;
            uint64_t t13 = gm_l1;
            gm_b16_8 t14 = gm_l2;
            gm_tf5srh4mp4b3i t15 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t15;
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
            if (!t16) {
                goto gm_lb5;
            }
            uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l3 = t17;
            void * t18 = gm_l0;
            uint64_t t19 = gm_l3;
            uint8_t t20 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t18, t19, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t20;
            goto gm_fret;
        }
gm_lb5:;
    }
gm_lb4:;
    gm_b16_8 t21 = gm_l2;
    uint8_t t22 = gm_f_is_builtin_type_name_v5ttoka(t21);
    F.ret = t22;
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
inline __attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_operand_3n5jchh F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* inner */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l6;
    void * t11 = gm_l0;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)28ULL));
            if (!t13) {
                goto gm_lb2;
            }
            gm_t3mf58gv9kf3n t14 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t11) + 8) + 8));
            gm_l1 = t14;
            void * t15 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
            void * t16 = gm_f_prefix_operand_3n5jchh(t15);
            gm_l6 = t16;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)24ULL));
            if (!t18) {
                goto gm_lb3;
            }
            void * t19 = GM_LD(void *, ((((uint8_t *)t11) + 8) + 8));
            gm_l2 = t19;
            void * t20 = gm_l2;
            void * t21 = gm_f_prefix_operand_3n5jchh(t20);
            gm_l6 = t21;
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)25ULL));
                    if (!t23) {
                        goto gm_lb6;
                    }
                    void * t24 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
                    gm_l3 = t24;
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)26ULL));
                    if (!t26) {
                        goto gm_lb7;
                    }
                    void * t27 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
                    gm_l3 = t27;
                    goto gm_lb5;
                }
gm_lb7:;
                {
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)27ULL));
                    if (!t29) {
                        goto gm_lb8;
                    }
                    void * t30 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
                    gm_l3 = t30;
                    goto gm_lb5;
                }
gm_lb8:;
                goto gm_lb4;
            }
gm_lb5:;
            void * t31 = gm_l3;
            void * t32 = gm_f_prefix_operand_3n5jchh(t31);
            gm_l6 = t32;
            goto gm_lb1;
        }
gm_lb4:;
        {
            void * t33 = gm_l0;
            gm_l6 = t33;
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 295, 64);
    }
gm_lb1:;
    void * t34 = gm_l6;
    F.ret = t34;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_push_down_hge098p F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* inner */
    void * gm_l2; /* b */
    void * gm_l3; /* b */
    void * gm_l4; /* b */
    void * gm_l5; /* b */
    void * gm_l6; /* b */
    void * gm_l7; /* b */
    gm_tao63pk3r18v5 gm_l8; /* x */
    void * gm_l9; /* callee */
    void * gm_l10; /* base */
    void * gm_l11; /* next */
    void * gm_l12; /* callee */
    void * gm_l13; /* down */
    void * gm_l14; /* i */
    gm_t08olq97rscn3 gm_l15; /* name */
    uint32_t gm_l16; /* i */
    gm_tl874t2atnlh3 gm_l17; /* at */
    gm_tao63pk3r18v5 gm_l18; /* x */
    gm_b16_8 gm_l19; /* args */
    uint8_t gm_l20; /* trailing */
    GM_STACK_CHECK(&F, 1841u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * gm_l22;
    void * gm_l23;
    gm_tq7s98ihonkju gm_l24;
    void * gm_l25;
    void * gm_l26;
    gm_tq7s98ihonkju gm_l27;
    gm_t2gtbh39ptekd gm_l28;
    gm_tq7s98ihonkju gm_l29;
    void * t26 = gm_l1;
    {
        {
            {
                {
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)20ULL));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    void * t29 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t29;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)22ULL));
                    if (!t31) {
                        goto gm_lb5;
                    }
                    void * t32 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t32;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)15ULL));
                    if (!t34) {
                        goto gm_lb6;
                    }
                    void * t35 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t35;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)16ULL));
                    if (!t37) {
                        goto gm_lb7;
                    }
                    void * t38 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t38;
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)18ULL));
                    if (!t40) {
                        goto gm_lb8;
                    }
                    void * t41 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t41;
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t42 = gm_l2;
            gm_l22 = t42;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)23ULL));
            if (!t44) {
                goto gm_lb9;
            }
            void * t45 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l7 = t45;
            void * t46 = gm_l7;
            gm_l22 = t46;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)21ULL));
            if (!t48) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t26) + 8) + 8), 32);
            void * t49 = GM_LD(void *, ((uint8_t *)&gm_l8));
            gm_l22 = t49;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)19ULL));
            if (!t51) {
                goto gm_lb11;
            }
            void * t52 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l9 = t52;
            void * t53 = gm_l9;
            uint8_t t54 = gm_f_calls_on_value_q0pa6h5(t53);
            if (t54) {
                void * t55 = gm_l9;
                gm_l23 = t55;
            } else {
                F.ret = ((void *)0);
                goto gm_fret;
                gm_l23 = ((void *)0);
            }
            void * t56 = gm_l23;
            gm_l22 = t56;
            goto gm_lb1;
        }
gm_lb11:;
        {
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 886, 13);
    }
gm_lb1:;
    void * t57 = gm_l22;
    gm_l10 = t57;
    void * t58 = gm_l10;
    gm_tq7s98ihonkju t59 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t58)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knjqrvnblc8)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l24 = t59;
    uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcsffver18si54, 893, 23);
    void * t61 = GM_LD(void *, (((uint8_t *)&gm_l24) + 8));
    gm_l11 = t61;
    {
        {
            void * t62 = gm_l10;
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t62) + 8) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)19ULL));
            if (!t64) {
                goto gm_lb14;
            }
            void * t65 = GM_LD(void *, ((((uint8_t *)t62) + 8) + 8));
            gm_l12 = t65;
            void * t66 = gm_l12;
            uint8_t t67 = gm_f_calls_on_value_q0pa6h5(t66);
            uint8_t t68 = (uint8_t)!t67;
            if (!t68) {
                goto gm_lb14;
            }
            void * t69 = gm_l0;
            void * t70 = gm_l10;
            void * t71 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t71;
            void * t72 = gm_l11;
            void * t73 = gm_l0;
            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 72));
            void * t75 = gm_l10;
            gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
            gm_tl874t2atnlh3 t77 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t74), GM_CONV(gm_cseahvg8v7p0vs, t76)));
            GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t72) + 72), t77);
            goto gm_lb13;
        }
gm_lb14:;
        void * t78 = gm_l0;
        void * t79 = gm_l10;
        void * t80 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t80;
        uint8_t t81 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l25)) != 0);
        if (t81) {
            void * t82 = GM_LD(void *, ((uint8_t *)&gm_l25));
            gm_l26 = t82;
        } else {
            F.ret = ((void *)0);
            goto gm_fret;
        }
        void * t83 = gm_l26;
        gm_l13 = t83;
        void * t84 = gm_l13;
        gm_tq7s98ihonkju t85 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t84)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kcodas3apkm)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l27 = t85;
        uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
        if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srcsffver18si54, 899, 16);
        void * t87 = GM_LD(void *, (((uint8_t *)&gm_l27) + 8));
        gm_l11 = t87;
    }
gm_lb13:;
    void * t88 = gm_l1;
    {
        {
            uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)20ULL));
            if (!t90) {
                goto gm_lb16;
            }
            void * t91 = GM_LD(void *, (((((uint8_t *)t88) + 8) + 8) + 8));
            gm_l14 = t91;
            void * t92 = gm_l11;
            void * t93 = gm_l14;
            gm_t3mf58gv9kf3n t94;
            GM_ST(void *, ((uint8_t *)&t94 + 0), t92);
            GM_ST(void *, ((uint8_t *)&t94 + 8), t93);
            gm_t2gtbh39ptekd t95;
            memset(&t95, 0, sizeof t95);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)20ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t95 + 8), t94);
            gm_l28 = t95;
            goto gm_lb15;
        }
gm_lb16:;
        {
            uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t97 = (uint8_t)(t96 == ((uint8_t)22ULL));
            if (!t97) {
                goto gm_lb17;
            }
            memmove(((uint8_t *)&gm_l15), (((((uint8_t *)t88) + 8) + 8) + 8), 32);
            void * t98 = gm_l11;
            gm_t08olq97rscn3 t99 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l15));
            gm_tioie2ju3q4ul t100;
            GM_ST(void *, ((uint8_t *)&t100 + 0), t98);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t100 + 8), t99);
            gm_t2gtbh39ptekd t101;
            GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t101 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t101 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t101 + 8), t100);
            gm_l28 = t101;
            goto gm_lb15;
        }
gm_lb17:;
        {
            uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t103 = (uint8_t)(t102 == ((uint8_t)15ULL));
            if (!t103) {
                goto gm_lb18;
            }
            void * t104 = gm_l11;
            gm_t2gtbh39ptekd t105;
            memset(&t105, 0, sizeof t105);
            GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)15ULL));
            GM_ST(void *, ((uint8_t *)&t105 + 8), t104);
            gm_l28 = t105;
            goto gm_lb15;
        }
gm_lb18:;
        {
            uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t107 = (uint8_t)(t106 == ((uint8_t)16ULL));
            if (!t107) {
                goto gm_lb19;
            }
            void * t108 = gm_l11;
            gm_t2gtbh39ptekd t109;
            memset(&t109, 0, sizeof t109);
            GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)16ULL));
            GM_ST(void *, ((uint8_t *)&t109 + 8), t108);
            gm_l28 = t109;
            goto gm_lb15;
        }
gm_lb19:;
        {
            uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)18ULL));
            if (!t111) {
                goto gm_lb20;
            }
            void * t112 = gm_l11;
            gm_t2gtbh39ptekd t113;
            memset(&t113, 0, sizeof t113);
            GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t113 + 8), t112);
            gm_l28 = t113;
            goto gm_lb15;
        }
gm_lb20:;
        {
            uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)23ULL));
            if (!t115) {
                goto gm_lb21;
            }
            uint32_t t116 = GM_LD(uint32_t, (((((uint8_t *)t88) + 8) + 8) + 8));
            gm_l16 = t116;
            gm_tl874t2atnlh3 t117 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t88) + 8) + 8) + 12));
            gm_l17 = t117;
            void * t118 = gm_l11;
            uint32_t t119 = gm_l16;
            gm_tl874t2atnlh3 t120 = gm_l17;
            gm_tupk62cr6lbdu t121;
            GM_ST(void *, ((uint8_t *)&t121 + 0), t118);
            GM_ST(uint32_t, ((uint8_t *)&t121 + 8), t119);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t121 + 12), t120);
            gm_t2gtbh39ptekd t122;
            memset(&t122, 0, sizeof t122);
            GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)23ULL));
            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t122 + 8), t121);
            gm_l28 = t122;
            goto gm_lb15;
        }
gm_lb21:;
        {
            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t124 = (uint8_t)(t123 == ((uint8_t)21ULL));
            if (!t124) {
                goto gm_lb22;
            }
            memmove(((uint8_t *)&gm_l18), ((((uint8_t *)t88) + 8) + 8), 32);
            void * t125 = gm_l11;
            void * t126 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
            void * t127 = GM_LD(void *, (((uint8_t *)&gm_l18) + 16));
            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 24));
            gm_tao63pk3r18v5 t129;
            GM_ST(uint8_t, (uint8_t *)&t129 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t129 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t129 + 28, 0);
            GM_ST(void *, ((uint8_t *)&t129 + 0), t125);
            GM_ST(void *, ((uint8_t *)&t129 + 8), t126);
            GM_ST(void *, ((uint8_t *)&t129 + 16), t127);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 24), t128);
            gm_t2gtbh39ptekd t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)21ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t130 + 8), t129);
            gm_l28 = t130;
            goto gm_lb15;
        }
gm_lb22:;
        {
            uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
            uint8_t t132 = (uint8_t)(t131 == ((uint8_t)19ULL));
            if (!t132) {
                goto gm_lb23;
            }
            gm_b16_8 t133 = GM_LD(gm_b16_8, (((((uint8_t *)t88) + 8) + 8) + 8));
            gm_l19 = t133;
            uint8_t t134 = GM_LD(uint8_t, (((((uint8_t *)t88) + 8) + 8) + 24));
            gm_l20 = t134;
            void * t135 = gm_l11;
            gm_b16_8 t136 = gm_l19;
            uint8_t t137 = gm_l20;
            gm_tlljg61a2ctgq t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 28, 0);
            GM_ST(void *, ((uint8_t *)&t138 + 0), t135);
            GM_ST(gm_b16_8, ((uint8_t *)&t138 + 8), t136);
            GM_ST(uint8_t, ((uint8_t *)&t138 + 24), t137);
            gm_t2gtbh39ptekd t139;
            GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t139 + 8), t138);
            gm_l28 = t139;
            goto gm_lb15;
        }
gm_lb23:;
        {
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 901, 26);
    }
gm_lb15:;
    memmove(((uint8_t *)&F.l21), ((uint8_t *)&gm_l28), 64);
    void * t140 = gm_l1;
    uint32_t t141 = GM_LD(uint32_t, ((uint8_t *)t140));
    void * t142 = gm_l1;
    gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t142) + 72));
    gm_t2gtbh39ptekd t144 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l21));
    gm_tc3oht4ksth9d t145;
    GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t145 + 84, 0);
    GM_ST(uint32_t, ((uint8_t *)&t145 + 0), t141);
    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t145 + 8), t144);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t145 + 72), t143);
    gm_tq7s98ihonkju t146 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t145, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k647t05rm86)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l29 = t146;
    uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcsffver18si54, 912, 12);
    void * t148 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
    F.ret = t148;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pushes_down_0djaflp F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    void * gm_l5; /* base */
    void * gm_l6; /* base */
    gm_tao63pk3r18v5 gm_l7; /* x */
    void * gm_l8; /* callee */
    void * gm_l9; /* next */
    void * gm_l10; /* callee */
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l11;
    void * gm_l12;
    void * t16 = gm_l0;
    {
        {
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)20ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    void * t19 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t19;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)22ULL));
                    if (!t21) {
                        goto gm_lb5;
                    }
                    void * t22 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t22;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)15ULL));
                    if (!t24) {
                        goto gm_lb6;
                    }
                    void * t25 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t25;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)16ULL));
                    if (!t27) {
                        goto gm_lb7;
                    }
                    void * t28 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t28;
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)18ULL));
                    if (!t30) {
                        goto gm_lb8;
                    }
                    void * t31 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t31;
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t32 = gm_l1;
            gm_l11 = t32;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)23ULL));
            if (!t34) {
                goto gm_lb9;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l6 = t35;
            void * t36 = gm_l6;
            gm_l11 = t36;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)21ULL));
            if (!t38) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t16) + 8) + 8), 32);
            void * t39 = GM_LD(void *, ((uint8_t *)&gm_l7));
            gm_l11 = t39;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)19ULL));
            if (!t41) {
                goto gm_lb11;
            }
            void * t42 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l8 = t42;
            void * t43 = gm_l8;
            uint8_t t44 = gm_f_calls_on_value_q0pa6h5(t43);
            if (t44) {
                void * t45 = gm_l8;
                gm_l12 = t45;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
                gm_l12 = ((void *)0);
            }
            void * t46 = gm_l12;
            gm_l11 = t46;
            goto gm_lb1;
        }
gm_lb11:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 303, 13);
    }
gm_lb1:;
    void * t47 = gm_l11;
    gm_l9 = t47;
    {
        {
            void * t48 = gm_l9;
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t48) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)19ULL));
            if (!t50) {
                goto gm_lb14;
            }
            void * t51 = GM_LD(void *, ((((uint8_t *)t48) + 8) + 8));
            gm_l10 = t51;
            void * t52 = gm_l10;
            uint8_t t53 = gm_f_calls_on_value_q0pa6h5(t52);
            uint8_t t54 = (uint8_t)!t53;
            if (!t54) {
                goto gm_lb14;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb14:;
    }
gm_lb13:;
    void * t55 = gm_l9;
    uint8_t t56 = gm_f_pushes_down_0djaflp(t55);
    F.ret = t56;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_jjodbei F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    uint8_t gm_l4; /* sized */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l5;
    gm_t53at80pfdus3 gm_l6;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l1;
    void * t14 = gm_l2;
    gm_t88rcucg1b7uk t15 = gm_f_resolve_type_unchecked_psme7dn((void *)NULL, cx, NULL, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t15;
    void * t16 = gm_l0;
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_t88rcucg1b7uk t18 = gm_l3;
    gm_f_kind_jcnt4hm((void *)&gm_l6, t17, GM_CONV(gm_csguankko4lmhd, t18));
    {
        {
            {
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)11ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)12ULL));
                    if (!t22) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)13ULL));
                    if (!t24) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)14ULL));
                    if (!t26) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 320, 14);
    }
gm_lb1:;
    uint8_t t27 = gm_l5;
    gm_l4 = t27;
    {
        {
            uint8_t t28 = gm_l4;
            if (!t28) {
                goto gm_lb10;
            }
            void * t29 = gm_l0;
            uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t29) + 1848) + 8));
            uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
            if (!t31) {
                goto gm_lb10;
            }
            void * t32 = gm_l0;
            uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)t32) + 1936));
            uint8_t t34 = ((t33 == ((uint32_t)0ULL)));
            if (!t34) {
                goto gm_lb10;
            }
            void * t35 = gm_l0;
            gm_t88rcucg1b7uk t36 = gm_l3;
            void * t37 = gm_l2;
            gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
            uint8_t t39 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t35, t36, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = (uint8_t)!t39;
            if (!t40) {
                goto gm_lb10;
            }
            void * t41 = gm_l0;
            void * t42 = (void *)(((uint8_t *)t41) + 8);
            gm_t88rcucg1b7uk t43 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t43;
            goto gm_fret;
        }
gm_lb10:;
    }
gm_lb9:;
    gm_t88rcucg1b7uk t44 = gm_l3;
    F.ret = t44;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_unchecked_psme7dn F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* tys */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint32_t gm_l6; /* p */
    uint64_t gm_l7; /* d */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* t */
    void * gm_l10; /* inner */
    gm_b16_8 gm_l11; /* elems */
    uint8_t gm_l12; /* named */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_t08olq97rscn3 gm_l21; /* n */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l25; /* n */
    void * gm_l26; /* inner */
    uint8_t gm_l27; /* is_const */
    void * gm_l28; /* inner */
    void * gm_l29; /* sig */
    uint8_t gm_l31; /* is_const */
    void * gm_l32; /* inner */
    uint8_t gm_l33; /* is_const */
    void * gm_l34; /* inner */
    gm_t3mf58gv9kf3n gm_l35; /* a */
    gm_t88rcucg1b7uk gm_l36; /* elem */
    void * gm_l37; /* len */
    gm_t88rcucg1b7uk gm_l38; /* index */
    uint64_t gm_l39; /* n */
    gm_t88rcucg1b7uk gm_l40; /* l */
    void * gm_l41; /* err */
    void * gm_l42; /* ok */
    void * gm_l43; /* err */
    void * gm_l44; /* ok */
    gm_t88rcucg1b7uk gm_l45; /* et */
    gm_t88rcucg1b7uk gm_l46; /* ot */
    void * gm_l47; /* sig */
    void * gm_l49; /* inner */
    void * gm_l50; /* callee */
    gm_b16_8 gm_l51; /* args */
    void * gm_l52; /* base */
    gm_t9805qtonbsag gm_l53; /* template */
    gm_tkb8hpcqf70t1 gm_l54; /* g */
    uint32_t gm_l55; /* t */
    gm_t08olq97rscn3 gm_l56; /* id */
    uint32_t gm_l57; /* t */
    gm_b16_8 gm_l58; /* targs */
    gm_b16_8 gm_l59; /* params */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    uint64_t gm_l62; /* i */
    uint32_t gm_l63; /* pid */
    gm_t88rcucg1b7uk gm_l64; /* a */
    gm_b16_8 gm_l65; /* <tmp> */
    uint64_t gm_l66; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l67; /* c */
    gm_b16_8 gm_l68; /* <tmp> */
    uint64_t gm_l69; /* <tmp> */
    gm_t88rcucg1b7uk gm_l70; /* a */
    gm_t88rcucg1b7uk gm_l71; /* part */
    gm_t88rcucg1b7uk gm_l72; /* inst */
    gm_b16_8 gm_l73; /* attrs */
    void * gm_l74; /* inner */
    void * gm_l75; /* f */
    gm_b16_8 gm_l76; /* <tmp> */
    uint64_t gm_l77; /* <tmp> */
    gm_b16_8 gm_l79; /* name */
    gm_b16_8 gm_l81; /* args */
    void * gm_l83; /* hook */
    void * gm_l84; /* base */
    gm_t08olq97rscn3 gm_l85; /* name */
    uint64_t gm_l86; /* m */
    uint64_t gm_l87; /* d */
    gm_t88rcucg1b7uk gm_l88; /* t */
    GM_STACK_CHECK(&F, 8062u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l89;
    gm_t53at80pfdus3 gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_tf5srh4mp4b3i gm_l92;
    gm_t6qooom8qa2do gm_l93;
    gm_tabb710d9mq2e gm_l94;
    gm_tkn42u7na3t81 gm_l95;
    uint8_t gm_l96;
    uint8_t gm_l97;
    gm_tftb1odc739t5 gm_l98;
    gm_t08olq97rscn3 gm_l99;
    gm_tftb1odc739t5 gm_l100;
    gm_tf1bjg81u0glv gm_l101;
    gm_tabb710d9mq2e gm_l102;
    gm_tg93b272d11vo gm_l103;
    gm_tqqhoivahirr0 gm_l104;
    void * gm_l105;
    gm_t5e6kkuq21nig gm_l106;
    gm_tg93b272d11vo gm_l107;
    gm_tqqhoivahirr0 gm_l108;
    gm_toblvhepnjqkh gm_l109;
    gm_t6qooom8qa2do gm_l110;
    uint8_t gm_l111;
    uint32_t gm_l112;
    uint8_t gm_l113;
    gm_tf5srh4mp4b3i gm_l114;
    uint8_t gm_l115;
    gm_tkn42u7na3t81 gm_l116;
    gm_th2lqbo77pvsc gm_l117;
    gm_b16_8 gm_l118;
    gm_tn68el8d14vku gm_l119;
    gm_tftb1odc739t5 gm_l120;
    gm_tkn42u7na3t81 gm_l121;
    uint8_t gm_l122;
    uint8_t gm_l123;
    uint8_t gm_l124;
    uint8_t gm_l125;
    uint8_t gm_l126;
    uint8_t gm_l127;
    gm_b16_8 gm_l128;
    uint8_t gm_l129;
    void * gm_l130;
    gm_tf5srh4mp4b3i gm_l131;
    gm_tf5srh4mp4b3i gm_l132;
    uint64_t gm_l133;
    gm_t6qooom8qa2do gm_l134;
    gm_tabb710d9mq2e gm_l135;
    {
        void * t135 = gm_l0;
        void * t136 = (void *)(((uint8_t *)t135) + 8);
        gm_l3 = t136;
        void * t137 = gm_l2;
        {
            {
                {
                    uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)6ULL));
                    if (!t139) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t137) + 8) + 8), 32);
                    {
                        {
                            void * t140 = gm_l0;
                            gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                            gm_tkn42u7na3t81 t142 = gm_f_type_param_3av4r8i(t140, t141);
                            gm_l89 = t142;
                            uint8_t t143 = (uint8_t)((((uint8_t *)&gm_l89))[4] != 0);
                            if (!t143) {
                                goto gm_lb4;
                            }
                            gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l89));
                            gm_l5 = t144;
                            {
                                {
                                    void * t145 = gm_l3;
                                    gm_t88rcucg1b7uk t146 = gm_l5;
                                    gm_f_kind_jcnt4hm((void *)&gm_l90, t145, GM_CONV(gm_csguankko4lmhd, t146));
                                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)19ULL));
                                    if (!t148) {
                                        goto gm_lb6;
                                    }
                                    uint32_t t149 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 8));
                                    gm_l6 = t149;
                                    void * t150 = gm_l0;
                                    void * t151 = GM_LD(void *, (((uint8_t *)t150) + 1128));
                                    uint64_t t152 = GM_LD(uint64_t, ((((uint8_t *)t150) + 1128) + 8));
                                    uint32_t t153 = gm_l6;
                                    uint64_t t154 = (uint64_t)t153;
                                    if ((uintptr_t)t154 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t154, t152, gm_srcsffver18si54, 335, 49);
                                    gm_tnnjeerolos0a t155 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t151) + (uintptr_t)t154 * 48));
                                    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&t155) + 0));
                                    uint8_t t157 = (uint8_t)((t156 == ((uint8_t)1ULL)));
                                    if (!t157) {
                                        goto gm_lb6;
                                    }
                                    void * t158 = gm_l0;
                                    void * t159 = gm_l2;
                                    gm_tl874t2atnlh3 t160 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t159) + 72));
                                    gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                                    gm_tnpifv20irnph t162;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t162 + 0), t161);
                                    gm_tnnjeerolos0a t163;
                                    GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)1ULL));
                                    gm_f_format_o5o5km2((void *)&gm_l91, (void *)NULL, cx, NULL, t162);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                    if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srcsffver18si54, 336, 37);
                                    gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                                    gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                                    gm_b0_1 t167 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t158, t160, t166);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t168 = gm_l3;
                                    gm_t88rcucg1b7uk t169 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t168);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t169;
                                    goto gm_fret;
                                }
gm_lb6:;
                            }
gm_lb5:;
                            gm_t88rcucg1b7uk t170 = gm_l5;
                            F.ret = t170;
                            goto gm_fret;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    {
                        {
                            void * t171 = gm_l0;
                            uint64_t t172 = gm_l1;
                            gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                            gm_tf5srh4mp4b3i t174 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t171, t172, t173);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l92 = t174;
                            uint8_t t175 = (uint8_t)((((uint8_t *)&gm_l92))[8] != 0);
                            if (!t175) {
                                goto gm_lb8;
                            }
                            uint64_t t176 = GM_LD(uint64_t, ((uint8_t *)&gm_l92));
                            gm_l7 = t176;
                            void * t177 = gm_l0;
                            uint64_t t178 = gm_l7;
                            gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                            gm_b0_1 t180 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t177, t178, t179);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t181 = gm_l0;
                            uint64_t t182 = gm_l7;
                            gm_f_resolve_decl_pus042v((void *)&gm_l93, (void *)NULL, cx, NULL, t181, t182);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l93) + 0));
                                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)0ULL));
                                    if (!t184) {
                                        goto gm_lb10;
                                    }
                                    gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l93) + 8));
                                    gm_l8 = t185;
                                    gm_t88rcucg1b7uk t186 = gm_l8;
                                    F.ret = t186;
                                    goto gm_fret;
                                }
gm_lb10:;
                                {
                                    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l93) + 0));
                                    uint8_t t188 = (uint8_t)(t187 == ((uint8_t)11ULL));
                                    if (!t188) {
                                        goto gm_lb11;
                                    }
                                    void * t189 = gm_l3;
                                    gm_t88rcucg1b7uk t190 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t189);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t190;
                                    goto gm_fret;
                                }
gm_lb11:;
                                {
                                    void * t191 = gm_l0;
                                    void * t192 = gm_l2;
                                    gm_tl874t2atnlh3 t193 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t192) + 72));
                                    gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                                    gm_tnpifv20irnph t195;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t195 + 0), t194);
                                    gm_f_format_sv91363((void *)&gm_l94, (void *)NULL, cx, NULL, t195);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t196 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                                    if (!t196) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 8, gm_srcsffver18si54, 347, 41);
                                    gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                                    gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
                                    gm_b0_1 t199 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t191, t193, t198);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t200 = gm_l3;
                                    gm_t88rcucg1b7uk t201 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t200);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t201;
                                    goto gm_fret;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 343, 17);
                            }
gm_lb9:;
                        }
gm_lb8:;
                    }
gm_lb7:;
                    {
                        {
                            void * t202 = gm_l0;
                            gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                            gm_tkn42u7na3t81 t204 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t202, t203);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l95 = t204;
                            uint8_t t205 = (uint8_t)((((uint8_t *)&gm_l95))[4] != 0);
                            if (!t205) {
                                goto gm_lb14;
                            }
                            gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l95));
                            gm_l9 = t206;
                            gm_t88rcucg1b7uk t207 = gm_l9;
                            F.ret = t207;
                            goto gm_fret;
                        }
gm_lb14:;
                    }
gm_lb13:;
                    void * t208 = gm_l0;
                    uint64_t t209 = gm_l1;
                    gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    void * t211 = gm_l2;
                    gm_tl874t2atnlh3 t212 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t211) + 72));
                    gm_b0_1 t213 = gm_f_unknown_type_beat5io((void *)NULL, cx, NULL, t208, t209, t210, t212);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t214 = gm_l3;
                    gm_t88rcucg1b7uk t215 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t215;
                    goto gm_fret;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t217 = (uint8_t)(t216 == ((uint8_t)9ULL));
                    if (!t217) {
                        goto gm_lb15;
                    }
                    void * t218 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l10 = t218;
                    void * t219 = gm_l0;
                    uint64_t t220 = gm_l1;
                    void * t221 = gm_l10;
                    gm_t88rcucg1b7uk t222 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t219, t220, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t222;
                    goto gm_fret;
                }
gm_lb15:;
                {
                    uint8_t t223 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t224 = (uint8_t)(t223 == ((uint8_t)10ULL));
                    if (!t224) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t225 = GM_LD(gm_b16_8, ((((uint8_t *)t137) + 8) + 8));
                    gm_l11 = t225;
                    gm_b16_8 t226 = gm_l11;
                    gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
                    uint8_t t228 = gm_f_has_spread_bndtpd6(t227);
                    if (t228) {
                        void * t229 = gm_l0;
                        void * t230 = gm_l2;
                        gm_tl874t2atnlh3 t231 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t230) + 72));
                        gm_b16_8 t232;
                        GM_ST(void *, (uint8_t *)&t232, (void *)(gm_shbaullahutpe));
                        GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(36));
                        gm_b0_1 t233 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t229, t231, t232);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t234 = gm_l3;
                        gm_t88rcucg1b7uk t235 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t234);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t235;
                        goto gm_fret;
                    }
                    uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t237 = (t236 > ((uint64_t)0ULL));
                    if (t237) {
                        void * t238 = GM_LD(void *, ((uint8_t *)&gm_l11));
                        uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        {
                            {
                                uint8_t t240 = (uint8_t)(((((uint8_t *)t238) + (uintptr_t)((uint64_t)0ULL) * 136))[32] != 0);
                                if (t240) {
                                    goto gm_lb18;
                                }
                                gm_l97 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l97 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 362, 40);
                        }
gm_lb17:;
                        uint8_t t241 = gm_l97;
                        gm_l96 = t241;
                    } else {
                        gm_l96 = ((uint8_t)0);
                    }
                    uint8_t t242 = gm_l96;
                    gm_l12 = t242;
                    gm_trqfvmjtt4sij t243;
                    gm_f_list_nof7vfj((void *)&t243, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l13 = t243;
                    gm_b16_8 t244 = gm_l11;
                    gm_l14 = t244;
                    gm_l15 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t245 = gm_l15;
                            uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            uint8_t t247 = (t245 < t246);
                            if (!t247) {
                                goto gm_lb20;
                            }
                            {
                                void * t248 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint64_t t250 = gm_l15;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l16), (((uint8_t *)t248) + (uintptr_t)t250 * 136), 136);
                                        void * t251 = (void *)((uint8_t *)&F.l13);
                                        void * t252 = gm_l0;
                                        uint64_t t253 = gm_l1;
                                        void * t254 = (void *)(((uint8_t *)&F.l16) + 40);
                                        gm_t88rcucg1b7uk t255 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t252, t253, t254);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t256 = gm_f_push_k3l9nic(t251, GM_CONV(gm_csguankko4lmhd, t255));
                                        gm_l98 = t256;
                                        uint8_t t257 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                                        if (!t257) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 1, gm_srcsffver18si54, 364, 31);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 364, 13);
                                }
gm_lb23:;
                            }
gm_lb22:;
                            uint64_t t258 = gm_l15;
                            uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                            uint64_t t261 = r259;
                            gm_l15 = t261;
                        }
                    }
gm_lb20:;
                    gm_trqfvmjtt4sij t262;
                    gm_f_list_epvducg((void *)&t262, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l17 = t262;
                    uint8_t t263 = gm_l12;
                    if (t263) {
                        gm_b16_8 t264 = gm_l11;
                        gm_l18 = t264;
                        gm_l19 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t265 = gm_l19;
                                uint64_t t266 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                uint8_t t267 = (t265 < t266);
                                if (!t267) {
                                    goto gm_lb25;
                                }
                                {
                                    void * t268 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                    uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                    uint64_t t270 = gm_l19;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l20), (((uint8_t *)t268) + (uintptr_t)t270 * 136), 136);
                                            uint8_t t271 = (uint8_t)((((uint8_t *)&F.l20))[32] != 0);
                                            if (t271) {
                                                memmove(((uint8_t *)&gm_l99), ((uint8_t *)&F.l20), 32);
                                            } else {
                                                void * t272 = gm_l0;
                                                void * t273 = gm_l2;
                                                gm_tl874t2atnlh3 t274 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t273) + 72));
                                                gm_b16_8 t275;
                                                GM_ST(void *, (uint8_t *)&t275, (void *)(gm_sjdn5gs7lu39i));
                                                GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(56));
                                                gm_b0_1 t276 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t272, t274, t275);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t277 = gm_l3;
                                                gm_t88rcucg1b7uk t278 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t277);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t278;
                                                goto gm_fret;
                                            }
                                            memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l99), 32);
                                            void * t279 = (void *)((uint8_t *)&F.l17);
                                            gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                            gm_tftb1odc739t5 t281 = gm_f_push_3ef746b(t279, t280);
                                            gm_l100 = t281;
                                            uint8_t t282 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                                            if (!t282) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 1, gm_srcsffver18si54, 372, 21);
                                            goto gm_lb28;
                                        }
gm_lb29:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 367, 17);
                                    }
gm_lb28:;
                                }
gm_lb27:;
                                uint64_t t283 = gm_l19;
                                uint64_t r284; bool o285 = __builtin_add_overflow(t283, ((uint64_t)1ULL), &r284);
                                uint64_t t286 = r284;
                                gm_l19 = t286;
                            }
                        }
gm_lb25:;
                        gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                        gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
                        gm_l22 = t288;
                        gm_l23 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t289 = gm_l23;
                                uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                uint8_t t291 = (t289 < t290);
                                if (!t291) {
                                    goto gm_lb30;
                                }
                                {
                                    uint64_t t292 = gm_l23;
                                    void * t293 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                    uint64_t t294 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                    uint64_t t295 = gm_l23;
                                    gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)t293) + (uintptr_t)t295 * 16));
                                    gm_tf1bjg81u0glv t297;
                                    GM_ST(uint64_t, ((uint8_t *)&t297 + 0), t292);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t297 + 8), t296);
                                    gm_l101 = t297;
                                    {
                                        {
                                            uint64_t t298 = GM_LD(uint64_t, ((uint8_t *)&gm_l101));
                                            gm_l24 = t298;
                                            gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
                                            gm_l25 = t299;
                                            gm_b16_8 t300 = gm_l25;
                                            void * t301 = GM_LD(void *, ((uint8_t *)&F.l17));
                                            uint64_t t302 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                            uint64_t t303 = gm_l24;
                                            uint64_t t304 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t305 = (uintptr_t)t303;
                                            if (t304 > t305 || t305 > (uintptr_t)t302) gm_trap_slice(t304, t305, t302, gm_srcsffver18si54, 375, 29);
                                            uint64_t t306 = ((uintptr_t)t303) - (uintptr_t)((uint64_t)0ULL);
                                            gm_b16_8 t307;
                                            GM_ST(void *, (uint8_t *)&t307, (void *)(((uint8_t *)t301 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                            GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(t306));
                                            uint8_t t308 = 0;
                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t307) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t307))) + i * 16, ((uint8_t *)&t300))) { t308 = 1; break; }
                                            uint8_t t309 = (uint8_t)(t308);
                                            if (t309) {
                                                void * t310 = gm_l0;
                                                void * t311 = gm_l2;
                                                gm_tl874t2atnlh3 t312 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t311) + 72));
                                                gm_b16_8 t313 = gm_l25;
                                                gm_tnpifv20irnph t314;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t314 + 0), t313);
                                                gm_f_format_6nmb1bf((void *)&gm_l102, (void *)NULL, cx, NULL, t314);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t315 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                                                if (!t315) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 8, gm_srcsffver18si54, 376, 41);
                                                gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l102) + 8));
                                                gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
                                                gm_b0_1 t318 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t310, t312, t317);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t319 = gm_l3;
                                                gm_t88rcucg1b7uk t320 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t319);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t320;
                                                goto gm_fret;
                                            }
                                            goto gm_lb33;
                                        }
gm_lb34:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 374, 17);
                                    }
gm_lb33:;
                                }
gm_lb32:;
                                uint64_t t321 = gm_l23;
                                uint64_t r322; bool o323 = __builtin_add_overflow(t321, ((uint64_t)1ULL), &r322);
                                uint64_t t324 = r322;
                                gm_l23 = t324;
                            }
                        }
gm_lb30:;
                    }
                    void * t325 = gm_l3;
                    gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                    gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
                    gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                    gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
                    gm_t6j6smj7qoctu t330;
                    GM_ST(gm_b16_8, ((uint8_t *)&t330 + 0), t327);
                    GM_ST(gm_b16_8, ((uint8_t *)&t330 + 16), t329);
                    gm_t53at80pfdus3 t331;
                    GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t331 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t331 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)12ULL));
                    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t331 + 8), t330);
                    gm_t88rcucg1b7uk t332 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t325, &t331);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t332;
                    goto gm_fret;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t333 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t334 = (uint8_t)(t333 == ((uint8_t)24ULL));
                    if (!t334) {
                        goto gm_lb35;
                    }
                    void * t335 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l26 = t335;
                    void * t336 = gm_l3;
                    void * t337 = gm_l0;
                    uint64_t t338 = gm_l1;
                    void * t339 = gm_l26;
                    gm_t88rcucg1b7uk t340 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t337, t338, t339);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t341 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t336, t340);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t341;
                    goto gm_fret;
                }
gm_lb35:;
                {
                    uint8_t t342 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t343 = (uint8_t)(t342 == ((uint8_t)25ULL));
                    if (!t343) {
                        goto gm_lb36;
                    }
                    uint8_t t344 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 8));
                    gm_l27 = t344;
                    void * t345 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                    gm_l28 = t345;
                    {
                        {
                            void * t346 = gm_l28;
                            uint8_t t347 = GM_LD(uint8_t, ((((uint8_t *)t346) + 8) + 0));
                            uint8_t t348 = (uint8_t)(t347 == ((uint8_t)30ULL));
                            if (!t348) {
                                goto gm_lb38;
                            }
                            void * t349 = GM_LD(void *, ((((uint8_t *)t346) + 8) + 8));
                            gm_l29 = t349;
                            uint8_t t350 = gm_l27;
                            if (t350) {
                                void * t351 = gm_l0;
                                void * t352 = gm_l2;
                                gm_tl874t2atnlh3 t353 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t352) + 72));
                                gm_b16_8 t354;
                                GM_ST(void *, (uint8_t *)&t354, (void *)(gm_sgva3apqq9jo2));
                                GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(67));
                                gm_b0_1 t355 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t351, t353, t354);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            void * t356 = gm_l0;
                            uint64_t t357 = gm_l1;
                            void * t358 = gm_l29;
                            gm_b16_8 t359 = gm_f_empty_slice_3jt8vgk();
                            gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
                            gm_f_resolve_fn_sig_pamuhve((void *)&gm_l103, (void *)NULL, cx, NULL, t356, t357, t358, ((uint8_t)0), t360);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t361 = (uint8_t)((((uint8_t *)&gm_l103))[136] != 0);
                            if (t361) {
                                memmove(((uint8_t *)&gm_l104), ((uint8_t *)&gm_l103), 136);
                            } else {
                                void * t362 = gm_l3;
                                gm_t88rcucg1b7uk t363 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t362);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t363;
                                goto gm_fret;
                            }
                            memmove(((uint8_t *)&F.l30), ((uint8_t *)&gm_l104), 136);
                            void * t364 = gm_l3;
                            void * t365 = gm_l0;
                            void * t366 = (void *)((uint8_t *)&F.l30);
                            gm_tddtjfggdat0d t367;
                            gm_f_fn_ty_qaih7ts((void *)&t367, (void *)NULL, cx, NULL, t365, t366);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t53at80pfdus3 t368;
                            GM_ST(uint8_t, (uint8_t *)&t368 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t368 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t368 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t368 + 0), ((uint8_t)16ULL));
                            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t368 + 8), t367);
                            gm_t88rcucg1b7uk t369 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t364, &t368);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t369;
                            goto gm_fret;
                        }
gm_lb38:;
                    }
gm_lb37:;
                    void * t370 = gm_l3;
                    uint8_t t371 = gm_l27;
                    void * t372 = gm_l0;
                    uint64_t t373 = gm_l1;
                    void * t374 = gm_l28;
                    gm_t88rcucg1b7uk t375 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t372, t373, t374);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t376 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t370, t371, t375);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t376;
                    goto gm_fret;
                }
gm_lb36:;
            }
            {
                {
                    uint8_t t377 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t378 = (uint8_t)(t377 == ((uint8_t)26ULL));
                    if (!t378) {
                        goto gm_lb39;
                    }
                    uint8_t t379 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 8));
                    gm_l31 = t379;
                    void * t380 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                    gm_l32 = t380;
                    void * t381 = gm_l3;
                    uint8_t t382 = gm_l31;
                    void * t383 = gm_l0;
                    uint64_t t384 = gm_l1;
                    void * t385 = gm_l32;
                    gm_t88rcucg1b7uk t386 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t383, t384, t385);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t387 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t381, t382, t386);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t387;
                    goto gm_fret;
                }
gm_lb39:;
                {
                    uint8_t t388 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t389 = (uint8_t)(t388 == ((uint8_t)27ULL));
                    if (!t389) {
                        goto gm_lb40;
                    }
                    uint8_t t390 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 8));
                    gm_l33 = t390;
                    void * t391 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                    gm_l34 = t391;
                    void * t392 = gm_l3;
                    uint8_t t393 = gm_l33;
                    void * t394 = gm_l0;
                    uint64_t t395 = gm_l1;
                    void * t396 = gm_l34;
                    gm_t88rcucg1b7uk t397 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t394, t395, t396);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t398 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t392, t393, t397);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t398;
                    goto gm_fret;
                }
gm_lb40:;
                {
                    uint8_t t399 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t400 = (uint8_t)(t399 == ((uint8_t)28ULL));
                    if (!t400) {
                        goto gm_lb41;
                    }
                    gm_t3mf58gv9kf3n t401 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t137) + 8) + 8));
                    gm_l35 = t401;
                    void * t402 = gm_l0;
                    uint64_t t403 = gm_l1;
                    void * t404 = GM_LD(void *, (((uint8_t *)&gm_l35) + 8));
                    gm_t88rcucg1b7uk t405 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t402, t403, t404);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l36 = t405;
                    uint8_t t406 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l35)) != 0);
                    if (t406) {
                        void * t407 = GM_LD(void *, ((uint8_t *)&gm_l35));
                        gm_l105 = t407;
                    } else {
                        void * t408 = gm_l0;
                        void * t409 = gm_l2;
                        gm_tl874t2atnlh3 t410 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t409) + 72));
                        gm_b16_8 t411;
                        GM_ST(void *, (uint8_t *)&t411, (void *)(gm_sm9funvq1222p));
                        GM_ST(uintptr_t, (uint8_t *)&t411 + 8, (uintptr_t)(43));
                        gm_b0_1 t412 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t408, t410, t411);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t413 = gm_l3;
                        gm_t88rcucg1b7uk t414 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t413);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t414;
                        goto gm_fret;
                    }
                    void * t415 = gm_l105;
                    gm_l37 = t415;
                    void * t416 = gm_l0;
                    uint64_t t417 = gm_l1;
                    void * t418 = gm_l37;
                    uint8_t t419 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t416, t417, t418);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t419) {
                        void * t420 = gm_l0;
                        uint64_t t421 = gm_l1;
                        void * t422 = gm_l37;
                        gm_t88rcucg1b7uk t423 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t420, t421, t422);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l38 = t423;
                        void * t424 = gm_l0;
                        gm_t88rcucg1b7uk t425 = gm_l38;
                        gm_t88rcucg1b7uk t426 = gm_l36;
                        void * t427 = gm_l37;
                        gm_tl874t2atnlh3 t428 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t427) + 72));
                        gm_t88rcucg1b7uk t429 = gm_f_enum_array_type_0sic6t0((void *)NULL, cx, NULL, t424, t425, t426, t428);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t429;
                        goto gm_fret;
                    }
                    void * t430 = gm_l0;
                    uint64_t t431 = gm_l1;
                    void * t432 = gm_l37;
                    gm_t5e6kkuq21nig t433 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t430, t431, t432);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l106 = t433;
                    {
                        {
                            uint8_t t434 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 0));
                            uint8_t t435 = (uint8_t)(t434 == ((uint8_t)0ULL));
                            if (!t435) {
                                goto gm_lb43;
                            }
                            uint64_t t436 = GM_LD(uint64_t, (((uint8_t *)&gm_l106) + 8));
                            gm_l39 = t436;
                            void * t437 = gm_l3;
                            uint64_t t438 = gm_l39;
                            gm_t88rcucg1b7uk t439 = gm_l36;
                            gm_t88rcucg1b7uk t440 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t437, t438, t439);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t440;
                            goto gm_fret;
                        }
gm_lb43:;
                        {
                            uint8_t t441 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 0));
                            uint8_t t442 = (uint8_t)(t441 == ((uint8_t)1ULL));
                            if (!t442) {
                                goto gm_lb44;
                            }
                            gm_t88rcucg1b7uk t443 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l106) + 8));
                            gm_l40 = t443;
                            void * t444 = gm_l3;
                            gm_t88rcucg1b7uk t445 = gm_l40;
                            gm_t88rcucg1b7uk t446 = gm_l36;
                            gm_tgglga8ro3isc t447;
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t447 + 0), t445);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t447 + 4), t446);
                            gm_t53at80pfdus3 t448;
                            memset(&t448, 0, sizeof t448);
                            GM_ST(uint8_t, ((uint8_t *)&t448 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t448 + 8), t447);
                            gm_t88rcucg1b7uk t449 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t444, &t448);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t449;
                            goto gm_fret;
                        }
gm_lb44:;
                        {
                            uint8_t t450 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 0));
                            uint8_t t451 = (uint8_t)(t450 == ((uint8_t)2ULL));
                            if (!t451) {
                                goto gm_lb45;
                            }
                            void * t452 = gm_l3;
                            gm_t88rcucg1b7uk t453 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t452);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t453;
                            goto gm_fret;
                        }
gm_lb45:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 404, 13);
                    }
gm_lb42:;
                }
gm_lb41:;
            }
            {
                {
                    {
                        {
                            uint8_t t454 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                            uint8_t t455 = (uint8_t)(t454 == ((uint8_t)29ULL));
                            if (!t455) {
                                goto gm_lb48;
                            }
                            void * t456 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                            gm_l41 = t456;
                            void * t457 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                            gm_l42 = t457;
                            goto gm_lb47;
                        }
gm_lb48:;
                        {
                            uint8_t t458 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                            uint8_t t459 = (uint8_t)(t458 == ((uint8_t)17ULL));
                            if (!t459) {
                                goto gm_lb49;
                            }
                            void * t460 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                            gm_l41 = t460;
                            void * t461 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                            gm_l42 = t461;
                            goto gm_lb47;
                        }
gm_lb49:;
                        goto gm_lb46;
                    }
gm_lb47:;
                    void * t462 = gm_l0;
                    uint64_t t463 = gm_l1;
                    void * t464 = gm_l41;
                    gm_t88rcucg1b7uk t465 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t462, t463, t464);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l45 = t465;
                    void * t466 = gm_l0;
                    uint64_t t467 = gm_l1;
                    void * t468 = gm_l42;
                    gm_t88rcucg1b7uk t469 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t466, t467, t468);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l46 = t469;
                    void * t470 = gm_l3;
                    gm_t88rcucg1b7uk t471 = gm_l45;
                    gm_t88rcucg1b7uk t472 = gm_l46;
                    gm_t88rcucg1b7uk t473 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t470, t471, t472);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t473;
                    goto gm_fret;
                }
gm_lb46:;
                {
                    uint8_t t474 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t475 = (uint8_t)(t474 == ((uint8_t)30ULL));
                    if (!t475) {
                        goto gm_lb50;
                    }
                    void * t476 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l47 = t476;
                    void * t477 = gm_l0;
                    uint64_t t478 = gm_l1;
                    void * t479 = gm_l47;
                    gm_b16_8 t480 = gm_f_empty_slice_3jt8vgk();
                    gm_b16_8 t481 = GM_LD(gm_b16_8, ((uint8_t *)&t480));
                    gm_f_resolve_fn_sig_pamuhve((void *)&gm_l107, (void *)NULL, cx, NULL, t477, t478, t479, ((uint8_t)0), t481);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t482 = (uint8_t)((((uint8_t *)&gm_l107))[136] != 0);
                    if (t482) {
                        memmove(((uint8_t *)&gm_l108), ((uint8_t *)&gm_l107), 136);
                    } else {
                        void * t483 = gm_l3;
                        gm_t88rcucg1b7uk t484 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t483);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t484;
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l48), ((uint8_t *)&gm_l108), 136);
                    void * t485 = gm_l3;
                    void * t486 = gm_l0;
                    void * t487 = (void *)((uint8_t *)&F.l48);
                    gm_tddtjfggdat0d t488;
                    gm_f_fn_ty_qaih7ts((void *)&t488, (void *)NULL, cx, NULL, t486, t487);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t53at80pfdus3 t489;
                    GM_ST(uint8_t, (uint8_t *)&t489 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t489 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t489 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t489 + 0), ((uint8_t)15ULL));
                    GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t489 + 8), t488);
                    gm_t88rcucg1b7uk t490 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t485, &t489);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t490;
                    goto gm_fret;
                }
gm_lb50:;
            }
            {
                {
                    uint8_t t491 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t492 = (uint8_t)(t491 == ((uint8_t)31ULL));
                    if (!t492) {
                        goto gm_lb51;
                    }
                    void * t493 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l49 = t493;
                    void * t494 = gm_l3;
                    void * t495 = gm_l0;
                    uint64_t t496 = gm_l1;
                    void * t497 = gm_l49;
                    gm_t88rcucg1b7uk t498 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t495, t496, t497);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t53at80pfdus3 t499;
                    memset(&t499, 0, sizeof t499);
                    GM_ST(uint8_t, ((uint8_t *)&t499 + 0), ((uint8_t)18ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t499 + 8), t498);
                    gm_t88rcucg1b7uk t500 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t494, &t499);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t500;
                    goto gm_fret;
                }
gm_lb51:;
                {
                    {
                        uint8_t t501 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                        uint8_t t502 = (uint8_t)(t501 == ((uint8_t)19ULL));
                        if (!t502) {
                            goto gm_lb52;
                        }
                        void * t503 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                        gm_l50 = t503;
                        gm_b16_8 t504 = GM_LD(gm_b16_8, (((((uint8_t *)t137) + 8) + 8) + 8));
                        gm_l51 = t504;
                        {
                            {
                                void * t505 = gm_l50;
                                uint8_t t506 = GM_LD(uint8_t, ((((uint8_t *)t505) + 8) + 0));
                                uint8_t t507 = (uint8_t)(t506 == ((uint8_t)22ULL));
                                if (!t507) {
                                    goto gm_lb54;
                                }
                                void * t508 = GM_LD(void *, ((((uint8_t *)t505) + 8) + 8));
                                gm_l52 = t508;
                                void * t509 = gm_l0;
                                uint64_t t510 = gm_l1;
                                void * t511 = gm_l52;
                                uint8_t t512 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t509, t510, t511);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (!t512) {
                                    goto gm_lb54;
                                }
                                void * t513 = gm_l3;
                                gm_t88rcucg1b7uk t514 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t513);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t514;
                                goto gm_fret;
                            }
gm_lb54:;
                        }
gm_lb53:;
                        gm_t9805qtonbsag t515;
                        memset(&t515, 0, sizeof t515);
                        gm_l53 = t515;
                        {
                            {
                                void * t516 = gm_l0;
                                uint64_t t517 = gm_l1;
                                void * t518 = gm_l50;
                                gm_f_generic_decl_oir9hm6((void *)&gm_l109, (void *)NULL, cx, NULL, t516, t517, t518, ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t519 = (uint8_t)((((uint8_t *)&gm_l109))[24] != 0);
                                if (!t519) {
                                    goto gm_lb56;
                                }
                                memmove(((uint8_t *)&gm_l54), ((uint8_t *)&gm_l109), 24);
                                void * t520 = gm_l0;
                                uint64_t t521 = GM_LD(uint64_t, ((uint8_t *)&gm_l54));
                                gm_f_resolve_decl_pus042v((void *)&gm_l110, (void *)NULL, cx, NULL, t520, t521);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    {
                                        uint8_t t522 = GM_LD(uint8_t, (((uint8_t *)&gm_l110) + 0));
                                        uint8_t t523 = (uint8_t)(t522 == ((uint8_t)1ULL));
                                        if (!t523) {
                                            goto gm_lb58;
                                        }
                                        uint32_t t524 = GM_LD(uint32_t, (((uint8_t *)&gm_l110) + 8));
                                        gm_l55 = t524;
                                        void * t525 = gm_l0;
                                        uint64_t t526 = GM_LD(uint64_t, ((uint8_t *)&gm_l54));
                                        gm_tl874t2atnlh3 t527 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l54) + 8));
                                        gm_b0_1 t528 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t525, t526, t527);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint32_t t529 = gm_l55;
                                        gm_t9805qtonbsag t530;
                                        GM_ST(uint8_t, (uint8_t *)&t530 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t530 + 6, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t530), t529);
                                        ((uint8_t *)&t530)[4] = 1;
                                        gm_l53 = t530;
                                        goto gm_lb57;
                                    }
gm_lb58:;
                                    {
                                        uint8_t t531 = GM_LD(uint8_t, (((uint8_t *)&gm_l110) + 0));
                                        uint8_t t532 = (uint8_t)(t531 == ((uint8_t)11ULL));
                                        if (!t532) {
                                            goto gm_lb59;
                                        }
                                        void * t533 = gm_l3;
                                        gm_t88rcucg1b7uk t534 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t533);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t534;
                                        goto gm_fret;
                                    }
gm_lb59:;
                                    {
                                        goto gm_lb57;
                                    }
gm_lb60:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 425, 17);
                                }
gm_lb57:;
                                goto gm_lb55;
                            }
gm_lb56:;
                            void * t535 = gm_l50;
                            {
                                {
                                    uint8_t t536 = GM_LD(uint8_t, ((((uint8_t *)t535) + 8) + 0));
                                    uint8_t t537 = (uint8_t)(t536 == ((uint8_t)22ULL));
                                    if (!t537) {
                                        goto gm_lb62;
                                    }
                                    gm_l111 = ((uint8_t)1);
                                    goto gm_lb61;
                                }
gm_lb62:;
                                {
                                    gm_l111 = ((uint8_t)0);
                                    goto gm_lb61;
                                }
gm_lb63:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 433, 23);
                            }
gm_lb61:;
                            uint8_t t538 = gm_l111;
                            if (t538) {
                                void * t539 = gm_l3;
                                gm_t88rcucg1b7uk t540 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t539);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t540;
                                goto gm_fret;
                            }
                        }
gm_lb55:;
                    }
                    {
                        uint8_t t541 = (uint8_t)((((uint8_t *)&gm_l53))[4] != 0);
                        if (t541) {
                            uint32_t t542 = GM_LD(uint32_t, ((uint8_t *)&gm_l53));
                            gm_l112 = t542;
                        } else {
                            {
                                {
                                    void * t543 = gm_l50;
                                    uint8_t t544 = GM_LD(uint8_t, ((((uint8_t *)t543) + 8) + 0));
                                    uint8_t t545 = (uint8_t)(t544 == ((uint8_t)6ULL));
                                    if (!t545) {
                                        goto gm_lb65;
                                    }
                                    memmove(((uint8_t *)&gm_l56), ((((uint8_t *)t543) + 8) + 8), 32);
                                    void * t546 = gm_l0;
                                    uint64_t t547 = gm_l1;
                                    gm_b16_8 t548 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                                    gm_tf5srh4mp4b3i t549 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t546, t547, t548);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l114 = t549;
                                    {
                                        {
                                            uint8_t t550 = (uint8_t)((((uint8_t *)&gm_l114))[8] != 0);
                                            if (t550) {
                                                goto gm_lb67;
                                            }
                                            gm_l113 = ((uint8_t)1);
                                            goto gm_lb66;
                                        }
gm_lb67:;
                                        {
                                            gm_l113 = ((uint8_t)0);
                                            goto gm_lb66;
                                        }
gm_lb68:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 438, 50);
                                    }
gm_lb66:;
                                    uint8_t t551 = gm_l113;
                                    if (!t551) {
                                        goto gm_lb65;
                                    }
                                    void * t552 = gm_l0;
                                    gm_b16_8 t553 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                                    gm_tkn42u7na3t81 t554 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t552, t553);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l116 = t554;
                                    {
                                        {
                                            uint8_t t555 = (uint8_t)((((uint8_t *)&gm_l116))[4] != 0);
                                            if (t555) {
                                                goto gm_lb70;
                                            }
                                            gm_l115 = ((uint8_t)1);
                                            goto gm_lb69;
                                        }
gm_lb70:;
                                        {
                                            gm_l115 = ((uint8_t)0);
                                            goto gm_lb69;
                                        }
gm_lb71:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 438, 100);
                                    }
gm_lb69:;
                                    uint8_t t556 = gm_l115;
                                    if (!t556) {
                                        goto gm_lb65;
                                    }
                                    void * t557 = gm_l0;
                                    uint64_t t558 = gm_l1;
                                    gm_b16_8 t559 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                                    void * t560 = gm_l50;
                                    gm_tl874t2atnlh3 t561 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t560) + 72));
                                    gm_b0_1 t562 = gm_f_unknown_type_beat5io((void *)NULL, cx, NULL, t557, t558, t559, t561);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb64;
                                }
gm_lb65:;
                                void * t563 = gm_l0;
                                void * t564 = gm_l2;
                                gm_tl874t2atnlh3 t565 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t564) + 72));
                                gm_b16_8 t566;
                                GM_ST(void *, (uint8_t *)&t566, (void *)(gm_s2328gjukbu28));
                                GM_ST(uintptr_t, (uint8_t *)&t566 + 8, (uintptr_t)(15));
                                gm_b0_1 t567 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t563, t565, t566);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
gm_lb64:;
                            void * t568 = gm_l3;
                            gm_t88rcucg1b7uk t569 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t568);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t569;
                            goto gm_fret;
                        }
                        uint32_t t570 = gm_l112;
                        gm_l57 = t570;
                        void * t571 = gm_l0;
                        uint64_t t572 = gm_l1;
                        uint32_t t573 = gm_l57;
                        gm_b16_8 t574 = gm_l51;
                        gm_b16_8 t575 = GM_LD(gm_b16_8, ((uint8_t *)&t574));
                        void * t576 = gm_l2;
                        gm_tl874t2atnlh3 t577 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t576) + 72));
                        gm_f_generic_type_args_5b8bvov((void *)&gm_l117, (void *)NULL, cx, NULL, t571, t572, t573, t575, t577);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint8_t t578 = (uint8_t)((((uint8_t *)&gm_l117))[16] != 0);
                        if (t578) {
                            gm_b16_8 t579 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l117));
                            gm_l118 = t579;
                        } else {
                            void * t580 = gm_l3;
                            gm_t88rcucg1b7uk t581 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t580);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t581;
                            goto gm_fret;
                        }
                        gm_b16_8 t582 = gm_l118;
                        gm_l58 = t582;
                        void * t583 = gm_l0;
                        void * t584 = GM_LD(void *, (((uint8_t *)t583) + 1088));
                        uint64_t t585 = GM_LD(uint64_t, ((((uint8_t *)t583) + 1088) + 8));
                        uint32_t t586 = gm_l57;
                        uint64_t t587 = (uint64_t)t586;
                        if ((uintptr_t)t587 >= (uintptr_t)(t585)) gm_trap_index((uintptr_t)t587, t585, gm_srcsffver18si54, 446, 23);
                        gm_b16_8 t588 = GM_LD(gm_b16_8, ((((uint8_t *)t584) + (uintptr_t)t587 * 184) + 120));
                        gm_l59 = t588;
                        gm_b16_8 t589 = gm_l59;
                        gm_b16_8 t590 = GM_LD(gm_b16_8, ((uint8_t *)&t589));
                        gm_l60 = t590;
                        gm_l61 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t591 = gm_l61;
                                uint64_t t592 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                                uint8_t t593 = (t591 < t592);
                                if (!t593) {
                                    goto gm_lb72;
                                }
                                {
                                    uint64_t t594 = gm_l61;
                                    void * t595 = GM_LD(void *, ((uint8_t *)&gm_l60));
                                    uint64_t t596 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                                    uint64_t t597 = gm_l61;
                                    uint32_t t598 = GM_LD(uint32_t, (((uint8_t *)t595) + (uintptr_t)t597 * 4));
                                    gm_tn68el8d14vku t599;
                                    GM_ST(uint32_t, (uint8_t *)&t599 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t599 + 0), t594);
                                    GM_ST(uint32_t, ((uint8_t *)&t599 + 8), t598);
                                    gm_l119 = t599;
                                    {
                                        {
                                            uint64_t t600 = GM_LD(uint64_t, ((uint8_t *)&gm_l119));
                                            gm_l62 = t600;
                                            uint32_t t601 = GM_LD(uint32_t, (((uint8_t *)&gm_l119) + 8));
                                            gm_l63 = t601;
                                            uint64_t t602 = gm_l62;
                                            uint64_t t603 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                                            uint8_t t604 = (t602 >= t603);
                                            if (t604) {
                                                goto gm_lb72;
                                            }
                                            void * t605 = GM_LD(void *, ((uint8_t *)&gm_l58));
                                            uint64_t t606 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                                            uint64_t t607 = gm_l62;
                                            gm_t88rcucg1b7uk t608 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t605) + (uintptr_t)t607 * 4));
                                            gm_l64 = t608;
                                            void * t609 = gm_l0;
                                            void * t610 = GM_LD(void *, (((uint8_t *)t609) + 1128));
                                            uint64_t t611 = GM_LD(uint64_t, ((((uint8_t *)t609) + 1128) + 8));
                                            uint32_t t612 = gm_l63;
                                            uint64_t t613 = (uint64_t)t612;
                                            if ((uintptr_t)t613 >= (uintptr_t)(t611)) gm_trap_index((uintptr_t)t613, t611, gm_srcsffver18si54, 450, 26);
                                            gm_b16_8 t614 = GM_LD(gm_b16_8, ((((uint8_t *)t610) + (uintptr_t)t613 * 48) + 8));
                                            gm_l65 = t614;
                                            gm_l66 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t615 = gm_l66;
                                                    uint64_t t616 = GM_LD(uint64_t, (((uint8_t *)&gm_l65) + 8));
                                                    uint8_t t617 = (t615 < t616);
                                                    if (!t617) {
                                                        goto gm_lb77;
                                                    }
                                                    {
                                                        void * t618 = GM_LD(void *, ((uint8_t *)&gm_l65));
                                                        uint64_t t619 = GM_LD(uint64_t, (((uint8_t *)&gm_l65) + 8));
                                                        uint64_t t620 = gm_l66;
                                                        {
                                                            {
                                                                gm_t9qkh0rsooc8d t621 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t618) + (uintptr_t)t620 * 1));
                                                                gm_l67 = t621;
                                                                void * t622 = gm_l0;
                                                                gm_t88rcucg1b7uk t623 = gm_l64;
                                                                gm_t9qkh0rsooc8d t624 = gm_l67;
                                                                uint8_t t625 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t622, t623, t624);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t626 = (uint8_t)!t625;
                                                                if (t626) {
                                                                    {
                                                                        {
                                                                            void * t627 = gm_l0;
                                                                            uint32_t t628 = GM_LD(uint32_t, (((uint8_t *)t627) + 1888));
                                                                            uint8_t t629 = (t628 > ((uint32_t)0ULL));
                                                                            if (!t629) {
                                                                                goto gm_lb83;
                                                                            }
                                                                            void * t630 = gm_l3;
                                                                            gm_t88rcucg1b7uk t631 = gm_l64;
                                                                            uint8_t t632 = gm_f_has_param_u9t0efs(t630, GM_CONV(gm_csguankko4lmhd, t631));
                                                                            if (!t632) {
                                                                                goto gm_lb83;
                                                                            }
                                                                            void * t633 = gm_l0;
                                                                            void * t634 = (void *)(((uint8_t *)t633) + 1896);
                                                                            gm_t88rcucg1b7uk t635 = gm_l64;
                                                                            gm_t9qkh0rsooc8d t636 = gm_l67;
                                                                            uint32_t t637 = gm_l63;
                                                                            uint32_t t638 = gm_l57;
                                                                            void * t639 = gm_l2;
                                                                            gm_tl874t2atnlh3 t640 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t639) + 72));
                                                                            gm_t8er5olt969ip t641;
                                                                            GM_ST(uint8_t, (uint8_t *)&t641 + 5, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&t641 + 6, 0);
                                                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t641 + 0), t635);
                                                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t641 + 4), t636);
                                                                            GM_ST(uint32_t, ((uint8_t *)&t641 + 8), t637);
                                                                            GM_ST(uint32_t, ((uint8_t *)&t641 + 12), t638);
                                                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t641 + 16), t640);
                                                                            gm_tftb1odc739t5 t642 = gm_f_push_afk8dfq(t634, &t641);
                                                                            gm_l120 = t642;
                                                                            uint8_t t643 = (uint8_t)!((((uint8_t *)&gm_l120))[0] != 0);
                                                                            if (!t643) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l120) + 1, gm_srcsffver18si54, 455, 29);
                                                                            goto gm_lb82;
                                                                        }
gm_lb83:;
                                                                        void * t644 = gm_l0;
                                                                        void * t645 = gm_l2;
                                                                        gm_tl874t2atnlh3 t646 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t645) + 72));
                                                                        void * t647 = gm_l0;
                                                                        gm_t88rcucg1b7uk t648 = gm_l64;
                                                                        gm_t9qkh0rsooc8d t649 = gm_l67;
                                                                        uint32_t t650 = gm_l63;
                                                                        uint32_t t651 = gm_l57;
                                                                        gm_b16_8 t652 = gm_f_unsatisfied_jnmtj8v((void *)NULL, cx, NULL, t647, t648, t649, t650, t651);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_b0_1 t653 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t644, t646, t652);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
gm_lb82:;
                                                                }
                                                                goto gm_lb80;
                                                            }
gm_lb81:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 450, 17);
                                                        }
gm_lb80:;
                                                    }
gm_lb79:;
                                                    uint64_t t654 = gm_l66;
                                                    uint64_t r655; bool o656 = __builtin_add_overflow(t654, ((uint64_t)1ULL), &r655);
                                                    uint64_t t657 = r655;
                                                    gm_l66 = t657;
                                                }
                                            }
gm_lb77:;
                                            goto gm_lb75;
                                        }
gm_lb76:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 447, 13);
                                    }
gm_lb75:;
                                }
gm_lb74:;
                                uint64_t t658 = gm_l61;
                                uint64_t r659; bool o660 = __builtin_add_overflow(t658, ((uint64_t)1ULL), &r659);
                                uint64_t t661 = r659;
                                gm_l61 = t661;
                            }
                        }
gm_lb72:;
                    }
                    {
                        gm_b16_8 t662 = gm_l58;
                        gm_l68 = t662;
                        gm_l69 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t663 = gm_l69;
                                uint64_t t664 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                                uint8_t t665 = (t663 < t664);
                                if (!t665) {
                                    goto gm_lb84;
                                }
                                {
                                    void * t666 = GM_LD(void *, ((uint8_t *)&gm_l68));
                                    uint64_t t667 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                                    uint64_t t668 = gm_l69;
                                    {
                                        {
                                            gm_t88rcucg1b7uk t669 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t666) + (uintptr_t)t668 * 4));
                                            gm_l70 = t669;
                                            {
                                                {
                                                    void * t670 = gm_l0;
                                                    gm_t88rcucg1b7uk t671 = gm_l70;
                                                    gm_tkn42u7na3t81 t672 = gm_f_second_class_part_k6i54mk(t670, GM_CONV(gm_csguankko4lmhd, t671));
                                                    gm_l121 = t672;
                                                    uint8_t t673 = (uint8_t)((((uint8_t *)&gm_l121))[4] != 0);
                                                    if (!t673) {
                                                        goto gm_lb90;
                                                    }
                                                    gm_t88rcucg1b7uk t674 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l121));
                                                    gm_l71 = t674;
                                                    void * t675 = gm_l0;
                                                    void * t676 = gm_l2;
                                                    gm_tl874t2atnlh3 t677 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t676) + 72));
                                                    void * t678 = gm_l0;
                                                    gm_t88rcucg1b7uk t679 = gm_l70;
                                                    gm_t88rcucg1b7uk t680 = gm_l71;
                                                    gm_b16_8 t681 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t678, t679, t680);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b0_1 t682 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t675, t677, t681);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    goto gm_lb89;
                                                }
gm_lb90:;
                                            }
gm_lb89:;
                                            goto gm_lb87;
                                        }
gm_lb88:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 462, 13);
                                    }
gm_lb87:;
                                }
gm_lb86:;
                                uint64_t t683 = gm_l69;
                                uint64_t r684; bool o685 = __builtin_add_overflow(t683, ((uint64_t)1ULL), &r684);
                                uint64_t t686 = r684;
                                gm_l69 = t686;
                            }
                        }
gm_lb84:;
                        void * t687 = gm_l0;
                        uint32_t t688 = gm_l57;
                        gm_b16_8 t689 = gm_l58;
                        gm_b16_8 t690 = GM_LD(gm_b16_8, ((uint8_t *)&t689));
                        gm_t88rcucg1b7uk t691 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t687, t688, t690);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l72 = t691;
                        void * t692 = gm_l0;
                        void * t693 = gm_l2;
                        gm_tl874t2atnlh3 t694 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t693) + 72));
                        gm_b0_1 t695 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t692, t694);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t696 = gm_l72;
                        F.ret = t696;
                        goto gm_fret;
                    }
                }
gm_lb52:;
            }
            {
                {
                    uint8_t t697 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t698 = (uint8_t)(t697 == ((uint8_t)35ULL));
                    if (!t698) {
                        goto gm_lb91;
                    }
                    gm_b16_8 t699 = GM_LD(gm_b16_8, ((((uint8_t *)t137) + 8) + 8));
                    gm_l73 = t699;
                    void * t700 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 16));
                    gm_l74 = t700;
                    {
                        {
                            void * t701 = gm_l74;
                            uint8_t t702 = GM_LD(uint8_t, ((((uint8_t *)t701) + 8) + 0));
                            uint8_t t703 = (uint8_t)(t702 == ((uint8_t)25ULL));
                            if (!t703) {
                                goto gm_lb93;
                            }
                            void * t704 = GM_LD(void *, (((((uint8_t *)t701) + 8) + 8) + 8));
                            gm_l75 = t704;
                            void * t705 = gm_l75;
                            {
                                {
                                    uint8_t t706 = GM_LD(uint8_t, ((((uint8_t *)t705) + 8) + 0));
                                    uint8_t t707 = (uint8_t)(t706 == ((uint8_t)30ULL));
                                    if (!t707) {
                                        goto gm_lb95;
                                    }
                                    gm_l122 = ((uint8_t)1);
                                    goto gm_lb94;
                                }
gm_lb95:;
                                {
                                    gm_l122 = ((uint8_t)0);
                                    goto gm_lb94;
                                }
gm_lb96:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 470, 49);
                            }
gm_lb94:;
                            uint8_t t708 = gm_l122;
                            if (!t708) {
                                goto gm_lb93;
                            }
                            void * t709 = gm_l0;
                            uint64_t t710 = gm_l1;
                            gm_b16_8 t711 = gm_l73;
                            gm_b16_8 t712 = GM_LD(gm_b16_8, ((uint8_t *)&t711));
                            void * t713 = gm_l74;
                            void * t714 = gm_l2;
                            gm_tl874t2atnlh3 t715 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t714) + 72));
                            gm_t88rcucg1b7uk t716 = gm_f_callconv_fn_ptr_l9kavc3((void *)NULL, cx, NULL, t709, t710, t712, t713, t715);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t716;
                            goto gm_fret;
                        }
gm_lb93:;
                    }
gm_lb92:;
                    gm_b16_8 t717 = gm_l73;
                    gm_l76 = t717;
                    gm_l77 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t718 = gm_l77;
                            uint64_t t719 = GM_LD(uint64_t, (((uint8_t *)&gm_l76) + 8));
                            uint8_t t720 = (t718 < t719);
                            if (!t720) {
                                goto gm_lb97;
                            }
                            {
                                void * t721 = GM_LD(void *, ((uint8_t *)&gm_l76));
                                uint64_t t722 = GM_LD(uint64_t, (((uint8_t *)&gm_l76) + 8));
                                uint64_t t723 = gm_l77;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l78), (((uint8_t *)t721) + (uintptr_t)t723 * 72), 72);
                                        {
                                            {
                                                gm_b16_8 t724 = GM_LD(gm_b16_8, ((uint8_t *)&F.l78));
                                                gm_b16_8 t725;
                                                GM_ST(void *, (uint8_t *)&t725, (void *)(gm_sqes10usq4q5e));
                                                GM_ST(uintptr_t, (uint8_t *)&t725 + 8, (uintptr_t)(3));
                                                uint8_t t726 = (gm_eq0iqiubfm7e(((uint8_t *)&t724), ((uint8_t *)&t725)));
                                                if (!t726) {
                                                    goto gm_lb103;
                                                }
                                                void * t727 = gm_l74;
                                                {
                                                    {
                                                        uint8_t t728 = GM_LD(uint8_t, ((((uint8_t *)t727) + 8) + 0));
                                                        uint8_t t729 = (uint8_t)(t728 == ((uint8_t)28ULL));
                                                        if (!t729) {
                                                            goto gm_lb105;
                                                        }
                                                        gm_l123 = ((uint8_t)1);
                                                        goto gm_lb104;
                                                    }
gm_lb105:;
                                                    {
                                                        uint8_t t730 = GM_LD(uint8_t, ((((uint8_t *)t727) + 8) + 0));
                                                        uint8_t t731 = (uint8_t)(t730 == ((uint8_t)26ULL));
                                                        if (!t731) {
                                                            goto gm_lb106;
                                                        }
                                                        gm_l123 = ((uint8_t)1);
                                                        goto gm_lb104;
                                                    }
gm_lb106:;
                                                    {
                                                        gm_l123 = ((uint8_t)0);
                                                        goto gm_lb104;
                                                    }
gm_lb107:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 474, 45);
                                                }
gm_lb104:;
                                                uint8_t t732 = gm_l123;
                                                if (!t732) {
                                                    goto gm_lb103;
                                                }
                                                uint64_t t733 = GM_LD(uint64_t, (((uint8_t *)&gm_l73) + 8));
                                                uint8_t t734 = (t733 > ((uint64_t)1ULL));
                                                if (t734) {
                                                    gm_l124 = ((uint8_t)1);
                                                } else {
                                                    void * t735 = GM_LD(void *, ((uint8_t *)&gm_l73));
                                                    uint64_t t736 = GM_LD(uint64_t, (((uint8_t *)&gm_l73) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t736)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t736, gm_srcsffver18si54, 475, 41);
                                                    {
                                                        {
                                                            uint8_t t737 = (uint8_t)((((((uint8_t *)t735) + (uintptr_t)((uint64_t)0ULL) * 72) + 32))[16] != 0);
                                                            if (t737) {
                                                                goto gm_lb109;
                                                            }
                                                            gm_l125 = ((uint8_t)0);
                                                            goto gm_lb108;
                                                        }
gm_lb109:;
                                                        {
                                                            gm_l125 = ((uint8_t)1);
                                                            goto gm_lb108;
                                                        }
gm_lb110:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 475, 41);
                                                    }
gm_lb108:;
                                                    uint8_t t738 = gm_l125;
                                                    gm_l124 = t738;
                                                }
                                                uint8_t t739 = gm_l124;
                                                if (t739) {
                                                    void * t740 = gm_l0;
                                                    void * t741 = gm_l2;
                                                    gm_tl874t2atnlh3 t742 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t741) + 72));
                                                    gm_b16_8 t743;
                                                    GM_ST(void *, (uint8_t *)&t743, (void *)(gm_sgrpmaenv85i1));
                                                    GM_ST(uintptr_t, (uint8_t *)&t743 + 8, (uintptr_t)(72));
                                                    gm_b0_1 t744 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t740, t742, t743);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t745 = gm_l3;
                                                    gm_t88rcucg1b7uk t746 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t745);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.ret = t746;
                                                    goto gm_fret;
                                                }
                                                void * t747 = gm_l0;
                                                uint64_t t748 = gm_l1;
                                                void * t749 = gm_l74;
                                                void * t750 = gm_l2;
                                                gm_tl874t2atnlh3 t751 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t750) + 72));
                                                gm_t88rcucg1b7uk t752 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t747, t748, t749, t751);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t752;
                                                goto gm_fret;
                                            }
gm_lb103:;
                                        }
gm_lb102:;
                                        goto gm_lb100;
                                    }
gm_lb101:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 473, 13);
                                }
gm_lb100:;
                            }
gm_lb99:;
                            uint64_t t753 = gm_l77;
                            uint64_t r754; bool o755 = __builtin_add_overflow(t753, ((uint64_t)1ULL), &r754);
                            uint64_t t756 = r754;
                            gm_l77 = t756;
                        }
                    }
gm_lb97:;
                    void * t757 = gm_l74;
                    {
                        {
                            uint8_t t758 = GM_LD(uint8_t, ((((uint8_t *)t757) + 8) + 0));
                            uint8_t t759 = (uint8_t)(t758 == ((uint8_t)28ULL));
                            if (!t759) {
                                goto gm_lb112;
                            }
                            gm_l126 = ((uint8_t)1);
                            goto gm_lb111;
                        }
gm_lb112:;
                        {
                            gm_l126 = ((uint8_t)0);
                            goto gm_lb111;
                        }
gm_lb113:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 482, 16);
                    }
gm_lb111:;
                    uint8_t t760 = gm_l126;
                    if (t760) {
                        void * t761 = gm_l0;
                        uint64_t t762 = gm_l1;
                        gm_b16_8 t763 = gm_l73;
                        gm_b16_8 t764 = GM_LD(gm_b16_8, ((uint8_t *)&t763));
                        void * t765 = gm_l74;
                        void * t766 = gm_l2;
                        gm_tl874t2atnlh3 t767 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t766) + 72));
                        gm_t88rcucg1b7uk t768 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t761, t762, t764, t765, t767);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t768;
                        goto gm_fret;
                    }
                    void * t769 = gm_l74;
                    {
                        {
                            uint8_t t770 = GM_LD(uint8_t, ((((uint8_t *)t769) + 8) + 0));
                            uint8_t t771 = (uint8_t)(t770 == ((uint8_t)32ULL));
                            if (!t771) {
                                goto gm_lb115;
                            }
                            gm_l127 = ((uint8_t)0);
                            goto gm_lb114;
                        }
gm_lb115:;
                        {
                            uint8_t t772 = GM_LD(uint8_t, ((((uint8_t *)t769) + 8) + 0));
                            uint8_t t773 = (uint8_t)(t772 == ((uint8_t)33ULL));
                            if (!t773) {
                                goto gm_lb116;
                            }
                            gm_l127 = ((uint8_t)0);
                            goto gm_lb114;
                        }
gm_lb116:;
                        {
                            gm_l127 = ((uint8_t)1);
                            goto gm_lb114;
                        }
gm_lb117:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 483, 16);
                    }
gm_lb114:;
                    uint8_t t774 = gm_l127;
                    if (t774) {
                        void * t775 = gm_l0;
                        void * t776 = gm_l2;
                        gm_tl874t2atnlh3 t777 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t776) + 72));
                        gm_b16_8 t778;
                        GM_ST(void *, (uint8_t *)&t778, (void *)(gm_ssj1a8k9nspq4));
                        GM_ST(uintptr_t, (uint8_t *)&t778 + 8, (uintptr_t)(81));
                        gm_b0_1 t779 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t775, t777, t778);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t780 = gm_l3;
                        gm_t88rcucg1b7uk t781 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t780);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t781;
                        goto gm_fret;
                    }
                    void * t782 = gm_l74;
                    {
                        {
                            uint8_t t783 = GM_LD(uint8_t, ((((uint8_t *)t782) + 8) + 0));
                            uint8_t t784 = (uint8_t)(t783 == ((uint8_t)33ULL));
                            if (!t784) {
                                goto gm_lb119;
                            }
                            gm_l129 = ((uint8_t)1);
                            goto gm_lb118;
                        }
gm_lb119:;
                        {
                            gm_l129 = ((uint8_t)0);
                            goto gm_lb118;
                        }
gm_lb120:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 487, 24);
                    }
gm_lb118:;
                    uint8_t t785 = gm_l129;
                    if (t785) {
                        gm_b16_8 t786;
                        GM_ST(void *, (uint8_t *)&t786, (void *)(gm_s8p1h96ji9kh0));
                        GM_ST(uintptr_t, (uint8_t *)&t786 + 8, (uintptr_t)(5));
                        gm_l128 = t786;
                    } else {
                        gm_b16_8 t787;
                        GM_ST(void *, (uint8_t *)&t787, (void *)(gm_sc49g68j2cekh));
                        GM_ST(uintptr_t, (uint8_t *)&t787 + 8, (uintptr_t)(6));
                        gm_l128 = t787;
                    }
                    gm_b16_8 t788 = gm_l128;
                    gm_l79 = t788;
                    void * t789 = gm_l0;
                    uint64_t t790 = gm_l1;
                    gm_tf5srh4mp4b3i t791;
                    memset(&t791, 0, sizeof t791);
                    gm_b16_8 t792 = gm_l79;
                    void * t793 = gm_l2;
                    gm_t88rcucg1b7uk t794 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t789, t790, t791, t792, t793);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t794;
                    goto gm_fret;
                }
gm_lb91:;
            }
            {
                {
                    uint8_t t795 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t796 = (uint8_t)(t795 == ((uint8_t)32ULL));
                    if (!t796) {
                        goto gm_lb121;
                    }
                    void * t797 = gm_l0;
                    uint64_t t798 = gm_l1;
                    gm_tf5srh4mp4b3i t799;
                    memset(&t799, 0, sizeof t799);
                    gm_b16_8 t800;
                    GM_ST(void *, (uint8_t *)&t800, (void *)(gm_sc49g68j2cekh));
                    GM_ST(uintptr_t, (uint8_t *)&t800 + 8, (uintptr_t)(6));
                    void * t801 = gm_l2;
                    gm_t88rcucg1b7uk t802 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t797, t798, t799, t800, t801);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t802;
                    goto gm_fret;
                }
gm_lb121:;
                {
                    uint8_t t803 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t804 = (uint8_t)(t803 == ((uint8_t)33ULL));
                    if (!t804) {
                        goto gm_lb122;
                    }
                    void * t805 = gm_l0;
                    uint64_t t806 = gm_l1;
                    gm_tf5srh4mp4b3i t807;
                    memset(&t807, 0, sizeof t807);
                    gm_b16_8 t808;
                    GM_ST(void *, (uint8_t *)&t808, (void *)(gm_s8p1h96ji9kh0));
                    GM_ST(uintptr_t, (uint8_t *)&t808 + 8, (uintptr_t)(5));
                    void * t809 = gm_l2;
                    gm_t88rcucg1b7uk t810 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t805, t806, t807, t808, t809);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t810;
                    goto gm_fret;
                }
gm_lb122:;
                {
                    uint8_t t811 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t812 = (uint8_t)(t811 == ((uint8_t)49ULL));
                    if (!t812) {
                        goto gm_lb123;
                    }
                    memmove(((uint8_t *)&F.l80), ((((uint8_t *)t137) + 8) + 8), 56);
                    gm_b16_8 t813 = GM_LD(gm_b16_8, ((uint8_t *)&F.l80));
                    gm_b16_8 t814;
                    GM_ST(void *, (uint8_t *)&t814, (void *)(gm_slbgk65sat4bm));
                    GM_ST(uintptr_t, (uint8_t *)&t814 + 8, (uintptr_t)(7));
                    uint8_t t815 = (gm_eq0iqiubfm7e(((uint8_t *)&t813), ((uint8_t *)&t814)));
                    if (t815) {
                        {
                            {
                                uint8_t t816 = (uint8_t)(((((uint8_t *)&F.l80) + 32))[16] != 0);
                                if (!t816) {
                                    goto gm_lb125;
                                }
                                gm_b16_8 t817 = GM_LD(gm_b16_8, (((uint8_t *)&F.l80) + 32));
                                gm_l81 = t817;
                                uint64_t t818 = GM_LD(uint64_t, (((uint8_t *)&gm_l81) + 8));
                                uint8_t t819 = ((t818 == ((uint64_t)1ULL)));
                                if (!t819) {
                                    goto gm_lb125;
                                }
                                void * t820 = GM_LD(void *, ((uint8_t *)&gm_l81));
                                uint64_t t821 = GM_LD(uint64_t, (((uint8_t *)&gm_l81) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t821)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t821, gm_srcsffver18si54, 494, 75);
                                uint8_t t822 = GM_LD(uint8_t, ((((uint8_t *)t820) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                uint8_t t823 = (uint8_t)(t822 == ((uint8_t)0ULL));
                                if (!t823) {
                                    goto gm_lb125;
                                }
                                memmove(((uint8_t *)&F.l82), ((((uint8_t *)t820) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                                void * t824 = gm_l0;
                                uint8_t t825 = (uint8_t)(GM_LD(void *, (((uint8_t *)t824) + 2304)) != 0);
                                if (t825) {
                                    void * t826 = GM_LD(void *, (((uint8_t *)t824) + 2304));
                                    gm_l130 = t826;
                                } else {
                                    void * t827 = gm_l0;
                                    void * t828 = gm_l2;
                                    gm_tl874t2atnlh3 t829 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t828) + 72));
                                    gm_b16_8 t830;
                                    GM_ST(void *, (uint8_t *)&t830, (void *)(gm_smi7k9v6u7cmo));
                                    GM_ST(uintptr_t, (uint8_t *)&t830 + 8, (uintptr_t)(10));
                                    gm_b0_1 t831 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t827, t829, t830);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t832 = gm_l3;
                                    gm_t88rcucg1b7uk t833 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t832);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t833;
                                    goto gm_fret;
                                }
                                void * t834 = gm_l130;
                                gm_l83 = t834;
                                void * t835 = gm_l83;
                                void * t836 = gm_l0;
                                uint64_t t837 = gm_l1;
                                void * t838 = (void *)((uint8_t *)&F.l82);
                                void * t839 = t835;
                                gm_t88rcucg1b7uk t840;
                                if (GM_PTR_TAGGED(t839)) {
                                    t840 = GM_CONV(gm_t88rcucg1b7uk, ((gm_csguankko4lmhd (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t839))(t836, t837, t838));
                                } else {
                                    t840 = ((gm_t88rcucg1b7uk (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t839)(NULL, cx, NULL, t836, t837, t838);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t840;
                                goto gm_fret;
                            }
gm_lb125:;
                        }
gm_lb124:;
                        void * t841 = gm_l0;
                        void * t842 = gm_l2;
                        gm_tl874t2atnlh3 t843 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t842) + 72));
                        gm_b16_8 t844;
                        GM_ST(void *, (uint8_t *)&t844, (void *)(gm_sfpopfqbubj89));
                        GM_ST(uintptr_t, (uint8_t *)&t844 + 8, (uintptr_t)(34));
                        gm_b0_1 t845 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t841, t843, t844);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t846 = gm_l3;
                        gm_t88rcucg1b7uk t847 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t846);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t847;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb123:;
            }
            {
                {
                    uint8_t t848 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t849 = (uint8_t)(t848 == ((uint8_t)22ULL));
                    if (!t849) {
                        goto gm_lb126;
                    }
                    void * t850 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l84 = t850;
                    memmove(((uint8_t *)&gm_l85), (((((uint8_t *)t137) + 8) + 8) + 8), 32);
                    {
                        {
                            void * t851 = gm_l0;
                            uint64_t t852 = gm_l1;
                            void * t853 = gm_l84;
                            gm_tf5srh4mp4b3i t854 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t851, t852, t853);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l131 = t854;
                            uint8_t t855 = (uint8_t)((((uint8_t *)&gm_l131))[8] != 0);
                            if (!t855) {
                                goto gm_lb128;
                            }
                            uint64_t t856 = GM_LD(uint64_t, ((uint8_t *)&gm_l131));
                            gm_l86 = t856;
                            void * t857 = gm_l0;
                            uint64_t t858 = gm_l86;
                            uint64_t t859 = gm_l1;
                            gm_tf5srh4mp4b3i t860 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t857, t858, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l85)), t859);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l132 = t860;
                            uint8_t t861 = (uint8_t)((((uint8_t *)&gm_l132))[8] != 0);
                            if (t861) {
                                uint64_t t862 = GM_LD(uint64_t, ((uint8_t *)&gm_l132));
                                gm_l133 = t862;
                            } else {
                                void * t863 = gm_l3;
                                gm_t88rcucg1b7uk t864 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t863);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t864;
                                goto gm_fret;
                            }
                            uint64_t t865 = gm_l133;
                            gm_l87 = t865;
                            void * t866 = gm_l0;
                            uint64_t t867 = gm_l87;
                            gm_f_resolve_decl_pus042v((void *)&gm_l134, (void *)NULL, cx, NULL, t866, t867);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t868 = GM_LD(uint8_t, (((uint8_t *)&gm_l134) + 0));
                                    uint8_t t869 = (uint8_t)(t868 == ((uint8_t)0ULL));
                                    if (!t869) {
                                        goto gm_lb130;
                                    }
                                    gm_t88rcucg1b7uk t870 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l134) + 8));
                                    gm_l88 = t870;
                                    gm_t88rcucg1b7uk t871 = gm_l88;
                                    F.ret = t871;
                                    goto gm_fret;
                                }
gm_lb130:;
                                {
                                    uint8_t t872 = GM_LD(uint8_t, (((uint8_t *)&gm_l134) + 0));
                                    uint8_t t873 = (uint8_t)(t872 == ((uint8_t)11ULL));
                                    if (!t873) {
                                        goto gm_lb131;
                                    }
                                    void * t874 = gm_l3;
                                    gm_t88rcucg1b7uk t875 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t874);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t875;
                                    goto gm_fret;
                                }
gm_lb131:;
                                {
                                    void * t876 = gm_l0;
                                    void * t877 = gm_l2;
                                    gm_tl874t2atnlh3 t878 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t877) + 72));
                                    gm_b16_8 t879 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l85));
                                    gm_tnpifv20irnph t880;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t880 + 0), t879);
                                    gm_f_format_sv91363((void *)&gm_l135, (void *)NULL, cx, NULL, t880);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t881 = (uint8_t)!((((uint8_t *)&gm_l135))[0] != 0);
                                    if (!t881) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l135) + 8, gm_srcsffver18si54, 513, 41);
                                    gm_b16_8 t882 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l135) + 8));
                                    gm_b16_8 t883 = GM_LD(gm_b16_8, ((uint8_t *)&t882));
                                    gm_b0_1 t884 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t876, t878, t883);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t885 = gm_l3;
                                    gm_t88rcucg1b7uk t886 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t885);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t886;
                                    goto gm_fret;
                                }
gm_lb132:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 509, 17);
                            }
gm_lb129:;
                        }
gm_lb128:;
                    }
gm_lb127:;
                    void * t887 = gm_l0;
                    uint64_t t888 = gm_l1;
                    void * t889 = gm_l84;
                    uint8_t t890 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t887, t888, t889);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t890) {
                        void * t891 = gm_l3;
                        gm_t88rcucg1b7uk t892 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t891);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t892;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb126:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb133:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 332, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t893 = gm_l0;
        void * t894 = gm_l2;
        gm_tl874t2atnlh3 t895 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t894) + 72));
        gm_b16_8 t896;
        GM_ST(void *, (uint8_t *)&t896, (void *)(gm_s2328gjukbu28));
        GM_ST(uintptr_t, (uint8_t *)&t896 + 8, (uintptr_t)(15));
        gm_b0_1 t897 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t893, t895, t896);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t898 = gm_l3;
        gm_t88rcucg1b7uk t899 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t898);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t899;
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
extern __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1) { gm_f_restore_scope_edqq1v8(a0, &a1); }
extern __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0) { gm_t66j1im9ip128 gm_r; gm_f_scope_mark_i3b2mvo(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_is_type_pe6ugv2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* inner */
    void * gm_l7; /* inner */
    void * gm_l8; /* callee */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint64_t gm_l10; /* d */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    uint64_t gm_l13; /* m */
    uint64_t gm_l14; /* d */
    GM_STACK_CHECK(&F, 831u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    uint64_t gm_l17;
    gm_tf5srh4mp4b3i gm_l18;
    uint64_t gm_l19;
    gm_tf5srh4mp4b3i gm_l20;
    uint64_t gm_l21;
    void * t29 = gm_l2;
    {
        {
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)6ULL));
            if (!t31) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t29) + 8) + 8), 32);
            {
                {
                    void * t32 = gm_l0;
                    uint64_t t33 = gm_l1;
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_tf5srh4mp4b3i t35 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t32, t33, t34);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t35;
                    uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                    if (!t36) {
                        goto gm_lb4;
                    }
                    uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                    gm_l5 = t37;
                    void * t38 = gm_l0;
                    uint64_t t39 = gm_l5;
                    uint32_t t40 = gm_l3;
                    uint32_t r41; bool o42 = __builtin_add_overflow(t40, ((uint32_t)1ULL), &r41);
                    if (o42) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 155, 86);
                    uint32_t t43 = r41;
                    uint8_t t44 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t38, t39, t43);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t44;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            uint8_t t46 = gm_f_is_builtin_type_name_v5ttoka(t45);
            F.ret = t46;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)9ULL));
            if (!t48) {
                goto gm_lb5;
            }
            void * t49 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l6 = t49;
            void * t50 = gm_l0;
            uint64_t t51 = gm_l1;
            void * t52 = gm_l6;
            uint32_t t53 = gm_l3;
            uint8_t t54 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t50, t51, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t54;
            goto gm_fret;
        }
gm_lb5:;
        {
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)35ULL));
            if (!t56) {
                goto gm_lb6;
            }
            void * t57 = GM_LD(void *, (((((uint8_t *)t29) + 8) + 8) + 16));
            gm_l7 = t57;
            void * t58 = gm_l0;
            uint64_t t59 = gm_l1;
            void * t60 = gm_l7;
            uint32_t t61 = gm_l3;
            uint8_t t62 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t58, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t62;
            goto gm_fret;
        }
gm_lb6:;
        {
            {
                {
                    uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t64 = (uint8_t)(t63 == ((uint8_t)32ULL));
                    if (!t64) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)33ULL));
                    if (!t66) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                {
                    uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)25ULL));
                    if (!t68) {
                        goto gm_lb11;
                    }
                    goto gm_lb8;
                }
gm_lb11:;
                {
                    uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)26ULL));
                    if (!t70) {
                        goto gm_lb12;
                    }
                    goto gm_lb8;
                }
gm_lb12:;
                {
                    uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)27ULL));
                    if (!t72) {
                        goto gm_lb13;
                    }
                    goto gm_lb8;
                }
gm_lb13:;
                {
                    uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)29ULL));
                    if (!t74) {
                        goto gm_lb14;
                    }
                    goto gm_lb8;
                }
gm_lb14:;
                {
                    uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)30ULL));
                    if (!t76) {
                        goto gm_lb15;
                    }
                    goto gm_lb8;
                }
gm_lb15:;
                {
                    uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)31ULL));
                    if (!t78) {
                        goto gm_lb16;
                    }
                    goto gm_lb8;
                }
gm_lb16:;
                goto gm_lb7;
            }
gm_lb8:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb7:;
        {
            {
                {
                    uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)24ULL));
                    if (!t80) {
                        goto gm_lb19;
                    }
                    goto gm_lb18;
                }
gm_lb19:;
                {
                    uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)28ULL));
                    if (!t82) {
                        goto gm_lb20;
                    }
                    goto gm_lb18;
                }
gm_lb20:;
                goto gm_lb17;
            }
gm_lb18:;
            void * t83 = gm_l2;
            uint8_t t84 = gm_f_hoists_13k2a2s(t83);
            uint8_t t85 = (uint8_t)!t84;
            F.ret = t85;
            goto gm_fret;
        }
gm_lb17:;
        {
            uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)19ULL));
            if (!t87) {
                goto gm_lb21;
            }
            void * t88 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l8 = t88;
            void * t89 = gm_l8;
            {
                {
                    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t89) + 8) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)6ULL));
                    if (!t91) {
                        goto gm_lb23;
                    }
                    memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t89) + 8) + 8), 32);
                    void * t92 = gm_l0;
                    uint64_t t93 = gm_l1;
                    gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                    gm_tf5srh4mp4b3i t95 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t92, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t95;
                    uint8_t t96 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                    if (t96) {
                        uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
                        gm_l17 = t97;
                    } else {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    uint64_t t98 = gm_l17;
                    gm_l10 = t98;
                    void * t99 = gm_l0;
                    uint64_t t100 = gm_l10;
                    uint32_t t101 = gm_l3;
                    uint32_t r102; bool o103 = __builtin_add_overflow(t101, ((uint32_t)1ULL), &r102);
                    if (o103) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 165, 42);
                    uint32_t t104 = r102;
                    uint8_t t105 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t99, t100, t104);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t105;
                    goto gm_fret;
                }
gm_lb23:;
                {
                    uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t89) + 8) + 0));
                    uint8_t t107 = (uint8_t)(t106 == ((uint8_t)22ULL));
                    if (!t107) {
                        goto gm_lb24;
                    }
                    void * t108 = gm_l0;
                    uint64_t t109 = gm_l1;
                    void * t110 = gm_l8;
                    uint32_t t111 = gm_l3;
                    uint8_t t112 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t108, t109, t110, t111);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t112;
                    goto gm_fret;
                }
gm_lb24:;
                {
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb25:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 162, 32);
            }
gm_lb22:;
        }
gm_lb21:;
        {
            uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t114 = (uint8_t)(t113 == ((uint8_t)22ULL));
            if (!t114) {
                goto gm_lb26;
            }
            void * t115 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l11 = t115;
            memmove(((uint8_t *)&gm_l12), (((((uint8_t *)t29) + 8) + 8) + 8), 32);
            void * t116 = gm_l0;
            uint64_t t117 = gm_l1;
            void * t118 = gm_l11;
            uint32_t t119 = gm_l3;
            gm_tf5srh4mp4b3i t120 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t116, t117, t118, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t120;
            uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
            if (t121) {
                uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                gm_l19 = t122;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            uint64_t t123 = gm_l19;
            gm_l13 = t123;
            void * t124 = gm_l0;
            void * t125 = GM_LD(void *, ((uint8_t *)t124));
            uint64_t t126 = gm_l13;
            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
            gm_tf5srh4mp4b3i t128 = gm_f_lookup_59juqoo(t125, t126, t127);
            gm_l20 = t128;
            uint8_t t129 = (uint8_t)((((uint8_t *)&gm_l20))[8] != 0);
            if (t129) {
                uint64_t t130 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                gm_l21 = t130;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            uint64_t t131 = gm_l21;
            gm_l14 = t131;
            void * t132 = gm_l0;
            uint64_t t133 = gm_l14;
            uint64_t t134 = gm_l1;
            uint8_t t135 = gm_f_decl_visible_bd168ti(t132, t133, t134);
            uint8_t t136 = (uint8_t)!t135;
            if (t136) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t137 = gm_l0;
            uint64_t t138 = gm_l14;
            uint32_t t139 = gm_l3;
            uint32_t r140; bool o141 = __builtin_add_overflow(t139, ((uint32_t)1ULL), &r140);
            if (o141) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 175, 38);
            uint32_t t142 = r140;
            uint8_t t143 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t137, t138, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t143;
            goto gm_fret;
        }
gm_lb26:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb27:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 153, 5);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_module_vho1001 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    uint64_t gm_l4; /* d */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l10; /* m */
    void * gm_l11; /* decl */
    uint64_t gm_l12; /* dm */
    uint64_t gm_l14; /* m */
    GM_STACK_CHECK(&F, 1358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l15;
    uint64_t gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    uint64_t gm_l18;
    gm_tf5srh4mp4b3i gm_l19;
    uint64_t gm_l20;
    gm_tagrkbqh054h7 gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    gm_t6qooom8qa2do gm_l24;
    uint32_t t25 = gm_l3;
    uint8_t t26 = (t25 > ((uint32_t)16ULL));
    if (t26) {
        gm_tf5srh4mp4b3i t27;
        memset(&t27, 0, sizeof t27);
        F.ret = t27;
        goto gm_fret;
    }
    gm_l4 = ((uint64_t)0ULL);
    void * t28 = gm_l2;
    {
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)6ULL));
            if (!t30) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t28) + 8) + 8), 32);
            void * t31 = gm_l0;
            uint64_t t32 = gm_l1;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_tf5srh4mp4b3i t34 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t34;
            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
            if (t35) {
                uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                gm_l16 = t36;
            } else {
                gm_tf5srh4mp4b3i t37;
                memset(&t37, 0, sizeof t37);
                F.ret = t37;
                goto gm_fret;
            }
            uint64_t t38 = gm_l16;
            gm_l4 = t38;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)22ULL));
            if (!t40) {
                goto gm_lb3;
            }
            void * t41 = GM_LD(void *, ((((uint8_t *)t28) + 8) + 8));
            gm_l6 = t41;
            memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t28) + 8) + 8) + 8), 32);
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            void * t44 = gm_l6;
            uint32_t t45 = gm_l3;
            uint32_t r46; bool o47 = __builtin_add_overflow(t45, ((uint32_t)1ULL), &r46);
            if (o47) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 189, 48);
            uint32_t t48 = r46;
            gm_tf5srh4mp4b3i t49 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t42, t43, t44, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t49;
            uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
            if (t50) {
                uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                gm_l18 = t51;
            } else {
                gm_tf5srh4mp4b3i t52;
                memset(&t52, 0, sizeof t52);
                F.ret = t52;
                goto gm_fret;
            }
            uint64_t t53 = gm_l18;
            gm_l8 = t53;
            void * t54 = gm_l0;
            void * t55 = GM_LD(void *, ((uint8_t *)t54));
            uint64_t t56 = gm_l8;
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_tf5srh4mp4b3i t58 = gm_f_lookup_59juqoo(t55, t56, t57);
            gm_l19 = t58;
            uint8_t t59 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
            if (t59) {
                uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                gm_l20 = t60;
            } else {
                gm_tf5srh4mp4b3i t61;
                memset(&t61, 0, sizeof t61);
                F.ret = t61;
                goto gm_fret;
            }
            uint64_t t62 = gm_l20;
            gm_l4 = t62;
            void * t63 = gm_l0;
            uint64_t t64 = gm_l4;
            uint64_t t65 = gm_l1;
            uint8_t t66 = gm_f_decl_visible_bd168ti(t63, t64, t65);
            uint8_t t67 = (uint8_t)!t66;
            if (t67) {
                gm_tf5srh4mp4b3i t68;
                memset(&t68, 0, sizeof t68);
                F.ret = t68;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_tf5srh4mp4b3i t69;
            memset(&t69, 0, sizeof t69);
            F.ret = t69;
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 186, 5);
    }
gm_lb1:;
    void * t70 = gm_l0;
    uint64_t t71 = gm_l4;
    gm_f_state_a5bnsjb((void *)&gm_l21, (void *)NULL, cx, NULL, t70, t71);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
            uint8_t t73 = (uint8_t)(t72 == ((uint8_t)2ULL));
            if (!t73) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l21) + 8), 208);
            {
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)9ULL));
                    if (!t75) {
                        goto gm_lb8;
                    }
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                    gm_l10 = t76;
                    uint64_t t77 = gm_l10;
                    gm_tf5srh4mp4b3i t78;
                    GM_ST(uint8_t, (uint8_t *)&t78 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t78 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t78 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t78), t77);
                    ((uint8_t *)&t78)[8] = 1;
                    F.ret = t78;
                    goto gm_fret;
                }
gm_lb8:;
                gm_tf5srh4mp4b3i t79;
                memset(&t79, 0, sizeof t79);
                F.ret = t79;
                goto gm_fret;
            }
gm_lb7:;
        }
gm_lb6:;
        {
            goto gm_lb5;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 195, 5);
    }
gm_lb5:;
    void * t80 = gm_l0;
    void * t81 = GM_LD(void *, ((uint8_t *)t80));
    void * t82 = GM_LD(void *, (((uint8_t *)t81) + 240));
    uint64_t t83 = GM_LD(uint64_t, ((((uint8_t *)t81) + 240) + 8));
    uint64_t t84 = gm_l4;
    if ((uintptr_t)t84 >= (uintptr_t)(t83)) gm_trap_index((uintptr_t)t84, t83, gm_srcsffver18si54, 202, 14);
    void * t85 = (void *)(((uint8_t *)t82) + (uintptr_t)t84 * 216);
    gm_l11 = t85;
    void * t86 = gm_l11;
    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t86) + 32));
    gm_l12 = t87;
    {
        {
            void * t88 = gm_l11;
            uint8_t t89 = GM_LD(uint8_t, (((((uint8_t *)t88) + 40) + 64) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)0ULL));
            if (!t90) {
                goto gm_lb11;
            }
            memmove(((uint8_t *)&F.l13), (((((uint8_t *)t88) + 40) + 64) + 8), 64);
            {
                {
                    uint8_t t91 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l13) + 24)) != 0);
                    if (t91) {
                        goto gm_lb13;
                    }
                    gm_l23 = ((uint8_t)0);
                    goto gm_lb12;
                }
gm_lb13:;
                {
                    gm_l23 = ((uint8_t)1);
                    goto gm_lb12;
                }
gm_lb14:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 205, 8);
            }
gm_lb12:;
            uint8_t t92 = gm_l23;
            if (t92) {
                gm_l22 = ((uint8_t)1);
            } else {
                uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)&F.l13) + 8) + 8));
                uint8_t t94 = (!(t93 == ((uint64_t)1ULL)));
                gm_l22 = t94;
            }
            uint8_t t95 = gm_l22;
            if (t95) {
                gm_tf5srh4mp4b3i t96;
                memset(&t96, 0, sizeof t96);
                F.ret = t96;
                goto gm_fret;
            }
            void * t97 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
            {
                {
                    uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t97) + 8) + 0));
                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)38ULL));
                    if (!t99) {
                        goto gm_lb16;
                    }
                    {
                        {
                            void * t100 = gm_l0;
                            uint64_t t101 = gm_l4;
                            gm_f_resolve_decl_pus042v((void *)&gm_l24, (void *)NULL, cx, NULL, t100, t101);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t103 = (uint8_t)(t102 == ((uint8_t)9ULL));
                            if (!t103) {
                                goto gm_lb18;
                            }
                            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                            gm_l14 = t104;
                            uint64_t t105 = gm_l14;
                            gm_tf5srh4mp4b3i t106;
                            GM_ST(uint8_t, (uint8_t *)&t106 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t106 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t106 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t106), t105);
                            ((uint8_t *)&t106)[8] = 1;
                            F.ret = t106;
                            goto gm_fret;
                        }
gm_lb18:;
                        gm_tf5srh4mp4b3i t107;
                        memset(&t107, 0, sizeof t107);
                        F.ret = t107;
                        goto gm_fret;
                    }
gm_lb17:;
                }
gm_lb16:;
                {
                    {
                        {
                            uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t97) + 8) + 0));
                            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)6ULL));
                            if (!t109) {
                                goto gm_lb21;
                            }
                            goto gm_lb20;
                        }
gm_lb21:;
                        {
                            uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t97) + 8) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)22ULL));
                            if (!t111) {
                                goto gm_lb22;
                            }
                            goto gm_lb20;
                        }
gm_lb22:;
                        goto gm_lb19;
                    }
gm_lb20:;
                    void * t112 = gm_l0;
                    uint64_t t113 = gm_l12;
                    void * t114 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
                    uint32_t t115 = gm_l3;
                    uint32_t r116; bool o117 = __builtin_add_overflow(t115, ((uint32_t)1ULL), &r116);
                    if (o117) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 212, 68);
                    uint32_t t118 = r116;
                    gm_tf5srh4mp4b3i t119 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t112, t113, t114, t118);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t119;
                    goto gm_fret;
                }
gm_lb19:;
                {
                    gm_tf5srh4mp4b3i t120;
                    memset(&t120, 0, sizeof t120);
                    F.ret = t120;
                    goto gm_fret;
                }
gm_lb23:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 206, 5);
            }
gm_lb15:;
        }
gm_lb11:;
        gm_tf5srh4mp4b3i t121;
        memset(&t121, 0, sizeof t121);
        F.ret = t121;
        goto gm_fret;
    }
gm_lb10:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_param_3av4r8i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1608) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 55, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1608));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1608) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 56, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 24));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1608));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1608) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 56, 57);
                    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t21) + (uintptr_t)t23 * 24) + 16));
                    gm_tkn42u7na3t81 t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[4] = 1;
                    F.ret = t25;
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tkn42u7na3t81 t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unknown_type_beat5io(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_unknown_type_beat5io F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* msg */
    gm_b16_8 gm_l6; /* hint */
    gm_b16_8 gm_l8; /* last */
    GM_STACK_CHECK(&F, 882u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_b16_8 gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_th2lqbo77pvsc gm_l12;
    gm_tqetrgo0q24b5 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_b16_8 t6 = gm_l2;
    uint8_t t7 = gm_f_is_oversized_int_name_nr25haf(t6);
    if (t7) {
        gm_b16_8 t8 = gm_l2;
        gm_tnpifv20irnph t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
        gm_f_format_oq3rvor((void *)&gm_l10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcsffver18si54, 932, 9);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
        gm_l9 = t11;
    } else {
        gm_b16_8 t12 = gm_l2;
        gm_tnpifv20irnph t13;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
        gm_f_format_pplcoh0((void *)&gm_l11, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcsffver18si54, 934, 9);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_l9 = t15;
    }
    gm_b16_8 t16 = gm_l9;
    gm_l4 = t16;
    gm_tl874t2atnlh3 t17 = gm_l3;
    gm_b16_8 t18 = gm_l4;
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
    gm_t4gut2c9fj1kt t20;
    gm_f_error_s6hn871((void *)&t20, (void *)NULL, cx, NULL, t17, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t20;
    {
        {
            void * t21 = gm_l0;
            gm_b16_8 t22 = gm_l2;
            gm_f_universe_hint_ams3f10((void *)&gm_l12, (void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
            if (!t23) {
                goto gm_lb2;
            }
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
            gm_l6 = t24;
            void * t25 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t26 = gm_l6;
            gm_f_add_note_46v502s(t25, t26);
            {
                {
                    void * t27 = gm_l0;
                    uint64_t t28 = gm_l1;
                    gm_b16_8 t29 = gm_l2;
                    gm_tl874t2atnlh3 t30 = gm_l3;
                    gm_f_import_fix_7822666((void *)&gm_l13, (void *)NULL, cx, NULL, t27, t28, t29, t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l13))[40] != 0);
                    if (!t31) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l13), 40);
                    void * t32 = GM_LD(void *, ((uint8_t *)&F.l7));
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                    uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                    if (o36) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 940, 33);
                    uint64_t t37 = r35;
                    if ((uintptr_t)t37 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t37, t33, gm_srcsffver18si54, 940, 21);
                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + (uintptr_t)t37 * 32) + 16));
                    gm_l8 = t38;
                    void * t39 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t40 = gm_l8;
                    gm_tnpifv20irnph t41;
                    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                    gm_f_format_ooibcq2((void *)&gm_l14, (void *)NULL, cx, NULL, t41);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcsffver18si54, 941, 29);
                    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                    gm_b0_1 t47 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t39, t44, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t48 = gm_l0;
    gm_b0_1 t49 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t48, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_param_tof1h6t F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1648) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 128, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1648));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1648) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 129, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 24));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1648));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1648) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 129, 57);
                    uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t21) + (uintptr_t)t23 * 24) + 16));
                    gm_t9805qtonbsag t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[4] = 1;
                    F.ret = t25;
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_t9805qtonbsag t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_with_operand_ajbkgld F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* operand */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    uint8_t gm_l4; /* c */
    void * gm_l5; /* inner */
    uint8_t gm_l6; /* c */
    void * gm_l7; /* inner */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* inner */
    GM_STACK_CHECK(&F, 1005u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t2gtbh39ptekd gm_l11;
    gm_tq7s98ihonkju gm_l12;
    gm_tq7s98ihonkju gm_l13;
    void * t9 = gm_l0;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)28ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t9) + 8) + 8));
            gm_l2 = t12;
            void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
            void * t14 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
            void * t15 = gm_l1;
            void * t16 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t17;
            GM_ST(void *, ((uint8_t *)&t17 + 0), t13);
            GM_ST(void *, ((uint8_t *)&t17 + 8), t16);
            gm_t2gtbh39ptekd t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)28ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t18 + 8), t17);
            gm_l11 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)24ULL));
            if (!t20) {
                goto gm_lb3;
            }
            void * t21 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l3 = t21;
            void * t22 = gm_l3;
            void * t23 = gm_l1;
            void * t24 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2gtbh39ptekd t25;
            memset(&t25, 0, sizeof t25);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&t25 + 8), t24);
            gm_l11 = t25;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)25ULL));
            if (!t27) {
                goto gm_lb4;
            }
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l4 = t28;
            void * t29 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
            gm_l5 = t29;
            uint8_t t30 = gm_l4;
            void * t31 = gm_l5;
            void * t32 = gm_l1;
            void * t33 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), t30);
            GM_ST(void *, ((uint8_t *)&t34 + 8), t33);
            gm_t2gtbh39ptekd t35;
            memset(&t35, 0, sizeof t35);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)25ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t35 + 8), t34);
            gm_l11 = t35;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)26ULL));
            if (!t37) {
                goto gm_lb5;
            }
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l6 = t38;
            void * t39 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
            gm_l7 = t39;
            uint8_t t40 = gm_l6;
            void * t41 = gm_l7;
            void * t42 = gm_l1;
            void * t43 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), t40);
            GM_ST(void *, ((uint8_t *)&t44 + 8), t43);
            gm_t2gtbh39ptekd t45;
            memset(&t45, 0, sizeof t45);
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)26ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t45 + 8), t44);
            gm_l11 = t45;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)27ULL));
            if (!t47) {
                goto gm_lb6;
            }
            uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l8 = t48;
            void * t49 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
            gm_l9 = t49;
            uint8_t t50 = gm_l8;
            void * t51 = gm_l9;
            void * t52 = gm_l1;
            void * t53 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), t50);
            GM_ST(void *, ((uint8_t *)&t54 + 8), t53);
            gm_t2gtbh39ptekd t55;
            memset(&t55, 0, sizeof t55);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)27ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t55 + 8), t54);
            gm_l11 = t55;
            goto gm_lb1;
        }
gm_lb6:;
        {
            void * t56 = gm_l1;
            gm_tq7s98ihonkju t57 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t56)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ks263t40fiu)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t57;
            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 923, 21);
            void * t59 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
            F.ret = t59;
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 917, 26);
    }
gm_lb1:;
    memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l11), 64);
    void * t60 = gm_l0;
    uint32_t t61 = GM_LD(uint32_t, ((uint8_t *)t60));
    void * t62 = gm_l0;
    gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 72));
    gm_t2gtbh39ptekd t64 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l10));
    gm_tc3oht4ksth9d t65;
    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t65 + 84, 0);
    GM_ST(uint32_t, ((uint8_t *)&t65 + 0), t61);
    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t65 + 8), t64);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t65 + 72), t63);
    gm_tq7s98ihonkju t66 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t65, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k027ibn62fv)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t66;
    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcsffver18si54, 925, 12);
    void * t68 = GM_LD(void *, (((uint8_t *)&gm_l13) + 8));
    F.ret = t68;
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
