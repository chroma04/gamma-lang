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
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
struct gm_fr_describe_rhl0asi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_format_6o2k0r9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g23e5qn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h4jcodi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_njg1saa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r31rqga {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_punct_fdeminj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keyword_from_1ofmh1u {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_keyword_text_149oqn4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_punct_text_ov4r40u {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_rhl0asi(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6o2k0r9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_g23e5qn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_njg1saa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r31rqga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_keyword_from_1ofmh1u(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s0eemp7of8buc[3];
extern const uint8_t gm_s0s4ef50964r9[3];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s1dbslfck7laf[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s277jq8ulrihr[7];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s33pl04oikskr[5];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3bkmscn5fi56[3];
extern const uint8_t gm_s4fd35uidvgkn[4];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5mu2shl2bmft[14];
extern const uint8_t gm_s5rv6umk4akl0[6];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_s6aiq6b6gsv1o[18];
extern const uint8_t gm_s6lcmor84nt1l[4];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s8av3ru5o85v4[6];
extern const uint8_t gm_s8m5q4i8uoe5j[6];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s97bho1civrt3[16];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_s9s0gbnmtpvjl[4];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sabc7m3rug04j[3];
extern const uint8_t gm_sad0udrpgk27o[3];
extern const uint8_t gm_saimepa8bfkkm[3];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_sbjiv038rtlcj[4];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_scc8p0a9eachg[4];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd7p31mtitiq9[3];
extern const uint8_t gm_sdavnmflj0bfl[4];
extern const uint8_t gm_sde2pekt9oc5v[4];
extern const uint8_t gm_sdeq555foclko[3];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfg1e0lbt8sf5[3];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgemh066sqogf[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shtiokhdiebr1[3];
extern const uint8_t gm_shttb5kqse295[12];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_si34mhnsv5dtr[8];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sidtn67sdk4e0[7];
extern const uint8_t gm_simovkrp5fi0r[16];
extern const uint8_t gm_sj0kptrfbec99[3];
extern const uint8_t gm_sj5j224m4ulf9[3];
extern const uint8_t gm_sjfg2291kjugh[4];
extern const uint8_t gm_sjid61i849qic[3];
extern const uint8_t gm_sjjig60ja37ka[3];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skkvljkq130ke[15];
extern const uint8_t gm_skm1g73974f87[4];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skriisqpofdin[3];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl4co37h2cmmb[3];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_sll7gknpe999d[4];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_slpo22sjs83b1[5];
extern const uint8_t gm_sm3gke457vdm3[5];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_smbls870q1nm0[16];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smu48daue2qai[6];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_snb8htg7a7p5u[16];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_sp3rdossibc7f[4];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_sq1oij2hrovtv[4];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr31o7m0aohvd[9];
extern const char gm_src41vqrhgekac9[];
extern const uint8_t gm_srcsoo8lineh7[3];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_ss6hmjpokkg23[13];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_su9718bmbci33[4];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const uint8_t gm_svkleo8hhv87n[4];
extern const uint8_t gm_svm0rfaqk3b2q[4];
extern const uint8_t gm_svqdrhms9i231[7];
extern const gm_tmpl gm_tpl5q9oi9aqgs43;
extern const gm_tmpl gm_tpls6cg1uub8gfo;
extern const gm_tmpl gm_tplsbqgr9k4itq0;
extern const gm_tmpl gm_tplu0ijdfgp8vs3;
extern const gm_tmpl gm_tplvqa6aqf5q6ij;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_format_6o2k0r9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6o2k0r9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snb8htg7a7p5u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvqa6aqf5q6ij), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g23e5qn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_g23e5qn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss6hmjpokkg23));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5q9oi9aqgs43), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h4jcodi F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqa5rf17c57e4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsbqgr9k4itq0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_njg1saa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_njg1saa F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s97bho1civrt3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu0ijdfgp8vs3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r31rqga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r31rqga F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_simovkrp5fi0r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls6cg1uub8gfo), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_fdeminj F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint8_t gm_l3;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)8ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t7;
            gm_t9qkh0rsooc8d t8 = gm_l2;
            gm_t9qkh0rsooc8d t9 = gm_l1;
            uint8_t t10 = (gm_eqv3tj1pu43g(((uint8_t *)&t8), ((uint8_t *)&t9)));
            gm_l3 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 308, 51);
    }
gm_lb1:;
    uint8_t t11 = gm_l3;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_rhl0asi(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_describe_rhl0asi F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_b16_8 gm_l1; /* s */
    gm_t9qkh0rsooc8d gm_l2; /* kw */
    void * gm_l3; /* l */
    gm_b16_8 gm_l4; /* s */
    gm_b16_8 gm_l5; /* s */
    gm_t9qkh0rsooc8d gm_l6; /* p */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_b16_8 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    {
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
            if (!t15) {
                goto gm_lb2;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l1 = t16;
            gm_b16_8 t17 = gm_l1;
            gm_tnpifv20irnph t18;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
            gm_f_format_6o2k0r9((void *)&gm_l8, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src41vqrhgekac9, 315, 18);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_l7 = t21;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)1ULL));
            if (!t23) {
                goto gm_lb3;
            }
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s6lcmor84nt1l));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            gm_l7 = t24;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
            if (!t26) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t27;
            gm_t9qkh0rsooc8d t28 = gm_l2;
            gm_b16_8 t29 = gm_f_keyword_text_149oqn4(t28);
            gm_tnpifv20irnph t30;
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
            gm_f_format_g23e5qn((void *)&gm_l9, (void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src41vqrhgekac9, 317, 21);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_l7 = t33;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)3ULL));
            if (!t35) {
                goto gm_lb5;
            }
            void * t36 = GM_LD(void *, (((uint8_t *)a0) + 8));
            gm_l3 = t36;
            void * t37 = gm_l3;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + 60));
            if (t38) {
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s6aiq6b6gsv1o));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(17));
                gm_l10 = t39;
            } else {
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_smbls870q1nm0));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(15));
                gm_l10 = t40;
            }
            gm_b16_8 t41 = gm_l10;
            gm_l7 = t41;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)4ULL));
            if (!t43) {
                goto gm_lb6;
            }
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s5mu2shl2bmft));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(13));
            gm_l7 = t44;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)5ULL));
            if (!t46) {
                goto gm_lb7;
            }
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_skkvljkq130ke));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(14));
            gm_l7 = t47;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)6ULL));
            if (!t49) {
                goto gm_lb8;
            }
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l4 = t50;
            gm_b16_8 t51 = gm_l4;
            gm_tnpifv20irnph t52;
            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t51);
            gm_f_format_njg1saa((void *)&gm_l11, (void *)NULL, cx, NULL, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src41vqrhgekac9, 321, 22);
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
            gm_l7 = t55;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)7ULL));
            if (!t57) {
                goto gm_lb9;
            }
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l5 = t58;
            gm_b16_8 t59 = gm_l5;
            gm_tnpifv20irnph t60;
            GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
            gm_f_format_r31rqga((void *)&gm_l12, (void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src41vqrhgekac9, 322, 22);
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            gm_l7 = t63;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)8ULL));
            if (!t65) {
                goto gm_lb10;
            }
            gm_t9qkh0rsooc8d t66 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l6 = t66;
            gm_t9qkh0rsooc8d t67 = gm_l6;
            gm_b16_8 t68 = gm_f_punct_text_ov4r40u(t67);
            gm_tnpifv20irnph t69;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
            gm_f_format_h4jcodi((void *)&gm_l13, (void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src41vqrhgekac9, 323, 18);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_l7 = t72;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)9ULL));
            if (!t74) {
                goto gm_lb11;
            }
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_si34mhnsv5dtr));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(7));
            gm_l7 = t75;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)10ULL));
            if (!t77) {
                goto gm_lb12;
            }
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_shttb5kqse295));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(11));
            gm_l7 = t78;
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 314, 59);
    }
