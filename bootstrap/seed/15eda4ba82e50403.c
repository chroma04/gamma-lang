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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_binop_symbol_9ijp0eo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_block_children_r2qinti {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_conds_children_3kg6846 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_default_spec_9ntjq2s {
    gm_frame h;
    gm_t7g88i52rrtm5 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hdas5h7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_each_child_fm49s73 {
    gm_frame h;
    gm_t710lsrq1m8gk l39; /* w */
    gm_tsth9qu1dv0g3 l54; /* callee */
    gm_t710lsrq1m8gk l70; /* x */
    gm_b0_1 ret;
};
struct gm_fr_for_each_subpat_arlrtrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_default_spec_cdd8jng {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_pat_children_e3peovr {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_sys_name_pcdoujk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_untyped_complex_20agrmu {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* re */
    gm_tngtuiuskn938 l1; /* im */
    gm_t2b6tm8c0vgo1 ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_t2b6tm8c0vgo1 ret;
};
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_block_children_r2qinti(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_conds_children_3kg6846(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) gm_t7g88i52rrtm5 gm_f_default_spec_9ntjq2s_c(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_fnepb5f(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pat_children_e3peovr(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_k0mnn4ntfo5[40] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_km0lis6h9ua[40] __attribute__((aligned(8)));
extern uint8_t gm_ksksp29ghso[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1pfh89airaa0[13];
extern const uint8_t gm_s26ul1d5snk55[9];
extern const uint8_t gm_s2jvv6mjt9vs3[13];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s4jhjvgae4896[13];
extern const uint8_t gm_s59a022mrijic[9];
extern const uint8_t gm_s65khs4s753q2[11];
extern const uint8_t gm_s6f0l2e1iha9i[8];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s78m4481dj77r[9];
extern const uint8_t gm_s7ri8monh3c4h[8];
extern const uint8_t gm_s84p47580vnhr[12];
extern const uint8_t gm_s86kdga2ul6h4[9];
extern const uint8_t gm_s8o3vqolbfl8t[11];
extern const uint8_t gm_s9819enjtsd7t[10];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_saamkuuf6jrtf[8];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sal1kdi1rij5t[10];
extern const uint8_t gm_sb641udihu5hs[10];
extern const uint8_t gm_sbgj0biroecr0[13];
extern const uint8_t gm_sd9o9rh5qe8e7[11];
extern const uint8_t gm_sdtlgdl6ed52a[14];
extern const uint8_t gm_se3l2nlomk2pj[9];
extern const uint8_t gm_seajfhgtfmhv7[12];
extern const uint8_t gm_sed3elknidrs3[11];
extern const uint8_t gm_sf5jsgjp29cqn[14];
extern const uint8_t gm_sfbjh3dq66kkg[17];
extern const uint8_t gm_sfcjpmkuggqo3[10];
extern const uint8_t gm_sfd9hctnph9ef[12];
extern const uint8_t gm_sfku76mf5fn5d[9];
extern const uint8_t gm_shegcik3mpeut[12];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_si8jv2q8d22kl[10];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sik1l49uru5dh[13];
extern const uint8_t gm_siph7o6oug1g2[14];
extern const uint8_t gm_siqaapb0mqsrg[14];
extern const uint8_t gm_sj49v74ko3ghf[15];
extern const uint8_t gm_sk0bnuggr6sri[14];
extern const uint8_t gm_sk1d0v1onojc1[8];
extern const uint8_t gm_sk77bvr488rq3[10];
extern const uint8_t gm_skk6lm5m17t6v[11];
extern const uint8_t gm_skm4ndn7op5l5[14];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skqa2fjpbsodp[9];
extern const uint8_t gm_skti0sb80pmgp[12];
extern const uint8_t gm_slginip33cenb[14];
extern const uint8_t gm_slkubuckp49ih[12];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smatmsv0oedot[8];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smfprcpea3ibe[10];
extern const uint8_t gm_smvhngj8mlfjc[13];
extern const uint8_t gm_sp8bmcd3m68n6[7];
extern const char gm_srcod98slk3vnra[];
extern const uint8_t gm_ssj8r83l87dr2[16];
extern const uint8_t gm_sso3tuh819o23[9];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stclkv4qnta30[9];
extern const uint8_t gm_stl47boisb31o[11];
extern const uint8_t gm_su6ipjjrnjs7p[10];
extern const uint8_t gm_suno9ilftpfht[12];
extern const uint8_t gm_sv1bu4l4kq188[8];
extern const uint8_t gm_sv9p44pp2n1jk[10];
extern const uint8_t gm_svkvl31jmj9ne[12];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    F.l2 = ((uint8_t)0);
    void * t2 = gm_l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_spec_9ntjq2s F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9805qtonbsag t2;
    memset(&t2, 0, sizeof t2);
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_t7g88i52rrtm5 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 6, 0);
    GM_ST(uint8_t, (uint8_t *)&t5 + 21, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 22, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)32ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5 + 1), t3);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 2), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t5 + 3), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t5 + 4), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), ((uint32_t)0ULL));
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t5 + 12), t2);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5 + 20), t4);
    memmove(gm_out, ((uint8_t *)&t5), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
        void * t5 = gm_l0;
        gm_closure t6 = t4;
        gm_node *ccx7 = t6.cx;
        void * t8 = t6.code;
        uint8_t t9;
        if (GM_PTR_TAGGED(t8)) {
            t9 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t8))(t5);
        } else {
            t9 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t8)(t6.env, ccx7, NULL, t5);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_hdas5h7 F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pat_children_e3peovr *)((&F.h)->up))->l1));
    gm_f_pat_children_e3peovr(t2, t3);
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l3;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l2;
    gm_t2b6tm8c0vgo1 t4 = GM_LD(gm_t2b6tm8c0vgo1, ((uint8_t *)&F.l1));
    gm_tcgrjq9toscke t5;
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 84, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_t2b6tm8c0vgo1, ((uint8_t *)&t5 + 8), t4);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 72), t3);
    gm_tq7s98ihonkju t6 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kg80t7hdccb)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcod98slk3vnra, 572, 81);
    void * t8 = GM_LD(void *, (((uint8_t *)&gm_l3) + 8));
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l3;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l2;
    gm_tb38v5s9j1r32 t4 = GM_LD(gm_tb38v5s9j1r32, ((uint8_t *)&F.l1));
    gm_tc9qv69frb08e t5;
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 132, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_tb38v5s9j1r32, ((uint8_t *)&t5 + 8), t4);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 120), t3);
    gm_tq7s98ihonkju t6 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksksp29ghso)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcod98slk3vnra, 325, 83);
    void * t8 = GM_LD(void *, (((uint8_t *)&gm_l3) + 8));
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
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_untyped_complex_20agrmu F;
    GM_STACK_CHECK(&F, 489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tq7s98ihonkju gm_l2;
    gm_tngtuiuskn938 t2 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)a0));
    gm_tngtuiuskn938 t3 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l1));
    gm_tu15p0hi2qlv6 t4;
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t4 + 96), t3);
    gm_tq7s98ihonkju t5 = gm_f_new_fnepb5f((void *)NULL, cx, NULL, &t4, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0mnn4ntfo5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcod98slk3vnra, 578, 89);
    void * t7 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
    gm_t2b6tm8c0vgo1 t8;
    memset(&t8, 0, sizeof t8);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)3ULL));
    GM_ST(void *, ((uint8_t *)&t8 + 8), t7);
    memmove(gm_out, ((uint8_t *)&t8), 64);
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju gm_l1;
    gm_tq7s98ihonkju t2 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km0lis6h9ua)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t3) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcod98slk3vnra, 575, 80);
    void * t4 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
    gm_t2b6tm8c0vgo1 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
    GM_ST(void *, ((uint8_t *)&t5 + 8), t4);
    memmove(gm_out, ((uint8_t *)&t5), 64);
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
extern __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binop_symbol_9ijp0eo F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sajhum9b4nr96));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_l1 = t19;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
            if (!t24) {
                goto gm_lb4;
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s6s96daqmhcs9));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
            gm_l1 = t25;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)3ULL));
            if (!t27) {
                goto gm_lb5;
            }
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_si62ejtp0vbct));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(2));
            gm_l1 = t28;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)4ULL));
            if (!t30) {
                goto gm_lb6;
            }
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
            gm_l1 = t31;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)5ULL));
            if (!t33) {
                goto gm_lb7;
            }
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_siamquc19uikr));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(1));
            gm_l1 = t34;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)6ULL));
            if (!t36) {
                goto gm_lb8;
            }
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s9hi985r6f61m));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(2));
            gm_l1 = t37;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)7ULL));
            if (!t39) {
                goto gm_lb9;
            }
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_smerqm8g793l6));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(1));
            gm_l1 = t40;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)8ULL));
            if (!t42) {
                goto gm_lb10;
            }
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(1));
            gm_l1 = t43;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)9ULL));
            if (!t45) {
                goto gm_lb11;
            }
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(1));
            gm_l1 = t46;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)10ULL));
            if (!t48) {
                goto gm_lb12;
            }
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
            gm_l1 = t49;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)11ULL));
            if (!t51) {
                goto gm_lb13;
            }
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s4g3cajt15dmi));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(2));
            gm_l1 = t52;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)12ULL));
            if (!t54) {
                goto gm_lb14;
            }
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(1));
            gm_l1 = t55;
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)13ULL));
            if (!t57) {
                goto gm_lb15;
            }
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s02ru83e3fffc));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
            gm_l1 = t58;
            goto gm_lb1;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 356, 48);
    }
