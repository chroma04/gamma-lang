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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { int64_t a[4]; } gm_t767tucha9lsh;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tm98ap4112hee f0; gm_b16_8 f1; } gm_t3olsle24rveu;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; gm_t9qkh0rsooc8d f10; gm_t9qkh0rsooc8d f11; gm_t9qkh0rsooc8d f12; gm_t9qkh0rsooc8d f13; } gm_t4qfn59m757do;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_t4ce0jd3tdro4 f0; uint8_t f1; uint8_t pe[7]; } gm_t6ie8iajlnpt1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t767tucha9lsh) == 32 && _Alignof(gm_t767tucha9lsh) == 8 && offsetof(gm_t767tucha9lsh, a) == 0, "layout of `[4]i64`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; uint16_t v1; } f1; } gm_tvcqs9u9iqpct;
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_t617vr72bd7b3 f0; uint8_t f1; uint8_t pe[7]; } gm_t11t9d8s5diq9;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3olsle24rveu) == 24 && _Alignof(gm_t3olsle24rveu) == 8 && offsetof(gm_t3olsle24rveu, f0) == 0 && offsetof(gm_t3olsle24rveu, f1) == 8, "layout of `(file: File, buf: []u8)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t4qfn59m757do) == 14 && _Alignof(gm_t4qfn59m757do) == 1 && offsetof(gm_t4qfn59m757do, f0) == 0 && offsetof(gm_t4qfn59m757do, f1) == 1 && offsetof(gm_t4qfn59m757do, f2) == 2 && offsetof(gm_t4qfn59m757do, f3) == 3 && offsetof(gm_t4qfn59m757do, f4) == 4 && offsetof(gm_t4qfn59m757do, f5) == 5 && offsetof(gm_t4qfn59m757do, f6) == 6 && offsetof(gm_t4qfn59m757do, f7) == 7 && offsetof(gm_t4qfn59m757do, f8) == 8 && offsetof(gm_t4qfn59m757do, f9) == 9 && offsetof(gm_t4qfn59m757do, f10) == 10 && offsetof(gm_t4qfn59m757do, f11) == 11 && offsetof(gm_t4qfn59m757do, f12) == 12 && offsetof(gm_t4qfn59m757do, f13) == 13, "layout of `(SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6ie8iajlnpt1) == 16 && _Alignof(gm_t6ie8iajlnpt1) == 8 && offsetof(gm_t6ie8iajlnpt1, f0) == 0 && offsetof(gm_t6ie8iajlnpt1, f1) == 8, "layout of `?Duration`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_trqfvmjtt4sij v1; } f1; } gm_t9922qjstblh1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; int64_t v1; } f1; } gm_tarq9lp8tdk6f;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_thf5kr0is9u9i f0; gm_tppo2hmut88dq f1; } gm_tfvg1o4f8mmhi;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3olsle24rveu v1; gm_tvaas22dl64e0 v2; gm_tm98ap4112hee v3; gm_t3olsle24rveu v4; gm_t3olsle24rveu v5; } f1; } gm_tmvdbo28o1i6h;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t62ebbi2i5rkd v1; } f1; } gm_toqti1ubjnfd1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_t4c7ipjj9s3fp f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; int64_t f5; gm_tf5srh4mp4b3i f6; } gm_ttse10gjbhca2;
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_tvcqs9u9iqpct) == 12 && _Alignof(gm_tvcqs9u9iqpct) == 4 && offsetof(gm_tvcqs9u9iqpct, f0) == 0 && offsetof(gm_tvcqs9u9iqpct, f1) == 4, "layout of `IoError!u16`");
_Static_assert(sizeof(gm_t11t9d8s5diq9) == 24 && _Alignof(gm_t11t9d8s5diq9) == 8 && offsetof(gm_t11t9d8s5diq9, f0) == 0 && offsetof(gm_t11t9d8s5diq9, f1) == 16, "layout of `?DirEntry`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tfvg1o4f8mmhi v1; } f1; } gm_t6j8vdmi81h7c;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_t9922qjstblh1) == 48 && _Alignof(gm_t9922qjstblh1) == 8 && offsetof(gm_t9922qjstblh1, f0) == 0 && offsetof(gm_t9922qjstblh1, f1) == 8, "layout of `Flow!List(string)`");
_Static_assert(sizeof(gm_tarq9lp8tdk6f) == 32 && _Alignof(gm_tarq9lp8tdk6f) == 8 && offsetof(gm_tarq9lp8tdk6f, f0) == 0 && offsetof(gm_tarq9lp8tdk6f, f1) == 8, "layout of `Flow!i64`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tfvg1o4f8mmhi) == 32 && _Alignof(gm_tfvg1o4f8mmhi) == 8 && offsetof(gm_tfvg1o4f8mmhi, f0) == 0 && offsetof(gm_tfvg1o4f8mmhi, f1) == 16, "layout of `HostResult`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t11t9d8s5diq9 v1; } f1; } gm_tiuq7k5hm0kak;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
_Static_assert(sizeof(gm_tmvdbo28o1i6h) == 32 && _Alignof(gm_tmvdbo28o1i6h) == 8 && offsetof(gm_tmvdbo28o1i6h, f0) == 0 && offsetof(gm_tmvdbo28o1i6h, f1) == 8, "layout of `HostOp`");
_Static_assert(sizeof(gm_toqti1ubjnfd1) == 32 && _Alignof(gm_toqti1ubjnfd1) == 8 && offsetof(gm_toqti1ubjnfd1, f0) == 0 && offsetof(gm_toqti1ubjnfd1, f1) == 8, "layout of `Flow!IoError!Socket`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
_Static_assert(sizeof(gm_ttse10gjbhca2) == 304 && _Alignof(gm_ttse10gjbhca2) == 8 && offsetof(gm_ttse10gjbhca2, f0) == 0 && offsetof(gm_ttse10gjbhca2, f1) == 56 && offsetof(gm_ttse10gjbhca2, f2) == 112 && offsetof(gm_ttse10gjbhca2, f3) == 168 && offsetof(gm_ttse10gjbhca2, f4) == 224 && offsetof(gm_ttse10gjbhca2, f5) == 280 && offsetof(gm_ttse10gjbhca2, f6) == 288, "layout of `SysState`");
_Static_assert(sizeof(gm_t6j8vdmi81h7c) == 40 && _Alignof(gm_t6j8vdmi81h7c) == 8 && offsetof(gm_t6j8vdmi81h7c, f0) == 0 && offsetof(gm_t6j8vdmi81h7c, f1) == 8, "layout of `Flow!HostResult`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tiuq7k5hm0kak) == 32 && _Alignof(gm_tiuq7k5hm0kak) == 8 && offsetof(gm_tiuq7k5hm0kak, f0) == 0 && offsetof(gm_tiuq7k5hm0kak, f1) == 8, "layout of `IoError!?DirEntry`");
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cpu_count_0o5882g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_deinit_gvn47sj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_16ab7e4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_57qa4h8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6l50clp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hgrt2o4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_khi2i98 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_1lff729 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2gkkusq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aqcn8lk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cijad0r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ducmvdu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j9i1sr8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mslt7ot {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r8m0qjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rg62rr4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tlii61s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_trt04qi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vhnrvmh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_map_3d5nsdr {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_3sndrtb {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_abi2bn7 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_irh77t1 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ql49boo {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_socket_on_host_thread_u8c2vg5 {
    gm_frame h;
    gm_toqti1ubjnfd1 ret;
};
struct gm_fr_sys_code_cjtlfo1 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_count_50c5lcp {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_dest_rtrb14h {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_fs_u0grkf6 {
    gm_frame h;
    gm_tpekhfs32op0f l28; /* err */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_has_0sego5l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sys_int_32e79ed {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_kind_sjfdqbt {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_aff7h5l {
    gm_frame h;
    gm_b16_8 l6; /* host */
    uint16_t l7; /* port */
    gm_t62ebbi2i5rkd l8; /* r */
    gm_b16_8 l12; /* host */
    uint16_t l13; /* port */
    gm_t62ebbi2i5rkd l14; /* r */
    gm_tm98ap4112hee l18; /* l */
    gm_tvcqs9u9iqpct l19; /* r */
    gm_tm98ap4112hee l22; /* l */
    gm_t62ebbi2i5rkd l23; /* r */
    gm_b16_8 l27; /* data */
    gm_tm98ap4112hee l28; /* s */
    gm_thf5kr0is9u9i l29; /* r */
    gm_b16_8 l30; /* buf */
    gm_tm98ap4112hee l31; /* s */
    gm_thf5kr0is9u9i l32; /* r */
    gm_tm98ap4112hee l33; /* s */
    gm_tm98ap4112hee l34; /* l */
    gm_t9qkh0rsooc8d l35; /* how */
    gm_tm98ap4112hee l36; /* s */
    gm_ttln0d0hd70en l37; /* r */
    gm_tm98ap4112hee l40; /* s */
    gm_t6ie8iajlnpt1 l41; /* recv */
    gm_t6ie8iajlnpt1 l42; /* send */
    gm_ttln0d0hd70en l43; /* r */
    gm_tm98ap4112hee l44; /* s */
    gm_b16_8 l45; /* buf */
    gm_thf5kr0is9u9i l46; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_net_code_ihgq920 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_count_46l7ueq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_next_handle_r35ek81 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_proc_jhdp277 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* argv */
    gm_trqfvmjtt4sij l6; /* env */
    gm_t32dckm4jfarl l10; /* opts */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_read_b0gj9fg {
    gm_frame h;
    gm_b16_8 l3; /* buf */
    gm_thf5kr0is9u9i l4; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_ret_vpa2k8i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sys_run_2vqjo3u {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_sys_src_5svsr6i {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_state_cvl1lvl {
    gm_frame h;
    gm_ttse10gjbhca2 ret;
};
struct gm_fr_sys_stdio_nsl9o9u {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_sys_strings_r85ragc {
    gm_frame h;
    gm_tpekhfs32op0f l5; /* e */
    gm_trqfvmjtt4sij l7; /* out */
    gm_t9922qjstblh1 ret;
};
struct gm_fr_sys_unit_nrsqciq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void);
__attribute__((aligned(4))) void gm_f_defaults_p077jgf(gm_closure a0);
inline __attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void);
__attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_8o0rtd2(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_f2mbmm8(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_fqooi5f(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_gej4j8d(void * a0, int64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_v07up09(void * a0, int64_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0c6jnkb(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0tusk8g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_endcgjj(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ha6815g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qkbvor7(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_20jm5n8(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_3j2d0bb(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_7bvuc21(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_82qm8hc(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_jvpr521(void * a0, int64_t a1);
__attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
__attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
inline __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cbcngu2pqs7[8] __attribute__((aligned(8)));
extern uint8_t gm_cndfnv7mbeh[8] __attribute__((aligned(8)));
extern const uint8_t gm_s27n8348bef64[63];
extern const uint8_t gm_sd8o4m7695hpq[38];
extern const uint8_t gm_sqn7kgtu67hrg[42];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcn3jjc71a2bv3[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stfd80s46313k[38];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint8_t t3 = (t2 <= ((uint64_t)1ULL));
    if (t3) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l1;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
    uint64_t t8 = r6;
    uint64_t t9 = (uint64_t)(t4 & t8);
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        uint64_t t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)(t11 & t15);
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l0;
    uint64_t t19 = gm_l1;
    if (t19 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
    uint64_t t20 = (uint64_t)(t18 % t19);
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cpu_count_0o5882g F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t t2 = gm_cpu_count();
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_gvn47sj F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)16ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_5nn7ph0();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_16ab7e4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_57qa4h8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6l50clp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hgrt2o4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_khi2i98 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1lff729 F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l34));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 406, 50);
    gm_b0_1 t4;
    t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[7])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_2gkkusq F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l44));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l45));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 436, 50);
    gm_thf5kr0is9u9i t5;
    t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[10])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l46), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_aqcn8lk F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l6));
    uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l7));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 341, 54);
    gm_t62ebbi2i5rkd t5;
    t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l8), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cijad0r F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l12));
    uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l13));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 352, 50);
    gm_t62ebbi2i5rkd t5;
    t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[1])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l14), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_ducmvdu F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l31));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l30));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 396, 54);
    gm_thf5kr0is9u9i t5;
    t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[5])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l32), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_j9i1sr8 F;
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l40));
    gm_t6ie8iajlnpt1 t3 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l41));
    gm_t6ie8iajlnpt1 t4 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l42));
    gm_node *pn5 = gm_find(cx, 163658498);
    if (!pn5) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 429, 50);
    gm_ttln0d0hd70en t6;
    t6 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t6ie8iajlnpt1, gm_t6ie8iajlnpt1))pn5->clauses[9])(pn5->env, pn5->hctx, pn5, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l43), t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_mslt7ot F;
    GM_STACK_CHECK(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l18));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 361, 50);
    gm_tvcqs9u9iqpct t4;
    t4 = ((gm_tvcqs9u9iqpct (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[2])(pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tvcqs9u9iqpct, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l19), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_r8m0qjr F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l33));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 402, 50);
    gm_b0_1 t4;
    t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[6])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_rg62rr4 F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l22));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 371, 54);
    gm_t62ebbi2i5rkd t4;
    t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[3])(pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l23), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_tlii61s F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l28));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l27));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 385, 54);
    gm_thf5kr0is9u9i t5;
    t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[4])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l29), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_trt04qi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l3));
    gm_node *pn3 = gm_find(cx, 1018230276);
    if (!pn3) gm_trap("no handler installed for effect `In`", gm_srcn3jjc71a2bv3, 195, 41);
    gm_thf5kr0is9u9i t4;
    t4 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[0])(pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l4), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_vhnrvmh F;
    GM_STACK_CHECK(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l36));
    gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l35));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 419, 50);
    gm_ttln0d0hd70en t5;
    t5 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t9qkh0rsooc8d))pn4->clauses[8])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l37), t5);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3d5nsdr F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hgrt2o4();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3sndrtb F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_57qa4h8();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_abi2bn7 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6l50clp();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_irh77t1 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_16ab7e4();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ql49boo F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_khi2i98();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
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
    gm_b16_8 t4 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
            ((uint8_t *)&t11)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
            memmove(gm_out, ((uint8_t *)&t11), 48);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t12 = gm_l1;
    uint64_t t13 = gm_l3;
    uint8_t t14 = gm_f_aligned_q6sor4h(t12, t13);
    if (t14) {
        {
            {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l1;
                uint64_t t17 = gm_l2;
                void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
                gm_l5 = t18;
                uint8_t t19 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t19) {
                    goto gm_lb4;
                }
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t20;
                void * t21 = gm_l4;
                uint64_t t22 = gm_l2;
                uint64_t t23 = gm_f_us_cuv01fp(t22);
                uint64_t t24 = ((uintptr_t)t23) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
                gm_t934k1j7u70ct t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 40, 0);
                ((uint8_t *)&t26)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
                memmove(gm_out, ((uint8_t *)&t26), 48);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    void * t27 = gm_l0;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_l2;
    uint64_t t30 = gm_l3;
    gm_tpekhfs32op0f t31;
    gm_f_fault_u31b92l((void *)&t31, t27, t28, t29, t30, ((uint8_t)0));
    gm_t934k1j7u70ct t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
    ((uint8_t *)&t32)[0] = 1;
    GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t32 + 8), t31);
    memmove(gm_out, ((uint8_t *)&t32), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_count_50c5lcp F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l3;
    {
        {
            uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (t5) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
            gm_l1 = t6;
            gm_toe5fmr3vvf4d t7 = gm_l1;
            int64_t t8 = gm_f_sys_code_cjtlfo1(t7);
            gm_l3 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (!t9) {
                goto gm_lb3;
            }
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            gm_l2 = t10;
            uint64_t t11 = gm_l2;
            int64_t t12 = (int64_t)t11;
            gm_l3 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 78, 45);
    }
gm_lb1:;
    int64_t t13 = gm_l3;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_has_0sego5l F;
    uint32_t gm_l0; /* flags */
    uint32_t gm_l1; /* bit */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint32_t t4 = (uint32_t)(t2 & t3);
    uint8_t t5 = (!(t4 == ((uint32_t)0ULL)));
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_int_32e79ed F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* b */
    __int128 gm_l3; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    uint8_t gm_l4;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
    __int128 t6;
    memset(&t6, 0, sizeof t6); memcpy(&t6, ((uint8_t *)&t5), sizeof t6 < 16 ? sizeof t6 : 16);
    gm_l3 = t6;
    __int128 t7 = gm_l3;
    uint8_t t8 = (t7 < ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 9223372036854775808ULL)));
    if (t8) {
        gm_l4 = ((uint8_t)1);
    } else {
        __int128 t9 = gm_l3;
        uint8_t t10 = (t9 > ((__int128)(((unsigned __int128)0ULL << 64) | 9223372036854775807ULL)));
        gm_l4 = t10;
    }
    uint8_t t11 = gm_l4;
    if (t11) {
        F.ret = ((int64_t)-1LL);
        goto gm_fret;
    }
    __int128 t12 = gm_l3;
    int64_t t13 = (int64_t)t12;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_code_ihgq920 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    gm_toe5fmr3vvf4d t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    uint8_t t4 = (uint8_t)((t3 == ((uint8_t)3ULL)));
    gm_toe5fmr3vvf4d t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)3ULL));
    if (t4) {
        int64_t t6 = GM_LD(int64_t, ((uint8_t *)gm_cbcngu2pqs7));
        gm_l1 = t6;
    } else {
        gm_toe5fmr3vvf4d t7 = gm_l0;
        int64_t t8 = gm_f_sys_code_cjtlfo1(t7);
        gm_l1 = t8;
    }
    int64_t t9 = gm_l1;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_count_46l7ueq F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l3;
    {
        {
            uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (t5) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
            gm_l1 = t6;
            gm_toe5fmr3vvf4d t7 = gm_l1;
            int64_t t8 = gm_f_sys_net_code_ihgq920(t7);
            gm_l3 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (!t9) {
                goto gm_lb3;
            }
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            gm_l2 = t10;
            uint64_t t11 = gm_l2;
            int64_t t12 = (int64_t)t11;
            gm_l3 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 84, 49);
    }
gm_lb1:;
    int64_t t13 = gm_l3;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_next_handle_r35ek81 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    int64_t t4 = GM_LD(int64_t, (((uint8_t *)t3) + 280));
    int64_t r5; bool o6 = __builtin_add_overflow(t4, ((int64_t)1LL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 50, 5);
    int64_t t7 = r5;
    GM_ST(int64_t, (((uint8_t *)t2) + 280), t7);
    void * t8 = gm_l0;
    int64_t t9 = GM_LD(int64_t, (((uint8_t *)t8) + 280));
    int64_t r10; bool o11 = __builtin_add_overflow(t9, ((int64_t)2LL), &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 51, 12);
    int64_t t12 = r10;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_ret_vpa2k8i F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* dst */
    int64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    int64_t t4 = gm_l2;
    uint64_t t5;
    memset(&t5, 0, sizeof t5); memcpy(&t5, ((uint8_t *)&t4), sizeof t5 < 8 ? sizeof t5 : 8);
    unsigned __int128 t6 = (unsigned __int128)t5;
    gm_f_put_uint_cddin99(t2, t3, t6);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_stdio_nsl9o9u F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l1;
    {
        {
            uint32_t t6 = gm_l0;
            uint8_t t7 = ((t6 == ((uint32_t)1ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t8;
            GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)1ULL));
            gm_l1 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint32_t t9 = gm_l0;
            uint8_t t10 = ((t9 == ((uint32_t)2ULL)));
            if (!t10) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)2ULL));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_l1 = t12;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 106, 48);
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t13 = gm_l1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_unit_nrsqciq F;
    gm_ttln0d0hd70en gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l2;
    {
        {
            uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (t5) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t6;
            gm_toe5fmr3vvf4d t7 = gm_l1;
            int64_t t8 = gm_f_sys_code_cjtlfo1(t7);
            gm_l2 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((int64_t)0LL);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 90, 41);
    }
gm_lb1:;
    int64_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 538u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    uint64_t t7 = r5;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n9 = (unsigned __int128)t8;
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
    uint64_t t10 = (uint64_t)(t7 >> (int)n9);
    gm_l4 = t10;
    uint64_t t11 = gm_l4;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    uint64_t t14 = (uint64_t)t13;
    uint8_t t15 = (t11 >= t14);
    if (t15) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = gm_l4;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 32);
    gm_l5 = t20;
    void * t21 = gm_l5;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)t21)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)t21));
        gm_l11 = t23;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t24 = gm_l11;
    gm_l6 = t24;
    uint64_t t25 = gm_l1;
    void * t26 = gm_l5;
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 8));
    uint64_t r28; bool o29 = __builtin_sub_overflow(t25, t27, &r28);
    if (o29) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
    uint64_t t30 = r28;
    gm_l7 = t30;
    uint64_t t31 = gm_l7;
    void * t32 = gm_l5;
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 16));
    unsigned __int128 n34 = (unsigned __int128)t33;
    if (n34 >= 64) gm_trap_shift((uint64_t)n34, "u64", gm_srcq7ohh8h77bn7, 211, 10);
    uint64_t t35 = (uint64_t)(t31 >> (int)n34);
    gm_l8 = t35;
    void * t36 = gm_l6;
    uint64_t t37 = gm_l8;
    uint64_t t38 = gm_f_us_cuv01fp(t37);
    void * t39 = (void *)(((uint8_t *)t36) + (uintptr_t)t38 * 24);
    gm_l9 = t39;
    uint64_t t40 = gm_l7;
    uint64_t t41 = gm_l8;
    void * t42 = gm_l5;
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 16));
    unsigned __int128 n44 = (unsigned __int128)t43;
    if (n44 >= 64) gm_trap_shift((uint64_t)n44, "u64", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t r45 = (uint64_t)((uint64_t)t41 << (int)n44);
    bool o46 = (uint64_t)(r45 >> (int)n44) != t41;
    if (o46) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t t47 = r45;
    uint64_t r48; bool o49 = __builtin_sub_overflow(t40, t47, &r48);
    if (o49) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
    uint64_t t50 = r48;
    gm_l10 = t50;
    void * t51 = gm_l9;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)t51) + 17));
    uint8_t t53 = (uint8_t)!t52;
    if (t53) {
        gm_l14 = ((uint8_t)1);
    } else {
        uint64_t t54 = gm_l2;
        void * t55 = gm_l9;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 8));
        uint8_t t57 = (t54 > t56);
        gm_l14 = t57;
    }
    uint8_t t58 = gm_l14;
    if (t58) {
        gm_l13 = ((uint8_t)1);
    } else {
        uint64_t t59 = gm_l10;
        void * t60 = gm_l9;
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 8));
        uint64_t t62 = gm_l2;
        uint64_t r63; bool o64 = __builtin_sub_overflow(t61, t62, &r63);
        if (o64) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
        uint64_t t65 = r63;
        uint8_t t66 = (t59 > t65);
        gm_l13 = t66;
    }
    uint8_t t67 = gm_l13;
    if (t67) {
        gm_l12 = ((uint8_t)1);
    } else {
        uint8_t t68 = gm_l3;
        if (t68) {
            void * t69 = gm_l9;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t69) + 18));
            uint8_t t71 = (uint8_t)!t70;
            gm_l15 = t71;
        } else {
            gm_l15 = ((uint8_t)0);
        }
        uint8_t t72 = gm_l15;
        gm_l12 = t72;
    }
    uint8_t t73 = gm_l12;
    if (t73) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t74 = gm_l9;
    void * t75 = GM_LD(void *, ((uint8_t *)t74));
    uint64_t t76 = gm_l10;
    uint64_t t77 = gm_f_us_cuv01fp(t76);
    void * t78 = (void *)(((uint8_t *)t75) + (uintptr_t)t77 * 1);
    void * t79 = t78;
    F.ret = t79;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
            ((uint8_t *)&t11)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
            memmove(gm_out, ((uint8_t *)&t11), 48);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t12 = gm_l1;
    uint64_t t13 = gm_l3;
    uint8_t t14 = gm_f_aligned_q6sor4h(t12, t13);
    if (t14) {
        {
            {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l1;
                uint64_t t17 = gm_l2;
                void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)1));
                gm_l5 = t18;
                uint8_t t19 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t19) {
                    goto gm_lb4;
                }
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t20;
                void * t21 = gm_l4;
                uint64_t t22 = gm_l2;
                uint64_t t23 = gm_f_us_cuv01fp(t22);
                uint64_t t24 = ((uintptr_t)t23) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
                gm_t934k1j7u70ct t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 40, 0);
                ((uint8_t *)&t26)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
                memmove(gm_out, ((uint8_t *)&t26), 48);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    void * t27 = gm_l0;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_l2;
    uint64_t t30 = gm_l3;
    gm_tpekhfs32op0f t31;
    gm_f_fault_u31b92l((void *)&t31, t27, t28, t29, t30, ((uint8_t)1));
    gm_t934k1j7u70ct t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
    ((uint8_t *)&t32)[0] = 1;
    GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t32 + 8), t31);
    memmove(gm_out, ((uint8_t *)&t32), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_socket_on_host_thread_u8c2vg5 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmvdbo28o1i6h gm_l2; /* op */
    gm_tfvg1o4f8mmhi gm_l3; /* r */
    gm_t62ebbi2i5rkd gm_l4; /* result */
    gm_tm98ap4112hee gm_l5; /* s */
    gm_toe5fmr3vvf4d gm_l6; /* e */
    GM_STACK_CHECK(&F, 475u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_t6j8vdmi81h7c gm_l7;
    uint64_t gm_l8;
    void * t4 = gm_l0;
    gm_tl874t2atnlh3 t5 = gm_l1;
    gm_tmvdbo28o1i6h t6 = GM_LD(gm_tmvdbo28o1i6h, ((uint8_t *)&gm_l2));
    gm_f_on_host_thread_kmegc07((void *)&gm_l7, (void *)NULL, cx, NULL, t4, t5, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t7) {
    } else {
        gm_t76123gfet73g t8 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l7) + 8));
        gm_toqti1ubjnfd1 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        ((uint8_t *)&t9)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t9 + 8), t8);
        memmove(gm_out, ((uint8_t *)&t9), 32);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l7) + 8), 32);
    gm_toe5fmr3vvf4d t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)12ULL));
    gm_t62ebbi2i5rkd t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
    ((uint8_t *)&t11)[0] = 1;
    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t11 + 8), t10);
    gm_l4 = t11;
    {
        {
            uint8_t t12 = (uint8_t)(((((uint8_t *)&gm_l3) + 16))[8] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            gm_tm98ap4112hee t13 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l3) + 16));
            gm_l5 = t13;
            gm_tm98ap4112hee t14 = gm_l5;
            gm_t62ebbi2i5rkd t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t15 + 8), t14);
            gm_l4 = t15;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t16) {
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_l8 = t17;
    } else {
        gm_toe5fmr3vvf4d t18 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l3) + 8));
        gm_l6 = t18;
        gm_toe5fmr3vvf4d t19 = gm_l6;
        gm_t62ebbi2i5rkd t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        ((uint8_t *)&t20)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t20 + 8), t19);
        gm_l4 = t20;
        gm_t62ebbi2i5rkd t21 = gm_l4;
        gm_toqti1ubjnfd1 t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
        ((uint8_t *)&t22)[0] = 0;
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&t22 + 8), t21);
        memmove(gm_out, ((uint8_t *)&t22), 32);
        goto gm_fdone;
    }
    uint64_t t23 = gm_l8;
    gm_t62ebbi2i5rkd t24 = gm_l4;
    gm_toqti1ubjnfd1 t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
    ((uint8_t *)&t25)[0] = 0;
    GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&t25 + 8), t24);
    memmove(gm_out, ((uint8_t *)&t25), 32);
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_code_cjtlfo1 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    int32_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l2;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_l2 = ((int64_t)-1LL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb3;
            }
            gm_l2 = ((int64_t)-2LL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)2ULL));
            if (!t22) {
                goto gm_lb4;
            }
            gm_l2 = ((int64_t)-3LL);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)3ULL));
            if (!t24) {
                goto gm_lb5;
            }
            gm_l2 = ((int64_t)-4LL);
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)4ULL));
            if (!t26) {
                goto gm_lb6;
            }
            gm_l2 = ((int64_t)-5LL);
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)5ULL));
            if (!t28) {
                goto gm_lb7;
            }
            gm_l2 = ((int64_t)-6LL);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)6ULL));
            if (!t30) {
                goto gm_lb8;
            }
            gm_l2 = ((int64_t)-7LL);
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)7ULL));
            if (!t32) {
                goto gm_lb9;
            }
            gm_l2 = ((int64_t)-8LL);
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)8ULL));
            if (!t34) {
                goto gm_lb10;
            }
            gm_l2 = ((int64_t)-9LL);
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)9ULL));
            if (!t36) {
                goto gm_lb11;
            }
            gm_l2 = ((int64_t)-10LL);
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)10ULL));
            if (!t38) {
                goto gm_lb12;
            }
            gm_l2 = ((int64_t)-11LL);
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)11ULL));
            if (!t40) {
                goto gm_lb13;
            }
            gm_l2 = ((int64_t)-12LL);
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)12ULL));
            if (!t42) {
                goto gm_lb14;
            }
            gm_l2 = ((int64_t)-13LL);
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)13ULL));
            if (!t44) {
                goto gm_lb15;
            }
            int32_t t45 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t45;
            int32_t t46 = gm_l1;
            int64_t t47 = (int64_t)t46;
            int64_t r48; bool o49 = __builtin_sub_overflow(((int64_t)-1000LL), t47, &r48);
            if (o49) gm_trap("integer overflow in `-`", gm_srcn3jjc71a2bv3, 70, 18);
            int64_t t50 = r48;
            gm_l2 = t50;
            goto gm_lb1;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 56, 38);
    }
gm_lb1:;
    int64_t t51 = gm_l2;
    F.ret = t51;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_dest_rtrb14h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 361u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t934k1j7u70ct gm_l6;
    gm_b16_8 gm_l7;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
    gm_l3 = t4;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 184);
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    gm_f_write_view_pebit6h((void *)&gm_l6, t6, t7, t8, ((uint64_t)1ULL));
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_l7 = t10;
    } else {
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l6) + 8), 40);
        void * t11 = gm_l0;
        gm_tl874t2atnlh3 t12 = gm_l1;
        gm_t76123gfet73g t13;
        gm_f_mem_trap_ecogko0((void *)&t13, (void *)NULL, cx, NULL, t11, t12, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_td0ept12e7q8b t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        ((uint8_t *)&t14)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), t13);
        memmove(gm_out, ((uint8_t *)&t14), 32);
        goto gm_fdone;
    }
    gm_b16_8 t15 = gm_l7;
    gm_l5 = t15;
    gm_b16_8 t16 = gm_l5;
    gm_td0ept12e7q8b t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
    ((uint8_t *)&t17)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 8), t16);
    memmove(gm_out, ((uint8_t *)&t17), 32);
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
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_fs_u0grkf6 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_b16_8 gm_l6; /* path */
    uint32_t gm_l7; /* flags */
    gm_t4q2b5ibsrdik gm_l8; /* opts */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* f */
    int64_t gm_l11; /* handle */
    gm_tm98ap4112hee gm_l12; /* f */
    gm_b16_8 gm_l13; /* buf */
    gm_tm98ap4112hee gm_l14; /* f */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* f */
    int64_t gm_l17; /* off */
    int64_t gm_l18; /* whence */
    gm_tm98ap4112hee gm_l19; /* f */
    gm_t9qkh0rsooc8d gm_l20; /* from */
    gm_toe5fmr3vvf4d gm_l21; /* err */
    uint64_t gm_l22; /* pos */
    gm_b16_8 gm_l23; /* path */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_t5j14m8ru132p gm_l25; /* info */
    gm_t2lqu8jsj511d gm_l26; /* sp */
    uint64_t gm_l27; /* count */
    gm_b16_8 gm_l29; /* view */
    gm_t767tucha9lsh gm_l30; /* fields */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* i */
    gm_b16_8 gm_l34; /* from */
    gm_b16_8 gm_l35; /* to */
    gm_b16_8 gm_l36; /* path */
    gm_toe5fmr3vvf4d gm_l37; /* err */
    gm_tm98ap4112hee gm_l38; /* d */
    int64_t gm_l39; /* handle */
    gm_tm98ap4112hee gm_l40; /* d */
    gm_b16_8 gm_l41; /* buf */
    gm_toe5fmr3vvf4d gm_l42; /* err */
    gm_t11t9d8s5diq9 gm_l43; /* next */
    gm_t617vr72bd7b3 gm_l44; /* entry */
    gm_tm98ap4112hee gm_l45; /* d */
    uint64_t gm_l46; /* size */
    gm_tm98ap4112hee gm_l47; /* f */
    gm_tm98ap4112hee gm_l48; /* f */
    gm_b16_8 gm_l49; /* buf */
    gm_b16_8 gm_l50; /* path */
    gm_b16_8 gm_l51; /* buf */
    gm_b16_8 gm_l52; /* path */
    gm_b16_8 gm_l53; /* buf */
    gm_b16_8 gm_l54; /* target */
    gm_b16_8 gm_l55; /* path */
    gm_b16_8 gm_l56; /* path */
    gm_b16_8 gm_l57; /* path */
    GM_STACK_CHECK(&F, 9291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_td0ept12e7q8b gm_l58;
    gm_t62ebbi2i5rkd gm_l59;
    gm_tm98ap4112hee gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tppo2hmut88dq gm_l62;
    gm_tm98ap4112hee gm_l63;
    gm_td0ept12e7q8b gm_l64;
    gm_tppo2hmut88dq gm_l65;
    gm_tm98ap4112hee gm_l66;
    gm_t6j8vdmi81h7c gm_l67;
    gm_tfvg1o4f8mmhi gm_l68;
    gm_td0ept12e7q8b gm_l69;
    gm_tppo2hmut88dq gm_l70;
    gm_tm98ap4112hee gm_l71;
    gm_tppo2hmut88dq gm_l72;
    gm_tm98ap4112hee gm_l73;
    gm_t9qkh0rsooc8d gm_l74;
    gm_thf5kr0is9u9i gm_l75;
    uint64_t gm_l76;
    gm_td0ept12e7q8b gm_l77;
    gm_tih6h4avv5nrb gm_l78;
    gm_tih6h4avv5nrb gm_l79;
    gm_t5j14m8ru132p gm_l80;
    gm_t934k1j7u70ct gm_l81;
    gm_b16_8 gm_l82;
    gm_td0ept12e7q8b gm_l83;
    gm_td0ept12e7q8b gm_l84;
    gm_td0ept12e7q8b gm_l85;
    gm_td0ept12e7q8b gm_l86;
    gm_td0ept12e7q8b gm_l87;
    gm_td0ept12e7q8b gm_l88;
    gm_t62ebbi2i5rkd gm_l89;
    gm_tm98ap4112hee gm_l90;
    gm_tftb1odc739t5 gm_l91;
    gm_tppo2hmut88dq gm_l92;
    gm_tm98ap4112hee gm_l93;
    gm_td0ept12e7q8b gm_l94;
    gm_tiuq7k5hm0kak gm_l95;
    gm_t11t9d8s5diq9 gm_l96;
    gm_t617vr72bd7b3 gm_l97;
    gm_tppo2hmut88dq gm_l98;
    gm_tm98ap4112hee gm_l99;
    gm_tppo2hmut88dq gm_l100;
    gm_tm98ap4112hee gm_l101;
    gm_tppo2hmut88dq gm_l102;
    gm_tm98ap4112hee gm_l103;
    gm_td0ept12e7q8b gm_l104;
    gm_td0ept12e7q8b gm_l105;
    gm_td0ept12e7q8b gm_l106;
    gm_td0ept12e7q8b gm_l107;
    gm_td0ept12e7q8b gm_l108;
    gm_td0ept12e7q8b gm_l109;
    gm_td0ept12e7q8b gm_l110;
    gm_td0ept12e7q8b gm_l111;
    gm_td0ept12e7q8b gm_l112;
    {
        gm_l5 = ((int64_t)-1LL);
        {
            {
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t43 = (t42 > ((uint64_t)0ULL));
                if (!t43) {
                    goto gm_lb2;
                }
                gm_t9qkh0rsooc8d t44 = gm_l2;
                gm_t9qkh0rsooc8d t45;
                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)5ULL));
                gm_t9qkh0rsooc8d t46;
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)10ULL));
                gm_t9qkh0rsooc8d t47;
                GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)18ULL));
                gm_t9qkh0rsooc8d t48;
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)11ULL));
                gm_t9qkh0rsooc8d t49;
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)12ULL));
                gm_t9qkh0rsooc8d t50;
                GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)13ULL));
                gm_t9qkh0rsooc8d t51;
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)14ULL));
                gm_t9qkh0rsooc8d t52;
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)15ULL));
                gm_t9qkh0rsooc8d t53;
                GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)21ULL));
                gm_t9qkh0rsooc8d t54;
                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)22ULL));
                gm_t9qkh0rsooc8d t55;
                GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)23ULL));
                gm_t9qkh0rsooc8d t56;
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)24ULL));
                gm_t9qkh0rsooc8d t57;
                GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)25ULL));
                gm_t9qkh0rsooc8d t58;
                GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)26ULL));
                gm_t4qfn59m757do t59;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 0), t45);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 1), t46);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 2), t47);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 3), t48);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 4), t49);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 5), t50);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 6), t51);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 7), t52);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 8), t53);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 9), t54);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 10), t55);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 11), t56);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 12), t57);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 13), t58);
                uint8_t t60 = 0;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 0, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 1, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 2, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 3, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 4, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 5, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 6, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 7, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 8, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 9, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 10, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 11, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 12, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 13, ((uint8_t *)&t44))) t60 = 1;
                uint8_t t61 = (uint8_t)(!t60);
                if (!t61) {
                    goto gm_lb2;
                }
                void * t62 = gm_l0;
                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                void * t65 = GM_LD(void *, (((uint8_t *)t63) + (uintptr_t)((uint64_t)0ULL) * 8));
                gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t65) + 56));
                void * t67 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t68)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t68, gm_srcn3jjc71a2bv3, 203, 36);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t67) + (uintptr_t)((uint64_t)0ULL) * 16));
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                int64_t t71 = gm_f_sys_int_32e79ed(t62, GM_CONV(gm_csguankko4lmhd, t66), t70);
                gm_l5 = t71;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                {
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)5ULL));
                    if (!t73) {
                        goto gm_lb4;
                    }
                    void * t74 = gm_l0;
                    gm_tl874t2atnlh3 t75 = gm_l1;
                    void * t76 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t77, gm_srcn3jjc71a2bv3, 207, 41);
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)t76) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                    gm_f_sys_src_5svsr6i((void *)&gm_l58, (void *)NULL, cx, NULL, t74, t75, t79);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                    if (t80) {
                    } else {
                        gm_t76123gfet73g t81 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l58) + 8));
                        gm_tarq9lp8tdk6f t82;
                        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                        ((uint8_t *)&t82)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t82 + 8), t81);
                        memmove(gm_out, ((uint8_t *)&t82), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                    gm_l6 = t83;
                    void * t84 = gm_l0;
                    void * t85 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t86)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t86, gm_srcn3jjc71a2bv3, 208, 33);
                    gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)t85) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                    unsigned __int128 t89 = gm_f_get_uint_bq8c4b7(t84, t88);
                    uint32_t t90 = (uint32_t)t89;
                    gm_l7 = t90;
                    uint32_t t91 = gm_l7;
                    uint8_t t92 = gm_f_sys_has_0sego5l(t91, ((uint32_t)1ULL));
                    uint32_t t93 = gm_l7;
                    uint8_t t94 = gm_f_sys_has_0sego5l(t93, ((uint32_t)2ULL));
                    uint32_t t95 = gm_l7;
                    uint8_t t96 = gm_f_sys_has_0sego5l(t95, ((uint32_t)4ULL));
                    uint32_t t97 = gm_l7;
                    uint8_t t98 = gm_f_sys_has_0sego5l(t97, ((uint32_t)8ULL));
                    uint32_t t99 = gm_l7;
                    uint8_t t100 = gm_f_sys_has_0sego5l(t99, ((uint32_t)16ULL));
                    uint32_t t101 = gm_l7;
                    uint8_t t102 = gm_f_sys_has_0sego5l(t101, ((uint32_t)32ULL));
                    gm_t4q2b5ibsrdik t103;
                    GM_ST(uint8_t, ((uint8_t *)&t103 + 0), t92);
                    GM_ST(uint8_t, ((uint8_t *)&t103 + 1), t94);
                    GM_ST(uint8_t, ((uint8_t *)&t103 + 2), t96);
                    GM_ST(uint8_t, ((uint8_t *)&t103 + 3), t98);
                    GM_ST(uint8_t, ((uint8_t *)&t103 + 4), t100);
                    GM_ST(uint8_t, ((uint8_t *)&t103 + 5), t102);
                    gm_l8 = t103;
                    gm_b16_8 t104 = gm_l6;
                    gm_t4q2b5ibsrdik t105 = gm_l8;
                    gm_node *pn106 = gm_find(cx, 2126215853);
                    if (!pn106) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 217, 18);
                    gm_t62ebbi2i5rkd t107;
                    t107 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn106->clauses[0])(pn106->env, pn106->hctx, pn106, t104, t105);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l59 = t107;
                    uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                    if (t108) {
                        gm_tm98ap4112hee t109 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l59) + 8));
                        gm_l60 = t109;
                    } else {
                        gm_toe5fmr3vvf4d t110 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l59) + 8));
                        gm_l9 = t110;
                        gm_toe5fmr3vvf4d t111 = gm_l9;
                        int64_t t112 = gm_f_sys_code_cjtlfo1(t111);
                        gm_tarq9lp8tdk6f t113;
                        GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t113 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t113 + 24, 0);
                        ((uint8_t *)&t113)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t113 + 8), t112);
                        memmove(gm_out, ((uint8_t *)&t113), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t114 = gm_l60;
                    gm_l10 = t114;
                    void * t115 = gm_l0;
                    void * t116 = (void *)(((uint8_t *)t115) + 1368);
                    int64_t t117 = gm_f_sys_next_handle_r35ek81(t116);
                    gm_l11 = t117;
                    void * t118 = gm_l0;
                    void * t119 = (void *)(((uint8_t *)t118) + 1368);
                    int64_t t120 = gm_l11;
                    gm_tm98ap4112hee t121 = gm_l10;
                    gm_tftb1odc739t5 t122 = gm_f_put_endcgjj(t119, t120, GM_CONV(gm_csfq64m1m1rt6m, t121));
                    gm_l61 = t122;
                    uint8_t t123 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                    if (!t123) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcn3jjc71a2bv3, 219, 13);
                    int64_t t124 = gm_l11;
                    gm_tarq9lp8tdk6f t125;
                    GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t125 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
                    ((uint8_t *)&t125)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t125 + 8), t124);
                    memmove(gm_out, ((uint8_t *)&t125), 32);
                    goto gm_fdone;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)6ULL));
                    if (!t127) {
                        goto gm_lb5;
                    }
                    void * t128 = gm_l0;
                    void * t129 = (void *)(((uint8_t *)t128) + 1368);
                    int64_t t130 = gm_l5;
                    gm_tppo2hmut88dq t131 = gm_f_remove_82qm8hc(t129, t130);
                    gm_l62 = t131;
                    uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l62))[8] != 0);
                    if (t132) {
                        gm_tm98ap4112hee t133 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l62));
                        gm_l63 = t133;
                    } else {
                        int64_t t134 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t135;
                        GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t135 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t135 + 24, 0);
                        ((uint8_t *)&t135)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t135 + 8), t134);
                        memmove(gm_out, ((uint8_t *)&t135), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t136 = gm_l63;
                    gm_l12 = t136;
                    gm_tm98ap4112hee t137 = gm_l12;
                    gm_node *pn138 = gm_find(cx, 2126215853);
                    if (!pn138) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 224, 13);
                    gm_b0_1 t139;
                    t139 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn138->clauses[1])(pn138->env, pn138->hctx, pn138, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tarq9lp8tdk6f t140;
                    GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t140 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t140 + 24, 0);
                    ((uint8_t *)&t140)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t140 + 8), ((int64_t)0LL));
                    memmove(gm_out, ((uint8_t *)&t140), 32);
                    goto gm_fdone;
                }
gm_lb5:;
                {
                    uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t142 = (uint8_t)(t141 == ((uint8_t)7ULL));
                    if (!t142) {
                        goto gm_lb6;
                    }
                    void * t143 = gm_l0;
                    gm_tl874t2atnlh3 t144 = gm_l1;
                    void * t145 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t146)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t146, gm_srcn3jjc71a2bv3, 228, 41);
                    gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)t145) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l64, (void *)NULL, cx, NULL, t143, t144, t148);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                    if (t149) {
                    } else {
                        gm_t76123gfet73g t150 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l64) + 8));
                        gm_tarq9lp8tdk6f t151;
                        GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                        ((uint8_t *)&t151)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t151 + 8), t150);
                        memmove(gm_out, ((uint8_t *)&t151), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                    gm_l13 = t152;
                    void * t153 = gm_l0;
                    void * t154 = (void *)(((uint8_t *)t153) + 1368);
                    int64_t t155 = gm_l5;
                    gm_tppo2hmut88dq t156 = gm_f_get_f2mbmm8(t154, t155);
                    gm_l65 = t156;
                    uint8_t t157 = (uint8_t)((((uint8_t *)&gm_l65))[8] != 0);
                    if (t157) {
                        gm_tm98ap4112hee t158 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l65));
                        gm_l66 = t158;
                    } else {
                        int64_t t159 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t160;
                        GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t160 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t160 + 24, 0);
                        ((uint8_t *)&t160)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t160 + 8), t159);
                        memmove(gm_out, ((uint8_t *)&t160), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t161 = gm_l66;
                    gm_l14 = t161;
                    void * t162 = gm_l0;
                    uint64_t t163 = GM_LD(uint64_t, ((((uint8_t *)t162) + 1672) + 8));
                    uint8_t t164 = (t163 > ((uint64_t)1ULL));
                    if (t164) {
                        void * t165 = gm_l0;
                        gm_tl874t2atnlh3 t166 = gm_l1;
                        gm_tm98ap4112hee t167 = gm_l14;
                        gm_b16_8 t168 = gm_l13;
                        gm_t3olsle24rveu t169;
                        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t169 + 0), t167);
                        GM_ST(gm_b16_8, ((uint8_t *)&t169 + 8), t168);
                        gm_tmvdbo28o1i6h t170;
                        GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t170 + 8), t169);
                        gm_f_on_host_thread_kmegc07((void *)&gm_l67, (void *)NULL, cx, NULL, t165, t166, &t170);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                        if (t171) {
                        } else {
                            gm_t76123gfet73g t172 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l67) + 8));
                            gm_tarq9lp8tdk6f t173;
                            GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
                            ((uint8_t *)&t173)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t173 + 8), t172);
                            memmove(gm_out, ((uint8_t *)&t173), 32);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l68), (((uint8_t *)&gm_l67) + 8), 32);
                        gm_thf5kr0is9u9i t174 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l68));
                        int64_t t175 = gm_f_sys_count_50c5lcp(t174);
                        gm_tarq9lp8tdk6f t176;
                        GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t176 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t176 + 24, 0);
                        ((uint8_t *)&t176)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t176 + 8), t175);
                        memmove(gm_out, ((uint8_t *)&t176), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t177 = gm_l14;
                    gm_b16_8 t178 = gm_l13;
                    gm_node *pn179 = gm_find(cx, 2126215853);
                    if (!pn179) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 231, 30);
                    gm_thf5kr0is9u9i t180;
                    t180 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn179->clauses[2])(pn179->env, pn179->hctx, pn179, t177, t178);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t181 = gm_f_sys_count_50c5lcp(t180);
                    gm_tarq9lp8tdk6f t182;
                    GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t182 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t182 + 24, 0);
                    ((uint8_t *)&t182)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t182 + 8), t181);
                    memmove(gm_out, ((uint8_t *)&t182), 32);
                    goto gm_fdone;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)8ULL));
                    if (!t184) {
                        goto gm_lb7;
                    }
                    void * t185 = gm_l0;
                    gm_tl874t2atnlh3 t186 = gm_l1;
                    void * t187 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t188)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t188, gm_srcn3jjc71a2bv3, 234, 41);
                    gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)t187) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
                    gm_f_sys_src_5svsr6i((void *)&gm_l69, (void *)NULL, cx, NULL, t185, t186, t190);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                    if (t191) {
                    } else {
                        gm_t76123gfet73g t192 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l69) + 8));
                        gm_tarq9lp8tdk6f t193;
                        GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
                        ((uint8_t *)&t193)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t193 + 8), t192);
                        memmove(gm_out, ((uint8_t *)&t193), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                    gm_l15 = t194;
                    void * t195 = gm_l0;
                    void * t196 = (void *)(((uint8_t *)t195) + 1368);
                    int64_t t197 = gm_l5;
                    gm_tppo2hmut88dq t198 = gm_f_get_f2mbmm8(t196, t197);
                    gm_l70 = t198;
                    uint8_t t199 = (uint8_t)((((uint8_t *)&gm_l70))[8] != 0);
                    if (t199) {
                        gm_tm98ap4112hee t200 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l70));
                        gm_l71 = t200;
                    } else {
                        int64_t t201 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t202;
                        GM_ST(uint8_t, (uint8_t *)&t202 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t202 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t202 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t202 + 24, 0);
                        ((uint8_t *)&t202)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t202 + 8), t201);
                        memmove(gm_out, ((uint8_t *)&t202), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t203 = gm_l71;
                    gm_l16 = t203;
                    gm_tm98ap4112hee t204 = gm_l16;
                    gm_b16_8 t205 = gm_l15;
                    gm_node *pn206 = gm_find(cx, 2126215853);
                    if (!pn206) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 236, 30);
                    gm_thf5kr0is9u9i t207;
                    t207 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn206->clauses[3])(pn206->env, pn206->hctx, pn206, t204, t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t208 = gm_f_sys_count_50c5lcp(t207);
                    gm_tarq9lp8tdk6f t209;
                    GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t209 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t209 + 24, 0);
                    ((uint8_t *)&t209)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t209 + 8), t208);
                    memmove(gm_out, ((uint8_t *)&t209), 32);
                    goto gm_fdone;
                }
gm_lb7:;
                {
                    uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t211 = (uint8_t)(t210 == ((uint8_t)9ULL));
                    if (!t211) {
                        goto gm_lb8;
                    }
                    void * t212 = gm_l0;
                    void * t213 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t214)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t214, gm_srcn3jjc71a2bv3, 239, 31);
                    void * t215 = GM_LD(void *, (((uint8_t *)t213) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t215) + 56));
                    void * t217 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t218)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t218, gm_srcn3jjc71a2bv3, 239, 43);
                    gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)t217) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                    int64_t t221 = gm_f_sys_int_32e79ed(t212, GM_CONV(gm_csguankko4lmhd, t216), t220);
                    gm_l17 = t221;
                    void * t222 = gm_l0;
                    void * t223 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t224)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t224, gm_srcn3jjc71a2bv3, 240, 34);
                    void * t225 = GM_LD(void *, (((uint8_t *)t223) + (uintptr_t)((uint64_t)2ULL) * 8));
                    gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t225) + 56));
                    void * t227 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t228)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t228, gm_srcn3jjc71a2bv3, 240, 46);
                    gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)t227) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                    int64_t t231 = gm_f_sys_int_32e79ed(t222, GM_CONV(gm_csguankko4lmhd, t226), t230);
                    gm_l18 = t231;
                    void * t232 = gm_l0;
                    void * t233 = (void *)(((uint8_t *)t232) + 1368);
                    int64_t t234 = gm_l5;
                    gm_tppo2hmut88dq t235 = gm_f_get_f2mbmm8(t233, t234);
                    gm_l72 = t235;
                    uint8_t t236 = (uint8_t)((((uint8_t *)&gm_l72))[8] != 0);
                    if (t236) {
                        gm_tm98ap4112hee t237 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l72));
                        gm_l73 = t237;
                    } else {
                        int64_t t238 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t239;
                        GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t239 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t239 + 24, 0);
                        ((uint8_t *)&t239)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t239 + 8), t238);
                        memmove(gm_out, ((uint8_t *)&t239), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t240 = gm_l73;
                    gm_l19 = t240;
                    {
                        {
                            int64_t t241 = gm_l18;
                            uint8_t t242 = ((t241 == ((int64_t)0LL)));
                            if (!t242) {
                                goto gm_lb10;
                            }
                            int64_t t243 = gm_l17;
                            uint8_t t244 = (t243 < ((int64_t)0LL));
                            if (!t244) {
                                goto gm_lb10;
                            }
                            int64_t t245 = GM_LD(int64_t, ((uint8_t *)gm_cndfnv7mbeh));
                            gm_tarq9lp8tdk6f t246;
                            GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t246 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t246 + 24, 0);
                            ((uint8_t *)&t246)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t246 + 8), t245);
                            memmove(gm_out, ((uint8_t *)&t246), 32);
                            goto gm_fdone;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    {
                        {
                            int64_t t247 = gm_l18;
                            uint8_t t248 = ((t247 == ((int64_t)0LL)));
                            if (!t248) {
                                goto gm_lb12;
                            }
                            gm_t9qkh0rsooc8d t249;
                            GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)0ULL));
                            gm_l74 = t249;
                            goto gm_lb11;
                        }
gm_lb12:;
                        {
                            int64_t t250 = gm_l18;
                            uint8_t t251 = ((t250 == ((int64_t)1LL)));
                            if (!t251) {
                                goto gm_lb13;
                            }
                            gm_t9qkh0rsooc8d t252;
                            GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)1ULL));
                            gm_l74 = t252;
                            goto gm_lb11;
                        }
gm_lb13:;
                        {
                            int64_t t253 = gm_l18;
                            uint8_t t254 = ((t253 == ((int64_t)2LL)));
                            if (!t254) {
                                goto gm_lb14;
                            }
                            gm_t9qkh0rsooc8d t255;
                            GM_ST(uint8_t, ((uint8_t *)&t255 + 0), ((uint8_t)2ULL));
                            gm_l74 = t255;
                            goto gm_lb11;
                        }
gm_lb14:;
                        {
                            int64_t t256 = GM_LD(int64_t, ((uint8_t *)gm_cndfnv7mbeh));
                            gm_tarq9lp8tdk6f t257;
                            GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t257 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t257 + 24, 0);
                            ((uint8_t *)&t257)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t257 + 8), t256);
                            memmove(gm_out, ((uint8_t *)&t257), 32);
                            goto gm_fdone;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 243, 33);
                    }
gm_lb11:;
                    gm_t9qkh0rsooc8d t258 = gm_l74;
                    gm_l20 = t258;
                    gm_tm98ap4112hee t259 = gm_l19;
                    int64_t t260 = gm_l17;
                    gm_t9qkh0rsooc8d t261 = gm_l20;
                    gm_node *pn262 = gm_find(cx, 2126215853);
                    if (!pn262) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 249, 20);
                    gm_thf5kr0is9u9i t263;
                    t263 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, int64_t, gm_t9qkh0rsooc8d))pn262->clauses[4])(pn262->env, pn262->hctx, pn262, t259, t260, t261);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l75 = t263;
                    uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                    if (t264) {
                        uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&gm_l75) + 8));
                        gm_l76 = t265;
                    } else {
                        gm_toe5fmr3vvf4d t266 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l75) + 8));
                        gm_l21 = t266;
                        gm_toe5fmr3vvf4d t267 = gm_l21;
                        int64_t t268 = gm_f_sys_code_cjtlfo1(t267);
                        gm_tarq9lp8tdk6f t269;
                        GM_ST(uint8_t, (uint8_t *)&t269 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t269 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t269 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t269 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t269 + 24, 0);
                        ((uint8_t *)&t269)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t269 + 8), t268);
                        memmove(gm_out, ((uint8_t *)&t269), 32);
                        goto gm_fdone;
                    }
                    uint64_t t270 = gm_l76;
                    gm_l22 = t270;
                    uint64_t t271 = gm_l22;
                    int64_t t272;
                    memset(&t272, 0, sizeof t272); memcpy(&t272, ((uint8_t *)&t271), sizeof t272 < 8 ? sizeof t272 : 8);
                    gm_tarq9lp8tdk6f t273;
                    GM_ST(uint8_t, (uint8_t *)&t273 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t273 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t273 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t273 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t273 + 24, 0);
                    ((uint8_t *)&t273)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t273 + 8), t272);
                    memmove(gm_out, ((uint8_t *)&t273), 32);
                    goto gm_fdone;
                }
gm_lb8:;
            }
            {
                {
                    {
                        {
                            uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t275 = (uint8_t)(t274 == ((uint8_t)10ULL));
                            if (!t275) {
                                goto gm_lb18;
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                        {
                            uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t277 = (uint8_t)(t276 == ((uint8_t)18ULL));
                            if (!t277) {
                                goto gm_lb19;
                            }
                            goto gm_lb17;
                        }
gm_lb19:;
                        goto gm_lb16;
                    }
gm_lb17:;
                    void * t278 = gm_l0;
                    gm_tl874t2atnlh3 t279 = gm_l1;
                    void * t280 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t281)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t281, gm_srcn3jjc71a2bv3, 253, 41);
                    gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)t280) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                    gm_f_sys_src_5svsr6i((void *)&gm_l77, (void *)NULL, cx, NULL, t278, t279, t283);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t284 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                    if (t284) {
                    } else {
                        gm_t76123gfet73g t285 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l77) + 8));
                        gm_tarq9lp8tdk6f t286;
                        GM_ST(uint8_t, (uint8_t *)&t286 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t286 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t286 + 4, 0);
                        ((uint8_t *)&t286)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t286 + 8), t285);
                        memmove(gm_out, ((uint8_t *)&t286), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
                    gm_l23 = t287;
                    gm_t9qkh0rsooc8d t288 = gm_l2;
                    uint8_t t289 = GM_LD(uint8_t, (((uint8_t *)&t288) + 0));
                    uint8_t t290 = (uint8_t)((t289 == ((uint8_t)10ULL)));
                    gm_t9qkh0rsooc8d t291;
                    GM_ST(uint8_t, ((uint8_t *)&t291 + 0), ((uint8_t)10ULL));
                    if (t290) {
                        gm_b16_8 t292 = gm_l23;
                        gm_node *pn293 = gm_find(cx, 2126215853);
                        if (!pn293) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 254, 41);
                        gm_tih6h4avv5nrb t294;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn293->clauses[5])((void *)&t294, pn293->env, pn293->hctx, pn293, t292);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l78 = t294;
                    } else {
                        gm_b16_8 t295 = gm_l23;
                        gm_node *pn296 = gm_find(cx, 2126215853);
                        if (!pn296) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 254, 64);
                        gm_tih6h4avv5nrb t297;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn296->clauses[13])((void *)&t297, pn296->env, pn296->hctx, pn296, t295);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l78 = t297;
                    }
                    memmove(((uint8_t *)&gm_l79), ((uint8_t *)&gm_l78), 40);
                    uint8_t t298 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                    if (t298) {
                        memmove(((uint8_t *)&gm_l80), (((uint8_t *)&gm_l79) + 8), 32);
                    } else {
                        gm_toe5fmr3vvf4d t299 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l79) + 8));
                        gm_l24 = t299;
                        gm_toe5fmr3vvf4d t300 = gm_l24;
                        int64_t t301 = gm_f_sys_code_cjtlfo1(t300);
                        gm_tarq9lp8tdk6f t302;
                        GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t302 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t302 + 24, 0);
                        ((uint8_t *)&t302)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t302 + 8), t301);
                        memmove(gm_out, ((uint8_t *)&t302), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l25), ((uint8_t *)&gm_l80), 32);
                    void * t303 = gm_l0;
                    void * t304 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t305 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t305)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t305, gm_srcn3jjc71a2bv3, 255, 33);
                    gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)t304) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t307 = GM_LD(gm_b16_8, ((uint8_t *)&t306));
                    gm_t2lqu8jsj511d t308 = gm_f_slice_parts_1se5hu4(t303, t307);
                    gm_l26 = t308;
                    uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint64_t t310 = (t309 < ((uint64_t)4ULL) ? t309 : ((uint64_t)4ULL));
                    gm_l27 = t310;
                    void * t311 = gm_l0;
                    void * t312 = (void *)(((uint8_t *)t311) + 184);
                    uint64_t t313 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                    uint64_t t314 = gm_l27;
                    uint64_t r315; bool o316 = __builtin_mul_overflow(t314, ((uint64_t)8ULL), &r315);
                    if (o316) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 257, 46);
                    uint64_t t317 = r315;
                    gm_f_write_view_pebit6h((void *)&gm_l81, t312, t313, t317, ((uint64_t)8ULL));
                    uint8_t t318 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                    if (t318) {
                        gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                        gm_l82 = t319;
                    } else {
                        memmove(((uint8_t *)&F.l28), (((uint8_t *)&gm_l81) + 8), 40);
                        void * t320 = gm_l0;
                        gm_tl874t2atnlh3 t321 = gm_l1;
                        gm_t76123gfet73g t322;
                        gm_f_mem_trap_ecogko0((void *)&t322, (void *)NULL, cx, NULL, t320, t321, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l28)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tarq9lp8tdk6f t323;
                        GM_ST(uint8_t, (uint8_t *)&t323 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t323 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t323 + 4, 0);
                        ((uint8_t *)&t323)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t323 + 8), t322);
                        memmove(gm_out, ((uint8_t *)&t323), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t324 = gm_l82;
                    gm_l29 = t324;
                    uint64_t t325 = GM_LD(uint64_t, ((uint8_t *)&gm_l25));
                    int64_t t326;
                    memset(&t326, 0, sizeof t326); memcpy(&t326, ((uint8_t *)&t325), sizeof t326 < 8 ? sizeof t326 : 8);
                    gm_t9qkh0rsooc8d t327 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l25) + 8));
                    int64_t t328 = gm_f_sys_kind_sjfdqbt(t327);
                    int64_t t329 = GM_LD(int64_t, (((uint8_t *)&gm_l25) + 16));
                    uint32_t t330 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 24));
                    int64_t t331 = (int64_t)t330;
                    gm_t767tucha9lsh t332;
                    GM_ST(int64_t, ((uint8_t *)&t332 + 0), t326);
                    GM_ST(int64_t, ((uint8_t *)&t332 + 8), t328);
                    GM_ST(int64_t, ((uint8_t *)&t332 + 16), t329);
                    GM_ST(int64_t, ((uint8_t *)&t332 + 24), t331);
                    gm_l30 = t332;
                    gm_l32 = ((uint64_t)0ULL);
                    uint64_t t333 = gm_l27;
                    uint64_t t334 = gm_f_us_cuv01fp(t333);
                    gm_l31 = t334;
                    {
                        for (;;) {
                            uint64_t t335 = gm_l32;
                            uint64_t t336 = gm_l31;
                            uint8_t t337 = (t335 < t336);
                            if (!t337) {
                                goto gm_lb20;
                            }
                            {
                                {
                                    {
                                        uint64_t t338 = gm_l32;
                                        gm_l33 = t338;
                                        void * t339 = gm_l0;
                                        void * t340 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                        uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                        uint64_t t342 = gm_l33;
                                        uint64_t r343; bool o344 = __builtin_mul_overflow(t342, ((uint64_t)8ULL), &r343);
                                        if (o344) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 259, 53);
                                        uint64_t t345 = r343;
                                        uint64_t t346 = gm_l33;
                                        uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                                        if (o348) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 259, 61);
                                        uint64_t t349 = r347;
                                        uint64_t r350; bool o351 = __builtin_mul_overflow(t349, ((uint64_t)8ULL), &r350);
                                        if (o351) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 259, 60);
                                        uint64_t t352 = r350;
                                        uint64_t t353 = (uintptr_t)t345;
                                        uint64_t t354 = (uintptr_t)t352;
                                        if (t353 > t354 || t354 > (uintptr_t)t341) gm_trap_slice(t353, t354, t341, gm_srcn3jjc71a2bv3, 259, 48);
                                        uint64_t t355 = ((uintptr_t)t352) - (uintptr_t)t345;
                                        gm_b16_8 t356;
                                        GM_ST(void *, (uint8_t *)&t356, (void *)(((uint8_t *)t340 + (uintptr_t)t345 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t356 + 8, (uintptr_t)(t355));
                                        uint64_t t357 = gm_l33;
                                        if ((uintptr_t)t357 >= (uintptr_t)(((uint64_t)4ULL))) gm_trap_index((uintptr_t)t357, ((uint64_t)4ULL), gm_srcn3jjc71a2bv3, 259, 79);
                                        int64_t t358 = GM_LD(int64_t, (((uint8_t *)&gm_l30) + (uintptr_t)t357 * 8));
                                        uint64_t t359;
                                        memset(&t359, 0, sizeof t359); memcpy(&t359, ((uint8_t *)&t358), sizeof t359 < 8 ? sizeof t359 : 8);
                                        unsigned __int128 t360 = (unsigned __int128)t359;
                                        gm_f_put_uint_cddin99(t339, t356, t360);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 259, 13);
                                }
gm_lb23:;
                            }
gm_lb22:;
                            uint64_t t361 = gm_l32;
                            uint64_t r362; bool o363 = __builtin_add_overflow(t361, ((uint64_t)1ULL), &r362);
                            uint64_t t364 = r362;
                            gm_l32 = t364;
                        }
                    }
gm_lb20:;
                    gm_tarq9lp8tdk6f t365;
                    GM_ST(uint8_t, (uint8_t *)&t365 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t365 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t365 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t365 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t365 + 24, 0);
                    ((uint8_t *)&t365)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t365 + 8), ((int64_t)0LL));
                    memmove(gm_out, ((uint8_t *)&t365), 32);
                    goto gm_fdone;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t367 = (uint8_t)(t366 == ((uint8_t)11ULL));
                    if (!t367) {
                        goto gm_lb25;
                    }
                    void * t368 = gm_l0;
                    gm_tl874t2atnlh3 t369 = gm_l1;
                    void * t370 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t371 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t371)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t371, gm_srcn3jjc71a2bv3, 262, 68);
                    gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)t370) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                    gm_f_sys_src_5svsr6i((void *)&gm_l83, (void *)NULL, cx, NULL, t368, t369, t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t374 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                    if (t374) {
                    } else {
                        gm_t76123gfet73g t375 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l83) + 8));
                        gm_tarq9lp8tdk6f t376;
                        GM_ST(uint8_t, (uint8_t *)&t376 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t376 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t376 + 4, 0);
                        ((uint8_t *)&t376)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t376 + 8), t375);
                        memmove(gm_out, ((uint8_t *)&t376), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t377 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
                    gm_node *pn378 = gm_find(cx, 2126215853);
                    if (!pn378) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 262, 38);
                    gm_ttln0d0hd70en t379;
                    t379 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn378->clauses[6])(pn378->env, pn378->hctx, pn378, t377);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t380 = gm_f_sys_unit_nrsqciq(t379);
                    gm_tarq9lp8tdk6f t381;
                    GM_ST(uint8_t, (uint8_t *)&t381 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t381 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t381 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t381 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t381 + 24, 0);
                    ((uint8_t *)&t381)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t381 + 8), t380);
                    memmove(gm_out, ((uint8_t *)&t381), 32);
                    goto gm_fdone;
                }
gm_lb25:;
                {
                    uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t383 = (uint8_t)(t382 == ((uint8_t)12ULL));
                    if (!t383) {
                        goto gm_lb26;
                    }
                    void * t384 = gm_l0;
                    gm_tl874t2atnlh3 t385 = gm_l1;
                    void * t386 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t387 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t387)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t387, gm_srcn3jjc71a2bv3, 264, 41);
                    gm_b16_8 t388 = GM_LD(gm_b16_8, (((uint8_t *)t386) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t389 = GM_LD(gm_b16_8, ((uint8_t *)&t388));
                    gm_f_sys_src_5svsr6i((void *)&gm_l84, (void *)NULL, cx, NULL, t384, t385, t389);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t390 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                    if (t390) {
                    } else {
                        gm_t76123gfet73g t391 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l84) + 8));
                        gm_tarq9lp8tdk6f t392;
                        GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
                        ((uint8_t *)&t392)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t392 + 8), t391);
                        memmove(gm_out, ((uint8_t *)&t392), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l84) + 8));
                    gm_l34 = t393;
                    void * t394 = gm_l0;
                    gm_tl874t2atnlh3 t395 = gm_l1;
                    void * t396 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t397)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t397, gm_srcn3jjc71a2bv3, 265, 39);
                    gm_b16_8 t398 = GM_LD(gm_b16_8, (((uint8_t *)t396) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t399 = GM_LD(gm_b16_8, ((uint8_t *)&t398));
                    gm_f_sys_src_5svsr6i((void *)&gm_l85, (void *)NULL, cx, NULL, t394, t395, t399);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t400 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                    if (t400) {
                    } else {
                        gm_t76123gfet73g t401 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l85) + 8));
                        gm_tarq9lp8tdk6f t402;
                        GM_ST(uint8_t, (uint8_t *)&t402 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t402 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t402 + 4, 0);
                        ((uint8_t *)&t402)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t402 + 8), t401);
                        memmove(gm_out, ((uint8_t *)&t402), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l85) + 8));
                    gm_l35 = t403;
                    gm_b16_8 t404 = gm_l34;
                    gm_b16_8 t405 = gm_l35;
                    gm_node *pn406 = gm_find(cx, 2126215853);
                    if (!pn406) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 266, 29);
                    gm_ttln0d0hd70en t407;
                    t407 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn406->clauses[7])(pn406->env, pn406->hctx, pn406, t404, t405);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t408 = gm_f_sys_unit_nrsqciq(t407);
                    gm_tarq9lp8tdk6f t409;
                    GM_ST(uint8_t, (uint8_t *)&t409 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t409 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t409 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t409 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t409 + 24, 0);
                    ((uint8_t *)&t409)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t409 + 8), t408);
                    memmove(gm_out, ((uint8_t *)&t409), 32);
                    goto gm_fdone;
                }
gm_lb26:;
                {
                    uint8_t t410 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t411 = (uint8_t)(t410 == ((uint8_t)13ULL));
                    if (!t411) {
                        goto gm_lb27;
                    }
                    void * t412 = gm_l0;
                    gm_tl874t2atnlh3 t413 = gm_l1;
                    void * t414 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t415 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t415)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t415, gm_srcn3jjc71a2bv3, 268, 71);
                    gm_b16_8 t416 = GM_LD(gm_b16_8, (((uint8_t *)t414) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t417 = GM_LD(gm_b16_8, ((uint8_t *)&t416));
                    gm_f_sys_src_5svsr6i((void *)&gm_l86, (void *)NULL, cx, NULL, t412, t413, t417);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t418 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                    if (t418) {
                    } else {
                        gm_t76123gfet73g t419 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l86) + 8));
                        gm_tarq9lp8tdk6f t420;
                        GM_ST(uint8_t, (uint8_t *)&t420 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t420 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t420 + 4, 0);
                        ((uint8_t *)&t420)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t420 + 8), t419);
                        memmove(gm_out, ((uint8_t *)&t420), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t421 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 8));
                    gm_node *pn422 = gm_find(cx, 2126215853);
                    if (!pn422) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 268, 39);
                    gm_ttln0d0hd70en t423;
                    t423 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn422->clauses[8])(pn422->env, pn422->hctx, pn422, t421);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t424 = gm_f_sys_unit_nrsqciq(t423);
                    gm_tarq9lp8tdk6f t425;
                    GM_ST(uint8_t, (uint8_t *)&t425 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t425 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t425 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t425 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t425 + 24, 0);
                    ((uint8_t *)&t425)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t425 + 8), t424);
                    memmove(gm_out, ((uint8_t *)&t425), 32);
                    goto gm_fdone;
                }
gm_lb27:;
            }
            {
                {
                    uint8_t t426 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t427 = (uint8_t)(t426 == ((uint8_t)14ULL));
                    if (!t427) {
                        goto gm_lb28;
                    }
                    void * t428 = gm_l0;
                    gm_tl874t2atnlh3 t429 = gm_l1;
                    void * t430 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t431 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t431)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t431, gm_srcn3jjc71a2bv3, 269, 75);
                    gm_b16_8 t432 = GM_LD(gm_b16_8, (((uint8_t *)t430) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&t432));
                    gm_f_sys_src_5svsr6i((void *)&gm_l87, (void *)NULL, cx, NULL, t428, t429, t433);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t434 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                    if (t434) {
                    } else {
                        gm_t76123gfet73g t435 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l87) + 8));
                        gm_tarq9lp8tdk6f t436;
                        GM_ST(uint8_t, (uint8_t *)&t436 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t436 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t436 + 4, 0);
                        ((uint8_t *)&t436)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t436 + 8), t435);
                        memmove(gm_out, ((uint8_t *)&t436), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t437 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l87) + 8));
                    gm_node *pn438 = gm_find(cx, 2126215853);
                    if (!pn438) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 269, 41);
                    gm_ttln0d0hd70en t439;
                    t439 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn438->clauses[9])(pn438->env, pn438->hctx, pn438, t437);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t440 = gm_f_sys_unit_nrsqciq(t439);
                    gm_tarq9lp8tdk6f t441;
                    GM_ST(uint8_t, (uint8_t *)&t441 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t441 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t441 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t441 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t441 + 24, 0);
                    ((uint8_t *)&t441)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t441 + 8), t440);
                    memmove(gm_out, ((uint8_t *)&t441), 32);
                    goto gm_fdone;
                }
gm_lb28:;
                {
                    uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t443 = (uint8_t)(t442 == ((uint8_t)15ULL));
                    if (!t443) {
                        goto gm_lb29;
                    }
                    void * t444 = gm_l0;
                    gm_tl874t2atnlh3 t445 = gm_l1;
                    void * t446 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t447 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t447)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t447, gm_srcn3jjc71a2bv3, 271, 41);
                    gm_b16_8 t448 = GM_LD(gm_b16_8, (((uint8_t *)t446) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t449 = GM_LD(gm_b16_8, ((uint8_t *)&t448));
                    gm_f_sys_src_5svsr6i((void *)&gm_l88, (void *)NULL, cx, NULL, t444, t445, t449);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t450 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                    if (t450) {
                    } else {
                        gm_t76123gfet73g t451 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l88) + 8));
                        gm_tarq9lp8tdk6f t452;
                        GM_ST(uint8_t, (uint8_t *)&t452 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t452 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t452 + 4, 0);
                        ((uint8_t *)&t452)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t452 + 8), t451);
                        memmove(gm_out, ((uint8_t *)&t452), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t453 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l88) + 8));
                    gm_l36 = t453;
                    gm_b16_8 t454 = gm_l36;
                    gm_node *pn455 = gm_find(cx, 2126215853);
                    if (!pn455) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 272, 18);
                    gm_t62ebbi2i5rkd t456;
                    t456 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn455->clauses[10])(pn455->env, pn455->hctx, pn455, t454);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l89 = t456;
                    uint8_t t457 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                    if (t457) {
                        gm_tm98ap4112hee t458 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l89) + 8));
                        gm_l90 = t458;
                    } else {
                        gm_toe5fmr3vvf4d t459 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l89) + 8));
                        gm_l37 = t459;
                        gm_toe5fmr3vvf4d t460 = gm_l37;
                        int64_t t461 = gm_f_sys_code_cjtlfo1(t460);
                        gm_tarq9lp8tdk6f t462;
                        GM_ST(uint8_t, (uint8_t *)&t462 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t462 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t462 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t462 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t462 + 24, 0);
                        ((uint8_t *)&t462)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t462 + 8), t461);
                        memmove(gm_out, ((uint8_t *)&t462), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t463 = gm_l90;
                    gm_l38 = t463;
                    void * t464 = gm_l0;
                    void * t465 = (void *)(((uint8_t *)t464) + 1368);
                    int64_t t466 = gm_f_sys_next_handle_r35ek81(t465);
                    gm_l39 = t466;
                    void * t467 = gm_l0;
                    void * t468 = (void *)((((uint8_t *)t467) + 1368) + 56);
                    int64_t t469 = gm_l39;
                    gm_tm98ap4112hee t470 = gm_l38;
                    gm_tftb1odc739t5 t471 = gm_f_put_0c6jnkb(t468, t469, GM_CONV(gm_csfq64m1m1rt6m, t470));
                    gm_l91 = t471;
                    uint8_t t472 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                    if (!t472) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 1, gm_srcn3jjc71a2bv3, 274, 13);
                    int64_t t473 = gm_l39;
                    gm_tarq9lp8tdk6f t474;
                    GM_ST(uint8_t, (uint8_t *)&t474 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t474 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t474 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t474 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t474 + 24, 0);
                    ((uint8_t *)&t474)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t474 + 8), t473);
                    memmove(gm_out, ((uint8_t *)&t474), 32);
                    goto gm_fdone;
                }
gm_lb29:;
            }
            {
                {
                    uint8_t t475 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t476 = (uint8_t)(t475 == ((uint8_t)16ULL));
                    if (!t476) {
                        goto gm_lb30;
                    }
                    void * t477 = gm_l0;
                    void * t478 = (void *)((((uint8_t *)t477) + 1368) + 56);
                    int64_t t479 = gm_l5;
                    gm_tppo2hmut88dq t480 = gm_f_get_v07up09(t478, t479);
                    gm_l92 = t480;
                    uint8_t t481 = (uint8_t)((((uint8_t *)&gm_l92))[8] != 0);
                    if (t481) {
                        gm_tm98ap4112hee t482 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l92));
                        gm_l93 = t482;
                    } else {
                        int64_t t483 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t484;
                        GM_ST(uint8_t, (uint8_t *)&t484 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t484 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t484 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t484 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t484 + 24, 0);
                        ((uint8_t *)&t484)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t484 + 8), t483);
                        memmove(gm_out, ((uint8_t *)&t484), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t485 = gm_l93;
                    gm_l40 = t485;
                    void * t486 = gm_l0;
                    gm_tl874t2atnlh3 t487 = gm_l1;
                    void * t488 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t489 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t489)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t489, gm_srcn3jjc71a2bv3, 279, 41);
                    gm_b16_8 t490 = GM_LD(gm_b16_8, (((uint8_t *)t488) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t491 = GM_LD(gm_b16_8, ((uint8_t *)&t490));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l94, (void *)NULL, cx, NULL, t486, t487, t491);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t492 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                    if (t492) {
                    } else {
                        gm_t76123gfet73g t493 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l94) + 8));
                        gm_tarq9lp8tdk6f t494;
                        GM_ST(uint8_t, (uint8_t *)&t494 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t494 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t494 + 4, 0);
                        ((uint8_t *)&t494)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t494 + 8), t493);
                        memmove(gm_out, ((uint8_t *)&t494), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t495 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                    gm_l41 = t495;
                    gm_tm98ap4112hee t496 = gm_l40;
                    gm_b16_8 t497 = gm_l41;
                    gm_node *pn498 = gm_find(cx, 2126215853);
                    if (!pn498) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 280, 21);
                    gm_tiuq7k5hm0kak t499;
                    ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn498->clauses[11])((void *)&t499, pn498->env, pn498->hctx, pn498, t496, t497);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l95 = t499;
                    uint8_t t500 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                    if (t500) {
                        memmove(((uint8_t *)&gm_l96), (((uint8_t *)&gm_l95) + 8), 24);
                    } else {
                        gm_toe5fmr3vvf4d t501 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l95) + 8));
                        gm_l42 = t501;
                        gm_toe5fmr3vvf4d t502 = gm_l42;
                        int64_t t503 = gm_f_sys_code_cjtlfo1(t502);
                        gm_tarq9lp8tdk6f t504;
                        GM_ST(uint8_t, (uint8_t *)&t504 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t504 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t504 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t504 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t504 + 24, 0);
                        ((uint8_t *)&t504)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t504 + 8), t503);
                        memmove(gm_out, ((uint8_t *)&t504), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l43), ((uint8_t *)&gm_l96), 24);
                    uint8_t t505 = (uint8_t)((((uint8_t *)&gm_l43))[16] != 0);
                    if (t505) {
                        gm_t617vr72bd7b3 t506 = GM_LD(gm_t617vr72bd7b3, ((uint8_t *)&gm_l43));
                        gm_l97 = t506;
                    } else {
                        gm_tarq9lp8tdk6f t507;
                        GM_ST(uint8_t, (uint8_t *)&t507 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t507 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t507 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t507 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t507 + 24, 0);
                        ((uint8_t *)&t507)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t507 + 8), ((int64_t)0LL));
                        memmove(gm_out, ((uint8_t *)&t507), 32);
                        goto gm_fdone;
                    }
                    gm_t617vr72bd7b3 t508 = gm_l97;
                    gm_l44 = t508;
                    uint64_t t509 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                    int64_t t510 = (int64_t)t509;
                    gm_t9qkh0rsooc8d t511 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l44) + 8));
                    int64_t t512 = gm_f_sys_kind_sjfdqbt(t511);
                    unsigned __int128 n513 = (unsigned __int128)((uint64_t)32ULL);
                    if (n513 >= 64) gm_trap_shift((uint64_t)n513, "i64", gm_srcn3jjc71a2bv3, 282, 44);
                    int64_t r514 = (int64_t)((uint64_t)t512 << (int)n513);
                    bool o515 = (int64_t)(r514 >> (int)n513) != t512;
                    if (o515) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 282, 44);
                    int64_t t516 = r514;
                    int64_t r517; bool o518 = __builtin_add_overflow(t510, t516, &r517);
                    if (o518) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 282, 20);
                    int64_t t519 = r517;
                    gm_tarq9lp8tdk6f t520;
                    GM_ST(uint8_t, (uint8_t *)&t520 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t520 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t520 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t520 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t520 + 24, 0);
                    ((uint8_t *)&t520)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t520 + 8), t519);
                    memmove(gm_out, ((uint8_t *)&t520), 32);
                    goto gm_fdone;
                }
gm_lb30:;
            }
            {
                {
                    uint8_t t521 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t522 = (uint8_t)(t521 == ((uint8_t)17ULL));
                    if (!t522) {
                        goto gm_lb31;
                    }
                    void * t523 = gm_l0;
                    void * t524 = (void *)((((uint8_t *)t523) + 1368) + 56);
                    int64_t t525 = gm_l5;
                    gm_tppo2hmut88dq t526 = gm_f_remove_20jm5n8(t524, t525);
                    gm_l98 = t526;
                    uint8_t t527 = (uint8_t)((((uint8_t *)&gm_l98))[8] != 0);
                    if (t527) {
                        gm_tm98ap4112hee t528 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l98));
                        gm_l99 = t528;
                    } else {
                        int64_t t529 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t530;
                        GM_ST(uint8_t, (uint8_t *)&t530 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t530 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t530 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t530 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t530 + 24, 0);
                        ((uint8_t *)&t530)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t530 + 8), t529);
                        memmove(gm_out, ((uint8_t *)&t530), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t531 = gm_l99;
                    gm_l45 = t531;
                    gm_tm98ap4112hee t532 = gm_l45;
                    gm_node *pn533 = gm_find(cx, 2126215853);
                    if (!pn533) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 286, 13);
                    gm_b0_1 t534;
                    t534 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn533->clauses[12])(pn533->env, pn533->hctx, pn533, t532);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tarq9lp8tdk6f t535;
                    GM_ST(uint8_t, (uint8_t *)&t535 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t535 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t535 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t535 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t535 + 24, 0);
                    ((uint8_t *)&t535)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t535 + 8), ((int64_t)0LL));
                    memmove(gm_out, ((uint8_t *)&t535), 32);
                    goto gm_fdone;
                }
gm_lb31:;
                {
                    uint8_t t536 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t537 = (uint8_t)(t536 == ((uint8_t)19ULL));
                    if (!t537) {
                        goto gm_lb32;
                    }
                    void * t538 = gm_l0;
                    void * t539 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t540 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t540)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t540, gm_srcn3jjc71a2bv3, 290, 32);
                    gm_b16_8 t541 = GM_LD(gm_b16_8, (((uint8_t *)t539) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t542 = GM_LD(gm_b16_8, ((uint8_t *)&t541));
                    unsigned __int128 t543 = gm_f_get_uint_bq8c4b7(t538, t542);
                    uint64_t t544 = (uint64_t)t543;
                    gm_l46 = t544;
                    void * t545 = gm_l0;
                    void * t546 = (void *)(((uint8_t *)t545) + 1368);
                    int64_t t547 = gm_l5;
                    gm_tppo2hmut88dq t548 = gm_f_get_f2mbmm8(t546, t547);
                    gm_l100 = t548;
                    uint8_t t549 = (uint8_t)((((uint8_t *)&gm_l100))[8] != 0);
                    if (t549) {
                        gm_tm98ap4112hee t550 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l100));
                        gm_l101 = t550;
                    } else {
                        int64_t t551 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t552;
                        GM_ST(uint8_t, (uint8_t *)&t552 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t552 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t552 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t552 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t552 + 24, 0);
                        ((uint8_t *)&t552)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t552 + 8), t551);
                        memmove(gm_out, ((uint8_t *)&t552), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t553 = gm_l101;
                    gm_l47 = t553;
                    gm_tm98ap4112hee t554 = gm_l47;
                    uint64_t t555 = gm_l46;
                    gm_node *pn556 = gm_find(cx, 2126215853);
                    if (!pn556) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 292, 29);
                    gm_ttln0d0hd70en t557;
                    t557 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, uint64_t))pn556->clauses[14])(pn556->env, pn556->hctx, pn556, t554, t555);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t558 = gm_f_sys_unit_nrsqciq(t557);
                    gm_tarq9lp8tdk6f t559;
                    GM_ST(uint8_t, (uint8_t *)&t559 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t559 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t559 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t559 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t559 + 24, 0);
                    ((uint8_t *)&t559)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t559 + 8), t558);
                    memmove(gm_out, ((uint8_t *)&t559), 32);
                    goto gm_fdone;
                }
gm_lb32:;
                {
                    uint8_t t560 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t561 = (uint8_t)(t560 == ((uint8_t)20ULL));
                    if (!t561) {
                        goto gm_lb33;
                    }
                    void * t562 = gm_l0;
                    void * t563 = (void *)(((uint8_t *)t562) + 1368);
                    int64_t t564 = gm_l5;
                    gm_tppo2hmut88dq t565 = gm_f_get_f2mbmm8(t563, t564);
                    gm_l102 = t565;
                    uint8_t t566 = (uint8_t)((((uint8_t *)&gm_l102))[8] != 0);
                    if (t566) {
                        gm_tm98ap4112hee t567 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l102));
                        gm_l103 = t567;
                    } else {
                        int64_t t568 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t569;
                        GM_ST(uint8_t, (uint8_t *)&t569 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t569 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t569 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t569 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t569 + 24, 0);
                        ((uint8_t *)&t569)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t569 + 8), t568);
                        memmove(gm_out, ((uint8_t *)&t569), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t570 = gm_l103;
                    gm_l48 = t570;
                    gm_tm98ap4112hee t571 = gm_l48;
                    gm_node *pn572 = gm_find(cx, 2126215853);
                    if (!pn572) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 296, 29);
                    gm_ttln0d0hd70en t573;
                    t573 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn572->clauses[15])(pn572->env, pn572->hctx, pn572, t571);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t574 = gm_f_sys_unit_nrsqciq(t573);
                    gm_tarq9lp8tdk6f t575;
                    GM_ST(uint8_t, (uint8_t *)&t575 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t575 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t575 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t575 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t575 + 24, 0);
                    ((uint8_t *)&t575)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t575 + 8), t574);
                    memmove(gm_out, ((uint8_t *)&t575), 32);
                    goto gm_fdone;
                }
gm_lb33:;
            }
            {
                {
                    uint8_t t576 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t577 = (uint8_t)(t576 == ((uint8_t)21ULL));
                    if (!t577) {
                        goto gm_lb34;
                    }
                    void * t578 = gm_l0;
                    gm_tl874t2atnlh3 t579 = gm_l1;
                    void * t580 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t581 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t581)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t581, gm_srcn3jjc71a2bv3, 299, 41);
                    gm_b16_8 t582 = GM_LD(gm_b16_8, (((uint8_t *)t580) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t583 = GM_LD(gm_b16_8, ((uint8_t *)&t582));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l104, (void *)NULL, cx, NULL, t578, t579, t583);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t584 = (uint8_t)!((((uint8_t *)&gm_l104))[0] != 0);
                    if (t584) {
                    } else {
                        gm_t76123gfet73g t585 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l104) + 8));
                        gm_tarq9lp8tdk6f t586;
                        GM_ST(uint8_t, (uint8_t *)&t586 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t586 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t586 + 4, 0);
                        ((uint8_t *)&t586)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t586 + 8), t585);
                        memmove(gm_out, ((uint8_t *)&t586), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t587 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l104) + 8));
                    gm_l49 = t587;
                    gm_b16_8 t588 = gm_l49;
                    gm_node *pn589 = gm_find(cx, 2126215853);
                    if (!pn589) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 300, 30);
                    gm_thf5kr0is9u9i t590;
                    t590 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn589->clauses[16])(pn589->env, pn589->hctx, pn589, t588);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t591 = gm_f_sys_count_50c5lcp(t590);
                    gm_tarq9lp8tdk6f t592;
                    GM_ST(uint8_t, (uint8_t *)&t592 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t592 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t592 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t592 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t592 + 24, 0);
                    ((uint8_t *)&t592)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t592 + 8), t591);
                    memmove(gm_out, ((uint8_t *)&t592), 32);
                    goto gm_fdone;
                }
gm_lb34:;
                {
                    uint8_t t593 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t594 = (uint8_t)(t593 == ((uint8_t)22ULL));
                    if (!t594) {
                        goto gm_lb35;
                    }
                    void * t595 = gm_l0;
                    gm_tl874t2atnlh3 t596 = gm_l1;
                    void * t597 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t598 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t598)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t598, gm_srcn3jjc71a2bv3, 303, 41);
                    gm_b16_8 t599 = GM_LD(gm_b16_8, (((uint8_t *)t597) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t600 = GM_LD(gm_b16_8, ((uint8_t *)&t599));
                    gm_f_sys_src_5svsr6i((void *)&gm_l105, (void *)NULL, cx, NULL, t595, t596, t600);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t601 = (uint8_t)!((((uint8_t *)&gm_l105))[0] != 0);
                    if (t601) {
                    } else {
                        gm_t76123gfet73g t602 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l105) + 8));
                        gm_tarq9lp8tdk6f t603;
                        GM_ST(uint8_t, (uint8_t *)&t603 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t603 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t603 + 4, 0);
                        ((uint8_t *)&t603)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t603 + 8), t602);
                        memmove(gm_out, ((uint8_t *)&t603), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t604 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l105) + 8));
                    gm_l50 = t604;
                    void * t605 = gm_l0;
                    gm_tl874t2atnlh3 t606 = gm_l1;
                    void * t607 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t608 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t608)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t608, gm_srcn3jjc71a2bv3, 304, 41);
                    gm_b16_8 t609 = GM_LD(gm_b16_8, (((uint8_t *)t607) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t610 = GM_LD(gm_b16_8, ((uint8_t *)&t609));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l106, (void *)NULL, cx, NULL, t605, t606, t610);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t611 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                    if (t611) {
                    } else {
                        gm_t76123gfet73g t612 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l106) + 8));
                        gm_tarq9lp8tdk6f t613;
                        GM_ST(uint8_t, (uint8_t *)&t613 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t613 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t613 + 4, 0);
                        ((uint8_t *)&t613)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t613 + 8), t612);
                        memmove(gm_out, ((uint8_t *)&t613), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t614 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l106) + 8));
                    gm_l51 = t614;
                    gm_b16_8 t615 = gm_l50;
                    gm_b16_8 t616 = gm_l51;
                    gm_node *pn617 = gm_find(cx, 2126215853);
                    if (!pn617) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 305, 30);
                    gm_thf5kr0is9u9i t618;
                    t618 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn617->clauses[17])(pn617->env, pn617->hctx, pn617, t615, t616);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t619 = gm_f_sys_count_50c5lcp(t618);
                    gm_tarq9lp8tdk6f t620;
                    GM_ST(uint8_t, (uint8_t *)&t620 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t620 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t620 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t620 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t620 + 24, 0);
                    ((uint8_t *)&t620)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t620 + 8), t619);
                    memmove(gm_out, ((uint8_t *)&t620), 32);
                    goto gm_fdone;
                }
gm_lb35:;
            }
            {
                {
                    uint8_t t621 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t622 = (uint8_t)(t621 == ((uint8_t)23ULL));
                    if (!t622) {
                        goto gm_lb36;
                    }
                    void * t623 = gm_l0;
                    gm_tl874t2atnlh3 t624 = gm_l1;
                    void * t625 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t626 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t626)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t626, gm_srcn3jjc71a2bv3, 308, 41);
                    gm_b16_8 t627 = GM_LD(gm_b16_8, (((uint8_t *)t625) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t628 = GM_LD(gm_b16_8, ((uint8_t *)&t627));
                    gm_f_sys_src_5svsr6i((void *)&gm_l107, (void *)NULL, cx, NULL, t623, t624, t628);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t629 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
                    if (t629) {
                    } else {
                        gm_t76123gfet73g t630 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l107) + 8));
                        gm_tarq9lp8tdk6f t631;
                        GM_ST(uint8_t, (uint8_t *)&t631 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t631 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t631 + 4, 0);
                        ((uint8_t *)&t631)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t631 + 8), t630);
                        memmove(gm_out, ((uint8_t *)&t631), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t632 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l107) + 8));
                    gm_l52 = t632;
                    void * t633 = gm_l0;
                    gm_tl874t2atnlh3 t634 = gm_l1;
                    void * t635 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t636 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t636)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t636, gm_srcn3jjc71a2bv3, 309, 41);
                    gm_b16_8 t637 = GM_LD(gm_b16_8, (((uint8_t *)t635) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t638 = GM_LD(gm_b16_8, ((uint8_t *)&t637));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l108, (void *)NULL, cx, NULL, t633, t634, t638);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t639 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                    if (t639) {
                    } else {
                        gm_t76123gfet73g t640 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l108) + 8));
                        gm_tarq9lp8tdk6f t641;
                        GM_ST(uint8_t, (uint8_t *)&t641 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t641 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t641 + 4, 0);
                        ((uint8_t *)&t641)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t641 + 8), t640);
                        memmove(gm_out, ((uint8_t *)&t641), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t642 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l108) + 8));
                    gm_l53 = t642;
                    gm_b16_8 t643 = gm_l52;
                    gm_b16_8 t644 = gm_l53;
                    gm_node *pn645 = gm_find(cx, 2126215853);
                    if (!pn645) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 310, 30);
                    gm_thf5kr0is9u9i t646;
                    t646 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn645->clauses[18])(pn645->env, pn645->hctx, pn645, t643, t644);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t647 = gm_f_sys_count_50c5lcp(t646);
                    gm_tarq9lp8tdk6f t648;
                    GM_ST(uint8_t, (uint8_t *)&t648 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t648 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t648 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t648 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t648 + 24, 0);
                    ((uint8_t *)&t648)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t648 + 8), t647);
                    memmove(gm_out, ((uint8_t *)&t648), 32);
                    goto gm_fdone;
                }
gm_lb36:;
                {
                    uint8_t t649 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t650 = (uint8_t)(t649 == ((uint8_t)24ULL));
                    if (!t650) {
                        goto gm_lb37;
                    }
                    void * t651 = gm_l0;
                    gm_tl874t2atnlh3 t652 = gm_l1;
                    void * t653 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t654 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t654)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t654, gm_srcn3jjc71a2bv3, 313, 43);
                    gm_b16_8 t655 = GM_LD(gm_b16_8, (((uint8_t *)t653) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t656 = GM_LD(gm_b16_8, ((uint8_t *)&t655));
                    gm_f_sys_src_5svsr6i((void *)&gm_l109, (void *)NULL, cx, NULL, t651, t652, t656);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t657 = (uint8_t)!((((uint8_t *)&gm_l109))[0] != 0);
                    if (t657) {
                    } else {
                        gm_t76123gfet73g t658 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l109) + 8));
                        gm_tarq9lp8tdk6f t659;
                        GM_ST(uint8_t, (uint8_t *)&t659 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t659 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t659 + 4, 0);
                        ((uint8_t *)&t659)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t659 + 8), t658);
                        memmove(gm_out, ((uint8_t *)&t659), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t660 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l109) + 8));
                    gm_l54 = t660;
                    void * t661 = gm_l0;
                    gm_tl874t2atnlh3 t662 = gm_l1;
                    void * t663 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t664 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t664)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t664, gm_srcn3jjc71a2bv3, 314, 41);
                    gm_b16_8 t665 = GM_LD(gm_b16_8, (((uint8_t *)t663) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t666 = GM_LD(gm_b16_8, ((uint8_t *)&t665));
                    gm_f_sys_src_5svsr6i((void *)&gm_l110, (void *)NULL, cx, NULL, t661, t662, t666);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t667 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
                    if (t667) {
                    } else {
                        gm_t76123gfet73g t668 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l110) + 8));
                        gm_tarq9lp8tdk6f t669;
                        GM_ST(uint8_t, (uint8_t *)&t669 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t669 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t669 + 4, 0);
                        ((uint8_t *)&t669)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t669 + 8), t668);
                        memmove(gm_out, ((uint8_t *)&t669), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t670 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l110) + 8));
                    gm_l55 = t670;
                    gm_b16_8 t671 = gm_l54;
                    gm_b16_8 t672 = gm_l55;
                    gm_node *pn673 = gm_find(cx, 2126215853);
                    if (!pn673) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 315, 29);
                    gm_ttln0d0hd70en t674;
                    t674 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn673->clauses[19])(pn673->env, pn673->hctx, pn673, t671, t672);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t675 = gm_f_sys_unit_nrsqciq(t674);
                    gm_tarq9lp8tdk6f t676;
                    GM_ST(uint8_t, (uint8_t *)&t676 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t676 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t676 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t676 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t676 + 24, 0);
                    ((uint8_t *)&t676)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t676 + 8), t675);
                    memmove(gm_out, ((uint8_t *)&t676), 32);
                    goto gm_fdone;
                }
gm_lb37:;
            }
            {
                {
                    uint8_t t677 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t678 = (uint8_t)(t677 == ((uint8_t)25ULL));
                    if (!t678) {
                        goto gm_lb38;
                    }
                    void * t679 = gm_l0;
                    gm_tl874t2atnlh3 t680 = gm_l1;
                    void * t681 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t682 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t682)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t682, gm_srcn3jjc71a2bv3, 318, 41);
                    gm_b16_8 t683 = GM_LD(gm_b16_8, (((uint8_t *)t681) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t684 = GM_LD(gm_b16_8, ((uint8_t *)&t683));
                    gm_f_sys_src_5svsr6i((void *)&gm_l111, (void *)NULL, cx, NULL, t679, t680, t684);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t685 = (uint8_t)!((((uint8_t *)&gm_l111))[0] != 0);
                    if (t685) {
                    } else {
                        gm_t76123gfet73g t686 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l111) + 8));
                        gm_tarq9lp8tdk6f t687;
                        GM_ST(uint8_t, (uint8_t *)&t687 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t687 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t687 + 4, 0);
                        ((uint8_t *)&t687)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t687 + 8), t686);
                        memmove(gm_out, ((uint8_t *)&t687), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t688 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l111) + 8));
                    gm_l56 = t688;
                    gm_b16_8 t689 = gm_l56;
                    void * t690 = gm_l0;
                    void * t691 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t692 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t692)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t692, gm_srcn3jjc71a2bv3, 319, 58);
                    gm_b16_8 t693 = GM_LD(gm_b16_8, (((uint8_t *)t691) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t694 = GM_LD(gm_b16_8, ((uint8_t *)&t693));
                    unsigned __int128 t695 = gm_f_get_uint_bq8c4b7(t690, t694);
                    uint32_t t696 = (uint32_t)t695;
                    gm_node *pn697 = gm_find(cx, 2126215853);
                    if (!pn697) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 319, 29);
                    gm_ttln0d0hd70en t698;
                    t698 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, uint32_t))pn697->clauses[20])(pn697->env, pn697->hctx, pn697, t689, t696);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t699 = gm_f_sys_unit_nrsqciq(t698);
                    gm_tarq9lp8tdk6f t700;
                    GM_ST(uint8_t, (uint8_t *)&t700 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t700 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t700 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t700 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t700 + 24, 0);
                    ((uint8_t *)&t700)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t700 + 8), t699);
                    memmove(gm_out, ((uint8_t *)&t700), 32);
                    goto gm_fdone;
                }
gm_lb38:;
                {
                    uint8_t t701 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t702 = (uint8_t)(t701 == ((uint8_t)26ULL));
                    if (!t702) {
                        goto gm_lb39;
                    }
                    void * t703 = gm_l0;
                    gm_tl874t2atnlh3 t704 = gm_l1;
                    void * t705 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t706 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t706)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t706, gm_srcn3jjc71a2bv3, 322, 41);
                    gm_b16_8 t707 = GM_LD(gm_b16_8, (((uint8_t *)t705) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t708 = GM_LD(gm_b16_8, ((uint8_t *)&t707));
                    gm_f_sys_src_5svsr6i((void *)&gm_l112, (void *)NULL, cx, NULL, t703, t704, t708);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t709 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
                    if (t709) {
                    } else {
                        gm_t76123gfet73g t710 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l112) + 8));
                        gm_tarq9lp8tdk6f t711;
                        GM_ST(uint8_t, (uint8_t *)&t711 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t711 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t711 + 4, 0);
                        ((uint8_t *)&t711)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t711 + 8), t710);
                        memmove(gm_out, ((uint8_t *)&t711), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t712 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l112) + 8));
                    gm_l57 = t712;
                    gm_b16_8 t713 = gm_l57;
                    void * t714 = gm_l0;
                    void * t715 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t716 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t716)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t716, gm_srcn3jjc71a2bv3, 323, 62);
                    void * t717 = GM_LD(void *, (((uint8_t *)t715) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_t88rcucg1b7uk t718 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t717) + 56));
                    void * t719 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t720 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t720)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t720, gm_srcn3jjc71a2bv3, 323, 74);
                    gm_b16_8 t721 = GM_LD(gm_b16_8, (((uint8_t *)t719) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t722 = GM_LD(gm_b16_8, ((uint8_t *)&t721));
                    int64_t t723 = gm_f_sys_int_32e79ed(t714, GM_CONV(gm_csguankko4lmhd, t718), t722);
                    gm_node *pn724 = gm_find(cx, 2126215853);
                    if (!pn724) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 323, 29);
                    gm_ttln0d0hd70en t725;
                    t725 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, int64_t))pn724->clauses[21])(pn724->env, pn724->hctx, pn724, t713, t723);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t726 = gm_f_sys_unit_nrsqciq(t725);
                    gm_tarq9lp8tdk6f t727;
                    GM_ST(uint8_t, (uint8_t *)&t727 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t727 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t727 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t727 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t727 + 24, 0);
                    ((uint8_t *)&t727)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t727 + 8), t726);
                    memmove(gm_out, ((uint8_t *)&t727), 32);
                    goto gm_fdone;
                }
gm_lb39:;
            }
            {
                {
                    void * t728 = gm_l0;
                    gm_tl874t2atnlh3 t729 = gm_l1;
                    gm_b16_8 t730;
                    GM_ST(void *, (uint8_t *)&t730, (void *)(gm_sqn7kgtu67hrg));
                    GM_ST(uintptr_t, (uint8_t *)&t730 + 8, (uintptr_t)(41));
                    gm_t76123gfet73g t731;
                    gm_f_trap_qh84l9d((void *)&t731, (void *)NULL, cx, NULL, t728, t729, t730);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tarq9lp8tdk6f t732;
                    GM_ST(uint8_t, (uint8_t *)&t732 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t732 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t732 + 4, 0);
                    ((uint8_t *)&t732)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t732 + 8), t731);
                    memmove(gm_out, ((uint8_t *)&t732), 32);
                    goto gm_fdone;
                }
gm_lb40:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 205, 5);
            }
        }
gm_lb3:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
extern __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_kind_sjfdqbt F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_l1 = ((int64_t)0LL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_l1 = ((int64_t)1LL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)2ULL));
            if (!t12) {
                goto gm_lb4;
            }
            gm_l1 = ((int64_t)2LL);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)3ULL));
            if (!t14) {
                goto gm_lb5;
            }
            gm_l1 = ((int64_t)3LL);
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 96, 42);
    }
gm_lb1:;
    int64_t t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_net_aff7h5l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* s */
    int64_t gm_l11; /* handle */
    gm_toe5fmr3vvf4d gm_l15; /* err */
    gm_tm98ap4112hee gm_l16; /* l */
    int64_t gm_l17; /* handle */
    gm_toe5fmr3vvf4d gm_l20; /* err */
    uint16_t gm_l21; /* port */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_tm98ap4112hee gm_l25; /* s */
    int64_t gm_l26; /* handle */
    uint64_t gm_l38; /* recv_ns */
    uint64_t gm_l39; /* send_ns */
    GM_STACK_CHECK(&F, 5254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_td0ept12e7q8b gm_l47;
    gm_toqti1ubjnfd1 gm_l48;
    gm_tm98ap4112hee gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_td0ept12e7q8b gm_l51;
    gm_tm98ap4112hee gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tppo2hmut88dq gm_l54;
    gm_tm98ap4112hee gm_l55;
    uint16_t gm_l56;
    gm_tppo2hmut88dq gm_l57;
    gm_tm98ap4112hee gm_l58;
    gm_toqti1ubjnfd1 gm_l59;
    gm_tm98ap4112hee gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_td0ept12e7q8b gm_l62;
    gm_tppo2hmut88dq gm_l63;
    gm_tm98ap4112hee gm_l64;
    gm_t6j8vdmi81h7c gm_l65;
    gm_tfvg1o4f8mmhi gm_l66;
    gm_td0ept12e7q8b gm_l67;
    gm_tppo2hmut88dq gm_l68;
    gm_tm98ap4112hee gm_l69;
    gm_t6j8vdmi81h7c gm_l70;
    gm_tfvg1o4f8mmhi gm_l71;
    gm_tppo2hmut88dq gm_l72;
    gm_tppo2hmut88dq gm_l73;
    gm_t9qkh0rsooc8d gm_l74;
    int64_t gm_l75;
    gm_tppo2hmut88dq gm_l76;
    gm_tm98ap4112hee gm_l77;
    gm_tppo2hmut88dq gm_l78;
    gm_tm98ap4112hee gm_l79;
    gm_t6ie8iajlnpt1 gm_l80;
    gm_t6ie8iajlnpt1 gm_l81;
    gm_tppo2hmut88dq gm_l82;
    gm_tm98ap4112hee gm_l83;
    gm_td0ept12e7q8b gm_l84;
    {
        gm_l5 = ((int64_t)-1LL);
        gm_t9qkh0rsooc8d t22 = gm_l2;
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)27ULL));
        gm_t9qkh0rsooc8d t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)28ULL));
        gm_t0am46iohlpol t25;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 0), t23);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 1), t24);
        uint8_t t26 = 0;
        if (gm_eqtje7cegoml(((uint8_t *)&t25) + 0, ((uint8_t *)&t22))) t26 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t25) + 1, ((uint8_t *)&t22))) t26 = 1;
        uint8_t t27 = (uint8_t)(!t26);
        if (t27) {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srcn3jjc71a2bv3, 332, 70);
            void * t31 = GM_LD(void *, (((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 8));
            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 56));
            void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t34, gm_srcn3jjc71a2bv3, 332, 82);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t33) + (uintptr_t)((uint64_t)0ULL) * 16));
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            int64_t t37 = gm_f_sys_int_32e79ed(t28, GM_CONV(gm_csguankko4lmhd, t32), t36);
            gm_l5 = t37;
        }
        {
            {
                {
                    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)27ULL));
                    if (!t39) {
                        goto gm_lb2;
                    }
                    void * t40 = gm_l0;
                    gm_tl874t2atnlh3 t41 = gm_l1;
                    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t43, gm_srcn3jjc71a2bv3, 335, 41);
                    gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t42) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                    gm_f_sys_src_5svsr6i((void *)&gm_l47, (void *)NULL, cx, NULL, t40, t41, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (t46) {
                    } else {
                        gm_t76123gfet73g t47 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l47) + 8));
                        gm_tarq9lp8tdk6f t48;
                        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                        ((uint8_t *)&t48)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t48 + 8), t47);
                        memmove(gm_out, ((uint8_t *)&t48), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                    F.l6 = t49;
                    void * t50 = gm_l0;
                    void * t51 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t52, gm_srcn3jjc71a2bv3, 336, 32);
                    gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t51) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                    unsigned __int128 t55 = gm_f_get_uint_bq8c4b7(t50, t54);
                    uint16_t t56 = (uint16_t)t55;
                    F.l7 = t56;
                    gm_toe5fmr3vvf4d t57;
                    GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)12ULL));
                    gm_t62ebbi2i5rkd t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t58 + 8), t57);
                    F.l8 = t58;
                    void * t59 = gm_l0;
                    uint64_t t60 = GM_LD(uint64_t, ((((uint8_t *)t59) + 1672) + 8));
                    uint8_t t61 = (t60 > ((uint64_t)1ULL));
                    if (t61) {
                        void * t62 = gm_l0;
                        gm_tl874t2atnlh3 t63 = gm_l1;
                        gm_b16_8 t64 = F.l6;
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                        uint16_t t66 = F.l7;
                        gm_tvaas22dl64e0 t67;
                        GM_ST(uint16_t, (uint8_t *)&t67 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t67 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t65);
                        GM_ST(uint16_t, ((uint8_t *)&t67 + 16), t66);
                        gm_tmvdbo28o1i6h t68;
                        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_tvaas22dl64e0, ((uint8_t *)&t68 + 8), t67);
                        gm_f_socket_on_host_thread_u8c2vg5((void *)&gm_l48, (void *)NULL, cx, NULL, t62, t63, &t68);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                        if (t69) {
                        } else {
                            gm_t76123gfet73g t70 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l48) + 8));
                            gm_tarq9lp8tdk6f t71;
                            GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                            ((uint8_t *)&t71)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t71 + 8), t70);
                            memmove(gm_out, ((uint8_t *)&t71), 32);
                            goto gm_fdone;
                        }
                        gm_t62ebbi2i5rkd t72 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&gm_l48) + 8));
                        F.l8 = t72;
                    } else {
                        gm_closure t73 = ((gm_closure){(void *)gm_f_fn_aqcn8lk, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t73);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t74 = (uint8_t)!((((uint8_t *)&F.l8))[0] != 0);
                    if (t74) {
                        gm_tm98ap4112hee t75 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l8) + 8));
                        gm_l49 = t75;
                    } else {
                        gm_toe5fmr3vvf4d t76 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l8) + 8));
                        gm_l9 = t76;
                        gm_toe5fmr3vvf4d t77 = gm_l9;
                        int64_t t78 = gm_f_sys_code_cjtlfo1(t77);
                        gm_tarq9lp8tdk6f t79;
                        GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t79 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t79 + 24, 0);
                        ((uint8_t *)&t79)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t79 + 8), t78);
                        memmove(gm_out, ((uint8_t *)&t79), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t80 = gm_l49;
                    gm_l10 = t80;
                    void * t81 = gm_l0;
                    void * t82 = (void *)(((uint8_t *)t81) + 1368);
                    int64_t t83 = gm_f_sys_next_handle_r35ek81(t82);
                    gm_l11 = t83;
                    void * t84 = gm_l0;
                    void * t85 = (void *)((((uint8_t *)t84) + 1368) + 112);
                    int64_t t86 = gm_l11;
                    gm_tm98ap4112hee t87 = gm_l10;
                    gm_tftb1odc739t5 t88 = gm_f_put_qkbvor7(t85, t86, GM_CONV(gm_csfq64m1m1rt6m, t87));
                    gm_l50 = t88;
                    uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                    if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcn3jjc71a2bv3, 345, 13);
                    int64_t t90 = gm_l11;
                    gm_tarq9lp8tdk6f t91;
                    GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t91 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t91 + 24, 0);
                    ((uint8_t *)&t91)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t91 + 8), t90);
                    memmove(gm_out, ((uint8_t *)&t91), 32);
                    goto gm_fdone;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t93 = (uint8_t)(t92 == ((uint8_t)28ULL));
                    if (!t93) {
                        goto gm_lb3;
                    }
                    void * t94 = gm_l0;
                    gm_tl874t2atnlh3 t95 = gm_l1;
                    void * t96 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t97)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t97, gm_srcn3jjc71a2bv3, 349, 41);
                    gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t96) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                    gm_f_sys_src_5svsr6i((void *)&gm_l51, (void *)NULL, cx, NULL, t94, t95, t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                    if (t100) {
                    } else {
                        gm_t76123gfet73g t101 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l51) + 8));
                        gm_tarq9lp8tdk6f t102;
                        GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                        ((uint8_t *)&t102)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t102 + 8), t101);
                        memmove(gm_out, ((uint8_t *)&t102), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                    F.l12 = t103;
                    void * t104 = gm_l0;
                    void * t105 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t106)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t106, gm_srcn3jjc71a2bv3, 350, 32);
                    gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)t105) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                    unsigned __int128 t109 = gm_f_get_uint_bq8c4b7(t104, t108);
                    uint16_t t110 = (uint16_t)t109;
                    F.l13 = t110;
                    gm_toe5fmr3vvf4d t111;
                    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)12ULL));
                    gm_t62ebbi2i5rkd t112;
                    GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                    ((uint8_t *)&t112)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t112 + 8), t111);
                    F.l14 = t112;
                    gm_closure t113 = ((gm_closure){(void *)gm_f_fn_cijad0r, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t114 = (uint8_t)!((((uint8_t *)&F.l14))[0] != 0);
                    if (t114) {
                        gm_tm98ap4112hee t115 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l14) + 8));
                        gm_l52 = t115;
                    } else {
                        gm_toe5fmr3vvf4d t116 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l14) + 8));
                        gm_l15 = t116;
                        gm_toe5fmr3vvf4d t117 = gm_l15;
                        int64_t t118 = gm_f_sys_code_cjtlfo1(t117);
                        gm_tarq9lp8tdk6f t119;
                        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t119 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t119 + 24, 0);
                        ((uint8_t *)&t119)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t119 + 8), t118);
                        memmove(gm_out, ((uint8_t *)&t119), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t120 = gm_l52;
                    gm_l16 = t120;
                    void * t121 = gm_l0;
                    void * t122 = (void *)(((uint8_t *)t121) + 1368);
                    int64_t t123 = gm_f_sys_next_handle_r35ek81(t122);
                    gm_l17 = t123;
                    void * t124 = gm_l0;
                    void * t125 = (void *)((((uint8_t *)t124) + 1368) + 168);
                    int64_t t126 = gm_l17;
                    gm_tm98ap4112hee t127 = gm_l16;
                    gm_tftb1odc739t5 t128 = gm_f_put_ha6815g(t125, t126, GM_CONV(gm_csfq64m1m1rt6m, t127));
                    gm_l53 = t128;
                    uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                    if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcn3jjc71a2bv3, 355, 13);
                    int64_t t130 = gm_l17;
                    gm_tarq9lp8tdk6f t131;
                    GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t131 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t131 + 24, 0);
                    ((uint8_t *)&t131)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t131 + 8), t130);
                    memmove(gm_out, ((uint8_t *)&t131), 32);
                    goto gm_fdone;
                }
gm_lb3:;
                {
                    uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t133 = (uint8_t)(t132 == ((uint8_t)29ULL));
                    if (!t133) {
                        goto gm_lb4;
                    }
                    void * t134 = gm_l0;
                    void * t135 = (void *)((((uint8_t *)t134) + 1368) + 168);
                    int64_t t136 = gm_l5;
                    gm_tppo2hmut88dq t137 = gm_f_get_8o0rtd2(t135, t136);
                    gm_l54 = t137;
                    uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l54))[8] != 0);
                    if (t138) {
                        gm_tm98ap4112hee t139 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l54));
                        gm_l55 = t139;
                    } else {
                        int64_t t140 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t141;
                        GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t141 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t141 + 24, 0);
                        ((uint8_t *)&t141)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t141 + 8), t140);
                        memmove(gm_out, ((uint8_t *)&t141), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t142 = gm_l55;
                    F.l18 = t142;
                    gm_toe5fmr3vvf4d t143;
                    GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)12ULL));
                    gm_tvcqs9u9iqpct t144;
                    GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                    ((uint8_t *)&t144)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t144 + 4), t143);
                    F.l19 = t144;
                    gm_closure t145 = ((gm_closure){(void *)gm_f_fn_mslt7ot, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t146 = (uint8_t)!((((uint8_t *)&F.l19))[0] != 0);
                    if (t146) {
                        uint16_t t147 = GM_LD(uint16_t, (((uint8_t *)&F.l19) + 4));
                        gm_l56 = t147;
                    } else {
                        gm_toe5fmr3vvf4d t148 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l19) + 4));
                        gm_l20 = t148;
                        gm_toe5fmr3vvf4d t149 = gm_l20;
                        int64_t t150 = gm_f_sys_code_cjtlfo1(t149);
                        gm_tarq9lp8tdk6f t151;
                        GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t151 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t151 + 24, 0);
                        ((uint8_t *)&t151)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t151 + 8), t150);
                        memmove(gm_out, ((uint8_t *)&t151), 32);
                        goto gm_fdone;
                    }
                    uint16_t t152 = gm_l56;
                    gm_l21 = t152;
                    uint16_t t153 = gm_l21;
                    int64_t t154 = (int64_t)t153;
                    gm_tarq9lp8tdk6f t155;
                    GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t155 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t155 + 24, 0);
                    ((uint8_t *)&t155)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t155 + 8), t154);
                    memmove(gm_out, ((uint8_t *)&t155), 32);
                    goto gm_fdone;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t157 = (uint8_t)(t156 == ((uint8_t)30ULL));
                    if (!t157) {
                        goto gm_lb5;
                    }
                    void * t158 = gm_l0;
                    void * t159 = (void *)((((uint8_t *)t158) + 1368) + 168);
                    int64_t t160 = gm_l5;
                    gm_tppo2hmut88dq t161 = gm_f_get_8o0rtd2(t159, t160);
                    gm_l57 = t161;
                    uint8_t t162 = (uint8_t)((((uint8_t *)&gm_l57))[8] != 0);
                    if (t162) {
                        gm_tm98ap4112hee t163 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l57));
                        gm_l58 = t163;
                    } else {
                        int64_t t164 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t165;
                        GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t165 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t165 + 24, 0);
                        ((uint8_t *)&t165)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t165 + 8), t164);
                        memmove(gm_out, ((uint8_t *)&t165), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t166 = gm_l58;
                    F.l22 = t166;
                    gm_toe5fmr3vvf4d t167;
                    GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)12ULL));
                    gm_t62ebbi2i5rkd t168;
                    GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                    ((uint8_t *)&t168)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t168 + 8), t167);
                    F.l23 = t168;
                    void * t169 = gm_l0;
                    uint64_t t170 = GM_LD(uint64_t, ((((uint8_t *)t169) + 1672) + 8));
                    uint8_t t171 = (t170 > ((uint64_t)1ULL));
                    if (t171) {
                        void * t172 = gm_l0;
                        gm_tl874t2atnlh3 t173 = gm_l1;
                        gm_tm98ap4112hee t174 = F.l22;
                        gm_tmvdbo28o1i6h t175;
                        GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t175 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t175 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t175 + 8), t174);
                        gm_f_socket_on_host_thread_u8c2vg5((void *)&gm_l59, (void *)NULL, cx, NULL, t172, t173, &t175);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                        if (t176) {
                        } else {
                            gm_t76123gfet73g t177 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l59) + 8));
                            gm_tarq9lp8tdk6f t178;
                            GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t178 + 4, 0);
                            ((uint8_t *)&t178)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t178 + 8), t177);
                            memmove(gm_out, ((uint8_t *)&t178), 32);
                            goto gm_fdone;
                        }
                        gm_t62ebbi2i5rkd t179 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&gm_l59) + 8));
                        F.l23 = t179;
                    } else {
                        gm_closure t180 = ((gm_closure){(void *)gm_f_fn_rg62rr4, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t180);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t181 = (uint8_t)!((((uint8_t *)&F.l23))[0] != 0);
                    if (t181) {
                        gm_tm98ap4112hee t182 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l23) + 8));
                        gm_l60 = t182;
                    } else {
                        gm_toe5fmr3vvf4d t183 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l23) + 8));
                        gm_l24 = t183;
                        gm_toe5fmr3vvf4d t184 = gm_l24;
                        int64_t t185 = gm_f_sys_code_cjtlfo1(t184);
                        gm_tarq9lp8tdk6f t186;
                        GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t186 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t186 + 24, 0);
                        ((uint8_t *)&t186)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t186 + 8), t185);
                        memmove(gm_out, ((uint8_t *)&t186), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t187 = gm_l60;
                    gm_l25 = t187;
                    void * t188 = gm_l0;
                    void * t189 = (void *)(((uint8_t *)t188) + 1368);
                    int64_t t190 = gm_f_sys_next_handle_r35ek81(t189);
                    gm_l26 = t190;
                    void * t191 = gm_l0;
                    void * t192 = (void *)((((uint8_t *)t191) + 1368) + 112);
                    int64_t t193 = gm_l26;
                    gm_tm98ap4112hee t194 = gm_l25;
                    gm_tftb1odc739t5 t195 = gm_f_put_qkbvor7(t192, t193, GM_CONV(gm_csfq64m1m1rt6m, t194));
                    gm_l61 = t195;
                    uint8_t t196 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                    if (!t196) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcn3jjc71a2bv3, 375, 13);
                    int64_t t197 = gm_l26;
                    gm_tarq9lp8tdk6f t198;
                    GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t198 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t198 + 24, 0);
                    ((uint8_t *)&t198)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t198 + 8), t197);
                    memmove(gm_out, ((uint8_t *)&t198), 32);
                    goto gm_fdone;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)31ULL));
                    if (!t200) {
                        goto gm_lb6;
                    }
                    void * t201 = gm_l0;
                    gm_tl874t2atnlh3 t202 = gm_l1;
                    void * t203 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t204)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t204, gm_srcn3jjc71a2bv3, 379, 41);
                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)t203) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                    gm_f_sys_src_5svsr6i((void *)&gm_l62, (void *)NULL, cx, NULL, t201, t202, t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                    if (t207) {
                    } else {
                        gm_t76123gfet73g t208 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l62) + 8));
                        gm_tarq9lp8tdk6f t209;
                        GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                        ((uint8_t *)&t209)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t209 + 8), t208);
                        memmove(gm_out, ((uint8_t *)&t209), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                    F.l27 = t210;
                    void * t211 = gm_l0;
                    void * t212 = (void *)((((uint8_t *)t211) + 1368) + 112);
                    int64_t t213 = gm_l5;
                    gm_tppo2hmut88dq t214 = gm_f_get_gej4j8d(t212, t213);
                    gm_l63 = t214;
                    uint8_t t215 = (uint8_t)((((uint8_t *)&gm_l63))[8] != 0);
                    if (t215) {
                        gm_tm98ap4112hee t216 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l63));
                        gm_l64 = t216;
                    } else {
                        int64_t t217 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t218;
                        GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t218 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
                        ((uint8_t *)&t218)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t218 + 8), t217);
                        memmove(gm_out, ((uint8_t *)&t218), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t219 = gm_l64;
                    F.l28 = t219;
                    gm_toe5fmr3vvf4d t220;
                    GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)12ULL));
                    gm_thf5kr0is9u9i t221;
                    GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                    ((uint8_t *)&t221)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t221 + 8), t220);
                    F.l29 = t221;
                    void * t222 = gm_l0;
                    uint64_t t223 = GM_LD(uint64_t, ((((uint8_t *)t222) + 1672) + 8));
                    uint8_t t224 = (t223 > ((uint64_t)1ULL));
                    if (t224) {
                        void * t225 = gm_l0;
                        gm_tl874t2atnlh3 t226 = gm_l1;
                        gm_tm98ap4112hee t227 = F.l28;
                        gm_b16_8 t228 = F.l27;
                        gm_t3olsle24rveu t229;
                        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t229 + 0), t227);
                        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 8), t228);
                        gm_tmvdbo28o1i6h t230;
                        GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t230 + 8), t229);
                        gm_f_on_host_thread_kmegc07((void *)&gm_l65, (void *)NULL, cx, NULL, t225, t226, &t230);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t231 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                        if (t231) {
                        } else {
                            gm_t76123gfet73g t232 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l65) + 8));
                            gm_tarq9lp8tdk6f t233;
                            GM_ST(uint8_t, (uint8_t *)&t233 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t233 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t233 + 4, 0);
                            ((uint8_t *)&t233)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t233 + 8), t232);
                            memmove(gm_out, ((uint8_t *)&t233), 32);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l66), (((uint8_t *)&gm_l65) + 8), 32);
                        gm_thf5kr0is9u9i t234 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l66));
                        F.l29 = t234;
                    } else {
                        gm_closure t235 = ((gm_closure){(void *)gm_f_fn_tlii61s, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t235);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_thf5kr0is9u9i t236 = F.l29;
                    int64_t t237 = gm_f_sys_net_count_46l7ueq(t236);
                    gm_tarq9lp8tdk6f t238;
                    GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t238 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t238 + 24, 0);
                    ((uint8_t *)&t238)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t238 + 8), t237);
                    memmove(gm_out, ((uint8_t *)&t238), 32);
                    goto gm_fdone;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t240 = (uint8_t)(t239 == ((uint8_t)32ULL));
                    if (!t240) {
                        goto gm_lb7;
                    }
                    void * t241 = gm_l0;
                    gm_tl874t2atnlh3 t242 = gm_l1;
                    void * t243 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t244)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t244, gm_srcn3jjc71a2bv3, 390, 41);
                    gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)t243) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t246 = GM_LD(gm_b16_8, ((uint8_t *)&t245));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l67, (void *)NULL, cx, NULL, t241, t242, t246);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                    if (t247) {
                    } else {
                        gm_t76123gfet73g t248 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l67) + 8));
                        gm_tarq9lp8tdk6f t249;
                        GM_ST(uint8_t, (uint8_t *)&t249 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t249 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t249 + 4, 0);
                        ((uint8_t *)&t249)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t249 + 8), t248);
                        memmove(gm_out, ((uint8_t *)&t249), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                    F.l30 = t250;
                    void * t251 = gm_l0;
                    void * t252 = (void *)((((uint8_t *)t251) + 1368) + 112);
                    int64_t t253 = gm_l5;
                    gm_tppo2hmut88dq t254 = gm_f_get_gej4j8d(t252, t253);
                    gm_l68 = t254;
                    uint8_t t255 = (uint8_t)((((uint8_t *)&gm_l68))[8] != 0);
                    if (t255) {
                        gm_tm98ap4112hee t256 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l68));
                        gm_l69 = t256;
                    } else {
                        int64_t t257 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t258;
                        GM_ST(uint8_t, (uint8_t *)&t258 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t258 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t258 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t258 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t258 + 24, 0);
                        ((uint8_t *)&t258)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t258 + 8), t257);
                        memmove(gm_out, ((uint8_t *)&t258), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t259 = gm_l69;
                    F.l31 = t259;
                    gm_toe5fmr3vvf4d t260;
                    GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t260 + 0), ((uint8_t)12ULL));
                    gm_thf5kr0is9u9i t261;
                    GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                    ((uint8_t *)&t261)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t261 + 8), t260);
                    F.l32 = t261;
                    void * t262 = gm_l0;
                    uint64_t t263 = GM_LD(uint64_t, ((((uint8_t *)t262) + 1672) + 8));
                    uint8_t t264 = (t263 > ((uint64_t)1ULL));
                    if (t264) {
                        void * t265 = gm_l0;
                        gm_tl874t2atnlh3 t266 = gm_l1;
                        gm_tm98ap4112hee t267 = F.l31;
                        gm_b16_8 t268 = F.l30;
                        gm_t3olsle24rveu t269;
                        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t269 + 0), t267);
                        GM_ST(gm_b16_8, ((uint8_t *)&t269 + 8), t268);
                        gm_tmvdbo28o1i6h t270;
                        GM_ST(uint8_t, (uint8_t *)&t270 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t270 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t270 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t270 + 0), ((uint8_t)5ULL));
                        GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t270 + 8), t269);
                        gm_f_on_host_thread_kmegc07((void *)&gm_l70, (void *)NULL, cx, NULL, t265, t266, &t270);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t271 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                        if (t271) {
                        } else {
                            gm_t76123gfet73g t272 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l70) + 8));
                            gm_tarq9lp8tdk6f t273;
                            GM_ST(uint8_t, (uint8_t *)&t273 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t273 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t273 + 4, 0);
                            ((uint8_t *)&t273)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t273 + 8), t272);
                            memmove(gm_out, ((uint8_t *)&t273), 32);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l71), (((uint8_t *)&gm_l70) + 8), 32);
                        gm_thf5kr0is9u9i t274 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l71));
                        F.l32 = t274;
                    } else {
                        gm_closure t275 = ((gm_closure){(void *)gm_f_fn_ducmvdu, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t275);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_thf5kr0is9u9i t276 = F.l32;
                    int64_t t277 = gm_f_sys_net_count_46l7ueq(t276);
                    gm_tarq9lp8tdk6f t278;
                    GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t278 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t278 + 24, 0);
                    ((uint8_t *)&t278)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t278 + 8), t277);
                    memmove(gm_out, ((uint8_t *)&t278), 32);
                    goto gm_fdone;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t280 = (uint8_t)(t279 == ((uint8_t)33ULL));
                    if (!t280) {
                        goto gm_lb8;
                    }
                    {
                        {
                            void * t281 = gm_l0;
                            void * t282 = (void *)((((uint8_t *)t281) + 1368) + 112);
                            int64_t t283 = gm_l5;
                            gm_tppo2hmut88dq t284 = gm_f_remove_7bvuc21(t282, t283);
                            gm_l72 = t284;
                            uint8_t t285 = (uint8_t)((((uint8_t *)&gm_l72))[8] != 0);
                            if (!t285) {
                                goto gm_lb10;
                            }
                            gm_tm98ap4112hee t286 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l72));
                            F.l33 = t286;
                            gm_closure t287 = ((gm_closure){(void *)gm_f_fn_r8m0qjr, (void *)&F.h, cx});
                            gm_f_defaults_p077jgf(t287);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tarq9lp8tdk6f t288;
                            GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t288 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t288 + 24, 0);
                            ((uint8_t *)&t288)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t288 + 8), ((int64_t)0LL));
                            memmove(gm_out, ((uint8_t *)&t288), 32);
                            goto gm_fdone;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    {
                        {
                            void * t289 = gm_l0;
                            void * t290 = (void *)((((uint8_t *)t289) + 1368) + 168);
                            int64_t t291 = gm_l5;
                            gm_tppo2hmut88dq t292 = gm_f_remove_3j2d0bb(t290, t291);
                            gm_l73 = t292;
                            uint8_t t293 = (uint8_t)((((uint8_t *)&gm_l73))[8] != 0);
                            if (!t293) {
                                goto gm_lb12;
                            }
                            gm_tm98ap4112hee t294 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l73));
                            F.l34 = t294;
                            gm_closure t295 = ((gm_closure){(void *)gm_f_fn_1lff729, (void *)&F.h, cx});
                            gm_f_defaults_p077jgf(t295);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tarq9lp8tdk6f t296;
                            GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t296 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t296 + 24, 0);
                            ((uint8_t *)&t296)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t296 + 8), ((int64_t)0LL));
                            memmove(gm_out, ((uint8_t *)&t296), 32);
                            goto gm_fdone;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    int64_t t297 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                    gm_tarq9lp8tdk6f t298;
                    GM_ST(uint8_t, (uint8_t *)&t298 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t298 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t298 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t298 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t298 + 24, 0);
                    ((uint8_t *)&t298)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t298 + 8), t297);
                    memmove(gm_out, ((uint8_t *)&t298), 32);
                    goto gm_fdone;
                }
gm_lb8:;
                {
                    uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t300 = (uint8_t)(t299 == ((uint8_t)34ULL));
                    if (!t300) {
                        goto gm_lb13;
                    }
                    void * t301 = gm_l0;
                    void * t302 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t303)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t303, gm_srcn3jjc71a2bv3, 412, 50);
                    void * t304 = GM_LD(void *, (((uint8_t *)t302) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t304) + 56));
                    void * t306 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t307)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t307, gm_srcn3jjc71a2bv3, 412, 62);
                    gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)t306) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                    int64_t t310 = gm_f_sys_int_32e79ed(t301, GM_CONV(gm_csguankko4lmhd, t305), t309);
                    gm_l75 = t310;
                    {
                        {
                            int64_t t311 = gm_l75;
                            uint8_t t312 = ((t311 == ((int64_t)0LL)));
                            if (!t312) {
                                goto gm_lb15;
                            }
                            gm_t9qkh0rsooc8d t313;
                            GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)0ULL));
                            gm_l74 = t313;
                            goto gm_lb14;
                        }
gm_lb15:;
                        {
                            int64_t t314 = gm_l75;
                            uint8_t t315 = ((t314 == ((int64_t)1LL)));
                            if (!t315) {
                                goto gm_lb16;
                            }
                            gm_t9qkh0rsooc8d t316;
                            GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)1ULL));
                            gm_l74 = t316;
                            goto gm_lb14;
                        }
gm_lb16:;
                        {
                            gm_t9qkh0rsooc8d t317;
                            GM_ST(uint8_t, ((uint8_t *)&t317 + 0), ((uint8_t)2ULL));
                            gm_l74 = t317;
                            goto gm_lb14;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 412, 33);
                    }
gm_lb14:;
                    gm_t9qkh0rsooc8d t318 = gm_l74;
                    F.l35 = t318;
                    void * t319 = gm_l0;
                    void * t320 = (void *)((((uint8_t *)t319) + 1368) + 112);
                    int64_t t321 = gm_l5;
                    gm_tppo2hmut88dq t322 = gm_f_get_gej4j8d(t320, t321);
                    gm_l76 = t322;
                    uint8_t t323 = (uint8_t)((((uint8_t *)&gm_l76))[8] != 0);
                    if (t323) {
                        gm_tm98ap4112hee t324 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l76));
                        gm_l77 = t324;
                    } else {
                        int64_t t325 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t326;
                        GM_ST(uint8_t, (uint8_t *)&t326 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t326 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t326 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t326 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t326 + 24, 0);
                        ((uint8_t *)&t326)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t326 + 8), t325);
                        memmove(gm_out, ((uint8_t *)&t326), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t327 = gm_l77;
                    F.l36 = t327;
                    gm_toe5fmr3vvf4d t328;
                    GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t328 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t328 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t328 + 0), ((uint8_t)12ULL));
                    gm_ttln0d0hd70en t329;
                    GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
                    ((uint8_t *)&t329)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t329 + 4), t328);
                    F.l37 = t329;
                    gm_closure t330 = ((gm_closure){(void *)gm_f_fn_vhnrvmh, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t330);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttln0d0hd70en t331 = F.l37;
                    int64_t t332 = gm_f_sys_unit_nrsqciq(t331);
                    gm_tarq9lp8tdk6f t333;
                    GM_ST(uint8_t, (uint8_t *)&t333 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t333 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t333 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t333 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t333 + 24, 0);
                    ((uint8_t *)&t333)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t333 + 8), t332);
                    memmove(gm_out, ((uint8_t *)&t333), 32);
                    goto gm_fdone;
                }
gm_lb13:;
            }
            {
                {
                    uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t335 = (uint8_t)(t334 == ((uint8_t)35ULL));
                    if (!t335) {
                        goto gm_lb18;
                    }
                    void * t336 = gm_l0;
                    void * t337 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t338 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t338)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t338, gm_srcn3jjc71a2bv3, 423, 35);
                    gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)t337) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
                    unsigned __int128 t341 = gm_f_get_uint_bq8c4b7(t336, t340);
                    uint64_t t342 = (uint64_t)t341;
                    gm_l38 = t342;
                    void * t343 = gm_l0;
                    void * t344 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t345)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t345, gm_srcn3jjc71a2bv3, 424, 35);
                    gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)t344) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
                    unsigned __int128 t348 = gm_f_get_uint_bq8c4b7(t343, t347);
                    uint64_t t349 = (uint64_t)t348;
                    gm_l39 = t349;
                    void * t350 = gm_l0;
                    void * t351 = (void *)((((uint8_t *)t350) + 1368) + 112);
                    int64_t t352 = gm_l5;
                    gm_tppo2hmut88dq t353 = gm_f_get_gej4j8d(t351, t352);
                    gm_l78 = t353;
                    uint8_t t354 = (uint8_t)((((uint8_t *)&gm_l78))[8] != 0);
                    if (t354) {
                        gm_tm98ap4112hee t355 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l78));
                        gm_l79 = t355;
                    } else {
                        int64_t t356 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t357;
                        GM_ST(uint8_t, (uint8_t *)&t357 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t357 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t357 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t357 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t357 + 24, 0);
                        ((uint8_t *)&t357)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t357 + 8), t356);
                        memmove(gm_out, ((uint8_t *)&t357), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t358 = gm_l79;
                    F.l40 = t358;
                    uint64_t t359 = gm_l38;
                    uint8_t t360 = (t359 > ((uint64_t)0ULL));
                    if (t360) {
                        uint64_t t361 = gm_l38;
                        gm_t4ce0jd3tdro4 t362;
                        GM_ST(uint64_t, ((uint8_t *)&t362 + 0), t361);
                        gm_t6ie8iajlnpt1 t363;
                        GM_ST(uint8_t, (uint8_t *)&t363 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t363 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t363 + 12, 0);
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t363), t362);
                        ((uint8_t *)&t363)[8] = 1;
                        gm_l80 = t363;
                    } else {
                        gm_t6ie8iajlnpt1 t364;
                        memset(&t364, 0, sizeof t364);
                        gm_l80 = t364;
                    }
                    gm_t6ie8iajlnpt1 t365 = gm_l80;
                    F.l41 = t365;
                    uint64_t t366 = gm_l39;
                    uint8_t t367 = (t366 > ((uint64_t)0ULL));
                    if (t367) {
                        uint64_t t368 = gm_l39;
                        gm_t4ce0jd3tdro4 t369;
                        GM_ST(uint64_t, ((uint8_t *)&t369 + 0), t368);
                        gm_t6ie8iajlnpt1 t370;
                        GM_ST(uint8_t, (uint8_t *)&t370 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t370 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t370 + 12, 0);
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t370), t369);
                        ((uint8_t *)&t370)[8] = 1;
                        gm_l81 = t370;
                    } else {
                        gm_t6ie8iajlnpt1 t371;
                        memset(&t371, 0, sizeof t371);
                        gm_l81 = t371;
                    }
                    gm_t6ie8iajlnpt1 t372 = gm_l81;
                    F.l42 = t372;
                    gm_toe5fmr3vvf4d t373;
                    GM_ST(uint8_t, (uint8_t *)&t373 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t373 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t373 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t373 + 0), ((uint8_t)12ULL));
                    gm_ttln0d0hd70en t374;
                    GM_ST(uint8_t, (uint8_t *)&t374 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t374 + 2, 0);
                    ((uint8_t *)&t374)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t374 + 4), t373);
                    F.l43 = t374;
                    gm_closure t375 = ((gm_closure){(void *)gm_f_fn_j9i1sr8, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t375);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttln0d0hd70en t376 = F.l43;
                    int64_t t377 = gm_f_sys_unit_nrsqciq(t376);
                    gm_tarq9lp8tdk6f t378;
                    GM_ST(uint8_t, (uint8_t *)&t378 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t378 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t378 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t378 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t378 + 24, 0);
                    ((uint8_t *)&t378)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t378 + 8), t377);
                    memmove(gm_out, ((uint8_t *)&t378), 32);
                    goto gm_fdone;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t379 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t380 = (uint8_t)(t379 == ((uint8_t)36ULL));
                    if (!t380) {
                        goto gm_lb19;
                    }
                    void * t381 = gm_l0;
                    void * t382 = (void *)((((uint8_t *)t381) + 1368) + 112);
                    int64_t t383 = gm_l5;
                    gm_tppo2hmut88dq t384 = gm_f_get_gej4j8d(t382, t383);
                    gm_l82 = t384;
                    uint8_t t385 = (uint8_t)((((uint8_t *)&gm_l82))[8] != 0);
                    if (t385) {
                        gm_tm98ap4112hee t386 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l82));
                        gm_l83 = t386;
                    } else {
                        int64_t t387 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t388;
                        GM_ST(uint8_t, (uint8_t *)&t388 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t388 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t388 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t388 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t388 + 24, 0);
                        ((uint8_t *)&t388)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t388 + 8), t387);
                        memmove(gm_out, ((uint8_t *)&t388), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t389 = gm_l83;
                    F.l44 = t389;
                    void * t390 = gm_l0;
                    gm_tl874t2atnlh3 t391 = gm_l1;
                    void * t392 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t393 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t393)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t393, gm_srcn3jjc71a2bv3, 434, 41);
                    gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)t392) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l84, (void *)NULL, cx, NULL, t390, t391, t395);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t396 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                    if (t396) {
                    } else {
                        gm_t76123gfet73g t397 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l84) + 8));
                        gm_tarq9lp8tdk6f t398;
                        GM_ST(uint8_t, (uint8_t *)&t398 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t398 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t398 + 4, 0);
                        ((uint8_t *)&t398)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t398 + 8), t397);
                        memmove(gm_out, ((uint8_t *)&t398), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t399 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l84) + 8));
                    F.l45 = t399;
                    gm_toe5fmr3vvf4d t400;
                    GM_ST(uint8_t, (uint8_t *)&t400 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t400 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t400 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t400 + 0), ((uint8_t)12ULL));
                    gm_thf5kr0is9u9i t401;
                    GM_ST(uint8_t, (uint8_t *)&t401 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t401 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t401 + 4, 0);
                    ((uint8_t *)&t401)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t401 + 8), t400);
                    F.l46 = t401;
                    gm_closure t402 = ((gm_closure){(void *)gm_f_fn_2gkkusq, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t402);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_thf5kr0is9u9i t403 = F.l46;
                    int64_t t404 = gm_f_sys_count_50c5lcp(t403);
                    gm_tarq9lp8tdk6f t405;
                    GM_ST(uint8_t, (uint8_t *)&t405 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t405 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t405 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t405 + 24, 0);
                    ((uint8_t *)&t405)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t405 + 8), t404);
                    memmove(gm_out, ((uint8_t *)&t405), 32);
                    goto gm_fdone;
                }
gm_lb19:;
                {
                    void * t406 = gm_l0;
                    gm_tl874t2atnlh3 t407 = gm_l1;
                    gm_b16_8 t408;
                    GM_ST(void *, (uint8_t *)&t408, (void *)(gm_sd8o4m7695hpq));
                    GM_ST(uintptr_t, (uint8_t *)&t408 + 8, (uintptr_t)(37));
                    gm_t76123gfet73g t409;
                    gm_f_trap_qh84l9d((void *)&t409, (void *)NULL, cx, NULL, t406, t407, t408);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tarq9lp8tdk6f t410;
                    GM_ST(uint8_t, (uint8_t *)&t410 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t410 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t410 + 4, 0);
                    ((uint8_t *)&t410)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t410 + 8), t409);
                    memmove(gm_out, ((uint8_t *)&t410), 32);
                    goto gm_fdone;
                }
gm_lb20:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 333, 5);
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_proc_jhdp277 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l7; /* dir */
    gm_b16_8 gm_l8; /* program */
    uint32_t gm_l9; /* flags */
    gm_toe5fmr3vvf4d gm_l11; /* err */
    gm_tm98ap4112hee gm_l12; /* c */
    int64_t gm_l13; /* handle */
    int64_t gm_l14; /* h */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* c */
    gm_tm98ap4112hee gm_l17; /* c */
    gm_b16_8 gm_l18; /* buf */
    gm_tm98ap4112hee gm_l19; /* c */
    gm_toe5fmr3vvf4d gm_l20; /* err */
    gm_thdrvp9q0u7b5 gm_l21; /* got */
    gm_te0mgnjdphs16 gm_l22; /* chunk */
    gm_tm98ap4112hee gm_l23; /* c */
    gm_tlmjd3nndlf3r gm_l24; /* r */
    gm_toe5fmr3vvf4d gm_l25; /* err */
    gm_tsoiri44d0i6q gm_l26; /* status */
    uint32_t gm_l27; /* code */
    int32_t gm_l28; /* sig */
    gm_tm98ap4112hee gm_l29; /* c */
    gm_tr6norheal11n gm_l30; /* r */
    gm_toe5fmr3vvf4d gm_l31; /* err */
    gm_tqnlpelep1582 gm_l32; /* got */
    gm_tsoiri44d0i6q gm_l33; /* status */
    uint32_t gm_l34; /* code */
    int32_t gm_l35; /* sig */
    gm_tm98ap4112hee gm_l36; /* c */
    GM_STACK_CHECK(&F, 4272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t9922qjstblh1 gm_l37;
    gm_t9922qjstblh1 gm_l38;
    gm_td0ept12e7q8b gm_l39;
    gm_td0ept12e7q8b gm_l40;
    gm_th2lqbo77pvsc gm_l41;
    gm_th2lqbo77pvsc gm_l42;
    gm_t62ebbi2i5rkd gm_l43;
    gm_tm98ap4112hee gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_td0ept12e7q8b gm_l46;
    gm_tppo2hmut88dq gm_l47;
    gm_tm98ap4112hee gm_l48;
    gm_tppo2hmut88dq gm_l49;
    gm_tm98ap4112hee gm_l50;
    gm_td0ept12e7q8b gm_l51;
    gm_tppo2hmut88dq gm_l52;
    gm_tm98ap4112hee gm_l53;
    gm_tlbmih0s72in3 gm_l54;
    gm_thdrvp9q0u7b5 gm_l55;
    gm_te0mgnjdphs16 gm_l56;
    int64_t gm_l57;
    gm_tppo2hmut88dq gm_l58;
    gm_tm98ap4112hee gm_l59;
    gm_tsoiri44d0i6q gm_l60;
    gm_tarq9lp8tdk6f gm_l61;
    gm_tppo2hmut88dq gm_l62;
    gm_tm98ap4112hee gm_l63;
    gm_tqnlpelep1582 gm_l64;
    gm_tsoiri44d0i6q gm_l65;
    gm_tarq9lp8tdk6f gm_l66;
    gm_tppo2hmut88dq gm_l67;
    gm_tm98ap4112hee gm_l68;
    {
        gm_t9qkh0rsooc8d t17 = gm_l2;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        uint8_t t19 = (uint8_t)((t18 == ((uint8_t)37ULL)));
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)37ULL));
        if (t19) {
            {
                void * t22 = gm_l0;
                gm_tl874t2atnlh3 t23 = gm_l1;
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t25, gm_srcn3jjc71a2bv3, 458, 41);
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t24) + (uintptr_t)((uint64_t)0ULL) * 16));
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                gm_f_sys_strings_r85ragc((void *)&gm_l37, (void *)NULL, cx, NULL, t22, t23, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                if (t28) {
                } else {
                    gm_t76123gfet73g t29 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l37) + 8));
                    gm_tarq9lp8tdk6f t30;
                    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                    ((uint8_t *)&t30)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t30 + 8), t29);
                    memmove(gm_out, ((uint8_t *)&t30), 32);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l37) + 8), 40);
                void * t31 = gm_l0;
                gm_tl874t2atnlh3 t32 = gm_l1;
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t34, gm_srcn3jjc71a2bv3, 459, 40);
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t33) + (uintptr_t)((uint64_t)1ULL) * 16));
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                gm_f_sys_strings_r85ragc((void *)&gm_l38, (void *)NULL, cx, NULL, t31, t32, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                if (t37) {
                } else {
                    gm_t76123gfet73g t38 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l38) + 8));
                    gm_tarq9lp8tdk6f t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                    ((uint8_t *)&t39)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t39 + 8), t38);
                    memmove(gm_out, ((uint8_t *)&t39), 32);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l38) + 8), 40);
                void * t40 = gm_l0;
                gm_tl874t2atnlh3 t41 = gm_l1;
                void * t42 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t43, gm_srcn3jjc71a2bv3, 460, 36);
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t42) + (uintptr_t)((uint64_t)2ULL) * 16));
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                gm_f_sys_src_5svsr6i((void *)&gm_l39, (void *)NULL, cx, NULL, t40, t41, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                if (t46) {
                } else {
                    gm_t76123gfet73g t47 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l39) + 8));
                    gm_tarq9lp8tdk6f t48;
                    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                    ((uint8_t *)&t48)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t48 + 8), t47);
                    memmove(gm_out, ((uint8_t *)&t48), 32);
                    goto gm_fdone;
                }
                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                gm_l7 = t49;
                void * t50 = gm_l0;
                gm_tl874t2atnlh3 t51 = gm_l1;
                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t53)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t53, gm_srcn3jjc71a2bv3, 461, 40);
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)t52) + (uintptr_t)((uint64_t)3ULL) * 16));
                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                gm_f_sys_src_5svsr6i((void *)&gm_l40, (void *)NULL, cx, NULL, t50, t51, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                if (t56) {
                } else {
                    gm_t76123gfet73g t57 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l40) + 8));
                    gm_tarq9lp8tdk6f t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t58 + 8), t57);
                    memmove(gm_out, ((uint8_t *)&t58), 32);
                    goto gm_fdone;
                }
                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                gm_l8 = t59;
                void * t60 = gm_l0;
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t62, gm_srcn3jjc71a2bv3, 462, 29);
                gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t61) + (uintptr_t)((uint64_t)4ULL) * 16));
                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                unsigned __int128 t65 = gm_f_get_uint_bq8c4b7(t60, t64);
                uint32_t t66 = (uint32_t)t65;
                gm_l9 = t66;
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                uint8_t t68 = ((t67 == ((uint64_t)0ULL)));
                if (t68) {
                    int64_t t69 = GM_LD(int64_t, ((uint8_t *)gm_cndfnv7mbeh));
                    gm_tarq9lp8tdk6f t70;
                    GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t70 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
                    ((uint8_t *)&t70)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t70 + 8), t69);
                    memmove(&F.ret, ((uint8_t *)&t70), 32);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld21_2;
                }
                uint32_t t71 = gm_l9;
                uint32_t t72 = (uint32_t)(t71 & ((uint32_t)3ULL));
                gm_t9qkh0rsooc8d t73 = gm_f_sys_stdio_nsl9o9u(t72);
                uint32_t t74 = gm_l9;
                unsigned __int128 n75 = (unsigned __int128)((uint64_t)2ULL);
                if (n75 >= 32) gm_trap_shift((uint64_t)n75, "u32", gm_srcn3jjc71a2bv3, 468, 32);
                uint32_t t76 = (uint32_t)(t74 >> (int)n75);
                uint32_t t77 = (uint32_t)(t76 & ((uint32_t)3ULL));
                gm_t9qkh0rsooc8d t78 = gm_f_sys_stdio_nsl9o9u(t77);
                uint32_t t79 = gm_l9;
                unsigned __int128 n80 = (unsigned __int128)((uint64_t)4ULL);
                if (n80 >= 32) gm_trap_shift((uint64_t)n80, "u32", gm_srcn3jjc71a2bv3, 469, 32);
                uint32_t t81 = (uint32_t)(t79 >> (int)n80);
                uint32_t t82 = (uint32_t)(t81 & ((uint32_t)3ULL));
                gm_t9qkh0rsooc8d t83 = gm_f_sys_stdio_nsl9o9u(t82);
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t85 = (t84 > ((uint64_t)0ULL));
                if (t85) {
                    gm_b16_8 t86 = gm_l7;
                    gm_th2lqbo77pvsc t87;
                    GM_ST(uint8_t, (uint8_t *)&t87 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t87 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t87 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t87), t86);
                    ((uint8_t *)&t87)[16] = 1;
                    gm_l41 = t87;
                } else {
                    gm_th2lqbo77pvsc t88;
                    memset(&t88, 0, sizeof t88);
                    gm_l41 = t88;
                }
                gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                uint32_t t91 = gm_l9;
                gm_th2lqbo77pvsc t92 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l41));
                uint8_t t93 = gm_f_sys_has_0sego5l(t91, ((uint32_t)64ULL));
                uint32_t t94 = gm_l9;
                uint8_t t95 = gm_f_sys_has_0sego5l(t94, ((uint32_t)128ULL));
                uint32_t t96 = gm_l9;
                uint8_t t97 = gm_f_sys_has_0sego5l(t96, ((uint32_t)256ULL));
                if (t97) {
                    gm_b16_8 t98 = gm_l8;
                    gm_th2lqbo77pvsc t99;
                    GM_ST(uint8_t, (uint8_t *)&t99 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t99 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t99 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t99), t98);
                    ((uint8_t *)&t99)[16] = 1;
                    gm_l42 = t99;
                } else {
                    gm_th2lqbo77pvsc t100;
                    memset(&t100, 0, sizeof t100);
                    gm_l42 = t100;
                }
                gm_th2lqbo77pvsc t101 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l42));
                gm_t32dckm4jfarl t102;
                GM_ST(uint8_t, (uint8_t *)&t102 + 3, 0);
                GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                GM_ST(uint16_t, (uint8_t *)&t102 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t102 + 52, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102 + 0), t73);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102 + 1), t78);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102 + 2), t83);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t102 + 8), t92);
                GM_ST(gm_b16_8, ((uint8_t *)&t102 + 32), t90);
                GM_ST(uint8_t, ((uint8_t *)&t102 + 48), t93);
                GM_ST(uint8_t, ((uint8_t *)&t102 + 49), t95);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t102 + 56), t101);
                F.l10 = t102;
                gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                gm_node *pn105 = gm_find(cx, 2119230);
                if (!pn105) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 476, 14);
                gm_t62ebbi2i5rkd t106;
                t106 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, const void *))pn105->clauses[0])(pn105->env, pn105->hctx, pn105, t104, &(*(gm_t32dckm4jfarl *)((uint8_t *)&F.l10)));
                if (gm_unw.kind) {
                    goto gm_ld21_2;
                }
                gm_l43 = t106;
                uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                if (t107) {
                    gm_tm98ap4112hee t108 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l43) + 8));
                    gm_l44 = t108;
                } else {
                    gm_toe5fmr3vvf4d t109 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l43) + 8));
                    gm_l11 = t109;
                    gm_toe5fmr3vvf4d t110 = gm_l11;
                    int64_t t111 = gm_f_sys_code_cjtlfo1(t110);
                    gm_tarq9lp8tdk6f t112;
                    GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t112 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t112 + 24, 0);
                    ((uint8_t *)&t112)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t112 + 8), t111);
                    memmove(&F.ret, ((uint8_t *)&t112), 32);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld21_2;
                }
                gm_tm98ap4112hee t113 = gm_l44;
                gm_l12 = t113;
                void * t114 = gm_l0;
                void * t115 = (void *)(((uint8_t *)t114) + 1368);
                int64_t t116 = gm_f_sys_next_handle_r35ek81(t115);
                gm_l13 = t116;
                void * t117 = gm_l0;
                void * t118 = (void *)((((uint8_t *)t117) + 1368) + 224);
                int64_t t119 = gm_l13;
                gm_tm98ap4112hee t120 = gm_l12;
                gm_tftb1odc739t5 t121 = gm_f_put_0tusk8g(t118, t119, GM_CONV(gm_csfq64m1m1rt6m, t120));
                gm_l45 = t121;
                uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcn3jjc71a2bv3, 478, 9);
                int64_t t123 = gm_l13;
                gm_tarq9lp8tdk6f t124;
                GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t124 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t124 + 24, 0);
                ((uint8_t *)&t124)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t124 + 8), t123);
                memmove(&F.ret, ((uint8_t *)&t124), 32);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                goto gm_ld21_2;
            }
            {
                void * t125 = (void *)((uint8_t *)&F.l6);
                gm_f_deinit_gvn47sj(t125);
            }
            {
                void * t126 = (void *)((uint8_t *)&F.l5);
                gm_f_deinit_gvn47sj(t126);
            }
            goto gm_ls127;
gm_ld21_2:;
            {
                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                void * t128 = (void *)((uint8_t *)&F.l6);
                gm_f_deinit_gvn47sj(t128);
                gm_unw = gm_saved;
            }
gm_ld21_1:;
            {
                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                void * t129 = (void *)((uint8_t *)&F.l5);
                gm_f_deinit_gvn47sj(t129);
                gm_unw = gm_saved;
            }
            goto gm_lad;
gm_ls127:;
gm_lb1:;
        }
    }
    {
        void * t130 = gm_l0;
        void * t131 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t132)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t132, gm_srcn3jjc71a2bv3, 481, 21);
        void * t133 = GM_LD(void *, (((uint8_t *)t131) + (uintptr_t)((uint64_t)0ULL) * 8));
        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t133) + 56));
        void * t135 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t136)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t136, gm_srcn3jjc71a2bv3, 481, 33);
        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)t135) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
        int64_t t139 = gm_f_sys_int_32e79ed(t130, GM_CONV(gm_csguankko4lmhd, t134), t138);
        gm_l14 = t139;
        {
            {
                {
                    uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t141 = (uint8_t)(t140 == ((uint8_t)38ULL));
                    if (!t141) {
                        goto gm_lb3;
                    }
                    void * t142 = gm_l0;
                    gm_tl874t2atnlh3 t143 = gm_l1;
                    void * t144 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t145)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t145, gm_srcn3jjc71a2bv3, 484, 41);
                    gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)t144) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                    gm_f_sys_src_5svsr6i((void *)&gm_l46, (void *)NULL, cx, NULL, t142, t143, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                    if (t148) {
                    } else {
                        gm_t76123gfet73g t149 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l46) + 8));
                        gm_tarq9lp8tdk6f t150;
                        GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
                        ((uint8_t *)&t150)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t150 + 8), t149);
                        memmove(gm_out, ((uint8_t *)&t150), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                    gm_l15 = t151;
                    void * t152 = gm_l0;
                    void * t153 = (void *)((((uint8_t *)t152) + 1368) + 224);
                    int64_t t154 = gm_l14;
                    gm_tppo2hmut88dq t155 = gm_f_get_fqooi5f(t153, t154);
                    gm_l47 = t155;
                    uint8_t t156 = (uint8_t)((((uint8_t *)&gm_l47))[8] != 0);
                    if (t156) {
                        gm_tm98ap4112hee t157 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l47));
                        gm_l48 = t157;
                    } else {
                        int64_t t158 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t159;
                        GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t159 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t159 + 24, 0);
                        ((uint8_t *)&t159)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t159 + 8), t158);
                        memmove(gm_out, ((uint8_t *)&t159), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t160 = gm_l48;
                    gm_l16 = t160;
                    gm_tm98ap4112hee t161 = gm_l16;
                    gm_b16_8 t162 = gm_l15;
                    gm_node *pn163 = gm_find(cx, 2119230);
                    if (!pn163) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 486, 30);
                    gm_thf5kr0is9u9i t164;
                    t164 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn163->clauses[1])(pn163->env, pn163->hctx, pn163, t161, t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t165 = gm_f_sys_count_50c5lcp(t164);
                    gm_tarq9lp8tdk6f t166;
                    GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t166 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t166 + 24, 0);
                    ((uint8_t *)&t166)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t166 + 8), t165);
                    memmove(gm_out, ((uint8_t *)&t166), 32);
                    goto gm_fdone;
                }
gm_lb3:;
                {
                    uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t168 = (uint8_t)(t167 == ((uint8_t)39ULL));
                    if (!t168) {
                        goto gm_lb4;
                    }
                    void * t169 = gm_l0;
                    void * t170 = (void *)((((uint8_t *)t169) + 1368) + 224);
                    int64_t t171 = gm_l14;
                    gm_tppo2hmut88dq t172 = gm_f_get_fqooi5f(t170, t171);
                    gm_l49 = t172;
                    uint8_t t173 = (uint8_t)((((uint8_t *)&gm_l49))[8] != 0);
                    if (t173) {
                        gm_tm98ap4112hee t174 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l49));
                        gm_l50 = t174;
                    } else {
                        int64_t t175 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t176;
                        GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t176 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t176 + 24, 0);
                        ((uint8_t *)&t176)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t176 + 8), t175);
                        memmove(gm_out, ((uint8_t *)&t176), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t177 = gm_l50;
                    gm_l17 = t177;
                    gm_tm98ap4112hee t178 = gm_l17;
                    gm_node *pn179 = gm_find(cx, 2119230);
                    if (!pn179) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 490, 13);
                    gm_b0_1 t180;
                    t180 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn179->clauses[2])(pn179->env, pn179->hctx, pn179, t178);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tarq9lp8tdk6f t181;
                    GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t181 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t181 + 24, 0);
                    ((uint8_t *)&t181)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t181 + 8), ((int64_t)0LL));
                    memmove(gm_out, ((uint8_t *)&t181), 32);
                    goto gm_fdone;
                }
gm_lb4:;
                {
                    uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t183 = (uint8_t)(t182 == ((uint8_t)40ULL));
                    if (!t183) {
                        goto gm_lb5;
                    }
                    void * t184 = gm_l0;
                    gm_tl874t2atnlh3 t185 = gm_l1;
                    void * t186 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t187)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t187, gm_srcn3jjc71a2bv3, 494, 41);
                    gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)t186) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l51, (void *)NULL, cx, NULL, t184, t185, t189);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                    if (t190) {
                    } else {
                        gm_t76123gfet73g t191 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l51) + 8));
                        gm_tarq9lp8tdk6f t192;
                        GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                        ((uint8_t *)&t192)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t192 + 8), t191);
                        memmove(gm_out, ((uint8_t *)&t192), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                    gm_l18 = t193;
                    void * t194 = gm_l0;
                    void * t195 = (void *)((((uint8_t *)t194) + 1368) + 224);
                    int64_t t196 = gm_l14;
                    gm_tppo2hmut88dq t197 = gm_f_get_fqooi5f(t195, t196);
                    gm_l52 = t197;
                    uint8_t t198 = (uint8_t)((((uint8_t *)&gm_l52))[8] != 0);
                    if (t198) {
                        gm_tm98ap4112hee t199 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l52));
                        gm_l53 = t199;
                    } else {
                        int64_t t200 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t201;
                        GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t201 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t201 + 24, 0);
                        ((uint8_t *)&t201)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t201 + 8), t200);
                        memmove(gm_out, ((uint8_t *)&t201), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t202 = gm_l53;
                    gm_l19 = t202;
                    gm_tm98ap4112hee t203 = gm_l19;
                    gm_b16_8 t204 = gm_l18;
                    gm_node *pn205 = gm_find(cx, 2119230);
                    if (!pn205) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 496, 20);
                    gm_tlbmih0s72in3 t206;
                    ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn205->clauses[3])((void *)&t206, pn205->env, pn205->hctx, pn205, t203, t204);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l54 = t206;
                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (t207) {
                        memmove(((uint8_t *)&gm_l55), (((uint8_t *)&gm_l54) + 8), 24);
                    } else {
                        gm_toe5fmr3vvf4d t208 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l54) + 8));
                        gm_l20 = t208;
                        gm_toe5fmr3vvf4d t209 = gm_l20;
                        int64_t t210 = gm_f_sys_code_cjtlfo1(t209);
                        gm_tarq9lp8tdk6f t211;
                        GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t211 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t211 + 24, 0);
                        ((uint8_t *)&t211)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t211 + 8), t210);
                        memmove(gm_out, ((uint8_t *)&t211), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l55), 24);
                    uint8_t t212 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
                    if (t212) {
                        gm_te0mgnjdphs16 t213 = GM_LD(gm_te0mgnjdphs16, ((uint8_t *)&gm_l21));
                        gm_l56 = t213;
                    } else {
                        gm_tarq9lp8tdk6f t214;
                        GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t214 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t214 + 24, 0);
                        ((uint8_t *)&t214)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t214 + 8), ((int64_t)0LL));
                        memmove(gm_out, ((uint8_t *)&t214), 32);
                        goto gm_fdone;
                    }
                    gm_te0mgnjdphs16 t215 = gm_l56;
                    gm_l22 = t215;
                    uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    int64_t t217 = (int64_t)t216;
                    gm_t9qkh0rsooc8d t218 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l22));
                    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&t218) + 0));
                    uint8_t t220 = (uint8_t)((t219 == ((uint8_t)1ULL)));
                    gm_t9qkh0rsooc8d t221;
                    GM_ST(uint8_t, ((uint8_t *)&t221 + 0), ((uint8_t)1ULL));
                    if (t220) {
                        unsigned __int128 n222 = (unsigned __int128)((uint64_t)32ULL);
                        if (n222 >= 64) gm_trap_shift((uint64_t)n222, "i64", gm_srcn3jjc71a2bv3, 498, 71);
                        int64_t r223 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n222);
                        bool o224 = (int64_t)(r223 >> (int)n222) != ((int64_t)1LL);
                        if (o224) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 498, 71);
                        int64_t t225 = r223;
                        gm_l57 = t225;
                    } else {
                        gm_l57 = ((int64_t)0LL);
                    }
                    int64_t t226 = gm_l57;
                    int64_t r227; bool o228 = __builtin_add_overflow(t217, t226, &r227);
                    if (o228) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 498, 20);
                    int64_t t229 = r227;
                    gm_tarq9lp8tdk6f t230;
                    GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t230 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t230 + 24, 0);
                    ((uint8_t *)&t230)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t230 + 8), t229);
                    memmove(gm_out, ((uint8_t *)&t230), 32);
                    goto gm_fdone;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t232 = (uint8_t)(t231 == ((uint8_t)41ULL));
                    if (!t232) {
                        goto gm_lb6;
                    }
                    void * t233 = gm_l0;
                    void * t234 = (void *)((((uint8_t *)t233) + 1368) + 224);
                    int64_t t235 = gm_l14;
                    gm_tppo2hmut88dq t236 = gm_f_get_fqooi5f(t234, t235);
                    gm_l58 = t236;
                    uint8_t t237 = (uint8_t)((((uint8_t *)&gm_l58))[8] != 0);
                    if (t237) {
                        gm_tm98ap4112hee t238 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l58));
                        gm_l59 = t238;
                    } else {
                        int64_t t239 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t240;
                        GM_ST(uint8_t, (uint8_t *)&t240 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t240 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t240 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t240 + 24, 0);
                        ((uint8_t *)&t240)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t240 + 8), t239);
                        memmove(gm_out, ((uint8_t *)&t240), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t241 = gm_l59;
                    gm_l23 = t241;
                    gm_tm98ap4112hee t242 = gm_l23;
                    gm_node *pn243 = gm_find(cx, 2119230);
                    if (!pn243) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 502, 18);
                    gm_tlmjd3nndlf3r t244;
                    t244 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn243->clauses[4])(pn243->env, pn243->hctx, pn243, t242);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t244;
                    void * t245 = gm_l0;
                    void * t246 = (void *)((((uint8_t *)t245) + 1368) + 224);
                    int64_t t247 = gm_l14;
                    gm_tppo2hmut88dq t248 = gm_f_remove_jvpr521(t246, t247);
                    uint8_t t249 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (t249) {
                        gm_tsoiri44d0i6q t250 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&gm_l24) + 4));
                        gm_l60 = t250;
                    } else {
                        gm_toe5fmr3vvf4d t251 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l24) + 4));
                        gm_l25 = t251;
                        gm_toe5fmr3vvf4d t252 = gm_l25;
                        int64_t t253 = gm_f_sys_code_cjtlfo1(t252);
                        gm_tarq9lp8tdk6f t254;
                        GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t254 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t254 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t254 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t254 + 24, 0);
                        ((uint8_t *)&t254)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t254 + 8), t253);
                        memmove(gm_out, ((uint8_t *)&t254), 32);
                        goto gm_fdone;
                    }
                    gm_tsoiri44d0i6q t255 = gm_l60;
                    gm_l26 = t255;
                    {
                        {
                            uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t257 = (uint8_t)(t256 == ((uint8_t)0ULL));
                            if (!t257) {
                                goto gm_lb8;
                            }
                            uint32_t t258 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 4));
                            gm_l27 = t258;
                            uint32_t t259 = gm_l27;
                            int64_t t260 = (int64_t)t259;
                            gm_tarq9lp8tdk6f t261;
                            GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t261 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t261 + 24, 0);
                            ((uint8_t *)&t261)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t261 + 8), t260);
                            gm_l61 = t261;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t263 = (uint8_t)(t262 == ((uint8_t)1ULL));
                            if (!t263) {
                                goto gm_lb9;
                            }
                            int32_t t264 = GM_LD(int32_t, (((uint8_t *)&gm_l26) + 4));
                            gm_l28 = t264;
                            int32_t t265 = gm_l28;
                            int64_t t266 = (int64_t)t265;
                            unsigned __int128 n267 = (unsigned __int128)((uint64_t)32ULL);
                            if (n267 >= 64) gm_trap_shift((uint64_t)n267, "i64", gm_srcn3jjc71a2bv3, 507, 47);
                            int64_t r268 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n267);
                            bool o269 = (int64_t)(r268 >> (int)n267) != ((int64_t)1LL);
                            if (o269) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 507, 47);
                            int64_t t270 = r268;
                            int64_t r271; bool o272 = __builtin_add_overflow(t266, t270, &r271);
                            if (o272) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 507, 35);
                            int64_t t273 = r271;
                            gm_tarq9lp8tdk6f t274;
                            GM_ST(uint8_t, (uint8_t *)&t274 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t274 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t274 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t274 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t274 + 24, 0);
                            ((uint8_t *)&t274)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t274 + 8), t273);
                            gm_l61 = t274;
                            goto gm_lb7;
                        }
gm_lb9:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 505, 20);
                    }
gm_lb7:;
                    memmove(gm_out, ((uint8_t *)&gm_l61), 32);
                    goto gm_fdone;
                }
gm_lb6:;
                {
                    uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t276 = (uint8_t)(t275 == ((uint8_t)43ULL));
                    if (!t276) {
                        goto gm_lb10;
                    }
                    void * t277 = gm_l0;
                    void * t278 = (void *)((((uint8_t *)t277) + 1368) + 224);
                    int64_t t279 = gm_l14;
                    gm_tppo2hmut88dq t280 = gm_f_get_fqooi5f(t278, t279);
                    gm_l62 = t280;
                    uint8_t t281 = (uint8_t)((((uint8_t *)&gm_l62))[8] != 0);
                    if (t281) {
                        gm_tm98ap4112hee t282 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l62));
                        gm_l63 = t282;
                    } else {
                        int64_t t283 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t284;
                        GM_ST(uint8_t, (uint8_t *)&t284 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t284 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t284 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t284 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t284 + 24, 0);
                        ((uint8_t *)&t284)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t284 + 8), t283);
                        memmove(gm_out, ((uint8_t *)&t284), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t285 = gm_l63;
                    gm_l29 = t285;
                    gm_tm98ap4112hee t286 = gm_l29;
                    gm_node *pn287 = gm_find(cx, 2119230);
                    if (!pn287) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 512, 18);
                    gm_tr6norheal11n t288;
                    t288 = ((gm_tr6norheal11n (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn287->clauses[5])(pn287->env, pn287->hctx, pn287, t286);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l30 = t288;
                    uint8_t t289 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (t289) {
                        gm_tqnlpelep1582 t290 = GM_LD(gm_tqnlpelep1582, (((uint8_t *)&gm_l30) + 4));
                        gm_l64 = t290;
                    } else {
                        gm_toe5fmr3vvf4d t291 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l30) + 4));
                        gm_l31 = t291;
                        gm_toe5fmr3vvf4d t292 = gm_l31;
                        int64_t t293 = gm_f_sys_code_cjtlfo1(t292);
                        gm_tarq9lp8tdk6f t294;
                        GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t294 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t294 + 24, 0);
                        ((uint8_t *)&t294)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t294 + 8), t293);
                        memmove(gm_out, ((uint8_t *)&t294), 32);
                        goto gm_fdone;
                    }
                    gm_tqnlpelep1582 t295 = gm_l64;
                    gm_l32 = t295;
                    uint8_t t296 = (uint8_t)((((uint8_t *)&gm_l32))[8] != 0);
                    if (t296) {
                        gm_tsoiri44d0i6q t297 = GM_LD(gm_tsoiri44d0i6q, ((uint8_t *)&gm_l32));
                        gm_l65 = t297;
                    } else {
                        unsigned __int128 n298 = (unsigned __int128)((uint64_t)33ULL);
                        if (n298 >= 64) gm_trap_shift((uint64_t)n298, "i64", gm_srcn3jjc71a2bv3, 514, 39);
                        int64_t r299 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n298);
                        bool o300 = (int64_t)(r299 >> (int)n298) != ((int64_t)1LL);
                        if (o300) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 514, 39);
                        int64_t t301 = r299;
                        gm_tarq9lp8tdk6f t302;
                        GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t302 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t302 + 24, 0);
                        ((uint8_t *)&t302)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t302 + 8), t301);
                        memmove(gm_out, ((uint8_t *)&t302), 32);
                        goto gm_fdone;
                    }
                    gm_tsoiri44d0i6q t303 = gm_l65;
                    gm_l33 = t303;
                    void * t304 = gm_l0;
                    void * t305 = (void *)((((uint8_t *)t304) + 1368) + 224);
                    int64_t t306 = gm_l14;
                    gm_tppo2hmut88dq t307 = gm_f_remove_jvpr521(t305, t306);
                    {
                        {
                            uint8_t t308 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                            uint8_t t309 = (uint8_t)(t308 == ((uint8_t)0ULL));
                            if (!t309) {
                                goto gm_lb12;
                            }
                            uint32_t t310 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 4));
                            gm_l34 = t310;
                            uint32_t t311 = gm_l34;
                            int64_t t312 = (int64_t)t311;
                            gm_tarq9lp8tdk6f t313;
                            GM_ST(uint8_t, (uint8_t *)&t313 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t313 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t313 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t313 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t313 + 24, 0);
                            ((uint8_t *)&t313)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t313 + 8), t312);
                            gm_l66 = t313;
                            goto gm_lb11;
                        }
gm_lb12:;
                        {
                            uint8_t t314 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                            uint8_t t315 = (uint8_t)(t314 == ((uint8_t)1ULL));
                            if (!t315) {
                                goto gm_lb13;
                            }
                            int32_t t316 = GM_LD(int32_t, (((uint8_t *)&gm_l33) + 4));
                            gm_l35 = t316;
                            int32_t t317 = gm_l35;
                            int64_t t318 = (int64_t)t317;
                            unsigned __int128 n319 = (unsigned __int128)((uint64_t)32ULL);
                            if (n319 >= 64) gm_trap_shift((uint64_t)n319, "i64", gm_srcn3jjc71a2bv3, 518, 47);
                            int64_t r320 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n319);
                            bool o321 = (int64_t)(r320 >> (int)n319) != ((int64_t)1LL);
                            if (o321) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 518, 47);
                            int64_t t322 = r320;
                            int64_t r323; bool o324 = __builtin_add_overflow(t318, t322, &r323);
                            if (o324) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 518, 35);
                            int64_t t325 = r323;
                            gm_tarq9lp8tdk6f t326;
                            GM_ST(uint8_t, (uint8_t *)&t326 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t326 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t326 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t326 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t326 + 24, 0);
                            ((uint8_t *)&t326)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&t326 + 8), t325);
                            gm_l66 = t326;
                            goto gm_lb11;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 516, 20);
                    }
gm_lb11:;
                    memmove(gm_out, ((uint8_t *)&gm_l66), 32);
                    goto gm_fdone;
                }
gm_lb10:;
            }
            {
                {
                    uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t328 = (uint8_t)(t327 == ((uint8_t)42ULL));
                    if (!t328) {
                        goto gm_lb14;
                    }
                    void * t329 = gm_l0;
                    void * t330 = (void *)((((uint8_t *)t329) + 1368) + 224);
                    int64_t t331 = gm_l14;
                    gm_tppo2hmut88dq t332 = gm_f_get_fqooi5f(t330, t331);
                    gm_l67 = t332;
                    uint8_t t333 = (uint8_t)((((uint8_t *)&gm_l67))[8] != 0);
                    if (t333) {
                        gm_tm98ap4112hee t334 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l67));
                        gm_l68 = t334;
                    } else {
                        int64_t t335 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        gm_tarq9lp8tdk6f t336;
                        GM_ST(uint8_t, (uint8_t *)&t336 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t336 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t336 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t336 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t336 + 24, 0);
                        ((uint8_t *)&t336)[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&t336 + 8), t335);
                        memmove(gm_out, ((uint8_t *)&t336), 32);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t337 = gm_l68;
                    gm_l36 = t337;
                    gm_tm98ap4112hee t338 = gm_l36;
                    gm_node *pn339 = gm_find(cx, 2119230);
                    if (!pn339) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 523, 29);
                    gm_ttln0d0hd70en t340;
                    t340 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn339->clauses[6])(pn339->env, pn339->hctx, pn339, t338);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t341 = gm_f_sys_unit_nrsqciq(t340);
                    gm_tarq9lp8tdk6f t342;
                    GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t342 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t342 + 24, 0);
                    ((uint8_t *)&t342)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&t342 + 8), t341);
                    memmove(gm_out, ((uint8_t *)&t342), 32);
                    goto gm_fdone;
                }
gm_lb14:;
                {
                    void * t343 = gm_l0;
                    gm_tl874t2atnlh3 t344 = gm_l1;
                    gm_b16_8 t345;
                    GM_ST(void *, (uint8_t *)&t345, (void *)(gm_stfd80s46313k));
                    GM_ST(uintptr_t, (uint8_t *)&t345 + 8, (uintptr_t)(37));
                    gm_t76123gfet73g t346;
                    gm_f_trap_qh84l9d((void *)&t346, (void *)NULL, cx, NULL, t343, t344, t345);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tarq9lp8tdk6f t347;
                    GM_ST(uint8_t, (uint8_t *)&t347 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t347 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t347 + 4, 0);
                    ((uint8_t *)&t347)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t347 + 8), t346);
                    memmove(gm_out, ((uint8_t *)&t347), 32);
                    goto gm_fdone;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 482, 5);
            }
        }
gm_lb2:;
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
inline __attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_read_b0gj9fg F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* vals */
    GM_STACK_CHECK(&F, 583u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_td0ept12e7q8b gm_l5;
    gm_t6j8vdmi81h7c gm_l6;
    gm_tfvg1o4f8mmhi gm_l7;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t5, gm_srcn3jjc71a2bv3, 192, 33);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_sys_dest_rtrb14h((void *)&gm_l5, (void *)NULL, cx, NULL, t2, t3, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t8) {
    } else {
        gm_t76123gfet73g t9 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l5) + 8));
        gm_tarq9lp8tdk6f t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        ((uint8_t *)&t10)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t10 + 8), t9);
        memmove(gm_out, ((uint8_t *)&t10), 32);
        goto gm_fdone;
    }
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    F.l3 = t11;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t12) + 1672) + 8));
    uint8_t t14 = (t13 > ((uint64_t)1ULL));
    if (t14) {
        void * t15 = gm_l0;
        gm_tl874t2atnlh3 t16 = gm_l1;
        gm_b16_8 t17 = F.l3;
        gm_tmvdbo28o1i6h t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
        gm_f_on_host_thread_kmegc07((void *)&gm_l6, (void *)NULL, cx, NULL, t15, t16, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (t19) {
        } else {
            gm_t76123gfet73g t20 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l6) + 8));
            gm_tarq9lp8tdk6f t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t21 + 8), t20);
            memmove(gm_out, ((uint8_t *)&t21), 32);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l6) + 8), 32);
        gm_thf5kr0is9u9i t22 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l7));
        int64_t t23 = gm_f_sys_count_50c5lcp(t22);
        gm_tarq9lp8tdk6f t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
        ((uint8_t *)&t24)[0] = 0;
        GM_ST(int64_t, ((uint8_t *)&t24 + 8), t23);
        memmove(gm_out, ((uint8_t *)&t24), 32);
        goto gm_fdone;
    }
    gm_toe5fmr3vvf4d t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)12ULL));
    gm_thf5kr0is9u9i t26;
    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
    ((uint8_t *)&t26)[0] = 1;
    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t26 + 8), t25);
    F.l4 = t26;
    gm_closure t27 = ((gm_closure){(void *)gm_f_fn_trt04qi, (void *)&F.h, cx});
    gm_f_defaults_p077jgf(t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_thf5kr0is9u9i t28 = F.l4;
    int64_t t29 = gm_f_sys_count_50c5lcp(t28);
    gm_tarq9lp8tdk6f t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t30 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t30 + 24, 0);
    ((uint8_t *)&t30)[0] = 0;
    GM_ST(int64_t, ((uint8_t *)&t30 + 8), t29);
    memmove(gm_out, ((uint8_t *)&t30), 32);
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
extern __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_sys_run_2vqjo3u F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* dst */
    int64_t gm_l6; /* n */
    uint64_t gm_l7; /* now */
    uint64_t gm_l8; /* base */
    gm_b16_8 gm_l9; /* buf */
    gm_b16_8 gm_l10; /* data */
    GM_STACK_CHECK(&F, 2032u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tarq9lp8tdk6f gm_l11;
    gm_t4ce0jd3tdro4 gm_l12;
    uint8_t gm_l13;
    uint64_t gm_l14;
    gm_td0ept12e7q8b gm_l15;
    gm_td0ept12e7q8b gm_l16;
    uint32_t gm_l17;
    gm_tarq9lp8tdk6f gm_l18;
    gm_tarq9lp8tdk6f gm_l19;
    gm_tarq9lp8tdk6f gm_l20;
    {
        gm_l6 = ((int64_t)0LL);
        {
            {
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)0ULL));
                    if (!t63) {
                        goto gm_lb2;
                    }
                    void * t64 = gm_l0;
                    gm_tl874t2atnlh3 t65 = gm_l1;
                    gm_b16_8 t66 = gm_l4;
                    gm_f_sys_read_b0gj9fg((void *)&gm_l11, (void *)NULL, cx, NULL, t64, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (t67) {
                    } else {
                        gm_t76123gfet73g t68 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l11) + 8));
                        gm_tcfa6l7reoosn t69;
                        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                        ((uint8_t *)&t69)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t69 + 8), t68);
                        memmove(gm_out, ((uint8_t *)&t69), 32);
                        goto gm_fdone;
                    }
                    int64_t t70 = GM_LD(int64_t, (((uint8_t *)&gm_l11) + 8));
                    gm_l6 = t70;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)1ULL));
                    if (!t72) {
                        goto gm_lb3;
                    }
                    gm_node *pn73 = gm_find(cx, 1579066750);
                    if (!pn73) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 158, 20);
                    gm_t4ce0jd3tdro4 t74;
                    t74 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn73->clauses[0])(pn73->env, pn73->hctx, pn73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t74;
                    uint64_t t75 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                    gm_l7 = t75;
                    void * t76 = gm_l0;
                    {
                        {
                            uint8_t t77 = (uint8_t)((((((uint8_t *)t76) + 1368) + 288))[8] != 0);
                            if (t77) {
                                goto gm_lb5;
                            }
                            gm_l13 = ((uint8_t)1);
                            goto gm_lb4;
                        }
gm_lb5:;
                        {
                            gm_l13 = ((uint8_t)0);
                            goto gm_lb4;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 159, 16);
                    }
gm_lb4:;
                    uint8_t t78 = gm_l13;
                    if (t78) {
                        void * t79 = gm_l0;
                        uint64_t t80 = gm_l7;
                        gm_tf5srh4mp4b3i t81;
                        GM_ST(uint8_t, (uint8_t *)&t81 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t81 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t81), t80);
                        ((uint8_t *)&t81)[8] = 1;
                        GM_ST(gm_tf5srh4mp4b3i, ((((uint8_t *)t79) + 1368) + 288), t81);
                    }
                    void * t82 = gm_l0;
                    uint8_t t83 = (uint8_t)((((((uint8_t *)t82) + 1368) + 288))[8] != 0);
                    if (t83) {
                        uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t82) + 1368) + 288));
                        gm_l14 = t84;
                    } else {
                        gm_l14 = ((uint64_t)0ULL);
                    }
                    uint64_t t85 = gm_l14;
                    gm_l8 = t85;
                    void * t86 = gm_l0;
                    gm_b16_8 t87 = gm_l5;
                    uint64_t t88 = gm_l7;
                    uint64_t t89 = gm_l8;
                    uint64_t r90; bool o91 = __builtin_sub_overflow(t88, t89, &r90);
                    uint64_t t92 = r90;
                    unsigned __int128 t93 = (unsigned __int128)t92;
                    gm_f_put_uint_cddin99(t86, t87, t93);
                    gm_tcfa6l7reoosn t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t94 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t94 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t94 + 24, 0);
                    ((uint8_t *)&t94)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t94 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t94), 32);
                    goto gm_fdone;
                }
gm_lb3:;
                {
                    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t96 = (uint8_t)(t95 == ((uint8_t)2ULL));
                    if (!t96) {
                        goto gm_lb7;
                    }
                    gm_node *pn97 = gm_find(cx, 1579066750);
                    if (!pn97) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 164, 29);
                    int64_t t98;
                    t98 = ((int64_t (*)(void *, gm_node *, gm_node *))pn97->clauses[1])(pn97->env, pn97->hctx, pn97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l6 = t98;
                    goto gm_lb1;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t100 = (uint8_t)(t99 == ((uint8_t)3ULL));
                    if (!t100) {
                        goto gm_lb8;
                    }
                    void * t101 = gm_l0;
                    gm_tl874t2atnlh3 t102 = gm_l1;
                    void * t103 = gm_l0;
                    void * t104 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t105)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t105, gm_srcn3jjc71a2bv3, 166, 45);
                    gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)t104) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                    unsigned __int128 t108 = gm_f_get_uint_bq8c4b7(t103, t107);
                    uint64_t t109 = (uint64_t)t108;
                    gm_f_sleep_gqtie06((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t101, t102, t109);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb8:;
                {
                    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)4ULL));
                    if (!t111) {
                        goto gm_lb9;
                    }
                    void * t112 = gm_l0;
                    gm_tl874t2atnlh3 t113 = gm_l1;
                    void * t114 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcn3jjc71a2bv3, 169, 41);
                    gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                    gm_f_sys_dest_rtrb14h((void *)&gm_l15, (void *)NULL, cx, NULL, t112, t113, t117);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                    if (t118) {
                    } else {
                        gm_t76123gfet73g t119 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l15) + 8));
                        gm_tcfa6l7reoosn t120;
                        GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                        ((uint8_t *)&t120)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t120 + 8), t119);
                        memmove(gm_out, ((uint8_t *)&t120), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                    gm_l9 = t121;
                    gm_b16_8 t122 = gm_l9;
                    gm_node *pn123 = gm_find(cx, 1087599154);
                    if (!pn123) gm_trap("no handler installed for effect `Rand`", gm_srcn3jjc71a2bv3, 170, 13);
                    gm_b0_1 t124;
                    t124 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn123->clauses[0])(pn123->env, pn123->hctx, pn123, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t125;
                    GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t125 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t125 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
                    ((uint8_t *)&t125)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t125 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t125), 32);
                    goto gm_fdone;
                }
gm_lb9:;
                {
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)54ULL));
                    if (!t127) {
                        goto gm_lb10;
                    }
                    void * t128 = gm_l0;
                    gm_tl874t2atnlh3 t129 = gm_l1;
                    void * t130 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t131)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t131, gm_srcn3jjc71a2bv3, 174, 41);
                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)t130) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                    gm_f_sys_src_5svsr6i((void *)&gm_l16, (void *)NULL, cx, NULL, t128, t129, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                    if (t134) {
                    } else {
                        gm_t76123gfet73g t135 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l16) + 8));
                        gm_tcfa6l7reoosn t136;
                        GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                        ((uint8_t *)&t136)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t136 + 8), t135);
                        memmove(gm_out, ((uint8_t *)&t136), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                    gm_l10 = t137;
                    void * t138 = gm_l0;
                    void * t139 = gm_l0;
                    void * t140 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t141, gm_srcn3jjc71a2bv3, 175, 40);
                    void * t142 = GM_LD(void *, (((uint8_t *)t140) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t142) + 56));
                    void * t144 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t145)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t145, gm_srcn3jjc71a2bv3, 175, 52);
                    gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)t144) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                    int64_t t148 = gm_f_sys_int_32e79ed(t139, GM_CONV(gm_csguankko4lmhd, t143), t147);
                    uint8_t t149 = ((t148 == ((int64_t)2LL)));
                    if (t149) {
                        gm_l17 = ((uint32_t)2ULL);
                    } else {
                        gm_l17 = ((uint32_t)1ULL);
                    }
                    uint32_t t150 = gm_l17;
                    gm_b16_8 t151 = gm_l10;
                    gm_b0_1 t152 = gm_f_host_write_r26no87((void *)NULL, cx, NULL, t138, t150, t151);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t153;
                    GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t153 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t153 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t153 + 24, 0);
                    ((uint8_t *)&t153)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t153 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t153), 32);
                    goto gm_fdone;
                }
gm_lb10:;
            }
            {
                {
                    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t155 = (uint8_t)(t154 == ((uint8_t)53ULL));
                    if (!t155) {
                        goto gm_lb11;
                    }
                    uint32_t t156 = gm_f_cpu_count_0o5882g();
                    int64_t t157 = (int64_t)t156;
                    gm_l6 = t157;
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    {
                        {
                            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t159 = (uint8_t)(t158 == ((uint8_t)5ULL));
                            if (!t159) {
                                goto gm_lb14;
                            }
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t161 = (uint8_t)(t160 == ((uint8_t)6ULL));
                            if (!t161) {
                                goto gm_lb15;
                            }
                            goto gm_lb13;
                        }
gm_lb15:;
                        {
                            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t163 = (uint8_t)(t162 == ((uint8_t)7ULL));
                            if (!t163) {
                                goto gm_lb16;
                            }
                            goto gm_lb13;
                        }
gm_lb16:;
                        {
                            uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t165 = (uint8_t)(t164 == ((uint8_t)8ULL));
                            if (!t165) {
                                goto gm_lb17;
                            }
                            goto gm_lb13;
                        }
gm_lb17:;
                        {
                            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t167 = (uint8_t)(t166 == ((uint8_t)9ULL));
                            if (!t167) {
                                goto gm_lb18;
                            }
                            goto gm_lb13;
                        }
gm_lb18:;
                        {
                            uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t169 = (uint8_t)(t168 == ((uint8_t)10ULL));
                            if (!t169) {
                                goto gm_lb19;
                            }
                            goto gm_lb13;
                        }
gm_lb19:;
                        {
                            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t171 = (uint8_t)(t170 == ((uint8_t)11ULL));
                            if (!t171) {
                                goto gm_lb20;
                            }
                            goto gm_lb13;
                        }
gm_lb20:;
                        {
                            uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t173 = (uint8_t)(t172 == ((uint8_t)12ULL));
                            if (!t173) {
                                goto gm_lb21;
                            }
                            goto gm_lb13;
                        }
gm_lb21:;
                        {
                            uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t175 = (uint8_t)(t174 == ((uint8_t)13ULL));
                            if (!t175) {
                                goto gm_lb22;
                            }
                            goto gm_lb13;
                        }
gm_lb22:;
                        {
                            uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t177 = (uint8_t)(t176 == ((uint8_t)14ULL));
                            if (!t177) {
                                goto gm_lb23;
                            }
                            goto gm_lb13;
                        }
gm_lb23:;
                        {
                            uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t179 = (uint8_t)(t178 == ((uint8_t)15ULL));
                            if (!t179) {
                                goto gm_lb24;
                            }
                            goto gm_lb13;
                        }
gm_lb24:;
                        {
                            uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t181 = (uint8_t)(t180 == ((uint8_t)16ULL));
                            if (!t181) {
                                goto gm_lb25;
                            }
                            goto gm_lb13;
                        }
gm_lb25:;
                        {
                            uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t183 = (uint8_t)(t182 == ((uint8_t)17ULL));
                            if (!t183) {
                                goto gm_lb26;
                            }
                            goto gm_lb13;
                        }
gm_lb26:;
                        {
                            uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t185 = (uint8_t)(t184 == ((uint8_t)18ULL));
                            if (!t185) {
                                goto gm_lb27;
                            }
                            goto gm_lb13;
                        }
gm_lb27:;
                        {
                            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t187 = (uint8_t)(t186 == ((uint8_t)19ULL));
                            if (!t187) {
                                goto gm_lb28;
                            }
                            goto gm_lb13;
                        }
gm_lb28:;
                        {
                            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t189 = (uint8_t)(t188 == ((uint8_t)20ULL));
                            if (!t189) {
                                goto gm_lb29;
                            }
                            goto gm_lb13;
                        }
gm_lb29:;
                        {
                            uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t191 = (uint8_t)(t190 == ((uint8_t)21ULL));
                            if (!t191) {
                                goto gm_lb30;
                            }
                            goto gm_lb13;
                        }
gm_lb30:;
                        {
                            uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t193 = (uint8_t)(t192 == ((uint8_t)22ULL));
                            if (!t193) {
                                goto gm_lb31;
                            }
                            goto gm_lb13;
                        }
gm_lb31:;
                        {
                            uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t195 = (uint8_t)(t194 == ((uint8_t)23ULL));
                            if (!t195) {
                                goto gm_lb32;
                            }
                            goto gm_lb13;
                        }
gm_lb32:;
                        {
                            uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t197 = (uint8_t)(t196 == ((uint8_t)24ULL));
                            if (!t197) {
                                goto gm_lb33;
                            }
                            goto gm_lb13;
                        }
gm_lb33:;
                        {
                            uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t199 = (uint8_t)(t198 == ((uint8_t)25ULL));
                            if (!t199) {
                                goto gm_lb34;
                            }
                            goto gm_lb13;
                        }
gm_lb34:;
                        {
                            uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t201 = (uint8_t)(t200 == ((uint8_t)26ULL));
                            if (!t201) {
                                goto gm_lb35;
                            }
                            goto gm_lb13;
                        }
gm_lb35:;
                        goto gm_lb12;
                    }
gm_lb13:;
                    void * t202 = gm_l0;
                    gm_tl874t2atnlh3 t203 = gm_l1;
                    gm_t9qkh0rsooc8d t204 = gm_l2;
                    gm_b16_8 t205 = gm_l3;
                    gm_b16_8 t206 = gm_l4;
                    gm_f_sys_fs_u0grkf6((void *)&gm_l18, (void *)NULL, cx, NULL, t202, t203, t204, t205, t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (t207) {
                    } else {
                        gm_t76123gfet73g t208 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l18) + 8));
                        gm_tcfa6l7reoosn t209;
                        GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                        ((uint8_t *)&t209)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t209 + 8), t208);
                        memmove(gm_out, ((uint8_t *)&t209), 32);
                        goto gm_fdone;
                    }
                    int64_t t210 = GM_LD(int64_t, (((uint8_t *)&gm_l18) + 8));
                    gm_l6 = t210;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    {
                        {
                            uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t212 = (uint8_t)(t211 == ((uint8_t)27ULL));
                            if (!t212) {
                                goto gm_lb38;
                            }
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t214 = (uint8_t)(t213 == ((uint8_t)28ULL));
                            if (!t214) {
                                goto gm_lb39;
                            }
                            goto gm_lb37;
                        }
gm_lb39:;
                        {
                            uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t216 = (uint8_t)(t215 == ((uint8_t)29ULL));
                            if (!t216) {
                                goto gm_lb40;
                            }
                            goto gm_lb37;
                        }
gm_lb40:;
                        {
                            uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t218 = (uint8_t)(t217 == ((uint8_t)30ULL));
                            if (!t218) {
                                goto gm_lb41;
                            }
                            goto gm_lb37;
                        }
gm_lb41:;
                        {
                            uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t220 = (uint8_t)(t219 == ((uint8_t)31ULL));
                            if (!t220) {
                                goto gm_lb42;
                            }
                            goto gm_lb37;
                        }
gm_lb42:;
                        {
                            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t222 = (uint8_t)(t221 == ((uint8_t)32ULL));
                            if (!t222) {
                                goto gm_lb43;
                            }
                            goto gm_lb37;
                        }
gm_lb43:;
                        {
                            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t224 = (uint8_t)(t223 == ((uint8_t)33ULL));
                            if (!t224) {
                                goto gm_lb44;
                            }
                            goto gm_lb37;
                        }
gm_lb44:;
                        {
                            uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t226 = (uint8_t)(t225 == ((uint8_t)34ULL));
                            if (!t226) {
                                goto gm_lb45;
                            }
                            goto gm_lb37;
                        }
gm_lb45:;
                        {
                            uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t228 = (uint8_t)(t227 == ((uint8_t)35ULL));
                            if (!t228) {
                                goto gm_lb46;
                            }
                            goto gm_lb37;
                        }
gm_lb46:;
                        {
                            uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t230 = (uint8_t)(t229 == ((uint8_t)36ULL));
                            if (!t230) {
                                goto gm_lb47;
                            }
                            goto gm_lb37;
                        }
gm_lb47:;
                        goto gm_lb36;
                    }
gm_lb37:;
                    void * t231 = gm_l0;
                    gm_tl874t2atnlh3 t232 = gm_l1;
                    gm_t9qkh0rsooc8d t233 = gm_l2;
                    gm_b16_8 t234 = gm_l3;
                    gm_b16_8 t235 = gm_l4;
                    gm_f_sys_net_aff7h5l((void *)&gm_l19, (void *)NULL, cx, NULL, t231, t232, t233, t234, t235);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t236 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (t236) {
                    } else {
                        gm_t76123gfet73g t237 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l19) + 8));
                        gm_tcfa6l7reoosn t238;
                        GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                        ((uint8_t *)&t238)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t238 + 8), t237);
                        memmove(gm_out, ((uint8_t *)&t238), 32);
                        goto gm_fdone;
                    }
                    int64_t t239 = GM_LD(int64_t, (((uint8_t *)&gm_l19) + 8));
                    gm_l6 = t239;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    {
                        {
                            uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t241 = (uint8_t)(t240 == ((uint8_t)37ULL));
                            if (!t241) {
                                goto gm_lb50;
                            }
                            goto gm_lb49;
                        }
gm_lb50:;
                        {
                            uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t243 = (uint8_t)(t242 == ((uint8_t)38ULL));
                            if (!t243) {
                                goto gm_lb51;
                            }
                            goto gm_lb49;
                        }
gm_lb51:;
                        {
                            uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t245 = (uint8_t)(t244 == ((uint8_t)39ULL));
                            if (!t245) {
                                goto gm_lb52;
                            }
                            goto gm_lb49;
                        }
gm_lb52:;
                        {
                            uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t247 = (uint8_t)(t246 == ((uint8_t)40ULL));
                            if (!t247) {
                                goto gm_lb53;
                            }
                            goto gm_lb49;
                        }
gm_lb53:;
                        {
                            uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t249 = (uint8_t)(t248 == ((uint8_t)41ULL));
                            if (!t249) {
                                goto gm_lb54;
                            }
                            goto gm_lb49;
                        }
gm_lb54:;
                        {
                            uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t251 = (uint8_t)(t250 == ((uint8_t)43ULL));
                            if (!t251) {
                                goto gm_lb55;
                            }
                            goto gm_lb49;
                        }
gm_lb55:;
                        {
                            uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t253 = (uint8_t)(t252 == ((uint8_t)42ULL));
                            if (!t253) {
                                goto gm_lb56;
                            }
                            goto gm_lb49;
                        }
gm_lb56:;
                        goto gm_lb48;
                    }
gm_lb49:;
                    void * t254 = gm_l0;
                    gm_tl874t2atnlh3 t255 = gm_l1;
                    gm_t9qkh0rsooc8d t256 = gm_l2;
                    gm_b16_8 t257 = gm_l3;
                    gm_b16_8 t258 = gm_l4;
                    gm_f_sys_proc_jhdp277((void *)&gm_l20, (void *)NULL, cx, NULL, t254, t255, t256, t257, t258);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t259 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (t259) {
                    } else {
                        gm_t76123gfet73g t260 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l20) + 8));
                        gm_tcfa6l7reoosn t261;
                        GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                        ((uint8_t *)&t261)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t261 + 8), t260);
                        memmove(gm_out, ((uint8_t *)&t261), 32);
                        goto gm_fdone;
                    }
                    int64_t t262 = GM_LD(int64_t, (((uint8_t *)&gm_l20) + 8));
                    gm_l6 = t262;
                    goto gm_lb1;
                }
gm_lb48:;
            }
            {
                {
                    uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t264 = (uint8_t)(t263 == ((uint8_t)44ULL));
                    if (!t264) {
                        goto gm_lb57;
                    }
                    void * t265 = gm_l0;
                    gm_tl874t2atnlh3 t266 = gm_l1;
                    gm_t9qkh0rsooc8d t267;
                    GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)2ULL));
                    gm_b16_8 t268 = gm_f_empty_slice_2p7d2kv();
                    gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
                    gm_b16_8 t270 = gm_l5;
                    gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t265, t266, t267, ((uint32_t)0ULL), t269, t270);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb57:;
                {
                    uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t272 = (uint8_t)(t271 == ((uint8_t)45ULL));
                    if (!t272) {
                        goto gm_lb58;
                    }
                    void * t273 = gm_l0;
                    gm_tl874t2atnlh3 t274 = gm_l1;
                    gm_b16_8 t275 = gm_l4;
                    gm_b16_8 t276 = gm_l5;
                    gm_t9qkh0rsooc8d t277;
                    GM_ST(uint8_t, ((uint8_t *)&t277 + 0), ((uint8_t)2ULL));
                    gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t273, t274, t277, ((uint32_t)1ULL), t275, t276);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb58:;
                {
                    uint8_t t278 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t279 = (uint8_t)(t278 == ((uint8_t)46ULL));
                    if (!t279) {
                        goto gm_lb59;
                    }
                    void * t280 = gm_l0;
                    void * t281 = gm_l0;
                    void * t282 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t283)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t283, gm_srcn3jjc71a2bv3, 184, 52);
                    gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)t282) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
                    unsigned __int128 t286 = gm_f_get_uint_bq8c4b7(t281, t285);
                    uint8_t t287 = (uint8_t)t286;
                    gm_f_env_exit_prbvpjt((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t280, t287);
                    goto gm_fdone;
                }
gm_lb59:;
                {
                    void * t288 = gm_l0;
                    gm_tl874t2atnlh3 t289 = gm_l1;
                    gm_b16_8 t290;
                    GM_ST(void *, (uint8_t *)&t290, (void *)(gm_s27n8348bef64));
                    GM_ST(uintptr_t, (uint8_t *)&t290 + 8, (uintptr_t)(62));
                    gm_t76123gfet73g t291;
                    gm_f_trap_qh84l9d((void *)&t291, (void *)NULL, cx, NULL, t288, t289, t290);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t292;
                    GM_ST(uint8_t, (uint8_t *)&t292 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t292 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t292 + 4, 0);
                    ((uint8_t *)&t292)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t292 + 8), t291);
                    memmove(gm_out, ((uint8_t *)&t292), 32);
                    goto gm_fdone;
                }
gm_lb60:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 155, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t293 = gm_l0;
        gm_b16_8 t294 = gm_l5;
        int64_t t295 = gm_l6;
        gm_f_sys_ret_vpa2k8i(t293, t294, t295);
        gm_tcfa6l7reoosn t296;
        GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t296 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t296 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t296 + 24, 0);
        ((uint8_t *)&t296)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t296 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t296), 32);
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
inline __attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_src_5svsr6i F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 377u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t934k1j7u70ct gm_l6;
    gm_b16_8 gm_l7;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
    gm_l3 = t4;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 184);
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    gm_f_read_u6r4vns((void *)&gm_l6, t6, t7, t8, ((uint64_t)1ULL));
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_l7 = t10;
    } else {
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l6) + 8), 40);
        void * t11 = gm_l0;
        gm_tl874t2atnlh3 t12 = gm_l1;
        gm_t76123gfet73g t13;
        gm_f_mem_trap_ecogko0((void *)&t13, (void *)NULL, cx, NULL, t11, t12, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_td0ept12e7q8b t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        ((uint8_t *)&t14)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), t13);
        memmove(gm_out, ((uint8_t *)&t14), 32);
        goto gm_fdone;
    }
    gm_b16_8 t15 = gm_l7;
    gm_l5 = t15;
    gm_b16_8 t16 = gm_l5;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
    gm_td0ept12e7q8b t18;
    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
    ((uint8_t *)&t18)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
    memmove(gm_out, ((uint8_t *)&t18), 32);
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
inline __attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_sys_state_cvl1lvl F;
    GM_STACK_CHECK(&F, 600u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t2;
    gm_f_hash_map_3sndrtb((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t3;
    gm_f_hash_map_irh77t1((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t4;
    gm_f_hash_map_abi2bn7((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_ql49boo((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t6;
    gm_f_hash_map_3d5nsdr((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tf5srh4mp4b3i t7;
    memset(&t7, 0, sizeof t7);
    gm_ttse10gjbhca2 t8;
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t8 + 0), t2);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t8 + 56), t3);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t8 + 112), t4);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t8 + 168), t5);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t8 + 224), t6);
    GM_ST(int64_t, ((uint8_t *)&t8 + 280), ((int64_t)0LL));
    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t8 + 288), t7);
    memmove(gm_out, ((uint8_t *)&t8), 304);
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_strings_r85ragc F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    uint64_t gm_l4; /* ps */
    gm_b16_8 gm_l6; /* items */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* s */
    GM_STACK_CHECK(&F, 932u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t934k1j7u70ct gm_l12;
    gm_b16_8 gm_l13;
    gm_td0ept12e7q8b gm_l14;
    gm_tftb1odc739t5 gm_l15;
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l2;
    gm_t2lqu8jsj511d t9 = gm_f_slice_parts_1se5hu4(t7, t8);
    gm_l3 = t9;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 176));
    uint64_t t12 = gm_f_us_cuv01fp(t11);
    gm_l4 = t12;
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 184);
    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t17 = gm_l4;
    uint64_t r18; bool o19 = __builtin_mul_overflow(((uint64_t)2ULL), t17, &r18);
    if (o19) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 143, 47);
    uint64_t t20 = r18;
    uint64_t t21 = (uint64_t)t20;
    uint64_t r22; bool o23 = __builtin_mul_overflow(t16, t21, &r22);
    uint64_t t24 = o23 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r22;
    gm_f_read_u6r4vns((void *)&gm_l12, t14, t15, t24, ((uint64_t)1ULL));
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t25) {
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_l13 = t26;
    } else {
        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l12) + 8), 40);
        void * t27 = gm_l0;
        gm_tl874t2atnlh3 t28 = gm_l1;
        gm_t76123gfet73g t29;
        gm_f_mem_trap_ecogko0((void *)&t29, (void *)NULL, cx, NULL, t27, t28, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9922qjstblh1 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 40, 0);
        ((uint8_t *)&t30)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t30 + 8), t29);
        memmove(gm_out, ((uint8_t *)&t30), 48);
        goto gm_fdone;
    }
    gm_b16_8 t31 = gm_l13;
    gm_l6 = t31;
    gm_trqfvmjtt4sij t32;
    gm_f_list_epvducg((void *)&t32, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t32;
    gm_l9 = ((uint64_t)0ULL);
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t34 = gm_f_us_cuv01fp(t33);
    gm_l8 = t34;
    {
        for (;;) {
            uint64_t t35 = gm_l9;
            uint64_t t36 = gm_l8;
            uint8_t t37 = (t35 < t36);
            if (!t37) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t38 = gm_l9;
                        gm_l10 = t38;
                        void * t39 = gm_l0;
                        gm_tl874t2atnlh3 t40 = gm_l1;
                        void * t41 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t43 = gm_l10;
                        uint64_t r44; bool o45 = __builtin_mul_overflow(t43, ((uint64_t)2ULL), &r44);
                        if (o45) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t46 = r44;
                        uint64_t t47 = gm_l4;
                        uint64_t r48; bool o49 = __builtin_mul_overflow(t46, t47, &r48);
                        if (o49) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t50 = r48;
                        uint64_t t51 = gm_l10;
                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                        if (o53) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 146, 53);
                        uint64_t t54 = r52;
                        uint64_t r55; bool o56 = __builtin_mul_overflow(t54, ((uint64_t)2ULL), &r55);
                        if (o56) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t57 = r55;
                        uint64_t t58 = gm_l4;
                        uint64_t r59; bool o60 = __builtin_mul_overflow(t57, t58, &r59);
                        if (o60) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t61 = r59;
                        uint64_t t62 = (uintptr_t)t50;
                        uint64_t t63 = (uintptr_t)t61;
                        if (t62 > t63 || t63 > (uintptr_t)t42) gm_trap_slice(t62, t63, t42, gm_srcn3jjc71a2bv3, 146, 34);
                        uint64_t t64 = ((uintptr_t)t61) - (uintptr_t)t50;
                        gm_b16_8 t65;
                        GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t41 + (uintptr_t)t50 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64));
                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                        gm_f_sys_src_5svsr6i((void *)&gm_l14, (void *)NULL, cx, NULL, t39, t40, t66);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (t67) {
                        } else {
                            gm_t76123gfet73g t68 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l14) + 8));
                            gm_t9922qjstblh1 t69;
                            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t69 + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&t69 + 40, 0);
                            ((uint8_t *)&t69)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t69 + 8), t68);
                            memmove(gm_out, ((uint8_t *)&t69), 48);
                            goto gm_fdone;
                        }
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                        gm_l11 = t70;
                        void * t71 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t72 = gm_l11;
                        gm_tftb1odc739t5 t73 = gm_f_push_3ef746b(t71, t72);
                        gm_l15 = t73;
                        uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcn3jjc71a2bv3, 147, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 145, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t75 = gm_l9;
            uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
            uint64_t t78 = r76;
            gm_l9 = t78;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t79 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l7));
    gm_t9922qjstblh1 t80;
    GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
    ((uint8_t *)&t80)[0] = 0;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t80 + 8), t79);
    memmove(gm_out, ((uint8_t *)&t80), 48);
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);