gm_lb1:;
    gm_b16_8 t79 = gm_l7;
    F.ret = t79;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1) { return gm_f_is_punct_fdeminj(&a0, a1); }
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_keyword_from_1ofmh1u(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keyword_from_1ofmh1u F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 1298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_taari1s0m51a7 gm_l2;
    gm_taari1s0m51a7 gm_l3;
    gm_taari1s0m51a7 gm_l4;
    gm_taari1s0m51a7 gm_l5;
    gm_taari1s0m51a7 gm_l6;
    gm_taari1s0m51a7 gm_l7;
    gm_taari1s0m51a7 gm_l8;
    gm_taari1s0m51a7 gm_l9;
    gm_taari1s0m51a7 gm_l10;
    {
        uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t64 = (t63 < ((uint64_t)2ULL));
        if (t64) {
            gm_l1 = ((uint8_t)1);
        } else {
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t66 = (t65 > ((uint64_t)8ULL));
            gm_l1 = t66;
        }
        uint8_t t67 = gm_l1;
        if (t67) {
            gm_taari1s0m51a7 t68;
            memset(&t68, 0, sizeof t68);
            F.ret = t68;
            goto gm_fret;
        }
        void * t69 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t70, gm_src41vqrhgekac9, 19, 11);
        {
            {
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t72 = ((t71 == ((uint8_t)97ULL)));
                    if (!t72) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t73 = gm_l0;
                    gm_b16_8 t74;
                    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s920dpvvv6cej));
                    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(3));
                    uint8_t t75 = (gm_eq0iqiubfm7e(((uint8_t *)&t73), ((uint8_t *)&t74)));
                    if (t75) {
                        gm_t9qkh0rsooc8d t76;
                        GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)0ULL));
                        gm_taari1s0m51a7 t77;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t77), t76);
                        ((uint8_t *)&t77)[1] = 1;
                        F.ret = t77;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t79 = ((t78 == ((uint8_t)98ULL)));
                    if (!t79) {
                        goto gm_lb3;
                    }
                    gm_b16_8 t80 = gm_l0;
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_s8av3ru5o85v4));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(5));
                    uint8_t t82 = (gm_eq0iqiubfm7e(((uint8_t *)&t80), ((uint8_t *)&t81)));
                    if (t82) {
                        gm_t9qkh0rsooc8d t83;
                        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)1ULL));
                        gm_taari1s0m51a7 t84;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t84), t83);
                        ((uint8_t *)&t84)[1] = 1;
                        F.ret = t84;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t86 = ((t85 == ((uint8_t)99ULL)));
                    if (!t86) {
                        goto gm_lb4;
                    }
                    {
                        {
                            gm_b16_8 t87 = gm_l0;
                            gm_b16_8 t88;
                            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_si1m7q6ia0hto));
                            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
                            uint8_t t89 = (gm_eq0iqiubfm7e(((uint8_t *)&t87), ((uint8_t *)&t88)));
                            if (!t89) {
                                goto gm_lb6;
                            }
                            gm_t9qkh0rsooc8d t90;
                            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)2ULL));
                            gm_taari1s0m51a7 t91;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t91), t90);
                            ((uint8_t *)&t91)[1] = 1;
                            gm_l2 = t91;
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_b16_8 t92 = gm_l0;
                            gm_b16_8 t93;
                            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sr31o7m0aohvd));
                            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(8));
                            uint8_t t94 = (gm_eq0iqiubfm7e(((uint8_t *)&t92), ((uint8_t *)&t93)));
                            if (!t94) {
                                goto gm_lb7;
                            }
                            gm_t9qkh0rsooc8d t95;
                            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)3ULL));
                            gm_taari1s0m51a7 t96;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t96), t95);
                            ((uint8_t *)&t96)[1] = 1;
                            gm_l2 = t96;
                            goto gm_lb5;
                        }
gm_lb7:;
                        {
                            gm_b16_8 t97 = gm_l0;
                            gm_b16_8 t98;
                            GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s66uvortgrtsg));
                            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(3));
                            uint8_t t99 = (gm_eq0iqiubfm7e(((uint8_t *)&t97), ((uint8_t *)&t98)));
                            if (!t99) {
                                goto gm_lb8;
                            }
                            gm_t9qkh0rsooc8d t100;
                            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)4ULL));
                            gm_taari1s0m51a7 t101;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t101), t100);
                            ((uint8_t *)&t101)[1] = 1;
                            gm_l2 = t101;
                            goto gm_lb5;
                        }
gm_lb8:;
                        {
                            gm_taari1s0m51a7 t102;
                            memset(&t102, 0, sizeof t102);
                            gm_l2 = t102;
                            goto gm_lb5;
                        }
gm_lb9:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 22, 23);
                    }
gm_lb5:;
                    gm_taari1s0m51a7 t103 = gm_l2;
                    F.ret = t103;
                    goto gm_fret;
                }
gm_lb4:;
                {
                    uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t105 = ((t104 == ((uint8_t)100ULL)));
                    if (!t105) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t106 = gm_l0;
                    gm_b16_8 t107;
                    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sllcm1pqagh28));
                    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(5));
                    uint8_t t108 = (gm_eq0iqiubfm7e(((uint8_t *)&t106), ((uint8_t *)&t107)));
                    if (t108) {
                        gm_t9qkh0rsooc8d t109;
                        GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)5ULL));
                        gm_taari1s0m51a7 t110;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t110), t109);
                        ((uint8_t *)&t110)[1] = 1;
                        F.ret = t110;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb10:;
            }
            {
                {
                    uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t112 = ((t111 == ((uint8_t)101ULL)));
                    if (!t112) {
                        goto gm_lb11;
                    }
                    {
                        {
                            gm_b16_8 t113 = gm_l0;
                            gm_b16_8 t114;
                            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_svqdrhms9i231));
                            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(6));
                            uint8_t t115 = (gm_eq0iqiubfm7e(((uint8_t *)&t113), ((uint8_t *)&t114)));
                            if (!t115) {
                                goto gm_lb13;
                            }
                            gm_t9qkh0rsooc8d t116;
                            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)6ULL));
                            gm_taari1s0m51a7 t117;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t117), t116);
                            ((uint8_t *)&t117)[1] = 1;
                            gm_l3 = t117;
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            gm_b16_8 t118 = gm_l0;
                            gm_b16_8 t119;
                            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s9es2tos1bhfp));
                            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(4));
                            uint8_t t120 = (gm_eq0iqiubfm7e(((uint8_t *)&t118), ((uint8_t *)&t119)));
                            if (!t120) {
                                goto gm_lb14;
                            }
                            gm_t9qkh0rsooc8d t121;
                            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)7ULL));
                            gm_taari1s0m51a7 t122;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t122), t121);
                            ((uint8_t *)&t122)[1] = 1;
                            gm_l3 = t122;
                            goto gm_lb12;
                        }
gm_lb14:;
                        {
                            gm_b16_8 t123 = gm_l0;
                            gm_b16_8 t124;
                            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_ser8ip7vtbv6c));
                            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(8));
                            uint8_t t125 = (gm_eq0iqiubfm7e(((uint8_t *)&t123), ((uint8_t *)&t124)));
                            if (!t125) {
                                goto gm_lb15;
                            }
                            gm_t9qkh0rsooc8d t126;
                            GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)8ULL));
                            gm_taari1s0m51a7 t127;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t127), t126);
                            ((uint8_t *)&t127)[1] = 1;
                            gm_l3 = t127;
                            goto gm_lb12;
                        }
gm_lb15:;
                        {
                            gm_b16_8 t128 = gm_l0;
                            gm_b16_8 t129;
                            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sb88rajuutaep));
                            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(3));
                            uint8_t t130 = (gm_eq0iqiubfm7e(((uint8_t *)&t128), ((uint8_t *)&t129)));
                            if (!t130) {
                                goto gm_lb16;
                            }
                            gm_t9qkh0rsooc8d t131;
                            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)9ULL));
                            gm_taari1s0m51a7 t132;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t132), t131);
                            ((uint8_t *)&t132)[1] = 1;
                            gm_l3 = t132;
                            goto gm_lb12;
                        }
gm_lb16:;
                        {
                            gm_taari1s0m51a7 t133;
                            memset(&t133, 0, sizeof t133);
                            gm_l3 = t133;
                            goto gm_lb12;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 29, 23);
                    }
gm_lb12:;
                    gm_taari1s0m51a7 t134 = gm_l3;
                    F.ret = t134;
                    goto gm_fret;
                }