gm_lb1:;
    gm_b16_8 t59 = gm_l1;
    F.ret = t59;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_block_children_r2qinti(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_block_children_r2qinti F;
    void * gm_l0; /* b */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    void * gm_l5; /* x */
    void * gm_l6; /* x */
    void * gm_l7; /* p */
    void * gm_l8; /* v */
    gm_b16_8 gm_l9; /* ps */
    void * gm_l10; /* v */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l13; /* p */
    void * gm_l14; /* x */
    void * gm_l15; /* x */
    void * gm_l16; /* x */
    void * gm_l17; /* t */
    GM_STACK_CHECK(&F, 996u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t26 = gm_l0;
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)t26));
    gm_l2 = t27;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l3;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t33 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t31) + (uintptr_t)t33 * 32), 32);
                        {
                            {
                                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t35 = (uint8_t)(t34 == ((uint8_t)0ULL));
                                if (!t35) {
                                    goto gm_lb7;
                                }
                                uint8_t t36 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0);
                                if (t36) {
                                    goto gm_lb7;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t38 = (uint8_t)(t37 == ((uint8_t)0ULL));
                                if (!t38) {
                                    goto gm_lb8;
                                }
                                uint8_t t39 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0);
                                if (!t39) {
                                    goto gm_lb8;
                                }
                                void * t40 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l5 = t40;
                                gm_closure t41 = gm_l1;
                                void * t42 = gm_l5;
                                gm_closure t43 = t41;
                                gm_node *ccx44 = t43.cx;
                                void * t45 = t43.code;
                                gm_b0_1 t46;
                                if (GM_PTR_TAGGED(t45)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t45))(t42);
                                } else {
                                    t46 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t45)(t43.env, ccx44, NULL, t42);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
                                if (!t48) {
                                    goto gm_lb9;
                                }
                                void * t49 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l6 = t49;
                                gm_closure t50 = gm_l1;
                                void * t51 = gm_l6;
                                gm_closure t52 = t50;
                                gm_node *ccx53 = t52.cx;
                                void * t54 = t52.code;
                                gm_b0_1 t55;
                                if (GM_PTR_TAGGED(t54)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t54))(t51);
                                } else {
                                    t55 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t54)(t52.env, ccx53, NULL, t51);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t57 = (uint8_t)(t56 == ((uint8_t)2ULL));
                                if (!t57) {
                                    goto gm_lb10;
                                }
                                void * t58 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l7 = t58;
                                void * t59 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l8 = t59;
                                gm_closure t60 = gm_l1;
                                void * t61 = gm_l7;
                                gm_closure t62 = t60;
                                gm_node *ccx63 = t62.cx;
                                void * t64 = t62.code;
                                gm_b0_1 t65;
                                if (GM_PTR_TAGGED(t64)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t64))(t61);
                                } else {
                                    t65 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t64)(t62.env, ccx63, NULL, t61);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_closure t66 = gm_l1;
                                void * t67 = gm_l8;
                                gm_closure t68 = t66;
                                gm_node *ccx69 = t68.cx;
                                void * t70 = t68.code;
                                gm_b0_1 t71;
                                if (GM_PTR_TAGGED(t70)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t70))(t67);
                                } else {
                                    t71 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t70)(t68.env, ccx69, NULL, t67);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t73 = (uint8_t)(t72 == ((uint8_t)3ULL));
                                if (!t73) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l9 = t74;
                                void * t75 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l10 = t75;
                                gm_b16_8 t76 = gm_l9;
                                gm_l11 = t76;
                                gm_l12 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t77 = gm_l12;
                                        uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                        uint8_t t79 = (t77 < t78);
                                        if (!t79) {
                                            goto gm_lb12;
                                        }
                                        {
                                            void * t80 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                            uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                            uint64_t t82 = gm_l12;
                                            {
                                                {
                                                    void * t83 = GM_LD(void *, (((uint8_t *)t80) + (uintptr_t)t82 * 8));
                                                    gm_l13 = t83;
                                                    {
                                                        {
                                                            uint8_t t84 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l13)) != 0);
                                                            if (!t84) {
                                                                goto gm_lb18;
                                                            }
                                                            void * t85 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                                            gm_l14 = t85;
                                                            gm_closure t86 = gm_l1;
                                                            void * t87 = gm_l14;
                                                            gm_closure t88 = t86;
                                                            gm_node *ccx89 = t88.cx;
                                                            void * t90 = t88.code;
                                                            gm_b0_1 t91;
                                                            if (GM_PTR_TAGGED(t90)) {
                                                                ((void (*)(void *))GM_PTR_UNTAG(t90))(t87);
                                                            } else {
                                                                t91 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t90)(t88.env, ccx89, NULL, t87);
                                                            }
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_lb17;
                                                        }
gm_lb18:;
                                                    }
gm_lb17:;
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 690, 17);
                                            }
gm_lb15:;
                                        }
gm_lb14:;
                                        uint64_t t92 = gm_l12;
                                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                                        uint64_t t95 = r93;
                                        gm_l12 = t95;
                                    }
                                }
gm_lb12:;
                                gm_closure t96 = gm_l1;
                                void * t97 = gm_l10;
                                gm_closure t98 = t96;
                                gm_node *ccx99 = t98.cx;
                                void * t100 = t98.code;
                                gm_b0_1 t101;
                                if (GM_PTR_TAGGED(t100)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t100))(t97);
                                } else {
                                    t101 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t100)(t98.env, ccx99, NULL, t97);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                {
                                    {
                                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                        uint8_t t103 = (uint8_t)(t102 == ((uint8_t)4ULL));
                                        if (!t103) {
                                            goto gm_lb21;
                                        }
                                        void * t104 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                        gm_l15 = t104;
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    {
                                        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                        uint8_t t106 = (uint8_t)(t105 == ((uint8_t)5ULL));
                                        if (!t106) {
                                            goto gm_lb22;
                                        }
                                        void * t107 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                        gm_l15 = t107;
                                        goto gm_lb20;
                                    }
gm_lb22:;
                                    goto gm_lb19;
                                }
gm_lb20:;
                                gm_closure t108 = gm_l1;
                                void * t109 = gm_l15;
                                gm_closure t110 = t108;
                                gm_node *ccx111 = t110.cx;
                                void * t112 = t110.code;
                                gm_b0_1 t113;
                                if (GM_PTR_TAGGED(t112)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t112))(t109);
                                } else {
                                    t113 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t112)(t110.env, ccx111, NULL, t109);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 681, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 680, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t114 = gm_l3;
            uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
            uint64_t t117 = r115;
            gm_l3 = t117;
        }
    }
gm_lb1:;
    {
        {
            void * t118 = gm_l0;
            uint8_t t119 = (uint8_t)(GM_LD(void *, (((uint8_t *)t118) + 16)) != 0);
            if (!t119) {
                goto gm_lb24;
            }
            void * t120 = GM_LD(void *, (((uint8_t *)t118) + 16));
            gm_l17 = t120;
            gm_closure t121 = gm_l1;
            void * t122 = gm_l17;
            gm_closure t123 = t121;
            gm_node *ccx124 = t123.cx;
            void * t125 = t123.code;
            gm_b0_1 t126;
            if (GM_PTR_TAGGED(t125)) {
                ((void (*)(void *))GM_PTR_UNTAG(t125))(t122);
            } else {
                t126 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t125)(t123.env, ccx124, NULL, t122);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb23;
        }
gm_lb24:;
    }
gm_lb23:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_conds_children_3kg6846(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conds_children_3kg6846 F;
    gm_b16_8 gm_l0; /* cond */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t10 = gm_l0;
    gm_l2 = t10;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t16 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t14) + (uintptr_t)t16 * 24), 24);
                        {
                            {
                                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                void * t19 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t19;
                                gm_closure t20 = gm_l1;
                                void * t21 = gm_l5;
                                gm_closure t22 = t20;
                                gm_node *ccx23 = t22.cx;
                                void * t24 = t22.code;
                                gm_b0_1 t25;
                                if (GM_PTR_TAGGED(t24)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t24))(t21);
                                } else {
                                    t25 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t24)(t22.env, ccx23, NULL, t21);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                                if (!t27) {
                                    goto gm_lb8;
                                }
                                void * t28 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t28;
                                void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t29;
                                void * t30 = gm_l6;
                                gm_closure t31 = gm_l1;
                                gm_f_pat_children_e3peovr(t30, t31);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_closure t32 = gm_l1;
                                void * t33 = gm_l7;
                                gm_closure t34 = t32;
                                gm_node *ccx35 = t34.cx;
                                void * t36 = t34.code;
                                gm_b0_1 t37;
                                if (GM_PTR_TAGGED(t36)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t36))(t33);
                                } else {
                                    t37 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t36)(t34.env, ccx35, NULL, t33);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 703, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 702, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t38 = gm_l3;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            uint64_t t41 = r39;
            gm_l3 = t41;
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
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) gm_t7g88i52rrtm5 gm_f_default_spec_9ntjq2s_c(void) { gm_t7g88i52rrtm5 gm_r; gm_f_default_spec_9ntjq2s(&gm_r); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_child_fm49s73 F;
    void * gm_l0; /* e */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* a */
    void * gm_l3; /* a */
    void * gm_l4; /* a */
    void * gm_l5; /* a */
    void * gm_l6; /* a */
    void * gm_l7; /* a */
    void * gm_l8; /* a */
    void * gm_l9; /* a */
    gm_tao63pk3r18v5 gm_l10; /* sl */
    void * gm_l11; /* x */
    void * gm_l12; /* x */
    void * gm_l13; /* a */
    void * gm_l14; /* a */
    void * gm_l15; /* a */
    gm_t3dcoc7077t1q gm_l16; /* x */
    gm_t083kd7b1kkag gm_l17; /* x */
    void * gm_l18; /* a */
    void * gm_l19; /* a */
    void * gm_l20; /* k */
    void * gm_l21; /* v */
    void * gm_l22; /* k */
    void * gm_l23; /* a */
    void * gm_l24; /* b */
    void * gm_l25; /* a */
    void * gm_l26; /* b */
    void * gm_l27; /* a */
    void * gm_l28; /* b */
    void * gm_l29; /* a */
    void * gm_l30; /* b */
    void * gm_l31; /* a */
    void * gm_l32; /* b */
    void * gm_l33; /* s */
    gm_b16_8 gm_l34; /* arms */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l37; /* a */
    void * gm_l38; /* g */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_t4lnf71mruqpt gm_l42; /* h */
    void * gm_l43; /* x */
    void * gm_l44; /* x */
    void * gm_l45; /* x */
    gm_b16_8 gm_l46; /* args */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    uint64_t gm_l49; /* i */
    gm_tandvbt63uk7n gm_l50; /* x */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* i */
    gm_b16_8 gm_l55; /* args */
    void * gm_l56; /* c */
    uint64_t gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* i */
    gm_b16_8 gm_l60; /* xs */
    gm_b16_8 gm_l61; /* xs */
    uint64_t gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* i */
    gm_ttggq7ng4kp3p gm_l65; /* v */
    void * gm_l66; /* p */
    void * gm_l67; /* b */
    gm_trl2khmuphncd gm_l68; /* x */
    void * gm_l69; /* e2 */
    void * gm_l71; /* st */
    gm_teo9d4mncjfdu gm_l72; /* x */
    void * gm_l73; /* v */
    void * gm_l74; /* w */
    void * gm_l75; /* x */
    void * gm_l76; /* t */
    uint64_t gm_l77; /* <tmp> */
    uint64_t gm_l78; /* <tmp> */
    uint64_t gm_l79; /* i */
    void * gm_l80; /* w */
    void * gm_l81; /* x */
    void * gm_l82; /* a */
    GM_STACK_CHECK(&F, 4989u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t113 = gm_l0;
        {
            {
                {
                    {
                        {
                            uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)18ULL));
                            if (!t115) {
                                goto gm_lb4;
                            }
                            void * t116 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t116;
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)24ULL));
                            if (!t118) {
                                goto gm_lb5;
                            }
                            void * t119 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t119;
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t121 = (uint8_t)(t120 == ((uint8_t)25ULL));
                            if (!t121) {
                                goto gm_lb6;
                            }
                            void * t122 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t122;
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t124 = (uint8_t)(t123 == ((uint8_t)39ULL));
                            if (!t124) {
                                goto gm_lb7;
                            }
                            void * t125 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t125;
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t127 = (uint8_t)(t126 == ((uint8_t)37ULL));
                            if (!t127) {
                                goto gm_lb8;
                            }
                            void * t128 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t128;
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t130 = (uint8_t)(t129 == ((uint8_t)52ULL));
                            if (!t130) {
                                goto gm_lb9;
                            }
                            void * t131 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t131;
                            goto gm_lb3;
                        }