gm_lb11:;
                {
                    uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t136 = ((t135 == ((uint8_t)102ULL)));
                    if (!t136) {
                        goto gm_lb18;
                    }
                    {
                        {
                            gm_b16_8 t137 = gm_l0;
                            gm_b16_8 t138;
                            GM_ST(void *, (uint8_t *)&t138, (void *)(gm_sogehh8t1m7tn));
                            GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(5));
                            uint8_t t139 = (gm_eq0iqiubfm7e(((uint8_t *)&t137), ((uint8_t *)&t138)));
                            if (!t139) {
                                goto gm_lb20;
                            }
                            gm_t9qkh0rsooc8d t140;
                            GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)10ULL));
                            gm_taari1s0m51a7 t141;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t141), t140);
                            ((uint8_t *)&t141)[1] = 1;
                            gm_l4 = t141;
                            goto gm_lb19;
                        }
gm_lb20:;
                        {
                            gm_b16_8 t142 = gm_l0;
                            gm_b16_8 t143;
                            GM_ST(void *, (uint8_t *)&t143, (void *)(gm_s579fb7l5l6df));
                            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(2));
                            uint8_t t144 = (gm_eq0iqiubfm7e(((uint8_t *)&t142), ((uint8_t *)&t143)));
                            if (!t144) {
                                goto gm_lb21;
                            }
                            gm_t9qkh0rsooc8d t145;
                            GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)11ULL));
                            gm_taari1s0m51a7 t146;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t146), t145);
                            ((uint8_t *)&t146)[1] = 1;
                            gm_l4 = t146;
                            goto gm_lb19;
                        }
gm_lb21:;
                        {
                            gm_b16_8 t147 = gm_l0;
                            gm_b16_8 t148;
                            GM_ST(void *, (uint8_t *)&t148, (void *)(gm_scc8p0a9eachg));
                            GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(3));
                            uint8_t t149 = (gm_eq0iqiubfm7e(((uint8_t *)&t147), ((uint8_t *)&t148)));
                            if (!t149) {
                                goto gm_lb22;
                            }
                            gm_t9qkh0rsooc8d t150;
                            GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)12ULL));
                            gm_taari1s0m51a7 t151;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t151), t150);
                            ((uint8_t *)&t151)[1] = 1;
                            gm_l4 = t151;
                            goto gm_lb19;
                        }
gm_lb22:;
                        {
                            gm_taari1s0m51a7 t152;
                            memset(&t152, 0, sizeof t152);
                            gm_l4 = t152;
                            goto gm_lb19;
                        }
gm_lb23:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 36, 23);
                    }
gm_lb19:;
                    gm_taari1s0m51a7 t153 = gm_l4;
                    F.ret = t153;
                    goto gm_fret;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t155 = ((t154 == ((uint8_t)105ULL)));
                    if (!t155) {
                        goto gm_lb24;
                    }
                    {
                        {
                            gm_b16_8 t156 = gm_l0;
                            gm_b16_8 t157;
                            GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sl9nvct67or45));
                            GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(2));
                            uint8_t t158 = (gm_eq0iqiubfm7e(((uint8_t *)&t156), ((uint8_t *)&t157)));
                            if (!t158) {
                                goto gm_lb26;
                            }
                            gm_t9qkh0rsooc8d t159;
                            GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)13ULL));
                            gm_taari1s0m51a7 t160;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t160), t159);
                            ((uint8_t *)&t160)[1] = 1;
                            gm_l5 = t160;
                            goto gm_lb25;
                        }
gm_lb26:;
                        {
                            gm_b16_8 t161 = gm_l0;
                            gm_b16_8 t162;
                            GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sg3kpa3h16no3));
                            GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(6));
                            uint8_t t163 = (gm_eq0iqiubfm7e(((uint8_t *)&t161), ((uint8_t *)&t162)));
                            if (!t163) {
                                goto gm_lb27;
                            }
                            gm_t9qkh0rsooc8d t164;
                            GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)14ULL));
                            gm_taari1s0m51a7 t165;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t165), t164);
                            ((uint8_t *)&t165)[1] = 1;
                            gm_l5 = t165;
                            goto gm_lb25;
                        }
gm_lb27:;
                        {
                            gm_b16_8 t166 = gm_l0;
                            gm_b16_8 t167;
                            GM_ST(void *, (uint8_t *)&t167, (void *)(gm_svbhcvlahd4n2));
                            GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(2));
                            uint8_t t168 = (gm_eq0iqiubfm7e(((uint8_t *)&t166), ((uint8_t *)&t167)));
                            if (!t168) {
                                goto gm_lb28;
                            }
                            gm_t9qkh0rsooc8d t169;
                            GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)15ULL));
                            gm_taari1s0m51a7 t170;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t170), t169);
                            ((uint8_t *)&t170)[1] = 1;
                            gm_l5 = t170;
                            goto gm_lb25;
                        }
gm_lb28:;
                        {
                            gm_b16_8 t171 = gm_l0;
                            gm_b16_8 t172;
                            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sgpdmvc9s0d2p));
                            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(2));
                            uint8_t t173 = (gm_eq0iqiubfm7e(((uint8_t *)&t171), ((uint8_t *)&t172)));
                            if (!t173) {
                                goto gm_lb29;
                            }
                            gm_t9qkh0rsooc8d t174;
                            GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)16ULL));
                            gm_taari1s0m51a7 t175;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t175), t174);
                            ((uint8_t *)&t175)[1] = 1;
                            gm_l5 = t175;
                            goto gm_lb25;
                        }
gm_lb29:;
                        {
                            gm_taari1s0m51a7 t176;
                            memset(&t176, 0, sizeof t176);
                            gm_l5 = t176;
                            goto gm_lb25;
                        }
gm_lb30:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 42, 23);
                    }
gm_lb25:;
                    gm_taari1s0m51a7 t177 = gm_l5;
                    F.ret = t177;
                    goto gm_fret;
                }
gm_lb24:;
                {
                    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t179 = ((t178 == ((uint8_t)109ULL)));
                    if (!t179) {
                        goto gm_lb31;
                    }
                    gm_b16_8 t180 = gm_l0;
                    gm_b16_8 t181;
                    GM_ST(void *, (uint8_t *)&t181, (void *)(gm_s8m5q4i8uoe5j));
                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(5));
                    uint8_t t182 = (gm_eq0iqiubfm7e(((uint8_t *)&t180), ((uint8_t *)&t181)));
                    if (t182) {
                        gm_t9qkh0rsooc8d t183;
                        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)17ULL));
                        gm_taari1s0m51a7 t184;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t184), t183);
                        ((uint8_t *)&t184)[1] = 1;
                        F.ret = t184;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb31:;
                {
                    uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t186 = ((t185 == ((uint8_t)110ULL)));
                    if (!t186) {
                        goto gm_lb32;
                    }
                    {
                        {
                            gm_b16_8 t187 = gm_l0;
                            gm_b16_8 t188;
                            GM_ST(void *, (uint8_t *)&t188, (void *)(gm_scvhpklutvpjg));
                            GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(3));
                            uint8_t t189 = (gm_eq0iqiubfm7e(((uint8_t *)&t187), ((uint8_t *)&t188)));
                            if (!t189) {
                                goto gm_lb34;
                            }
                            gm_t9qkh0rsooc8d t190;
                            GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)18ULL));
                            gm_taari1s0m51a7 t191;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t191), t190);
                            ((uint8_t *)&t191)[1] = 1;
                            gm_l6 = t191;
                            goto gm_lb33;
                        }
gm_lb34:;
                        {
                            gm_b16_8 t192 = gm_l0;
                            gm_b16_8 t193;
                            GM_ST(void *, (uint8_t *)&t193, (void *)(gm_squedv51820an));
                            GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(4));
                            uint8_t t194 = (gm_eq0iqiubfm7e(((uint8_t *)&t192), ((uint8_t *)&t193)));
                            if (!t194) {
                                goto gm_lb35;
                            }
                            gm_t9qkh0rsooc8d t195;
                            GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)19ULL));
                            gm_taari1s0m51a7 t196;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t196), t195);
                            ((uint8_t *)&t196)[1] = 1;
                            gm_l6 = t196;
                            goto gm_lb33;
                        }