gm_lb9:;
                        {
                            uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t133 = (uint8_t)(t132 == ((uint8_t)22ULL));
                            if (!t133) {
                                goto gm_lb10;
                            }
                            void * t134 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t134;
                            goto gm_lb3;
                        }
gm_lb10:;
                        {
                            uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t136 = (uint8_t)(t135 == ((uint8_t)23ULL));
                            if (!t136) {
                                goto gm_lb11;
                            }
                            void * t137 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l2 = t137;
                            goto gm_lb3;
                        }
gm_lb11:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    gm_closure t138 = gm_l1;
                    void * t139 = gm_l2;
                    gm_closure t140 = t138;
                    gm_node *ccx141 = t140.cx;
                    void * t142 = t140.code;
                    gm_b0_1 t143;
                    if (GM_PTR_TAGGED(t142)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t142))(t139);
                    } else {
                        t143 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t142)(t140.env, ccx141, NULL, t139);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t145 = (uint8_t)(t144 == ((uint8_t)21ULL));
                    if (!t145) {
                        goto gm_lb12;
                    }
                    memmove(((uint8_t *)&gm_l10), ((((uint8_t *)t113) + 8) + 8), 32);
                    gm_closure t146 = gm_l1;
                    void * t147 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    gm_closure t148 = t146;
                    gm_node *ccx149 = t148.cx;
                    void * t150 = t148.code;
                    gm_b0_1 t151;
                    if (GM_PTR_TAGGED(t150)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t150))(t147);
                    } else {
                        t151 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t150)(t148.env, ccx149, NULL, t147);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t152 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l10) + 8)) != 0);
                            if (!t152) {
                                goto gm_lb14;
                            }
                            void * t153 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                            gm_l11 = t153;
                            gm_closure t154 = gm_l1;
                            void * t155 = gm_l11;
                            gm_closure t156 = t154;
                            gm_node *ccx157 = t156.cx;
                            void * t158 = t156.code;
                            gm_b0_1 t159;
                            if (GM_PTR_TAGGED(t158)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t158))(t155);
                            } else {
                                t159 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t158)(t156.env, ccx157, NULL, t155);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb13;
                        }
gm_lb14:;
                    }
gm_lb13:;
                    {
                        {
                            uint8_t t160 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l10) + 16)) != 0);
                            if (!t160) {
                                goto gm_lb16;
                            }
                            void * t161 = GM_LD(void *, (((uint8_t *)&gm_l10) + 16));
                            gm_l12 = t161;
                            gm_closure t162 = gm_l1;
                            void * t163 = gm_l12;
                            gm_closure t164 = t162;
                            gm_node *ccx165 = t164.cx;
                            void * t166 = t164.code;
                            gm_b0_1 t167;
                            if (GM_PTR_TAGGED(t166)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t166))(t163);
                            } else {
                                t167 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t166)(t164.env, ccx165, NULL, t163);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb15;
                        }
gm_lb16:;
                    }
gm_lb15:;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t169 = (uint8_t)(t168 == ((uint8_t)29ULL));
                    if (!t169) {
                        goto gm_lb17;
                    }
                    void * t170 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                    gm_l13 = t170;
                    gm_closure t171 = gm_l1;
                    void * t172 = gm_l13;
                    gm_closure t173 = t171;
                    gm_node *ccx174 = t173.cx;
                    void * t175 = t173.code;
                    gm_b0_1 t176;
                    if (GM_PTR_TAGGED(t175)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t175))(t172);
                    } else {
                        t176 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t175)(t173.env, ccx174, NULL, t172);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb17:;
                {
                    uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t178 = (uint8_t)(t177 == ((uint8_t)32ULL));
                    if (!t178) {
                        goto gm_lb18;
                    }
                    void * t179 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                    gm_l14 = t179;
                    gm_closure t180 = gm_l1;
                    void * t181 = gm_l14;
                    gm_closure t182 = t180;
                    gm_node *ccx183 = t182.cx;
                    void * t184 = t182.code;
                    gm_b0_1 t185;
                    if (GM_PTR_TAGGED(t184)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t184))(t181);
                    } else {
                        t185 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t184)(t182.env, ccx183, NULL, t181);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t187 = (uint8_t)(t186 == ((uint8_t)40ULL));
                    if (!t187) {
                        goto gm_lb19;
                    }
                    void * t188 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l15 = t188;
                    gm_closure t189 = gm_l1;
                    void * t190 = gm_l15;
                    gm_closure t191 = t189;
                    gm_node *ccx192 = t191.cx;
                    void * t193 = t191.code;
                    gm_b0_1 t194;
                    if (GM_PTR_TAGGED(t193)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t193))(t190);
                    } else {
                        t194 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t193)(t191.env, ccx192, NULL, t190);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb19:;
                {
                    uint8_t t195 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t196 = (uint8_t)(t195 == ((uint8_t)41ULL));
                    if (!t196) {
                        goto gm_lb20;
                    }
                    memmove(((uint8_t *)&gm_l16), ((((uint8_t *)t113) + 8) + 8), 24);
                    gm_closure t197 = gm_l1;
                    void * t198 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    gm_closure t199 = t197;
                    gm_node *ccx200 = t199.cx;
                    void * t201 = t199.code;
                    gm_b0_1 t202;
                    if (GM_PTR_TAGGED(t201)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t201))(t198);
                    } else {
                        t202 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t201)(t199.env, ccx200, NULL, t198);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t203 = gm_l1;
                    void * t204 = GM_LD(void *, (((uint8_t *)&gm_l16) + 16));
                    gm_closure t205 = t203;
                    gm_node *ccx206 = t205.cx;
                    void * t207 = t205.code;
                    gm_b0_1 t208;
                    if (GM_PTR_TAGGED(t207)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t207))(t204);
                    } else {
                        t208 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t207)(t205.env, ccx206, NULL, t204);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t209 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t210 = (uint8_t)(t209 == ((uint8_t)42ULL));
                    if (!t210) {
                        goto gm_lb21;
                    }
                    memmove(((uint8_t *)&gm_l17), ((((uint8_t *)t113) + 8) + 8), 24);
                    gm_closure t211 = gm_l1;
                    void * t212 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    gm_closure t213 = t211;
                    gm_node *ccx214 = t213.cx;
                    void * t215 = t213.code;
                    gm_b0_1 t216;
                    if (GM_PTR_TAGGED(t215)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t215))(t212);
                    } else {
                        t216 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t215)(t213.env, ccx214, NULL, t212);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t218 = (uint8_t)(t217 == ((uint8_t)19ULL));
                    if (!t218) {
                        goto gm_lb22;
                    }
                    void * t219 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l18 = t219;
                    gm_closure t220 = gm_l1;
                    void * t221 = gm_l18;
                    gm_closure t222 = t220;
                    gm_node *ccx223 = t222.cx;
                    void * t224 = t222.code;
                    gm_b0_1 t225;
                    if (GM_PTR_TAGGED(t224)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t224))(t221);
                    } else {
                        t225 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t224)(t222.env, ccx223, NULL, t221);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t226 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t227 = (uint8_t)(t226 == ((uint8_t)51ULL));
                    if (!t227) {
                        goto gm_lb23;
                    }
                    void * t228 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                    gm_l19 = t228;
                    gm_closure t229 = gm_l1;
                    void * t230 = gm_l19;
                    gm_closure t231 = t229;
                    gm_node *ccx232 = t231.cx;
                    void * t233 = t231.code;
                    gm_b0_1 t234;
                    if (GM_PTR_TAGGED(t233)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t233))(t230);
                    } else {
                        t234 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t233)(t231.env, ccx232, NULL, t230);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t235 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t236 = (uint8_t)(t235 == ((uint8_t)53ULL));
                    if (!t236) {
                        goto gm_lb24;
                    }
                    void * t237 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l20 = t237;
                    void * t238 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                    gm_l21 = t238;
                    gm_closure t239 = gm_l1;
                    void * t240 = gm_l20;
                    gm_closure t241 = t239;
                    gm_node *ccx242 = t241.cx;
                    void * t243 = t241.code;
                    gm_b0_1 t244;
                    if (GM_PTR_TAGGED(t243)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t243))(t240);
                    } else {
                        t244 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t243)(t241.env, ccx242, NULL, t240);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t245 = gm_l1;
                    void * t246 = gm_l21;
                    gm_closure t247 = t245;
                    gm_node *ccx248 = t247.cx;
                    void * t249 = t247.code;
                    gm_b0_1 t250;
                    if (GM_PTR_TAGGED(t249)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t249))(t246);
                    } else {
                        t250 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t249)(t247.env, ccx248, NULL, t246);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb24:;
                {
                    uint8_t t251 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t252 = (uint8_t)(t251 == ((uint8_t)54ULL));
                    if (!t252) {
                        goto gm_lb25;
                    }
                    void * t253 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l22 = t253;
                    gm_closure t254 = gm_l1;
                    void * t255 = gm_l22;
                    gm_closure t256 = t254;
                    gm_node *ccx257 = t256.cx;
                    void * t258 = t256.code;
                    gm_b0_1 t259;
                    if (GM_PTR_TAGGED(t258)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t258))(t255);
                    } else {
                        t259 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t258)(t256.env, ccx257, NULL, t255);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t260 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t261 = (uint8_t)(t260 == ((uint8_t)20ULL));
                    if (!t261) {
                        goto gm_lb26;
                    }
                    void * t262 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l23 = t262;
                    void * t263 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                    gm_l24 = t263;
                    gm_closure t264 = gm_l1;
                    void * t265 = gm_l23;
                    gm_closure t266 = t264;
                    gm_node *ccx267 = t266.cx;
                    void * t268 = t266.code;
                    gm_b0_1 t269;
                    if (GM_PTR_TAGGED(t268)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t268))(t265);
                    } else {
                        t269 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t268)(t266.env, ccx267, NULL, t265);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t270 = gm_l1;
                    void * t271 = gm_l24;
                    gm_closure t272 = t270;
                    gm_node *ccx273 = t272.cx;
                    void * t274 = t272.code;
                    gm_b0_1 t275;
                    if (GM_PTR_TAGGED(t274)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t274))(t271);
                    } else {
                        t275 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t274)(t272.env, ccx273, NULL, t271);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb26:;
                {
                    {
                        {
                            uint8_t t276 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t277 = (uint8_t)(t276 == ((uint8_t)30ULL));
                            if (!t277) {
                                goto gm_lb29;
                            }
                            void * t278 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                            gm_l25 = t278;
                            void * t279 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 16));
                            gm_l26 = t279;
                            goto gm_lb28;
                        }
gm_lb29:;
                        {
                            uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t281 = (uint8_t)(t280 == ((uint8_t)31ULL));
                            if (!t281) {
                                goto gm_lb30;
                            }
                            void * t282 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                            gm_l25 = t282;
                            void * t283 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 16));
                            gm_l26 = t283;
                            goto gm_lb28;
                        }
gm_lb30:;
                        goto gm_lb27;
                    }