gm_lb35:;
                        {
                            gm_taari1s0m51a7 t197;
                            memset(&t197, 0, sizeof t197);
                            gm_l6 = t197;
                            goto gm_lb33;
                        }
gm_lb36:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 50, 23);
                    }
gm_lb33:;
                    gm_taari1s0m51a7 t198 = gm_l6;
                    F.ret = t198;
                    goto gm_fret;
                }
gm_lb32:;
            }
            {
                {
                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t200 = ((t199 == ((uint8_t)111ULL)));
                    if (!t200) {
                        goto gm_lb37;
                    }
                    gm_b16_8 t201 = gm_l0;
                    gm_b16_8 t202;
                    GM_ST(void *, (uint8_t *)&t202, (void *)(gm_spi47lg4tr80j));
                    GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(2));
                    uint8_t t203 = (gm_eq0iqiubfm7e(((uint8_t *)&t201), ((uint8_t *)&t202)));
                    if (t203) {
                        gm_t9qkh0rsooc8d t204;
                        GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)20ULL));
                        gm_taari1s0m51a7 t205;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t205), t204);
                        ((uint8_t *)&t205)[1] = 1;
                        F.ret = t205;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb37:;
                {
                    uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t207 = ((t206 == ((uint8_t)112ULL)));
                    if (!t207) {
                        goto gm_lb38;
                    }
                    gm_b16_8 t208 = gm_l0;
                    gm_b16_8 t209;
                    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sjqs7q3ostqbm));
                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(3));
                    uint8_t t210 = (gm_eq0iqiubfm7e(((uint8_t *)&t208), ((uint8_t *)&t209)));
                    if (t210) {
                        gm_t9qkh0rsooc8d t211;
                        GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)21ULL));
                        gm_taari1s0m51a7 t212;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t212), t211);
                        ((uint8_t *)&t212)[1] = 1;
                        F.ret = t212;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t214 = ((t213 == ((uint8_t)114ULL)));
                    if (!t214) {
                        goto gm_lb39;
                    }
                    {
                        {
                            gm_b16_8 t215 = gm_l0;
                            gm_b16_8 t216;
                            GM_ST(void *, (uint8_t *)&t216, (void *)(gm_sn6sqlran7h3r));
                            GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(6));
                            uint8_t t217 = (gm_eq0iqiubfm7e(((uint8_t *)&t215), ((uint8_t *)&t216)));
                            if (!t217) {
                                goto gm_lb41;
                            }
                            gm_t9qkh0rsooc8d t218;
                            GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)22ULL));
                            gm_taari1s0m51a7 t219;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t219), t218);
                            ((uint8_t *)&t219)[1] = 1;
                            gm_l7 = t219;
                            goto gm_lb40;
                        }
gm_lb41:;
                        {
                            gm_b16_8 t220 = gm_l0;
                            gm_b16_8 t221;
                            GM_ST(void *, (uint8_t *)&t221, (void *)(gm_s277jq8ulrihr));
                            GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(6));
                            uint8_t t222 = (gm_eq0iqiubfm7e(((uint8_t *)&t220), ((uint8_t *)&t221)));
                            if (!t222) {
                                goto gm_lb42;
                            }
                            gm_t9qkh0rsooc8d t223;
                            GM_ST(uint8_t, ((uint8_t *)&t223 + 0), ((uint8_t)23ULL));
                            gm_taari1s0m51a7 t224;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t224), t223);
                            ((uint8_t *)&t224)[1] = 1;
                            gm_l7 = t224;
                            goto gm_lb40;
                        }
gm_lb42:;
                        {
                            gm_taari1s0m51a7 t225;
                            memset(&t225, 0, sizeof t225);
                            gm_l7 = t225;
                            goto gm_lb40;
                        }
gm_lb43:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 57, 23);
                    }
gm_lb40:;
                    gm_taari1s0m51a7 t226 = gm_l7;
                    F.ret = t226;
                    goto gm_fret;
                }
gm_lb39:;
                {
                    uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t228 = ((t227 == ((uint8_t)115ULL)));
                    if (!t228) {
                        goto gm_lb44;
                    }
                    gm_b16_8 t229 = gm_l0;
                    gm_b16_8 t230;
                    GM_ST(void *, (uint8_t *)&t230, (void *)(gm_sc49g68j2cekh));
                    GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(6));
                    uint8_t t231 = (gm_eq0iqiubfm7e(((uint8_t *)&t229), ((uint8_t *)&t230)));
                    if (t231) {
                        gm_t9qkh0rsooc8d t232;
                        GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)24ULL));
                        gm_taari1s0m51a7 t233;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t233), t232);
                        ((uint8_t *)&t233)[1] = 1;
                        F.ret = t233;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb44:;
            }
            {
                {
                    uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t235 = ((t234 == ((uint8_t)116ULL)));
                    if (!t235) {
                        goto gm_lb45;
                    }
                    {
                        {
                            gm_b16_8 t236 = gm_l0;
                            gm_b16_8 t237;
                            GM_ST(void *, (uint8_t *)&t237, (void *)(gm_sehko5bb96s6p));
                            GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(4));
                            uint8_t t238 = (gm_eq0iqiubfm7e(((uint8_t *)&t236), ((uint8_t *)&t237)));
                            if (!t238) {
                                goto gm_lb47;
                            }
                            gm_t9qkh0rsooc8d t239;
                            GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)25ULL));
                            gm_taari1s0m51a7 t240;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t240), t239);
                            ((uint8_t *)&t240)[1] = 1;
                            gm_l8 = t240;
                            goto gm_lb46;
                        }
gm_lb47:;
                        {
                            gm_b16_8 t241 = gm_l0;
                            gm_b16_8 t242;
                            GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s4fd35uidvgkn));
                            GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(3));
                            uint8_t t243 = (gm_eq0iqiubfm7e(((uint8_t *)&t241), ((uint8_t *)&t242)));
                            if (!t243) {
                                goto gm_lb48;
                            }
                            gm_t9qkh0rsooc8d t244;
                            GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)26ULL));
                            gm_taari1s0m51a7 t245;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t245), t244);
                            ((uint8_t *)&t245)[1] = 1;
                            gm_l8 = t245;
                            goto gm_lb46;
                        }
gm_lb48:;
                        {
                            gm_taari1s0m51a7 t246;
                            memset(&t246, 0, sizeof t246);
                            gm_l8 = t246;
                            goto gm_lb46;
                        }
gm_lb49:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 63, 23);
                    }
gm_lb46:;
                    gm_taari1s0m51a7 t247 = gm_l8;
                    F.ret = t247;
                    goto gm_fret;
                }
gm_lb45:;
                {
                    uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t249 = ((t248 == ((uint8_t)117ULL)));
                    if (!t249) {
                        goto gm_lb50;
                    }
                    {
                        {
                            gm_b16_8 t250 = gm_l0;
                            gm_b16_8 t251;
                            GM_ST(void *, (uint8_t *)&t251, (void *)(gm_s8p1h96ji9kh0));
                            GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(5));
                            uint8_t t252 = (gm_eq0iqiubfm7e(((uint8_t *)&t250), ((uint8_t *)&t251)));
                            if (!t252) {
                                goto gm_lb52;
                            }
                            gm_t9qkh0rsooc8d t253;
                            GM_ST(uint8_t, ((uint8_t *)&t253 + 0), ((uint8_t)27ULL));
                            gm_taari1s0m51a7 t254;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t254), t253);
                            ((uint8_t *)&t254)[1] = 1;
                            gm_l9 = t254;
                            goto gm_lb51;
                        }
gm_lb52:;
                        {
                            gm_b16_8 t255 = gm_l0;
                            gm_b16_8 t256;
                            GM_ST(void *, (uint8_t *)&t256, (void *)(gm_smu48daue2qai));
                            GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(5));
                            uint8_t t257 = (gm_eq0iqiubfm7e(((uint8_t *)&t255), ((uint8_t *)&t256)));
                            if (!t257) {
                                goto gm_lb53;
                            }
                            gm_t9qkh0rsooc8d t258;
                            GM_ST(uint8_t, ((uint8_t *)&t258 + 0), ((uint8_t)28ULL));
                            gm_taari1s0m51a7 t259;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t259), t258);
                            ((uint8_t *)&t259)[1] = 1;
                            gm_l9 = t259;
                            goto gm_lb51;
                        }