gm_lb28:;
                    gm_closure t284 = gm_l1;
                    void * t285 = gm_l25;
                    gm_closure t286 = t284;
                    gm_node *ccx287 = t286.cx;
                    void * t288 = t286.code;
                    gm_b0_1 t289;
                    if (GM_PTR_TAGGED(t288)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t288))(t285);
                    } else {
                        t289 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t288)(t286.env, ccx287, NULL, t285);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t290 = gm_l1;
                    void * t291 = gm_l26;
                    gm_closure t292 = t290;
                    gm_node *ccx293 = t292.cx;
                    void * t294 = t292.code;
                    gm_b0_1 t295;
                    if (GM_PTR_TAGGED(t294)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t294))(t291);
                    } else {
                        t295 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t294)(t292.env, ccx293, NULL, t291);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    {
                        {
                            uint8_t t296 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t297 = (uint8_t)(t296 == ((uint8_t)43ULL));
                            if (!t297) {
                                goto gm_lb33;
                            }
                            void * t298 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l29 = t298;
                            void * t299 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                            gm_l30 = t299;
                            goto gm_lb32;
                        }
gm_lb33:;
                        {
                            uint8_t t300 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t301 = (uint8_t)(t300 == ((uint8_t)45ULL));
                            if (!t301) {
                                goto gm_lb34;
                            }
                            void * t302 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                            gm_l29 = t302;
                            void * t303 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 8));
                            gm_l30 = t303;
                            goto gm_lb32;
                        }
gm_lb34:;
                        goto gm_lb31;
                    }
gm_lb32:;
                    gm_closure t304 = gm_l1;
                    void * t305 = gm_l29;
                    gm_closure t306 = t304;
                    gm_node *ccx307 = t306.cx;
                    void * t308 = t306.code;
                    gm_b0_1 t309;
                    if (GM_PTR_TAGGED(t308)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t308))(t305);
                    } else {
                        t309 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t308)(t306.env, ccx307, NULL, t305);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t310 = gm_l1;
                    void * t311 = gm_l30;
                    gm_closure t312 = t310;
                    gm_node *ccx313 = t312.cx;
                    void * t314 = t312.code;
                    gm_b0_1 t315;
                    if (GM_PTR_TAGGED(t314)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t314))(t311);
                    } else {
                        t315 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t314)(t312.env, ccx313, NULL, t311);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb31:;
            }
            {
                {
                    uint8_t t316 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t317 = (uint8_t)(t316 == ((uint8_t)44ULL));
                    if (!t317) {
                        goto gm_lb35;
                    }
                    void * t318 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l33 = t318;
                    gm_b16_8 t319 = GM_LD(gm_b16_8, (((((uint8_t *)t113) + 8) + 8) + 8));
                    gm_l34 = t319;
                    gm_closure t320 = gm_l1;
                    void * t321 = gm_l33;
                    gm_closure t322 = t320;
                    gm_node *ccx323 = t322.cx;
                    void * t324 = t322.code;
                    gm_b0_1 t325;
                    if (GM_PTR_TAGGED(t324)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t324))(t321);
                    } else {
                        t325 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t324)(t322.env, ccx323, NULL, t321);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t326 = gm_l34;
                    gm_l35 = t326;
                    gm_l36 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t327 = gm_l36;
                            uint64_t t328 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                            uint8_t t329 = (t327 < t328);
                            if (!t329) {
                                goto gm_lb36;
                            }
                            {
                                void * t330 = GM_LD(void *, ((uint8_t *)&gm_l35));
                                uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                uint64_t t332 = gm_l36;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l37), (((uint8_t *)t330) + (uintptr_t)t332 * 24), 24);
                                        void * t333 = GM_LD(void *, ((uint8_t *)&gm_l37));
                                        gm_closure t334 = gm_l1;
                                        gm_f_pat_children_e3peovr(t333, t334);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        {
                                            {
                                                uint8_t t335 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l37) + 8)) != 0);
                                                if (!t335) {
                                                    goto gm_lb42;
                                                }
                                                void * t336 = GM_LD(void *, (((uint8_t *)&gm_l37) + 8));
                                                gm_l38 = t336;
                                                gm_closure t337 = gm_l1;
                                                void * t338 = gm_l38;
                                                gm_closure t339 = t337;
                                                gm_node *ccx340 = t339.cx;
                                                void * t341 = t339.code;
                                                gm_b0_1 t342;
                                                if (GM_PTR_TAGGED(t341)) {
                                                    ((void (*)(void *))GM_PTR_UNTAG(t341))(t338);
                                                } else {
                                                    t342 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t341)(t339.env, ccx340, NULL, t338);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                goto gm_lb41;
                                            }
gm_lb42:;
                                        }
gm_lb41:;
                                        gm_closure t343 = gm_l1;
                                        void * t344 = GM_LD(void *, (((uint8_t *)&gm_l37) + 16));
                                        gm_closure t345 = t343;
                                        gm_node *ccx346 = t345.cx;
                                        void * t347 = t345.code;
                                        gm_b0_1 t348;
                                        if (GM_PTR_TAGGED(t347)) {
                                            ((void (*)(void *))GM_PTR_UNTAG(t347))(t344);
                                        } else {
                                            t348 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t347)(t345.env, ccx346, NULL, t344);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb39;
                                    }
gm_lb40:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 619, 13);
                                }
gm_lb39:;
                            }
gm_lb38:;
                            uint64_t t349 = gm_l36;
                            uint64_t r350; bool o351 = __builtin_add_overflow(t349, ((uint64_t)1ULL), &r350);
                            uint64_t t352 = r350;
                            gm_l36 = t352;
                        }
                    }
gm_lb36:;
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    uint8_t t353 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t354 = (uint8_t)(t353 == ((uint8_t)59ULL));
                    if (!t354) {
                        goto gm_lb43;
                    }
                    memmove(((uint8_t *)&F.l39), ((((uint8_t *)t113) + 8) + 8), 40);
                    gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 8));
                    gm_l40 = t355;
                    gm_l41 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t356 = gm_l41;
                            uint64_t t357 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                            uint8_t t358 = (t356 < t357);
                            if (!t358) {
                                goto gm_lb44;
                            }
                            {
                                void * t359 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                uint64_t t360 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                uint64_t t361 = gm_l41;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l42), (((uint8_t *)t359) + (uintptr_t)t361 * 32), 32);
                                        {
                                            {
                                                uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                                                uint8_t t363 = (uint8_t)(t362 == ((uint8_t)0ULL));
                                                if (!t363) {
                                                    goto gm_lb50;
                                                }
                                                void * t364 = GM_LD(void *, (((uint8_t *)&gm_l42) + 8));
                                                gm_l43 = t364;
                                                gm_closure t365 = gm_l1;
                                                void * t366 = gm_l43;
                                                gm_closure t367 = t365;
                                                gm_node *ccx368 = t367.cx;
                                                void * t369 = t367.code;
                                                gm_b0_1 t370;
                                                if (GM_PTR_TAGGED(t369)) {
                                                    ((void (*)(void *))GM_PTR_UNTAG(t369))(t366);
                                                } else {
                                                    t370 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t369)(t367.env, ccx368, NULL, t366);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                goto gm_lb49;
                                            }
gm_lb50:;
                                            {
                                                uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                                                uint8_t t372 = (uint8_t)(t371 == ((uint8_t)1ULL));
                                                if (!t372) {
                                                    goto gm_lb51;
                                                }
                                                void * t373 = GM_LD(void *, ((((uint8_t *)&gm_l42) + 8) + 16));
                                                gm_l44 = t373;
                                                gm_closure t374 = gm_l1;
                                                void * t375 = gm_l44;
                                                gm_closure t376 = t374;
                                                gm_node *ccx377 = t376.cx;
                                                void * t378 = t376.code;
                                                gm_b0_1 t379;
                                                if (GM_PTR_TAGGED(t378)) {
                                                    ((void (*)(void *))GM_PTR_UNTAG(t378))(t375);
                                                } else {
                                                    t379 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t378)(t376.env, ccx377, NULL, t375);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                goto gm_lb49;
                                            }
gm_lb51:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 627, 17);
                                        }
gm_lb49:;
                                        goto gm_lb47;
                                    }
gm_lb48:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 626, 13);
                                }
gm_lb47:;
                            }
gm_lb46:;
                            uint64_t t380 = gm_l41;
                            uint64_t r381; bool o382 = __builtin_add_overflow(t380, ((uint64_t)1ULL), &r381);
                            uint64_t t383 = r381;
                            gm_l41 = t383;
                        }
                    }
gm_lb44:;
                    {
                        {
                            uint8_t t384 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l39) + 24)) != 0);
                            if (!t384) {
                                goto gm_lb53;
                            }
                            void * t385 = GM_LD(void *, (((uint8_t *)&F.l39) + 24));
                            gm_l45 = t385;
                            gm_closure t386 = gm_l1;
                            void * t387 = gm_l45;
                            gm_closure t388 = t386;
                            gm_node *ccx389 = t388.cx;
                            void * t390 = t388.code;
                            gm_b0_1 t391;
                            if (GM_PTR_TAGGED(t390)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t390))(t387);
                            } else {
                                t391 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t390)(t388.env, ccx389, NULL, t387);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb52;
                        }
gm_lb53:;
                    }
gm_lb52:;
                    gm_closure t392 = gm_l1;
                    void * t393 = GM_LD(void *, (((uint8_t *)&F.l39) + 32));
                    gm_closure t394 = t392;
                    gm_node *ccx395 = t394.cx;
                    void * t396 = t394.code;
                    gm_b0_1 t397;
                    if (GM_PTR_TAGGED(t396)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t396))(t393);
                    } else {
                        t397 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t396)(t394.env, ccx395, NULL, t393);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb43:;
            }
            {
                {
                    uint8_t t398 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t399 = (uint8_t)(t398 == ((uint8_t)33ULL));
                    if (!t399) {
                        goto gm_lb54;
                    }
                    gm_b16_8 t400 = GM_LD(gm_b16_8, (((((uint8_t *)t113) + 8) + 8) + 16));
                    gm_l46 = t400;
                    gm_l48 = ((uint64_t)0ULL);
                    uint64_t t401 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                    gm_l47 = t401;
                    {
                        for (;;) {
                            uint64_t t402 = gm_l48;
                            uint64_t t403 = gm_l47;
                            uint8_t t404 = (t402 < t403);
                            if (!t404) {
                                goto gm_lb55;
                            }
                            {
                                {
                                    {
                                        uint64_t t405 = gm_l48;
                                        gm_l49 = t405;
                                        gm_closure t406 = gm_l1;
                                        void * t407 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                        uint64_t t408 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                        uint64_t t409 = gm_l49;
                                        void * t410 = (void *)(((uint8_t *)t407) + (uintptr_t)t409 * 88);
                                        gm_closure t411 = t406;
                                        gm_node *ccx412 = t411.cx;
                                        void * t413 = t411.code;
                                        gm_b0_1 t414;
                                        if (GM_PTR_TAGGED(t413)) {
                                            ((void (*)(void *))GM_PTR_UNTAG(t413))(t410);
                                        } else {
                                            t414 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t413)(t411.env, ccx412, NULL, t410);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb58;
                                    }
gm_lb59:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 635, 30);
                                }
gm_lb58:;
                            }
gm_lb57:;
                            uint64_t t415 = gm_l48;
                            uint64_t r416; bool o417 = __builtin_add_overflow(t415, ((uint64_t)1ULL), &r416);
                            uint64_t t418 = r416;
                            gm_l48 = t418;
                        }
                    }
gm_lb55:;
                    goto gm_lb1;
                }
gm_lb54:;
                {
                    uint8_t t419 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t420 = (uint8_t)(t419 == ((uint8_t)58ULL));
                    if (!t420) {
                        goto gm_lb60;
                    }
                    memmove(((uint8_t *)&gm_l50), ((((uint8_t *)t113) + 8) + 8), 24);
                    gm_l52 = ((uint64_t)0ULL);
                    uint64_t t421 = GM_LD(uint64_t, ((((uint8_t *)&gm_l50) + 8) + 8));
                    gm_l51 = t421;
                    {
                        for (;;) {
                            uint64_t t422 = gm_l52;
                            uint64_t t423 = gm_l51;
                            uint8_t t424 = (t422 < t423);
                            if (!t424) {
                                goto gm_lb61;
                            }
                            {
                                {
                                    {
                                        uint64_t t425 = gm_l52;
                                        gm_l53 = t425;
                                        gm_closure t426 = gm_l1;
                                        void * t427 = GM_LD(void *, (((uint8_t *)&gm_l50) + 8));
                                        uint64_t t428 = GM_LD(uint64_t, ((((uint8_t *)&gm_l50) + 8) + 8));
                                        uint64_t t429 = gm_l53;
                                        void * t430 = (void *)(((uint8_t *)t427) + (uintptr_t)t429 * 88);
                                        gm_closure t431 = t426;
                                        gm_node *ccx432 = t431.cx;
                                        void * t433 = t431.code;
                                        gm_b0_1 t434;
                                        if (GM_PTR_TAGGED(t433)) {
                                            ((void (*)(void *))GM_PTR_UNTAG(t433))(t430);
                                        } else {
                                            t434 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t433)(t431.env, ccx432, NULL, t430);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb64;
                                    }
gm_lb65:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 636, 28);
                                }
gm_lb64:;
                            }
gm_lb63:;
                            uint64_t t435 = gm_l52;
                            uint64_t r436; bool o437 = __builtin_add_overflow(t435, ((uint64_t)1ULL), &r436);
                            uint64_t t438 = r436;
                            gm_l52 = t438;
                        }
                    }
gm_lb61:;
                    goto gm_lb1;
                }
gm_lb60:;
                {
                    uint8_t t439 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t440 = (uint8_t)(t439 == ((uint8_t)34ULL));
                    if (!t440) {
                        goto gm_lb66;
                    }
                    memmove(((uint8_t *)&F.l54), ((((uint8_t *)t113) + 8) + 8), 40);
                    gm_b16_8 t441 = GM_LD(gm_b16_8, (((((uint8_t *)t113) + 8) + 8) + 40));
                    gm_l55 = t441;
                    {
                        {
                            uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)&F.l54) + 0));
                            uint8_t t443 = (uint8_t)(t442 == ((uint8_t)2ULL));
                            if (!t443) {
                                goto gm_lb68;
                            }
                            void * t444 = GM_LD(void *, (((uint8_t *)&F.l54) + 8));
                            gm_l56 = t444;
                            gm_closure t445 = gm_l1;
                            void * t446 = gm_l56;
                            gm_closure t447 = t445;
                            gm_node *ccx448 = t447.cx;
                            void * t449 = t447.code;
                            gm_b0_1 t450;
                            if (GM_PTR_TAGGED(t449)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t449))(t446);
                            } else {
                                t450 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t449)(t447.env, ccx448, NULL, t446);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb67;
                        }
gm_lb68:;
                    }
gm_lb67:;
                    gm_l58 = ((uint64_t)0ULL);
                    uint64_t t451 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                    gm_l57 = t451;
                    {
                        for (;;) {
                            uint64_t t452 = gm_l58;
                            uint64_t t453 = gm_l57;
                            uint8_t t454 = (t452 < t453);
                            if (!t454) {
                                goto gm_lb69;
                            }
                            {
                                {
                                    {
                                        uint64_t t455 = gm_l58;
                                        gm_l59 = t455;
                                        gm_closure t456 = gm_l1;
                                        void * t457 = GM_LD(void *, ((uint8_t *)&gm_l55));
                                        uint64_t t458 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                        uint64_t t459 = gm_l59;
                                        void * t460 = (void *)(((uint8_t *)t457) + (uintptr_t)t459 * 88);
                                        gm_closure t461 = t456;
                                        gm_node *ccx462 = t461.cx;
                                        void * t463 = t461.code;
                                        gm_b0_1 t464;
                                        if (GM_PTR_TAGGED(t463)) {
                                            ((void (*)(void *))GM_PTR_UNTAG(t463))(t460);
                                        } else {
                                            t464 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t463)(t461.env, ccx462, NULL, t460);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb72;
                                    }
gm_lb73:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 639, 13);
                                }
gm_lb72:;
                            }
gm_lb71:;
                            uint64_t t465 = gm_l58;
                            uint64_t r466; bool o467 = __builtin_add_overflow(t465, ((uint64_t)1ULL), &r466);
                            uint64_t t468 = r466;
                            gm_l58 = t468;
                        }
                    }
gm_lb69:;
                    goto gm_lb1;
                }
gm_lb66:;
            }
            {
                {
                    {
                        {
                            uint8_t t469 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t470 = (uint8_t)(t469 == ((uint8_t)35ULL));
                            if (!t470) {
                                goto gm_lb76;
                            }
                            gm_b16_8 t471 = GM_LD(gm_b16_8, ((((uint8_t *)t113) + 8) + 8));
                            gm_l60 = t471;
                            goto gm_lb75;
                        }
gm_lb76:;
                        {
                            uint8_t t472 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                            uint8_t t473 = (uint8_t)(t472 == ((uint8_t)36ULL));
                            if (!t473) {
                                goto gm_lb77;
                            }
                            gm_b16_8 t474 = GM_LD(gm_b16_8, ((((uint8_t *)t113) + 8) + 8));
                            gm_l60 = t474;
                            goto gm_lb75;
                        }
gm_lb77:;
                        goto gm_lb74;
                    }
gm_lb75:;
                    gm_l63 = ((uint64_t)0ULL);
                    uint64_t t475 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                    gm_l62 = t475;
                    {
                        for (;;) {
                            uint64_t t476 = gm_l63;
                            uint64_t t477 = gm_l62;
                            uint8_t t478 = (t476 < t477);
                            if (!t478) {
                                goto gm_lb78;
                            }
                            {
                                {
                                    {
                                        uint64_t t479 = gm_l63;
                                        gm_l64 = t479;
                                        gm_closure t480 = gm_l1;
                                        void * t481 = GM_LD(void *, ((uint8_t *)&gm_l60));
                                        uint64_t t482 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                                        uint64_t t483 = gm_l64;
                                        void * t484 = (void *)(((uint8_t *)t481) + (uintptr_t)t483 * 88);
                                        gm_closure t485 = t480;
                                        gm_node *ccx486 = t485.cx;
                                        void * t487 = t485.code;
                                        gm_b0_1 t488;
                                        if (GM_PTR_TAGGED(t487)) {
                                            ((void (*)(void *))GM_PTR_UNTAG(t487))(t484);
                                        } else {
                                            t488 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t487)(t485.env, ccx486, NULL, t484);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb81;
                                    }
gm_lb82:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 641, 39);
                                }
gm_lb81:;
                            }
gm_lb80:;
                            uint64_t t489 = gm_l63;
                            uint64_t r490; bool o491 = __builtin_add_overflow(t489, ((uint64_t)1ULL), &r490);
                            uint64_t t492 = r490;
                            gm_l63 = t492;
                        }
                    }
gm_lb78:;
                    goto gm_lb1;
                }
gm_lb74:;
                {
                    uint8_t t493 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t494 = (uint8_t)(t493 == ((uint8_t)38ULL));
                    if (!t494) {
                        goto gm_lb83;
                    }
                    gm_ttggq7ng4kp3p t495 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t113) + 8) + 8));
                    gm_l65 = t495;
                    {
                        {
                            uint8_t t496 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l65) + 8)) != 0);
                            if (!t496) {
                                goto gm_lb85;
                            }
                            void * t497 = GM_LD(void *, (((uint8_t *)&gm_l65) + 8));
                            gm_l66 = t497;
                            gm_closure t498 = gm_l1;
                            void * t499 = gm_l66;
                            gm_closure t500 = t498;
                            gm_node *ccx501 = t500.cx;
                            void * t502 = t500.code;
                            gm_b0_1 t503;
                            if (GM_PTR_TAGGED(t502)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t502))(t499);
                            } else {
                                t503 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t502)(t500.env, ccx501, NULL, t499);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb84;
                        }
gm_lb85:;
                    }
gm_lb84:;
                    goto gm_lb1;
                }
gm_lb83:;
                {
                    uint8_t t504 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t505 = (uint8_t)(t504 == ((uint8_t)46ULL));
                    if (!t505) {
                        goto gm_lb86;
                    }
                    void * t506 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l67 = t506;
                    void * t507 = gm_l67;
                    gm_closure t508 = gm_l1;
                    gm_f_block_children_r2qinti(t507, t508);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb86:;
            }
            {
                {
                    uint8_t t509 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t510 = (uint8_t)(t509 == ((uint8_t)47ULL));
                    if (!t510) {
                        goto gm_lb87;
                    }
                    memmove(((uint8_t *)&gm_l68), ((((uint8_t *)t113) + 8) + 8), 32);
                    gm_b16_8 t511 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l68));
                    gm_closure t512 = gm_l1;
                    gm_f_conds_children_3kg6846(t511, t512);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t513 = gm_l1;
                    void * t514 = GM_LD(void *, (((uint8_t *)&gm_l68) + 16));
                    gm_closure t515 = t513;
                    gm_node *ccx516 = t515.cx;
                    void * t517 = t515.code;
                    gm_b0_1 t518;
                    if (GM_PTR_TAGGED(t517)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t517))(t514);
                    } else {
                        t518 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t517)(t515.env, ccx516, NULL, t514);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t519 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l68) + 24)) != 0);
                            if (!t519) {
                                goto gm_lb89;
                            }
                            void * t520 = GM_LD(void *, (((uint8_t *)&gm_l68) + 24));
                            gm_l69 = t520;
                            gm_closure t521 = gm_l1;
                            void * t522 = gm_l69;
                            gm_closure t523 = t521;
                            gm_node *ccx524 = t523.cx;
                            void * t525 = t523.code;
                            gm_b0_1 t526;
                            if (GM_PTR_TAGGED(t525)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t525))(t522);
                            } else {
                                t526 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t525)(t523.env, ccx524, NULL, t522);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb88;
                        }
gm_lb89:;
                    }
gm_lb88:;
                    goto gm_lb1;
                }