gm_lb53:;
                        {
                            gm_taari1s0m51a7 t260;
                            memset(&t260, 0, sizeof t260);
                            gm_l9 = t260;
                            goto gm_lb51;
                        }
gm_lb54:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 68, 23);
                    }
gm_lb51:;
                    gm_taari1s0m51a7 t261 = gm_l9;
                    F.ret = t261;
                    goto gm_fret;
                }
gm_lb50:;
                {
                    uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t263 = ((t262 == ((uint8_t)119ULL)));
                    if (!t263) {
                        goto gm_lb55;
                    }
                    {
                        {
                            gm_b16_8 t264 = gm_l0;
                            gm_b16_8 t265;
                            GM_ST(void *, (uint8_t *)&t265, (void *)(gm_slpo22sjs83b1));
                            GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(4));
                            uint8_t t266 = (gm_eq0iqiubfm7e(((uint8_t *)&t264), ((uint8_t *)&t265)));
                            if (!t266) {
                                goto gm_lb57;
                            }
                            gm_t9qkh0rsooc8d t267;
                            GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)29ULL));
                            gm_taari1s0m51a7 t268;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t268), t267);
                            ((uint8_t *)&t268)[1] = 1;
                            gm_l10 = t268;
                            goto gm_lb56;
                        }
gm_lb57:;
                        {
                            gm_b16_8 t269 = gm_l0;
                            gm_b16_8 t270;
                            GM_ST(void *, (uint8_t *)&t270, (void *)(gm_s5rv6umk4akl0));
                            GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(5));
                            uint8_t t271 = (gm_eq0iqiubfm7e(((uint8_t *)&t269), ((uint8_t *)&t270)));
                            if (!t271) {
                                goto gm_lb58;
                            }
                            gm_t9qkh0rsooc8d t272;
                            GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)30ULL));
                            gm_taari1s0m51a7 t273;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t273), t272);
                            ((uint8_t *)&t273)[1] = 1;
                            gm_l10 = t273;
                            goto gm_lb56;
                        }
gm_lb58:;
                        {
                            gm_b16_8 t274 = gm_l0;
                            gm_b16_8 t275;
                            GM_ST(void *, (uint8_t *)&t275, (void *)(gm_srri8c6al1v1g));
                            GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(4));
                            uint8_t t276 = (gm_eq0iqiubfm7e(((uint8_t *)&t274), ((uint8_t *)&t275)));
                            if (!t276) {
                                goto gm_lb59;
                            }
                            gm_t9qkh0rsooc8d t277;
                            GM_ST(uint8_t, ((uint8_t *)&t277 + 0), ((uint8_t)31ULL));
                            gm_taari1s0m51a7 t278;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t278), t277);
                            ((uint8_t *)&t278)[1] = 1;
                            gm_l10 = t278;
                            goto gm_lb56;
                        }
gm_lb59:;
                        {
                            gm_taari1s0m51a7 t279;
                            memset(&t279, 0, sizeof t279);
                            gm_l10 = t279;
                            goto gm_lb56;
                        }
gm_lb60:;
                        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 73, 23);
                    }
gm_lb56:;
                    gm_taari1s0m51a7 t280 = gm_l10;
                    F.ret = t280;
                    goto gm_fret;
                }
gm_lb55:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb61:;
                gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 19, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_taari1s0m51a7 t281;
        memset(&t281, 0, sizeof t281);
        F.ret = t281;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keyword_text_149oqn4 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 641u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)0ULL));
            if (!t36) {
                goto gm_lb2;
            }
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s920dpvvv6cej));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(3));
            gm_l1 = t37;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)1ULL));
            if (!t39) {
                goto gm_lb3;
            }
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s8av3ru5o85v4));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(5));
            gm_l1 = t40;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)2ULL));
            if (!t42) {
                goto gm_lb4;
            }
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_si1m7q6ia0hto));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(6));
            gm_l1 = t43;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)3ULL));
            if (!t45) {
                goto gm_lb5;
            }
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sr31o7m0aohvd));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(8));
            gm_l1 = t46;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)4ULL));
            if (!t48) {
                goto gm_lb6;
            }
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s66uvortgrtsg));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
            gm_l1 = t49;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)5ULL));
            if (!t51) {
                goto gm_lb7;
            }
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sllcm1pqagh28));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(5));
            gm_l1 = t52;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)6ULL));
            if (!t54) {
                goto gm_lb8;
            }
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_svqdrhms9i231));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(6));
            gm_l1 = t55;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)7ULL));
            if (!t57) {
                goto gm_lb9;
            }
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(4));
            gm_l1 = t58;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)8ULL));
            if (!t60) {
                goto gm_lb10;
            }
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_ser8ip7vtbv6c));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(8));
            gm_l1 = t61;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)9ULL));
            if (!t63) {
                goto gm_lb11;
            }
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sb88rajuutaep));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(3));
            gm_l1 = t64;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)10ULL));
            if (!t66) {
                goto gm_lb12;
            }
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(5));
            gm_l1 = t67;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)11ULL));
            if (!t69) {
                goto gm_lb13;
            }
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s579fb7l5l6df));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(2));
            gm_l1 = t70;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)12ULL));
            if (!t72) {
                goto gm_lb14;
            }
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_scc8p0a9eachg));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(3));
            gm_l1 = t73;
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)13ULL));
            if (!t75) {
                goto gm_lb15;
            }
            gm_b16_8 t76;
            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sl9nvct67or45));
            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(2));
            gm_l1 = t76;
            goto gm_lb1;
        }
gm_lb15:;
        {
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)14ULL));
            if (!t78) {
                goto gm_lb16;
            }
            gm_b16_8 t79;
            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sg3kpa3h16no3));
            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(6));
            gm_l1 = t79;
            goto gm_lb1;
        }
gm_lb16:;
        {
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t81 = (uint8_t)(t80 == ((uint8_t)15ULL));
            if (!t81) {
                goto gm_lb17;
            }
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_svbhcvlahd4n2));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(2));
            gm_l1 = t82;
            goto gm_lb1;
        }
gm_lb17:;
        {
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)16ULL));
            if (!t84) {
                goto gm_lb18;
            }
            gm_b16_8 t85;
            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sgpdmvc9s0d2p));
            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(2));
            gm_l1 = t85;
            goto gm_lb1;
        }
gm_lb18:;
        {
            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)17ULL));
            if (!t87) {
                goto gm_lb19;
            }
            gm_b16_8 t88;
            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s8m5q4i8uoe5j));
            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(5));
            gm_l1 = t88;
            goto gm_lb1;
        }
gm_lb19:;
        {
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)18ULL));
            if (!t90) {
                goto gm_lb20;
            }
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_scvhpklutvpjg));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(3));
            gm_l1 = t91;
            goto gm_lb1;
        }
gm_lb20:;
        {
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)19ULL));
            if (!t93) {
                goto gm_lb21;
            }
            gm_b16_8 t94;
            GM_ST(void *, (uint8_t *)&t94, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(4));
            gm_l1 = t94;
            goto gm_lb1;
        }
gm_lb21:;
        {
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)20ULL));
            if (!t96) {
                goto gm_lb22;
            }
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_spi47lg4tr80j));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
            gm_l1 = t97;
            goto gm_lb1;
        }
gm_lb22:;
        {
            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t99 = (uint8_t)(t98 == ((uint8_t)21ULL));
            if (!t99) {
                goto gm_lb23;
            }
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sjqs7q3ostqbm));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(3));
            gm_l1 = t100;
            goto gm_lb1;
        }
gm_lb23:;
        {
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)22ULL));
            if (!t102) {
                goto gm_lb24;
            }
            gm_b16_8 t103;
            GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sn6sqlran7h3r));
            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(6));
            gm_l1 = t103;
            goto gm_lb1;
        }
gm_lb24:;
        {
            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t105 = (uint8_t)(t104 == ((uint8_t)23ULL));
            if (!t105) {
                goto gm_lb25;
            }
            gm_b16_8 t106;
            GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s277jq8ulrihr));
            GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(6));
            gm_l1 = t106;
            goto gm_lb1;
        }