gm_lb87:;
                {
                    uint8_t t527 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t528 = (uint8_t)(t527 == ((uint8_t)48ULL));
                    if (!t528) {
                        goto gm_lb90;
                    }
                    memmove(((uint8_t *)&F.l70), ((((uint8_t *)t113) + 8) + 8), 40);
                    gm_b16_8 t529 = GM_LD(gm_b16_8, (((uint8_t *)&F.l70) + 8));
                    gm_closure t530 = gm_l1;
                    gm_f_conds_children_3kg6846(t529, t530);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t531 = GM_LD(void *, (((uint8_t *)&F.l70) + 24));
                    gm_closure t532 = gm_l1;
                    gm_f_block_children_r2qinti(t531, t532);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t533 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l70) + 32)) != 0);
                            if (!t533) {
                                goto gm_lb92;
                            }
                            void * t534 = GM_LD(void *, (((uint8_t *)&F.l70) + 32));
                            gm_l71 = t534;
                            gm_closure t535 = gm_l1;
                            void * t536 = gm_l71;
                            gm_closure t537 = t535;
                            gm_node *ccx538 = t537.cx;
                            void * t539 = t537.code;
                            gm_b0_1 t540;
                            if (GM_PTR_TAGGED(t539)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t539))(t536);
                            } else {
                                t540 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t539)(t537.env, ccx538, NULL, t536);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb91;
                        }
gm_lb92:;
                    }
gm_lb91:;
                    goto gm_lb1;
                }
gm_lb90:;
                {
                    uint8_t t541 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t542 = (uint8_t)(t541 == ((uint8_t)49ULL));
                    if (!t542) {
                        goto gm_lb93;
                    }
                    memmove(((uint8_t *)&gm_l72), ((((uint8_t *)t113) + 8) + 8), 24);
                    {
                        {
                            uint8_t t543 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l72) + 16)) != 0);
                            if (!t543) {
                                goto gm_lb95;
                            }
                            void * t544 = GM_LD(void *, (((uint8_t *)&gm_l72) + 16));
                            gm_l73 = t544;
                            gm_closure t545 = gm_l1;
                            void * t546 = gm_l73;
                            gm_closure t547 = t545;
                            gm_node *ccx548 = t547.cx;
                            void * t549 = t547.code;
                            gm_b0_1 t550;
                            if (GM_PTR_TAGGED(t549)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t549))(t546);
                            } else {
                                t550 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t549)(t547.env, ccx548, NULL, t546);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb94;
                        }
gm_lb95:;
                    }
gm_lb94:;
                    goto gm_lb1;
                }
gm_lb93:;
                {
                    uint8_t t551 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t552 = (uint8_t)(t551 == ((uint8_t)55ULL));
                    if (!t552) {
                        goto gm_lb96;
                    }
                    void * t553 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l74 = t553;
                    {
                        {
                            void * t554 = gm_l74;
                            uint8_t t555 = GM_LD(uint8_t, (((uint8_t *)t554) + 0));
                            uint8_t t556 = (uint8_t)(t555 == ((uint8_t)1ULL));
                            if (!t556) {
                                goto gm_lb98;
                            }
                            void * t557 = GM_LD(void *, (((uint8_t *)t554) + 8));
                            gm_l75 = t557;
                            gm_closure t558 = gm_l1;
                            void * t559 = gm_l75;
                            gm_closure t560 = t558;
                            gm_node *ccx561 = t560.cx;
                            void * t562 = t560.code;
                            gm_b0_1 t563;
                            if (GM_PTR_TAGGED(t562)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t562))(t559);
                            } else {
                                t563 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t562)(t560.env, ccx561, NULL, t559);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb97;
                        }
gm_lb98:;
                    }
gm_lb97:;
                    {
                        {
                            void * t564 = gm_l74;
                            uint8_t t565 = (uint8_t)(GM_LD(void *, (((uint8_t *)t564) + 88)) != 0);
                            if (!t565) {
                                goto gm_lb100;
                            }
                            void * t566 = GM_LD(void *, (((uint8_t *)t564) + 88));
                            gm_l76 = t566;
                            gm_closure t567 = gm_l1;
                            void * t568 = gm_l76;
                            gm_closure t569 = t567;
                            gm_node *ccx570 = t569.cx;
                            void * t571 = t569.code;
                            gm_b0_1 t572;
                            if (GM_PTR_TAGGED(t571)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t571))(t568);
                            } else {
                                t572 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t571)(t569.env, ccx570, NULL, t568);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb99;
                        }
gm_lb100:;
                    }
gm_lb99:;
                    gm_l78 = ((uint64_t)0ULL);
                    void * t573 = gm_l74;
                    uint64_t t574 = GM_LD(uint64_t, ((((uint8_t *)t573) + 72) + 8));
                    gm_l77 = t574;
                    {
                        for (;;) {
                            uint64_t t575 = gm_l78;
                            uint64_t t576 = gm_l77;
                            uint8_t t577 = (t575 < t576);
                            if (!t577) {
                                goto gm_lb101;
                            }
                            {
                                {
                                    {
                                        uint64_t t578 = gm_l78;
                                        gm_l79 = t578;
                                        gm_closure t579 = gm_l1;
                                        void * t580 = gm_l74;
                                        void * t581 = GM_LD(void *, (((uint8_t *)t580) + 72));
                                        uint64_t t582 = GM_LD(uint64_t, ((((uint8_t *)t580) + 72) + 8));
                                        uint64_t t583 = gm_l79;
                                        if ((uintptr_t)t583 >= (uintptr_t)(t582)) gm_trap_index((uintptr_t)t583, t582, gm_srcod98slk3vnra, 658, 41);
                                        void * t584 = (void *)(((uint8_t *)t581) + (uintptr_t)t583 * 88);
                                        gm_closure t585 = t579;
                                        gm_node *ccx586 = t585.cx;
                                        void * t587 = t585.code;
                                        gm_b0_1 t588;
                                        if (GM_PTR_TAGGED(t587)) {
                                            ((void (*)(void *))GM_PTR_UNTAG(t587))(t584);
                                        } else {
                                            t588 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t587)(t585.env, ccx586, NULL, t584);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb104;
                                    }
gm_lb105:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 658, 13);
                                }
gm_lb104:;
                            }
gm_lb103:;
                            uint64_t t589 = gm_l78;
                            uint64_t r590; bool o591 = __builtin_add_overflow(t589, ((uint64_t)1ULL), &r590);
                            uint64_t t592 = r590;
                            gm_l78 = t592;
                        }
                    }
gm_lb101:;
                    goto gm_lb1;
                }
gm_lb96:;
            }
            {
                {
                    uint8_t t593 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                    uint8_t t594 = (uint8_t)(t593 == ((uint8_t)56ULL));
                    if (!t594) {
                        goto gm_lb106;
                    }
                    void * t595 = GM_LD(void *, ((((uint8_t *)t113) + 8) + 8));
                    gm_l80 = t595;
                    {
                        {
                            void * t596 = gm_l80;
                            uint8_t t597 = GM_LD(uint8_t, (((uint8_t *)t596) + 0));
                            uint8_t t598 = (uint8_t)(t597 == ((uint8_t)1ULL));
                            if (!t598) {
                                goto gm_lb108;
                            }
                            void * t599 = GM_LD(void *, (((uint8_t *)t596) + 8));
                            gm_l81 = t599;
                            gm_closure t600 = gm_l1;
                            void * t601 = gm_l81;
                            gm_closure t602 = t600;
                            gm_node *ccx603 = t602.cx;
                            void * t604 = t602.code;
                            gm_b0_1 t605;
                            if (GM_PTR_TAGGED(t604)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t604))(t601);
                            } else {
                                t605 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t604)(t602.env, ccx603, NULL, t601);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb107;
                        }
gm_lb108:;
                    }
gm_lb107:;
                    gm_closure t606 = gm_l1;
                    void * t607 = gm_l80;
                    void * t608 = GM_LD(void *, (((uint8_t *)t607) + 40));
                    gm_closure t609 = t606;
                    gm_node *ccx610 = t609.cx;
                    void * t611 = t609.code;
                    gm_b0_1 t612;
                    if (GM_PTR_TAGGED(t611)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t611))(t608);
                    } else {
                        t612 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t611)(t609.env, ccx610, NULL, t608);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            void * t613 = gm_l80;
                            uint8_t t614 = (uint8_t)(GM_LD(void *, (((uint8_t *)t613) + 48)) != 0);
                            if (!t614) {
                                goto gm_lb110;
                            }
                            void * t615 = GM_LD(void *, (((uint8_t *)t613) + 48));
                            gm_l82 = t615;
                            gm_closure t616 = gm_l1;
                            void * t617 = gm_l82;
                            gm_closure t618 = t616;
                            gm_node *ccx619 = t618.cx;
                            void * t620 = t618.code;
                            gm_b0_1 t621;
                            if (GM_PTR_TAGGED(t620)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t620))(t617);
                            } else {
                                t621 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t620)(t618.env, ccx619, NULL, t617);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb109;
                        }
gm_lb110:;
                    }
gm_lb109:;
                    goto gm_lb1;
                }
gm_lb106:;
                {
                    goto gm_lb1;
                }
gm_lb111:;
                gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 583, 5);
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
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_subpat_arlrtrm F;
    void * gm_l0; /* p */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    void * gm_l5; /* q */
    void * gm_l6; /* x */
    gm_b16_8 gm_l7; /* fs */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l10; /* x */
    gm_b16_8 gm_l11; /* ps */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l14; /* q */
    GM_STACK_CHECK(&F, 695u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t24 = gm_l0;
    {
        {
            {
                {
                    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)5ULL));
                    if (!t26) {
                        goto gm_lb4;
                    }
                    void * t27 = GM_LD(void *, ((((uint8_t *)t24) + 8) + 8));
                    gm_l2 = t27;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)6ULL));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    void * t30 = GM_LD(void *, ((((uint8_t *)t24) + 8) + 8));
                    gm_l2 = t30;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)7ULL));
                    if (!t32) {
                        goto gm_lb6;
                    }
                    void * t33 = GM_LD(void *, ((((uint8_t *)t24) + 8) + 8));
                    gm_l2 = t33;
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_closure t34 = gm_l1;
            void * t35 = gm_l2;
            gm_closure t36 = t34;
            gm_node *ccx37 = t36.cx;
            void * t38 = t36.code;
            gm_b0_1 t39;
            if (GM_PTR_TAGGED(t38)) {
                ((void (*)(void *))GM_PTR_UNTAG(t38))(t35);
            } else {
                t39 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t38)(t36.env, ccx37, NULL, t35);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)8ULL));
            if (!t41) {
                goto gm_lb7;
            }
            void * t42 = GM_LD(void *, (((((uint8_t *)t24) + 8) + 8) + 8));
            gm_l5 = t42;
            {
                {
                    uint8_t t43 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                    if (!t43) {
                        goto gm_lb9;
                    }
                    void * t44 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    gm_l6 = t44;
                    gm_closure t45 = gm_l1;
                    void * t46 = gm_l6;
                    gm_closure t47 = t45;
                    gm_node *ccx48 = t47.cx;
                    void * t49 = t47.code;
                    gm_b0_1 t50;
                    if (GM_PTR_TAGGED(t49)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t49))(t46);
                    } else {
                        t50 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t49)(t47.env, ccx48, NULL, t46);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
            }