gm_lb25:;
        {
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)24ULL));
            if (!t108) {
                goto gm_lb26;
            }
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sc49g68j2cekh));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(6));
            gm_l1 = t109;
            goto gm_lb1;
        }
gm_lb26:;
        {
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)25ULL));
            if (!t111) {
                goto gm_lb27;
            }
            gm_b16_8 t112;
            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(4));
            gm_l1 = t112;
            goto gm_lb1;
        }
gm_lb27:;
        {
            uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t114 = (uint8_t)(t113 == ((uint8_t)26ULL));
            if (!t114) {
                goto gm_lb28;
            }
            gm_b16_8 t115;
            GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s4fd35uidvgkn));
            GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(3));
            gm_l1 = t115;
            goto gm_lb1;
        }
gm_lb28:;
        {
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t117 = (uint8_t)(t116 == ((uint8_t)27ULL));
            if (!t117) {
                goto gm_lb29;
            }
            gm_b16_8 t118;
            GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s8p1h96ji9kh0));
            GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(5));
            gm_l1 = t118;
            goto gm_lb1;
        }
gm_lb29:;
        {
            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t120 = (uint8_t)(t119 == ((uint8_t)28ULL));
            if (!t120) {
                goto gm_lb30;
            }
            gm_b16_8 t121;
            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_smu48daue2qai));
            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(5));
            gm_l1 = t121;
            goto gm_lb1;
        }
gm_lb30:;
        {
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t123 = (uint8_t)(t122 == ((uint8_t)29ULL));
            if (!t123) {
                goto gm_lb31;
            }
            gm_b16_8 t124;
            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_slpo22sjs83b1));
            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(4));
            gm_l1 = t124;
            goto gm_lb1;
        }
gm_lb31:;
        {
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t126 = (uint8_t)(t125 == ((uint8_t)30ULL));
            if (!t126) {
                goto gm_lb32;
            }
            gm_b16_8 t127;
            GM_ST(void *, (uint8_t *)&t127, (void *)(gm_s5rv6umk4akl0));
            GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(5));
            gm_l1 = t127;
            goto gm_lb1;
        }
gm_lb32:;
        {
            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t129 = (uint8_t)(t128 == ((uint8_t)31ULL));
            if (!t129) {
                goto gm_lb33;
            }
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_srri8c6al1v1g));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(4));
            gm_l1 = t130;
            goto gm_lb1;
        }
gm_lb33:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 84, 49);
    }
gm_lb1:;
    gm_b16_8 t131 = gm_l1;
    F.ret = t131;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_punct_text_ov4r40u F;
    gm_t9qkh0rsooc8d gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 1307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            {
                {
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)0ULL));
                    if (!t73) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t74;
                    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sajhum9b4nr96));
                    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
                    gm_l1 = t74;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)1ULL));
                    if (!t76) {
                        goto gm_lb3;
                    }
                    gm_b16_8 t77;
                    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_skolmnfc0ccbd));
                    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
                    gm_l1 = t77;
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t79 = (uint8_t)(t78 == ((uint8_t)2ULL));
                    if (!t79) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t80;
                    GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s6s96daqmhcs9));
                    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(1));
                    gm_l1 = t80;
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)3ULL));
                    if (!t82) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_st3ja0nqi37bv));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(1));
                    gm_l1 = t83;
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t85 = (uint8_t)(t84 == ((uint8_t)4ULL));
                    if (!t85) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t86;
                    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_siamquc19uikr));
                    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(1));
                    gm_l1 = t86;
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)5ULL));
                    if (!t88) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t89;
                    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_si62ejtp0vbct));
                    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(2));
                    gm_l1 = t89;
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)6ULL));
                    if (!t91) {
                        goto gm_lb8;
                    }
                    gm_b16_8 t92;
                    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_s9hi985r6f61m));
                    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(2));
                    gm_l1 = t92;
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t94 = (uint8_t)(t93 == ((uint8_t)7ULL));
                    if (!t94) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t95;
                    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_smerqm8g793l6));
                    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(1));
                    gm_l1 = t95;
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)8ULL));
                    if (!t97) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t98;
                    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s2u5posfkbgnq));
                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(1));
                    gm_l1 = t98;
                    goto gm_lb1;
                }
gm_lb10:;
                {
                    uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t100 = (uint8_t)(t99 == ((uint8_t)9ULL));
                    if (!t100) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t101;
                    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sa831sfid0hf3));
                    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(1));
                    gm_l1 = t101;
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t103 = (uint8_t)(t102 == ((uint8_t)10ULL));
                    if (!t103) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t104;
                    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_skriisqpofdin));
                    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(2));
                    gm_l1 = t104;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t106 = (uint8_t)(t105 == ((uint8_t)11ULL));
                    if (!t106) {
                        goto gm_lb13;
                    }
                    gm_b16_8 t107;
                    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sjjig60ja37ka));
                    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(2));
                    gm_l1 = t107;
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t109 = (uint8_t)(t108 == ((uint8_t)12ULL));
                    if (!t109) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t110;
                    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_s1dbslfck7laf));
                    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(2));
                    gm_l1 = t110;
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t112 = (uint8_t)(t111 == ((uint8_t)13ULL));
                    if (!t112) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t113;
                    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sgemh066sqogf));
                    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(3));
                    gm_l1 = t113;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t115 = (uint8_t)(t114 == ((uint8_t)14ULL));
                    if (!t115) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t116;
                    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_s0s4ef50964r9));
                    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(2));
                    gm_l1 = t116;
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)15ULL));
                    if (!t118) {
                        goto gm_lb17;
                    }
                    gm_b16_8 t119;
                    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sfg1e0lbt8sf5));
                    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(2));
                    gm_l1 = t119;
                    goto gm_lb1;
                }
gm_lb17:;
                {
                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t121 = (uint8_t)(t120 == ((uint8_t)16ULL));
                    if (!t121) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t122;
                    GM_ST(void *, (uint8_t *)&t122, (void *)(gm_sad0udrpgk27o));
                    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(2));
                    gm_l1 = t122;
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)17ULL));
                    if (!t124) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t125;
                    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_s9s0gbnmtpvjl));
                    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(3));
                    gm_l1 = t125;
                    goto gm_lb1;
                }
gm_lb19:;
                {
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)18ULL));
                    if (!t127) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t128;
                    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sjid61i849qic));
                    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(2));
                    gm_l1 = t128;
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t130 = (uint8_t)(t129 == ((uint8_t)19ULL));
                    if (!t130) {
                        goto gm_lb21;
                    }
                    gm_b16_8 t131;
                    GM_ST(void *, (uint8_t *)&t131, (void *)(gm_sabc7m3rug04j));
                    GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(2));
                    gm_l1 = t131;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t133 = (uint8_t)(t132 == ((uint8_t)20ULL));
                    if (!t133) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t134;
                    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s3bkmscn5fi56));
                    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(2));
                    gm_l1 = t134;
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t136 = (uint8_t)(t135 == ((uint8_t)21ULL));
                    if (!t136) {
                        goto gm_lb23;
                    }
                    gm_b16_8 t137;
                    GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sp3rdossibc7f));
                    GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(3));
                    gm_l1 = t137;
                    goto gm_lb1;
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)22ULL));
                    if (!t139) {
                        goto gm_lb24;
                    }
                    gm_b16_8 t140;
                    GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sh0j2ggs1vvoa));
                    GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(1));
                    gm_l1 = t140;
                    goto gm_lb1;
                }
gm_lb24:;
                {
                    uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t142 = (uint8_t)(t141 == ((uint8_t)23ULL));
                    if (!t142) {
                        goto gm_lb25;
                    }
                    gm_b16_8 t143;
                    GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sdeq555foclko));
                    GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(2));
                    gm_l1 = t143;
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t145 = (uint8_t)(t144 == ((uint8_t)24ULL));
                    if (!t145) {
                        goto gm_lb26;
                    }
                    gm_b16_8 t146;
                    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_shtiokhdiebr1));
                    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(2));
                    gm_l1 = t146;
                    goto gm_lb1;
                }