gm_lb8:;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)9ULL));
            if (!t52) {
                goto gm_lb10;
            }
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((((uint8_t *)t24) + 8) + 8));
            gm_l7 = t53;
            gm_b16_8 t54 = gm_l7;
            gm_l8 = t54;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t55 = gm_l9;
                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t57 = (t55 < t56);
                    if (!t57) {
                        goto gm_lb11;
                    }
                    {
                        void * t58 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t60 = gm_l9;
                        {
                            {
                                gm_ttggq7ng4kp3p t61 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t58) + (uintptr_t)t60 * 16));
                                gm_l10 = t61;
                                gm_closure t62 = gm_l1;
                                void * t63 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                                gm_closure t64 = t62;
                                gm_node *ccx65 = t64.cx;
                                void * t66 = t64.code;
                                gm_b0_1 t67;
                                if (GM_PTR_TAGGED(t66)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t66))(t63);
                                } else {
                                    t67 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t66)(t64.env, ccx65, NULL, t63);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 726, 24);
                        }
gm_lb14:;
                    }
gm_lb13:;
                    uint64_t t68 = gm_l9;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l9 = t71;
                }
            }
gm_lb11:;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t73 = (uint8_t)(t72 == ((uint8_t)10ULL));
            if (!t73) {
                goto gm_lb16;
            }
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((((uint8_t *)t24) + 8) + 8));
            gm_l11 = t74;
            gm_b16_8 t75 = gm_l11;
            gm_l12 = t75;
            gm_l13 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t76 = gm_l13;
                    uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint8_t t78 = (t76 < t77);
                    if (!t78) {
                        goto gm_lb17;
                    }
                    {
                        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l12));
                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint64_t t81 = gm_l13;
                        {
                            {
                                void * t82 = GM_LD(void *, (((uint8_t *)t79) + (uintptr_t)t81 * 8));
                                gm_l14 = t82;
                                gm_closure t83 = gm_l1;
                                void * t84 = gm_l14;
                                gm_closure t85 = t83;
                                gm_node *ccx86 = t85.cx;
                                void * t87 = t85.code;
                                gm_b0_1 t88;
                                if (GM_PTR_TAGGED(t87)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t87))(t84);
                                } else {
                                    t88 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t87)(t85.env, ccx86, NULL, t84);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb20;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 727, 20);
                        }
gm_lb20:;
                    }
gm_lb19:;
                    uint64_t t89 = gm_l13;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    uint64_t t92 = r90;
                    gm_l13 = t92;
                }
            }
gm_lb17:;
            goto gm_lb1;
        }
gm_lb16:;
        {
            goto gm_lb1;
        }
gm_lb22:;
        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 723, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_default_spec_cdd8jng F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    void * t5 = gm_l0;
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)t5));
    uint8_t t7 = ((t6 == ((uint8_t)32ULL)));
    if (t7) {
        void * t8 = gm_l0;
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t8) + 1));
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        uint8_t t11 = (uint8_t)((t10 == ((uint8_t)0ULL)));
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
        gm_l7 = t11;
    } else {
        gm_l7 = ((uint8_t)0);
    }
    uint8_t t13 = gm_l7;
    if (t13) {
        void * t14 = gm_l0;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 2));
        uint8_t t16 = (uint8_t)!t15;
        gm_l6 = t16;
    } else {
        gm_l6 = ((uint8_t)0);
    }
    uint8_t t17 = gm_l6;
    if (t17) {
        void * t18 = gm_l0;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t18) + 3));
        uint8_t t20 = (uint8_t)!t19;
        gm_l5 = t20;
    } else {
        gm_l5 = ((uint8_t)0);
    }
    uint8_t t21 = gm_l5;
    if (t21) {
        void * t22 = gm_l0;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t22) + 4));
        uint8_t t24 = (uint8_t)!t23;
        gm_l4 = t24;
    } else {
        gm_l4 = ((uint8_t)0);
    }
    uint8_t t25 = gm_l4;
    if (t25) {
        void * t26 = gm_l0;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 8));
        uint8_t t28 = ((t27 == ((uint32_t)0ULL)));
        gm_l3 = t28;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t29 = gm_l3;
    if (t29) {
        void * t30 = gm_l0;
        {
            {
                uint8_t t31 = (uint8_t)(((((uint8_t *)t30) + 12))[4] != 0);
                if (t31) {
                    goto gm_lb2;
                }
                gm_l8 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l8 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 541, 112);
        }
gm_lb1:;
        uint8_t t32 = gm_l8;
        gm_l2 = t32;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t33 = gm_l2;
    if (t33) {
        void * t34 = gm_l0;
        gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t34) + 20));
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        uint8_t t37 = (uint8_t)((t36 == ((uint8_t)0ULL)));
        gm_t9qkh0rsooc8d t38;
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
        gm_l1 = t37;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t39 = gm_l1;
    F.ret = t39;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
extern __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_pat_children_e3peovr(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pat_children_e3peovr F;
    void * gm_l0; /* p */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)2ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            gm_closure t8 = F.l1;
            void * t9 = gm_l2;
            gm_closure t10 = t8;
            gm_node *ccx11 = t10.cx;
            void * t12 = t10.code;
            gm_b0_1 t13;
            if (GM_PTR_TAGGED(t12)) {
                ((void (*)(void *))GM_PTR_UNTAG(t12))(t9);
            } else {
                t13 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t12)(t10.env, ccx11, NULL, t9);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = gm_l0;
    gm_closure t15 = ((gm_closure){(void *)gm_f_fn_hdas5h7, (void *)&F.h, cx});
    gm_f_for_each_subpat_arlrtrm(t14, t15);
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
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_name_pcdoujk F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 1055u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            {
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)0ULL));
                    if (!t59) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t60;
                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s78m4481dj77r));
                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(8));
                    gm_l1 = t60;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)1ULL));
                    if (!t62) {
                        goto gm_lb3;
                    }
                    gm_b16_8 t63;
                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sv9p44pp2n1jk));
                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(9));
                    gm_l1 = t63;
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
                    if (!t65) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s8o3vqolbfl8t));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(10));
                    gm_l1 = t66;
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)3ULL));
                    if (!t68) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t69;
                    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_skti0sb80pmgp));
                    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(11));
                    gm_l1 = t69;
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t71 = (uint8_t)(t70 == ((uint8_t)4ULL));
                    if (!t71) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sb641udihu5hs));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(9));
                    gm_l1 = t72;
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)5ULL));
                    if (!t74) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t75;
                    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s6f0l2e1iha9i));
                    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(7));
                    gm_l1 = t75;
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)6ULL));
                    if (!t77) {
                        goto gm_lb8;
                    }
                    gm_b16_8 t78;
                    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sfku76mf5fn5d));
                    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(8));
                    gm_l1 = t78;
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)7ULL));
                    if (!t80) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_saamkuuf6jrtf));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(7));
                    gm_l1 = t81;
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)8ULL));
                    if (!t83) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t84;
                    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_se3l2nlomk2pj));
                    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(8));
                    gm_l1 = t84;
                    goto gm_lb1;
                }
gm_lb10:;
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)9ULL));
                    if (!t86) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t87;
                    GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sv1bu4l4kq188));
                    GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(7));
                    gm_l1 = t87;
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)10ULL));
                    if (!t89) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t90;
                    GM_ST(void *, (uint8_t *)&t90, (void *)(gm_smatmsv0oedot));
                    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(7));
                    gm_l1 = t90;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t92 = (uint8_t)(t91 == ((uint8_t)11ULL));
                    if (!t92) {
                        goto gm_lb13;
                    }
                    gm_b16_8 t93;
                    GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s9819enjtsd7t));
                    GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(9));
                    gm_l1 = t93;
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)12ULL));
                    if (!t95) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t96;
                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sal1kdi1rij5t));
                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(9));
                    gm_l1 = t96;
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t98 = (uint8_t)(t97 == ((uint8_t)13ULL));
                    if (!t98) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t99;
                    GM_ST(void *, (uint8_t *)&t99, (void *)(gm_svkvl31jmj9ne));
                    GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(11));
                    gm_l1 = t99;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t101 = (uint8_t)(t100 == ((uint8_t)14ULL));
                    if (!t101) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t102;
                    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_siph7o6oug1g2));
                    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(13));
                    gm_l1 = t102;
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t104 = (uint8_t)(t103 == ((uint8_t)15ULL));
                    if (!t104) {
                        goto gm_lb17;
                    }
                    gm_b16_8 t105;
                    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_seajfhgtfmhv7));
                    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(11));
                    gm_l1 = t105;
                    goto gm_lb1;
                }
gm_lb17:;
                {
                    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t107 = (uint8_t)(t106 == ((uint8_t)16ULL));
                    if (!t107) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t108;
                    GM_ST(void *, (uint8_t *)&t108, (void *)(gm_slginip33cenb));
                    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(13));
                    gm_l1 = t108;
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t110 = (uint8_t)(t109 == ((uint8_t)17ULL));
                    if (!t110) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t111;
                    GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sbgj0biroecr0));
                    GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(12));
                    gm_l1 = t111;
                    goto gm_lb1;
                }
gm_lb19:;
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)18ULL));
                    if (!t113) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t114;
                    GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sso3tuh819o23));
                    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(8));
                    gm_l1 = t114;
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)19ULL));
                    if (!t116) {
                        goto gm_lb21;
                    }
                    gm_b16_8 t117;
                    GM_ST(void *, (uint8_t *)&t117, (void *)(gm_shegcik3mpeut));
                    GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(11));
                    gm_l1 = t117;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)20ULL));
                    if (!t119) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t120;
                    GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sk1d0v1onojc1));
                    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(7));
                    gm_l1 = t120;
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)21ULL));
                    if (!t122) {
                        goto gm_lb23;
                    }
                    gm_b16_8 t123;
                    GM_ST(void *, (uint8_t *)&t123, (void *)(gm_sp8bmcd3m68n6));
                    GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(6));
                    gm_l1 = t123;
                    goto gm_lb1;
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t125 = (uint8_t)(t124 == ((uint8_t)22ULL));
                    if (!t125) {
                        goto gm_lb24;
                    }
                    gm_b16_8 t126;
                    GM_ST(void *, (uint8_t *)&t126, (void *)(gm_smvhngj8mlfjc));
                    GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(12));
                    gm_l1 = t126;
                    goto gm_lb1;
                }
gm_lb24:;
                {
                    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t128 = (uint8_t)(t127 == ((uint8_t)23ULL));
                    if (!t128) {
                        goto gm_lb25;
                    }
                    gm_b16_8 t129;
                    GM_ST(void *, (uint8_t *)&t129, (void *)(gm_s2jvv6mjt9vs3));
                    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(12));
                    gm_l1 = t129;
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)24ULL));
                    if (!t131) {
                        goto gm_lb26;
                    }
                    gm_b16_8 t132;
                    GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sd9o9rh5qe8e7));
                    GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(10));
                    gm_l1 = t132;
                    goto gm_lb1;
                }