gm_lb26:;
                {
                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)25ULL));
                    if (!t148) {
                        goto gm_lb27;
                    }
                    gm_b16_8 t149;
                    GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sd7p31mtitiq9));
                    GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(2));
                    gm_l1 = t149;
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t151 = (uint8_t)(t150 == ((uint8_t)26ULL));
                    if (!t151) {
                        goto gm_lb28;
                    }
                    gm_b16_8 t152;
                    GM_ST(void *, (uint8_t *)&t152, (void *)(gm_sj0kptrfbec99));
                    GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(2));
                    gm_l1 = t152;
                    goto gm_lb1;
                }
gm_lb28:;
                {
                    uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t154 = (uint8_t)(t153 == ((uint8_t)27ULL));
                    if (!t154) {
                        goto gm_lb29;
                    }
                    gm_b16_8 t155;
                    GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sj5j224m4ulf9));
                    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(2));
                    gm_l1 = t155;
                    goto gm_lb1;
                }
gm_lb29:;
                {
                    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t157 = (uint8_t)(t156 == ((uint8_t)28ULL));
                    if (!t157) {
                        goto gm_lb30;
                    }
                    gm_b16_8 t158;
                    GM_ST(void *, (uint8_t *)&t158, (void *)(gm_sbjiv038rtlcj));
                    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(3));
                    gm_l1 = t158;
                    goto gm_lb1;
                }
gm_lb30:;
                {
                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t160 = (uint8_t)(t159 == ((uint8_t)29ULL));
                    if (!t160) {
                        goto gm_lb31;
                    }
                    gm_b16_8 t161;
                    GM_ST(void *, (uint8_t *)&t161, (void *)(gm_su9718bmbci33));
                    GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(3));
                    gm_l1 = t161;
                    goto gm_lb1;
                }
gm_lb31:;
                {
                    uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t163 = (uint8_t)(t162 == ((uint8_t)30ULL));
                    if (!t163) {
                        goto gm_lb32;
                    }
                    gm_b16_8 t164;
                    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_srcsoo8lineh7));
                    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(2));
                    gm_l1 = t164;
                    goto gm_lb1;
                }
gm_lb32:;
                {
                    uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)31ULL));
                    if (!t166) {
                        goto gm_lb33;
                    }
                    gm_b16_8 t167;
                    GM_ST(void *, (uint8_t *)&t167, (void *)(gm_saimepa8bfkkm));
                    GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(2));
                    gm_l1 = t167;
                    goto gm_lb1;
                }
gm_lb33:;
                {
                    uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t169 = (uint8_t)(t168 == ((uint8_t)32ULL));
                    if (!t169) {
                        goto gm_lb34;
                    }
                    gm_b16_8 t170;
                    GM_ST(void *, (uint8_t *)&t170, (void *)(gm_sl4co37h2cmmb));
                    GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(2));
                    gm_l1 = t170;
                    goto gm_lb1;
                }
gm_lb34:;
            }
            {
                {
                    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t172 = (uint8_t)(t171 == ((uint8_t)33ULL));
                    if (!t172) {
                        goto gm_lb35;
                    }
                    gm_b16_8 t173;
                    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sdavnmflj0bfl));
                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(3));
                    gm_l1 = t173;
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)34ULL));
                    if (!t175) {
                        goto gm_lb36;
                    }
                    gm_b16_8 t176;
                    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sll7gknpe999d));
                    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(3));
                    gm_l1 = t176;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t178 = (uint8_t)(t177 == ((uint8_t)35ULL));
                    if (!t178) {
                        goto gm_lb37;
                    }
                    gm_b16_8 t179;
                    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sq1oij2hrovtv));
                    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(3));
                    gm_l1 = t179;
                    goto gm_lb1;
                }
gm_lb37:;
                {
                    uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t181 = (uint8_t)(t180 == ((uint8_t)36ULL));
                    if (!t181) {
                        goto gm_lb38;
                    }
                    gm_b16_8 t182;
                    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sm3gke457vdm3));
                    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(4));
                    gm_l1 = t182;
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)37ULL));
                    if (!t184) {
                        goto gm_lb39;
                    }
                    gm_b16_8 t185;
                    GM_ST(void *, (uint8_t *)&t185, (void *)(gm_sde2pekt9oc5v));
                    GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(3));
                    gm_l1 = t185;
                    goto gm_lb1;
                }
gm_lb39:;
                {
                    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t187 = (uint8_t)(t186 == ((uint8_t)38ULL));
                    if (!t187) {
                        goto gm_lb40;
                    }
                    gm_b16_8 t188;
                    GM_ST(void *, (uint8_t *)&t188, (void *)(gm_skm1g73974f87));
                    GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(3));
                    gm_l1 = t188;
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t190 = (uint8_t)(t189 == ((uint8_t)39ULL));
                    if (!t190) {
                        goto gm_lb41;
                    }
                    gm_b16_8 t191;
                    GM_ST(void *, (uint8_t *)&t191, (void *)(gm_svkleo8hhv87n));
                    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(3));
                    gm_l1 = t191;
                    goto gm_lb1;
                }
gm_lb41:;
                {
                    uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t193 = (uint8_t)(t192 == ((uint8_t)40ULL));
                    if (!t193) {
                        goto gm_lb42;
                    }
                    gm_b16_8 t194;
                    GM_ST(void *, (uint8_t *)&t194, (void *)(gm_s33pl04oikskr));
                    GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(4));
                    gm_l1 = t194;
                    goto gm_lb1;
                }
gm_lb42:;
                {
                    uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t196 = (uint8_t)(t195 == ((uint8_t)41ULL));
                    if (!t196) {
                        goto gm_lb43;
                    }
                    gm_b16_8 t197;
                    GM_ST(void *, (uint8_t *)&t197, (void *)(gm_s1f440st4mnkp));
                    GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(1));
                    gm_l1 = t197;
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t199 = (uint8_t)(t198 == ((uint8_t)42ULL));
                    if (!t199) {
                        goto gm_lb44;
                    }
                    gm_b16_8 t200;
                    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_s4g3cajt15dmi));
                    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(2));
                    gm_l1 = t200;
                    goto gm_lb1;
                }
gm_lb44:;
                {
                    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t202 = (uint8_t)(t201 == ((uint8_t)43ULL));
                    if (!t202) {
                        goto gm_lb45;
                    }
                    gm_b16_8 t203;
                    GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sm5be4659ulci));
                    GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(1));
                    gm_l1 = t203;
                    goto gm_lb1;
                }
gm_lb45:;
            }
            {
                {
                    uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t205 = (uint8_t)(t204 == ((uint8_t)44ULL));
                    if (!t205) {
                        goto gm_lb46;
                    }
                    gm_b16_8 t206;
                    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_s02ru83e3fffc));
                    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(2));
                    gm_l1 = t206;
                    goto gm_lb1;
                }
gm_lb46:;
                {
                    uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t208 = (uint8_t)(t207 == ((uint8_t)45ULL));
                    if (!t208) {
                        goto gm_lb47;
                    }
                    gm_b16_8 t209;
                    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sfqk3rro4g7t0));
                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(1));
                    gm_l1 = t209;
                    goto gm_lb1;
                }
gm_lb47:;
                {
                    uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t211 = (uint8_t)(t210 == ((uint8_t)46ULL));
                    if (!t211) {
                        goto gm_lb48;
                    }
                    gm_b16_8 t212;
                    GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sf9a2kg3eo1gs));
                    GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(1));
                    gm_l1 = t212;
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t214 = (uint8_t)(t213 == ((uint8_t)47ULL));
                    if (!t214) {
                        goto gm_lb49;
                    }
                    gm_b16_8 t215;
                    GM_ST(void *, (uint8_t *)&t215, (void *)(gm_srs1vrn2uaams));
                    GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(1));
                    gm_l1 = t215;
                    goto gm_lb1;
                }
gm_lb49:;
                {
                    uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t217 = (uint8_t)(t216 == ((uint8_t)48ULL));
                    if (!t217) {
                        goto gm_lb50;
                    }
                    gm_b16_8 t218;
                    GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sfi2ha42r1nr2));
                    GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(1));
                    gm_l1 = t218;
                    goto gm_lb1;
                }