gm_lb26:;
                {
                    uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t134 = (uint8_t)(t133 == ((uint8_t)25ULL));
                    if (!t134) {
                        goto gm_lb27;
                    }
                    gm_b16_8 t135;
                    GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sfd9hctnph9ef));
                    GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(11));
                    gm_l1 = t135;
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t137 = (uint8_t)(t136 == ((uint8_t)26ULL));
                    if (!t137) {
                        goto gm_lb28;
                    }
                    gm_b16_8 t138;
                    GM_ST(void *, (uint8_t *)&t138, (void *)(gm_ssj8r83l87dr2));
                    GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(15));
                    gm_l1 = t138;
                    goto gm_lb1;
                }
gm_lb28:;
                {
                    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t140 = (uint8_t)(t139 == ((uint8_t)27ULL));
                    if (!t140) {
                        goto gm_lb29;
                    }
                    gm_b16_8 t141;
                    GM_ST(void *, (uint8_t *)&t141, (void *)(gm_suno9ilftpfht));
                    GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(11));
                    gm_l1 = t141;
                    goto gm_lb1;
                }
gm_lb29:;
                {
                    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t143 = (uint8_t)(t142 == ((uint8_t)28ULL));
                    if (!t143) {
                        goto gm_lb30;
                    }
                    gm_b16_8 t144;
                    GM_ST(void *, (uint8_t *)&t144, (void *)(gm_s65khs4s753q2));
                    GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(10));
                    gm_l1 = t144;
                    goto gm_lb1;
                }
gm_lb30:;
                {
                    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t146 = (uint8_t)(t145 == ((uint8_t)29ULL));
                    if (!t146) {
                        goto gm_lb31;
                    }
                    gm_b16_8 t147;
                    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sj49v74ko3ghf));
                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(14));
                    gm_l1 = t147;
                    goto gm_lb1;
                }
gm_lb31:;
                {
                    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t149 = (uint8_t)(t148 == ((uint8_t)30ULL));
                    if (!t149) {
                        goto gm_lb32;
                    }
                    gm_b16_8 t150;
                    GM_ST(void *, (uint8_t *)&t150, (void *)(gm_stl47boisb31o));
                    GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(10));
                    gm_l1 = t150;
                    goto gm_lb1;
                }
gm_lb32:;
                {
                    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t152 = (uint8_t)(t151 == ((uint8_t)31ULL));
                    if (!t152) {
                        goto gm_lb33;
                    }
                    gm_b16_8 t153;
                    GM_ST(void *, (uint8_t *)&t153, (void *)(gm_s86kdga2ul6h4));
                    GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(8));
                    gm_l1 = t153;
                    goto gm_lb1;
                }
gm_lb33:;
                {
                    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t155 = (uint8_t)(t154 == ((uint8_t)32ULL));
                    if (!t155) {
                        goto gm_lb34;
                    }
                    gm_b16_8 t156;
                    GM_ST(void *, (uint8_t *)&t156, (void *)(gm_s59a022mrijic));
                    GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(8));
                    gm_l1 = t156;
                    goto gm_lb1;
                }
gm_lb34:;
            }
            {
                {
                    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t158 = (uint8_t)(t157 == ((uint8_t)33ULL));
                    if (!t158) {
                        goto gm_lb35;
                    }
                    gm_b16_8 t159;
                    GM_ST(void *, (uint8_t *)&t159, (void *)(gm_sk77bvr488rq3));
                    GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(9));
                    gm_l1 = t159;
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t161 = (uint8_t)(t160 == ((uint8_t)34ULL));
                    if (!t161) {
                        goto gm_lb36;
                    }
                    gm_b16_8 t162;
                    GM_ST(void *, (uint8_t *)&t162, (void *)(gm_s4jhjvgae4896));
                    GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(12));
                    gm_l1 = t162;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t164 = (uint8_t)(t163 == ((uint8_t)35ULL));
                    if (!t164) {
                        goto gm_lb37;
                    }
                    gm_b16_8 t165;
                    GM_ST(void *, (uint8_t *)&t165, (void *)(gm_sfbjh3dq66kkg));
                    GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(16));
                    gm_l1 = t165;
                    goto gm_lb1;
                }
gm_lb37:;
                {
                    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t167 = (uint8_t)(t166 == ((uint8_t)36ULL));
                    if (!t167) {
                        goto gm_lb38;
                    }
                    gm_b16_8 t168;
                    GM_ST(void *, (uint8_t *)&t168, (void *)(gm_skqa2fjpbsodp));
                    GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(8));
                    gm_l1 = t168;
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t170 = (uint8_t)(t169 == ((uint8_t)37ULL));
                    if (!t170) {
                        goto gm_lb39;
                    }
                    gm_b16_8 t171;
                    GM_ST(void *, (uint8_t *)&t171, (void *)(gm_s84p47580vnhr));
                    GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(11));
                    gm_l1 = t171;
                    goto gm_lb1;
                }
gm_lb39:;
                {
                    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t173 = (uint8_t)(t172 == ((uint8_t)38ULL));
                    if (!t173) {
                        goto gm_lb40;
                    }
                    gm_b16_8 t174;
                    GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sk0bnuggr6sri));
                    GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(13));
                    gm_l1 = t174;
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)39ULL));
                    if (!t176) {
                        goto gm_lb41;
                    }
                    gm_b16_8 t177;
                    GM_ST(void *, (uint8_t *)&t177, (void *)(gm_siqaapb0mqsrg));
                    GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(13));
                    gm_l1 = t177;
                    goto gm_lb1;
                }
gm_lb41:;
                {
                    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t179 = (uint8_t)(t178 == ((uint8_t)40ULL));
                    if (!t179) {
                        goto gm_lb42;
                    }
                    gm_b16_8 t180;
                    GM_ST(void *, (uint8_t *)&t180, (void *)(gm_sdtlgdl6ed52a));
                    GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(13));
                    gm_l1 = t180;
                    goto gm_lb1;
                }
gm_lb42:;
                {
                    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)41ULL));
                    if (!t182) {
                        goto gm_lb43;
                    }
                    gm_b16_8 t183;
                    GM_ST(void *, (uint8_t *)&t183, (void *)(gm_smfprcpea3ibe));
                    GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(9));
                    gm_l1 = t183;
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t185 = (uint8_t)(t184 == ((uint8_t)42ULL));
                    if (!t185) {
                        goto gm_lb44;
                    }
                    gm_b16_8 t186;
                    GM_ST(void *, (uint8_t *)&t186, (void *)(gm_si8jv2q8d22kl));
                    GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(9));
                    gm_l1 = t186;
                    goto gm_lb1;
                }
gm_lb44:;
                {
                    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t188 = (uint8_t)(t187 == ((uint8_t)43ULL));
                    if (!t188) {
                        goto gm_lb45;
                    }
                    gm_b16_8 t189;
                    GM_ST(void *, (uint8_t *)&t189, (void *)(gm_skm4ndn7op5l5));
                    GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(13));
                    gm_l1 = t189;
                    goto gm_lb1;
                }
gm_lb45:;
            }
            {
                {
                    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)44ULL));
                    if (!t191) {
                        goto gm_lb46;
                    }
                    gm_b16_8 t192;
                    GM_ST(void *, (uint8_t *)&t192, (void *)(gm_stclkv4qnta30));
                    GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(8));
                    gm_l1 = t192;
                    goto gm_lb1;
                }
gm_lb46:;
                {
                    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t194 = (uint8_t)(t193 == ((uint8_t)45ULL));
                    if (!t194) {
                        goto gm_lb47;
                    }
                    gm_b16_8 t195;
                    GM_ST(void *, (uint8_t *)&t195, (void *)(gm_s7ri8monh3c4h));
                    GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(7));
                    gm_l1 = t195;
                    goto gm_lb1;
                }
gm_lb47:;
                {
                    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t197 = (uint8_t)(t196 == ((uint8_t)46ULL));
                    if (!t197) {
                        goto gm_lb48;
                    }
                    gm_b16_8 t198;
                    GM_ST(void *, (uint8_t *)&t198, (void *)(gm_s26ul1d5snk55));
                    GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(8));
                    gm_l1 = t198;
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)47ULL));
                    if (!t200) {
                        goto gm_lb49;
                    }
                    gm_b16_8 t201;
                    GM_ST(void *, (uint8_t *)&t201, (void *)(gm_s1pfh89airaa0));
                    GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(12));
                    gm_l1 = t201;
                    goto gm_lb1;
                }
gm_lb49:;
                {
                    uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t203 = (uint8_t)(t202 == ((uint8_t)48ULL));
                    if (!t203) {
                        goto gm_lb50;
                    }
                    gm_b16_8 t204;
                    GM_ST(void *, (uint8_t *)&t204, (void *)(gm_slkubuckp49ih));
                    GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(11));
                    gm_l1 = t204;
                    goto gm_lb1;
                }
gm_lb50:;
                {
                    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t206 = (uint8_t)(t205 == ((uint8_t)49ULL));
                    if (!t206) {
                        goto gm_lb51;
                    }
                    gm_b16_8 t207;
                    GM_ST(void *, (uint8_t *)&t207, (void *)(gm_sf5jsgjp29cqn));
                    GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(13));
                    gm_l1 = t207;
                    goto gm_lb1;
                }
gm_lb51:;
                {
                    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t209 = (uint8_t)(t208 == ((uint8_t)50ULL));
                    if (!t209) {
                        goto gm_lb52;
                    }
                    gm_b16_8 t210;
                    GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sik1l49uru5dh));
                    GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(12));
                    gm_l1 = t210;
                    goto gm_lb1;
                }
gm_lb52:;
                {
                    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t212 = (uint8_t)(t211 == ((uint8_t)51ULL));
                    if (!t212) {
                        goto gm_lb53;
                    }
                    gm_b16_8 t213;
                    GM_ST(void *, (uint8_t *)&t213, (void *)(gm_skk6lm5m17t6v));
                    GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(10));
                    gm_l1 = t213;
                    goto gm_lb1;
                }
gm_lb53:;
                {
                    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t215 = (uint8_t)(t214 == ((uint8_t)52ULL));
                    if (!t215) {
                        goto gm_lb54;
                    }
                    gm_b16_8 t216;
                    GM_ST(void *, (uint8_t *)&t216, (void *)(gm_sed3elknidrs3));
                    GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(10));
                    gm_l1 = t216;
                    goto gm_lb1;
                }
gm_lb54:;
                {
                    uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t218 = (uint8_t)(t217 == ((uint8_t)53ULL));
                    if (!t218) {
                        goto gm_lb55;
                    }
                    gm_b16_8 t219;
                    GM_ST(void *, (uint8_t *)&t219, (void *)(gm_sfcjpmkuggqo3));
                    GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(9));
                    gm_l1 = t219;
                    goto gm_lb1;
                }
gm_lb55:;
                {
                    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t221 = (uint8_t)(t220 == ((uint8_t)54ULL));
                    if (!t221) {
                        goto gm_lb56;
                    }
                    gm_b16_8 t222;
                    GM_ST(void *, (uint8_t *)&t222, (void *)(gm_su6ipjjrnjs7p));
                    GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(9));
                    gm_l1 = t222;
                    goto gm_lb1;
                }
gm_lb56:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 793, 44);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t223 = gm_l1;
        F.ret = t223;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