gm_lb50:;
                {
                    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t220 = (uint8_t)(t219 == ((uint8_t)49ULL));
                    if (!t220) {
                        goto gm_lb51;
                    }
                    gm_b16_8 t221;
                    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_seccpktgmrevt));
                    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(1));
                    gm_l1 = t221;
                    goto gm_lb1;
                }
gm_lb51:;
                {
                    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t223 = (uint8_t)(t222 == ((uint8_t)50ULL));
                    if (!t223) {
                        goto gm_lb52;
                    }
                    gm_b16_8 t224;
                    GM_ST(void *, (uint8_t *)&t224, (void *)(gm_snenkhfev166s));
                    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(2));
                    gm_l1 = t224;
                    goto gm_lb1;
                }
gm_lb52:;
                {
                    uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t226 = (uint8_t)(t225 == ((uint8_t)51ULL));
                    if (!t226) {
                        goto gm_lb53;
                    }
                    gm_b16_8 t227;
                    GM_ST(void *, (uint8_t *)&t227, (void *)(gm_svm0rfaqk3b2q));
                    GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(3));
                    gm_l1 = t227;
                    goto gm_lb1;
                }
gm_lb53:;
                {
                    uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t229 = (uint8_t)(t228 == ((uint8_t)52ULL));
                    if (!t229) {
                        goto gm_lb54;
                    }
                    gm_b16_8 t230;
                    GM_ST(void *, (uint8_t *)&t230, (void *)(gm_soqt3jnmovq5l));
                    GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(3));
                    gm_l1 = t230;
                    goto gm_lb1;
                }
gm_lb54:;
                {
                    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t232 = (uint8_t)(t231 == ((uint8_t)53ULL));
                    if (!t232) {
                        goto gm_lb55;
                    }
                    gm_b16_8 t233;
                    GM_ST(void *, (uint8_t *)&t233, (void *)(gm_sjfg2291kjugh));
                    GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(3));
                    gm_l1 = t233;
                    goto gm_lb1;
                }
gm_lb55:;
                {
                    uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t235 = (uint8_t)(t234 == ((uint8_t)54ULL));
                    if (!t235) {
                        goto gm_lb56;
                    }
                    gm_b16_8 t236;
                    GM_ST(void *, (uint8_t *)&t236, (void *)(gm_ss67f0gmfen1m));
                    GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(2));
                    gm_l1 = t236;
                    goto gm_lb1;
                }
gm_lb56:;
            }
            {
                {
                    uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t238 = (uint8_t)(t237 == ((uint8_t)55ULL));
                    if (!t238) {
                        goto gm_lb57;
                    }
                    gm_b16_8 t239;
                    GM_ST(void *, (uint8_t *)&t239, (void *)(gm_shge5n743ua4h));
                    GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(2));
                    gm_l1 = t239;
                    goto gm_lb1;
                }
gm_lb57:;
                {
                    uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t241 = (uint8_t)(t240 == ((uint8_t)56ULL));
                    if (!t241) {
                        goto gm_lb58;
                    }
                    gm_b16_8 t242;
                    GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s39mljh669qgl));
                    GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(1));
                    gm_l1 = t242;
                    goto gm_lb1;
                }
gm_lb58:;
                {
                    uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t244 = (uint8_t)(t243 == ((uint8_t)57ULL));
                    if (!t244) {
                        goto gm_lb59;
                    }
                    gm_b16_8 t245;
                    GM_ST(void *, (uint8_t *)&t245, (void *)(gm_sc8o4oaph9gbr));
                    GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(1));
                    gm_l1 = t245;
                    goto gm_lb1;
                }
gm_lb59:;
                {
                    uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t247 = (uint8_t)(t246 == ((uint8_t)58ULL));
                    if (!t247) {
                        goto gm_lb60;
                    }
                    gm_b16_8 t248;
                    GM_ST(void *, (uint8_t *)&t248, (void *)(gm_sl3q52ep6aa53));
                    GM_ST(uintptr_t, (uint8_t *)&t248 + 8, (uintptr_t)(1));
                    gm_l1 = t248;
                    goto gm_lb1;
                }
gm_lb60:;
                {
                    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t250 = (uint8_t)(t249 == ((uint8_t)59ULL));
                    if (!t250) {
                        goto gm_lb61;
                    }
                    gm_b16_8 t251;
                    GM_ST(void *, (uint8_t *)&t251, (void *)(gm_sm8obe455p2d9));
                    GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(2));
                    gm_l1 = t251;
                    goto gm_lb1;
                }
gm_lb61:;
                {
                    uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t253 = (uint8_t)(t252 == ((uint8_t)60ULL));
                    if (!t253) {
                        goto gm_lb62;
                    }
                    gm_b16_8 t254;
                    GM_ST(void *, (uint8_t *)&t254, (void *)(gm_s0eemp7of8buc));
                    GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(2));
                    gm_l1 = t254;
                    goto gm_lb1;
                }
gm_lb62:;
                {
                    uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t256 = (uint8_t)(t255 == ((uint8_t)61ULL));
                    if (!t256) {
                        goto gm_lb63;
                    }
                    gm_b16_8 t257;
                    GM_ST(void *, (uint8_t *)&t257, (void *)(gm_smooaouqmm8dj));
                    GM_ST(uintptr_t, (uint8_t *)&t257 + 8, (uintptr_t)(1));
                    gm_l1 = t257;
                    goto gm_lb1;
                }
gm_lb63:;
                {
                    uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t259 = (uint8_t)(t258 == ((uint8_t)62ULL));
                    if (!t259) {
                        goto gm_lb64;
                    }
                    gm_b16_8 t260;
                    GM_ST(void *, (uint8_t *)&t260, (void *)(gm_s1asl1e1tvb0k));
                    GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(1));
                    gm_l1 = t260;
                    goto gm_lb1;
                }
gm_lb64:;
                {
                    uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t262 = (uint8_t)(t261 == ((uint8_t)63ULL));
                    if (!t262) {
                        goto gm_lb65;
                    }
                    gm_b16_8 t263;
                    GM_ST(void *, (uint8_t *)&t263, (void *)(gm_sk754mj3p1el6));
                    GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(1));
                    gm_l1 = t263;
                    goto gm_lb1;
                }
gm_lb65:;
                {
                    uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t265 = (uint8_t)(t264 == ((uint8_t)64ULL));
                    if (!t265) {
                        goto gm_lb66;
                    }
                    gm_b16_8 t266;
                    GM_ST(void *, (uint8_t *)&t266, (void *)(gm_s2053hhvf16fm));
                    GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(1));
                    gm_l1 = t266;
                    goto gm_lb1;
                }
gm_lb66:;
                {
                    uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t268 = (uint8_t)(t267 == ((uint8_t)65ULL));
                    if (!t268) {
                        goto gm_lb67;
                    }
                    gm_b16_8 t269;
                    GM_ST(void *, (uint8_t *)&t269, (void *)(gm_sh40na9qbga3l));
                    GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(1));
                    gm_l1 = t269;
                    goto gm_lb1;
                }
gm_lb67:;
            }
            {
                {
                    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t271 = (uint8_t)(t270 == ((uint8_t)66ULL));
                    if (!t271) {
                        goto gm_lb68;
                    }
                    gm_b16_8 t272;
                    GM_ST(void *, (uint8_t *)&t272, (void *)(gm_s2cuflhqkkuaq));
                    GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(1));
                    gm_l1 = t272;
                    goto gm_lb1;
                }
gm_lb68:;
                {
                    uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t274 = (uint8_t)(t273 == ((uint8_t)67ULL));
                    if (!t274) {
                        goto gm_lb69;
                    }
                    gm_b16_8 t275;
                    GM_ST(void *, (uint8_t *)&t275, (void *)(gm_s1c4sfff8ac39));
                    GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(6));
                    gm_l1 = t275;
                    goto gm_lb1;
                }
gm_lb69:;
                {
                    uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t277 = (uint8_t)(t276 == ((uint8_t)68ULL));
                    if (!t277) {
                        goto gm_lb70;
                    }
                    gm_b16_8 t278;
                    GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sidtn67sdk4e0));
                    GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(6));
                    gm_l1 = t278;
                    goto gm_lb1;
                }
gm_lb70:;
                gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 146, 45);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t279 = gm_l1;
        F.ret = t279;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
