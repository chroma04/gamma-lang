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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; uint8_t p4[3]; uint32_t f4; } gm_t84tkl8vf34ko;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t84tkl8vf34ko) == 56 && _Alignof(gm_t84tkl8vf34ko) == 8 && offsetof(gm_t84tkl8vf34ko, f0) == 0 && offsetof(gm_t84tkl8vf34ko, f1) == 16 && offsetof(gm_t84tkl8vf34ko, f2) == 32 && offsetof(gm_t84tkl8vf34ko, f3) == 48 && offsetof(gm_t84tkl8vf34ko, f4) == 52, "layout of `Decl`");
typedef struct { uint64_t f0; gm_t84tkl8vf34ko f1; } gm_t9re6l0g6jicn;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint64_t f0; gm_trqfvmjtt4sij f1; } gm_tmupb6k0asuti;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t9re6l0g6jicn) == 64 && _Alignof(gm_t9re6l0g6jicn) == 8 && offsetof(gm_t9re6l0g6jicn, f0) == 0 && offsetof(gm_t9re6l0g6jicn, f1) == 8, "layout of `(i: usize, x: Decl)`");
_Static_assert(sizeof(gm_tmupb6k0asuti) == 48 && _Alignof(gm_tmupb6k0asuti) == 8 && offsetof(gm_tmupb6k0asuti, f0) == 0 && offsetof(gm_tmupb6k0asuti, f1) == 8, "layout of `(i: usize, x: List((name: string, def: string)))`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
struct gm_fr_brace_balance_aofv41t {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_c_fn_name_77uspqp {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_code_bytes_u2ir6qv {
    gm_frame h;
    uint64_t l1; /* n */
    uint64_t ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_before_fp800ft {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_items_l330ksi {
    gm_frame h;
    gm_b16_8 l0; /* text */
    gm_trqfvmjtt4sij l1; /* items */
    uint64_t l2; /* start */
    int32_t l3; /* depth */
    uint64_t l4; /* i */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_declared_name_hovvm2b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_decls_of_srep9du {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1di25gg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_465e48m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7hq6cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n7sb6c8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tatvtbf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tklonjv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vt6lar6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_k51ik07 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_external_f65nm8q {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_575gb0c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_idfg0st {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_69vpqmq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_fe8qj55 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_j11vulv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_keqa9hq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_n9765ef {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_ui0bpsf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_vn00ddp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_1u4jnnu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_69nn7l3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8l8mb6d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c2lphrf {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dg0m8l8 {
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
struct gm_fr_format_q8i7gsb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rse2efa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_stmnn5h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u8fgvjp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uaq30dv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_function_o2lv8g6 {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_get_ece4opn {
    gm_frame h;
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_get_ptr_53uc4fm {
    gm_frame h;
    void * ret;
};
struct gm_fr_gm_names_6ner0lu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_t4c7ipjj9s3fp l2; /* seen */
    gm_b16_8 ret;
};
struct gm_fr_has_word_l11ltcj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_dklib0j {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_lljdp3h {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_ident_char_pmpasvq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ident_set_9ajalpn {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* set */
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_inline_definition_6aokb9d {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_cont_317fpdb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_inline_aqdknmj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_items_of_q103oiv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* items */
    gm_th2lqbo77pvsc ret;
    gm_b0_1 w5;
};
struct gm_fr_line_reset_73tgnor {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_016fo30 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1g734tk {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_b7l80f8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_c8kt75n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lj5vg1g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_optimized_unit_count_5mur81l {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_push_0ek3ea4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_97b8r3k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rda4o9n {
    gm_frame h;
    gm_t84tkl8vf34ko l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rlpbnuq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vnejmrp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_46rs2i6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7c10m5i {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_e4d8f0p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_qpqf461 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vefhvf2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_set_depths_1781b1p {
    gm_frame h;
    gm_t84tkl8vf34ko l5; /* d */
    gm_trqfvmjtt4sij l10; /* l */
    gm_b0_1 ret;
};
struct gm_fr_skip_literal_17lq2p8 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_sort_by_08k4kf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_by_4j6ktaf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_name_ro82p6b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_split_by_file_hgjqgsp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* decls */
    gm_t4c7ipjj9s3fp l2; /* inline_names */
    gm_trqfvmjtt4sij l3; /* bodies */
    gm_trqfvmjtt4sij l7; /* fns */
    gm_trqfvmjtt4sij l15; /* externs */
    gm_trqfvmjtt4sij l19; /* groups */
    gm_t4c7ipjj9s3fp l23; /* by_name */
    gm_trqfvmjtt4sij l24; /* nameless */
    gm_t84tkl8vf34ko l28; /* d */
    gm_trqfvmjtt4sij l30; /* l */
    gm_trqfvmjtt4sij l31; /* units */
    gm_trqfvmjtt4sij l35; /* g */
    gm_trqfvmjtt4sij l36; /* text */
    gm_trqfvmjtt4sij l43; /* fns */
    gm_trqfvmjtt4sij l44; /* ext */
    gm_trqfvmjtt4sij l45; /* all */
    gm_trqfvmjtt4sij l46; /* body */
    gm_ttu09lij36f8c ret;
    gm_b0_1 w3;
    gm_b0_1 w5;
};
struct gm_fr_split_jendjdm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_split_shared_a91530r {
    gm_frame h;
    gm_t4c7ipjj9s3fp l3; /* inline_names */
    gm_trqfvmjtt4sij l4; /* inline_defs */
    gm_trqfvmjtt4sij l9; /* decls */
    gm_trqfvmjtt4sij l10; /* defs */
    gm_trqfvmjtt4sij l11; /* externs */
    gm_trqfvmjtt4sij l14; /* items */
    gm_trqfvmjtt4sij l25; /* units */
    gm_trqfvmjtt4sij l26; /* first */
    gm_trqfvmjtt4sij l31; /* out */
    gm_trqfvmjtt4sij l34; /* u */
    gm_ttu09lij36f8c ret;
    gm_b0_1 w1;
    gm_b0_1 w2;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_count_dk0d2e4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_unit_text_mmejmp8 {
    gm_frame h;
    gm_b16_8 l0; /* decls */
    gm_trqfvmjtt4sij l5; /* taken */
    gm_t4c7ipjj9s3fp l8; /* defined */
    gm_trqfvmjtt4sij l9; /* work */
    gm_trqfvmjtt4sij l11; /* l */
    gm_trqfvmjtt4sij l26; /* chosen */
    gm_trqfvmjtt4sij l31; /* text */
    gm_t4c7ipjj9s3fp l32; /* seen */
    gm_trqfvmjtt4sij l36; /* used */
    gm_t4c7ipjj9s3fp l37; /* idents */
    gm_b16_8 ret;
};
struct gm_fr_write_815rgbg {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_cdouvhd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_d8c7tpm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_qm3m7a0 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) int32_t gm_f_brace_balance_aofv41t(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_c_fn_name_77uspqp(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_fn_name_77uspqp_c(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_code_bytes_u2ir6qv(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_decl_before_fp800ft(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_items_l330ksi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_declared_name_hovvm2b(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_decls_of_srep9du(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_deinit_lehld1n(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_nvuvq05(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_vtu6170(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_465e48m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tatvtbf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tklonjv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vt6lar6(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_k51ik07(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_external_f65nm8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_heobcrg(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_sktqt7b(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_idfg0st(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_69vpqmq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_j11vulv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_keqa9hq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n9765ef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui0bpsf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_69nn7l3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8l8mb6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c2lphrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dg0m8l8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dq39vq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rse2efa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_stmnn5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_u8fgvjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uaq30dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_function_o2lv8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_ece4opn(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_ohh8mo3(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_get_ptr_53uc4fm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gm_names_6ner0lu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_guard_o7k9a0e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_has_word_l11ltcj(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_dklib0j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0);
__attribute__((aligned(4))) void gm_f_ident_set_9ajalpn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_inline_definition_6aokb9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_introsort_3q5sbn1(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) void gm_f_items_of_q103oiv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_line_reset_73tgnor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1f7rak(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1hv6k0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sa157kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sfohpq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snvhnj5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sp8kip0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_svejqbu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_016fo30(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1g734tk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_b7l80f8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_c8kt75n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ek3ea4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rda4o9n(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rlpbnuq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vnejmrp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7o5vpta(void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_drsjd1t(void * a0, gm_b16_8 a1, gm_b0_1 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7c10m5i(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e4d8f0p(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_qpqf461(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vefhvf2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_depths_1781b1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_skip_literal_17lq2p8(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_08k4kf2(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_4j6ktaf(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sort_name_ro82p6b(void * a0);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_shared_a91530r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_suffix_8155gbi(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_text_mmejmp8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5um95qj(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_815rgbg(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_d8c7tpm(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qm3m7a0(void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_c30tdq2p48i[1] __attribute__((aligned(1)));
extern uint8_t gm_c4crumb5mc9[1] __attribute__((aligned(1)));
extern uint8_t gm_c72j1g8d455[1] __attribute__((aligned(1)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cos4c858gsq[1] __attribute__((aligned(1)));
extern uint8_t gm_crpb2rekieb[1] __attribute__((aligned(1)));
extern uint8_t gm_csu9mkbsfpo[16] __attribute__((aligned(8)));
extern void *const gm_hl1l1s81m49k[];
extern void *const gm_hl8sdnavrdjn[];
extern void *const gm_hlc7jtja1te5[];
extern void *const gm_hlrddq2io9s1[];
extern void *const gm_hlth8t37utgd[];
extern void *const gm_hlu84m640fm9[];
extern void *const gm_hlvtf175jpmc[];
extern const uint8_t gm_s1oiqt105ulvb[20];
extern const uint8_t gm_s24mra5tninbk[15];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s4pucugl3svdm[11];
extern const uint8_t gm_s7ipvt5onv6ht[4];
extern const uint8_t gm_saalafjdd3tnp[9];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sd1uj6gf5v8qh[8];
extern const uint8_t gm_se1t3fpfm9j7m[12];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_shv3ttlq9l359[15];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_skr2kn3pd8es7[11];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sm7oa350rtqnm[12];
extern const uint8_t gm_snmrruc88a739[10];
extern const uint8_t gm_so0h0lkl6bp4l[39];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so7qmna0fo4so[6];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_spkrf9e7cr6h4[7];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srccfhjr4r9mdef[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssi4rrma2q00g[7];
extern const uint8_t gm_summega2vonc9[6];
extern const gm_tmpl gm_tpl5o8dcpa0ctot;
extern const gm_tmpl gm_tpl75b43ohtuvq2;
extern const gm_tmpl gm_tpl78lk9ugsmc0b;
extern const gm_tmpl gm_tpla1fscog446dv;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_tmpl gm_tplhkjcpttbsqt0;
extern const gm_tmpl gm_tpli3v24a6u14sa;
extern const gm_tmpl gm_tpllcp48j1qim70;
extern const gm_tmpl gm_tpllfhth9mee4q8;
extern const gm_tmpl gm_tplnl7d4k0qgogq;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) uint64_t gm_f_code_bytes_u2ir6qv(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_code_bytes_u2ir6qv F;
    gm_b16_8 gm_l0; /* item */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = ((uint64_t)0ULL);
    gm_b16_8 t3 = gm_l0;
    gm_l2 = t3;
    {
        gm_node wn4 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlvtf175jpmc, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t7 = gm_l2;
        gm_b0_1 t8 = gm_f_lines_9uns0k2_sfohpq4((void *)NULL, (&wn4), NULL, t7);
        if (gm_unw.kind) {
            goto gm_ll6;
        }
    }
    goto gm_ls9;
gm_ll6:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we5; }
    goto gm_lad;
gm_ls9:;
gm_lb1:;
we5:;
    uint64_t t10 = F.l1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = gm_f_find_nab29co(t5, t6);
    gm_l3 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
    }
gm_lb1:;
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1di25gg F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_465e48m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_465e48m F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7hq6cu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jckt05t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n7sb6c8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tatvtbf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tatvtbf F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tklonjv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tklonjv F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 64)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vt6lar6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vt6lar6 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_k51ik07(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_k51ik07 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_e4d8f0p(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 32);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vogsd72 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_0jmnnpk(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 16);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_575gb0c F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)t4));
    if ((uintptr_t)t5 >= (uintptr_t)(t3)) gm_trap_index((uintptr_t)t5, t3, gm_srccfhjr4r9mdef, 683, 73);
    void * t6 = (void *)(((uint8_t *)t2) + (uintptr_t)t5 * 56);
    void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0) + 8));
    void * t9 = gm_l1;
    uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)t9));
    if ((uintptr_t)t10 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t10, t8, gm_srccfhjr4r9mdef, 683, 85);
    void * t11 = (void *)(((uint8_t *)t7) + (uintptr_t)t10 * 56);
    uint8_t t12 = gm_f_decl_before_fp800ft(t6, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_idfg0st(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_idfg0st F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5));
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
        void * t9 = gm_l1;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t9));
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t8), ((uint8_t *)&t10)));
        if (t11) {
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 16));
            void * t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 16));
            uint8_t t16 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t15));
            gm_l3 = t16;
        } else {
            gm_l3 = ((uint8_t)0);
        }
        uint8_t t17 = gm_l3;
        gm_l2 = t17;
    }
    uint8_t t18 = gm_l2;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1u4jnnu F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk04qg20jmac0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhkjcpttbsqt0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_69nn7l3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_69nn7l3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s24mra5tninbk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl78lk9ugsmc0b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8l8mb6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8l8mb6d F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skr2kn3pd8es7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli3v24a6u14sa), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8n626pi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c2lphrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c2lphrf F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_dq39vq2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dg0m8l8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dg0m8l8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si731m8b0sr6s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla1fscog446dv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q8i7gsb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_summega2vonc9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllfhth9mee4q8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_rse2efa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rse2efa F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snmrruc88a739));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllcp48j1qim70), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_stmnn5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_stmnn5h F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm7oa350rtqnm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5o8dcpa0ctot), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_u8fgvjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_u8fgvjp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pucugl3svdm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnl7d4k0qgogq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uaq30dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uaq30dv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk04qg20jmac0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhkjcpttbsqt0), t3);
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
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_ece4opn(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ece4opn F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_sktqt7b(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        gm_ttsm01t0d8srg t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    uint64_t t8 = gm_l4;
    gm_l2 = t8;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint64_t t12 = gm_l2;
    if ((uintptr_t)t12 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t12, t11, gm_srcbsc23is03ttm, 139, 12);
    gm_ttsm01t0d8srg t13;
    GM_ST(gm_b0_1, ((uint8_t *)&t13), GM_ZERO(gm_b0_1));
    ((uint8_t *)&t13)[0] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_get_ptr_53uc4fm(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ptr_53uc4fm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_heobcrg(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    uint64_t t7 = gm_l4;
    gm_l2 = t7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 147, 13);
    void * t12 = (void *)((((uint8_t *)t9) + (uintptr_t)t11 * 64) + 24);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_dklib0j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_dklib0j F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_tklonjv();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_lljdp3h F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_7hq6cu2();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_m04clhi F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jckt05t();
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
inline __attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_char_pmpasvq F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 5u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_ident_cont_317fpdb(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alnum_n81ervr F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_digit_2hoggnj(t4);
        gm_l1 = t5;
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alpha_12215ga F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_upper_iekrftp(t2);
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_lower_4ebokoh(t4);
        gm_l1 = t5;
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_digit_2hoggnj F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)48ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)57ULL));
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_cont_317fpdb F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alnum_n81ervr(t2);
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
        gm_l1 = t5;
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_inline_aqdknmj F;
    gm_b16_8 gm_l0; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soa9mnl4ahjud));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (t4) {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_so0h0lkl6bp4l));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(38));
        uint8_t t7 = gm_f_contains_gkki4kq(t5, t6);
        gm_l1 = t7;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t8 = gm_l1;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_4ebokoh F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)97ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)122ULL));
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_iekrftp F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)65ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)90ULL));
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_016fo30(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_016fo30 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n7sb6c8();
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
inline __attribute__((aligned(4))) void gm_f_list_1g734tk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1g734tk F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1di25gg();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_b7l80f8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_b7l80f8 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vt6lar6();
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
inline __attribute__((aligned(4))) void gm_f_list_c8kt75n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_c8kt75n F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_tatvtbf();
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
inline __attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lj5vg1g F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_465e48m();
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
inline __attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_optimized_unit_count_5mur81l F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
            gm_l3 = t5;
            uint64_t t6 = gm_l3;
            uint64_t t7 = (t6 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t6 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t6));
            F.ret = t7;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 64) + 8));
    if (((uint64_t)4194304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 74, 13);
    uint64_t t10 = (uint64_t)(t9 / ((uint64_t)4194304ULL));
    uint64_t t11 = gm_l1;
    uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
    uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_th2lqbo77pvsc t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 24);
        goto gm_fdone;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t8) + (uintptr_t)t13 * 16));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    gm_b16_8 t27 = gm_l2;
    gm_th2lqbo77pvsc t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t28 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t28), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ek3ea4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0ek3ea4 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_tatvtbf();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_e4d8f0p(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_97b8r3k F;
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
        gm_b16_8 t9 = gm_f_empty_slice_465e48m();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_vefhvf2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rda4o9n(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rda4o9n F;
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
        gm_b16_8 t9 = gm_f_empty_slice_n7sb6c8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_46rs2i6(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)a1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rlpbnuq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rlpbnuq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1di25gg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_qpqf461(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_73v4ttb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k4j2gj3(t8, ((uint64_t)1ULL), t10);
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
    uint64_t t18 = gm_l1;
    GM_ST(uint64_t, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vnejmrp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vnejmrp F;
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
        gm_b16_8 t9 = gm_f_empty_slice_vt6lar6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_7c10m5i(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)a1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_46rs2i6 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7c10m5i(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7c10m5i F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e4d8f0p(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_e4d8f0p F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_qpqf461(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_qpqf461 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vefhvf2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vefhvf2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_08k4kf2(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_08k4kf2 F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = gm_l1;
    gm_f_introsort_6k4m7ds(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_4j6ktaf(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_4j6ktaf F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = gm_l1;
    gm_f_introsort_3q5sbn1(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sort_name_ro82p6b(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_name_ro82p6b F;
    void * gm_l0; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 16) + 8));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 16));
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 32) + 8));
    uint8_t t9 = (t8 > ((uint64_t)0ULL));
    if (t9) {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 32));
        uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t10) + 32) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t12, gm_srccfhjr4r9mdef, 716, 32);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)((uint64_t)0ULL) * 16));
        F.ret = t13;
        goto gm_fret;
    }
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(0));
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
    gm_f_introsort_r6p6ckl(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_start_179crak(t2);
    gm_b16_8 t4 = gm_f_trim_end_6sjv6rg(t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unit_count_dk0d2e4 F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
            gm_l3 = t5;
            uint64_t t6 = gm_l3;
            uint64_t t7 = (t6 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t6 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t6));
            F.ret = t7;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 64) + 8));
    if (((uint64_t)98304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 82, 13);
    uint64_t t10 = (uint64_t)(t9 / ((uint64_t)98304ULL));
    uint64_t t11 = gm_l1;
    uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
    uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_815rgbg(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_815rgbg F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_se1t3fpfm9j7m));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(11));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl75b43ohtuvq2), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_cdouvhd F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sk04qg20jmac0));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplhkjcpttbsqt0), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_d8c7tpm(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_d8c7tpm F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s4pucugl3svdm));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplnl7d4k0qgogq), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qm3m7a0(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_qm3m7a0 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t4 = gm_f_write_5um95qj(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplfqr816g9de0k), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) int32_t gm_f_brace_balance_aofv41t(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_brace_balance_aofv41t F;
    gm_b16_8 gm_l0; /* line */
    int32_t gm_l1; /* d */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_l1 = ((int32_t)0LL);
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t7 = (t5 < t6);
            if (!t7) {
                goto gm_lb1;
            }
            {
                void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t10 = gm_l2;
                uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t8) + (uintptr_t)t10 * 1));
                gm_l3 = t11;
                uint8_t t12 = gm_l3;
                uint8_t t13 = ((t12 == ((uint8_t)34ULL)));
                if (t13) {
                    gm_l4 = ((uint8_t)1);
                } else {
                    uint8_t t14 = gm_l3;
                    uint8_t t15 = ((t14 == ((uint8_t)39ULL)));
                    gm_l4 = t15;
                }
                uint8_t t16 = gm_l4;
                if (t16) {
                    gm_b16_8 t17 = gm_l0;
                    uint64_t t18 = gm_l2;
                    uint64_t t19 = gm_f_skip_literal_17lq2p8(t17, t18);
                    gm_l2 = t19;
                    goto gm_lb3;
                }
                uint8_t t20 = gm_l3;
                uint8_t t21 = ((t20 == ((uint8_t)123ULL)));
                if (t21) {
                    int32_t t22 = gm_l1;
                    int32_t r23; bool o24 = __builtin_add_overflow(t22, ((int32_t)1LL), &r23);
                    if (o24) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 419, 23);
                    int32_t t25 = r23;
                    gm_l1 = t25;
                } else {
                    uint8_t t26 = gm_l3;
                    uint8_t t27 = ((t26 == ((uint8_t)125ULL)));
                    if (t27) {
                        int32_t t28 = gm_l1;
                        int32_t r29; bool o30 = __builtin_sub_overflow(t28, ((int32_t)1LL), &r29);
                        if (o30) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 419, 51);
                        int32_t t31 = r29;
                        gm_l1 = t31;
                    }
                }
                uint64_t t32 = gm_l2;
                uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 420, 9);
                uint64_t t35 = r33;
                gm_l2 = t35;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    int32_t t36 = gm_l1;
    F.ret = t36;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_c_fn_name_77uspqp(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_c_fn_name_77uspqp F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    GM_STACK_CHECK(&F, 454u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l3;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l1;
            uint64_t r13; bool o14 = __builtin_add_overflow(t12, ((uint64_t)3ULL), &r13);
            if (o14) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 176, 11);
            uint64_t t15 = r13;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t17 = (t15 <= t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t20 = gm_l1;
                        uint64_t t21 = gm_l1;
                        uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)3ULL), &r22);
                        if (o23) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 177, 20);
                        uint64_t t24 = r22;
                        uint64_t t25 = (uintptr_t)t20;
                        uint64_t t26 = (uintptr_t)t24;
                        if (t25 > t26 || t26 > (uintptr_t)t19) gm_trap_slice(t25, t26, t19, gm_srccfhjr4r9mdef, 177, 12);
                        uint64_t t27 = ((uintptr_t)t24) - (uintptr_t)t20;
                        gm_b16_8 t28;
                        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t18 + (uintptr_t)t20 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s7ipvt5onv6ht));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
                        uint8_t t30 = (gm_eq0iqiubfm7e(((uint8_t *)&t28), ((uint8_t *)&t29)));
                        if (!t30) {
                            goto gm_lb5;
                        }
                        uint64_t t31 = gm_l1;
                        uint8_t t32 = ((t31 == ((uint64_t)0ULL)));
                        if (t32) {
                            gm_l3 = ((uint8_t)1);
                        } else {
                            void * t33 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t35 = gm_l1;
                            uint64_t r36; bool o37 = __builtin_sub_overflow(t35, ((uint64_t)1ULL), &r36);
                            if (o37) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 177, 81);
                            uint64_t t38 = r36;
                            if ((uintptr_t)t38 >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)t38, t34, gm_srccfhjr4r9mdef, 177, 76);
                            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)t38 * 1));
                            uint8_t t40 = gm_f_is_ident_cont_317fpdb(t39);
                            uint8_t t41 = (uint8_t)!t40;
                            gm_l3 = t41;
                        }
                        uint8_t t42 = gm_l3;
                        if (!t42) {
                            goto gm_lb5;
                        }
                        uint64_t t43 = gm_l1;
                        gm_l2 = t43;
                        {
                            for (;;) {
                                uint64_t t44 = gm_l2;
                                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t46 = (t44 < t45);
                                if (!t46) {
                                    goto gm_lb6;
                                }
                                void * t47 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t49 = gm_l2;
                                uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t47) + (uintptr_t)t49 * 1));
                                uint8_t t51 = gm_f_is_ident_cont_317fpdb(t50);
                                if (!t51) {
                                    goto gm_lb6;
                                }
                                {
                                    uint64_t t52 = gm_l2;
                                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                                    if (o54) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 179, 68);
                                    uint64_t t55 = r53;
                                    gm_l2 = t55;
                                }
gm_lb8:;
                            }
                        }
gm_lb6:;
                        {
                            {
                                uint64_t t56 = gm_l2;
                                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t58 = (t56 < t57);
                                if (!t58) {
                                    goto gm_lb10;
                                }
                                void * t59 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t61 = gm_l2;
                                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t59) + (uintptr_t)t61 * 1));
                                uint8_t t63 = ((t62 == ((uint8_t)40ULL)));
                                if (!t63) {
                                    goto gm_lb10;
                                }
                                void * t64 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t66 = gm_l1;
                                uint64_t t67 = gm_l2;
                                uint64_t t68 = (uintptr_t)t66;
                                uint64_t t69 = (uintptr_t)t67;
                                if (t68 > t69 || t69 > (uintptr_t)t65) gm_trap_slice(t68, t69, t65, gm_srccfhjr4r9mdef, 180, 57);
                                uint64_t t70 = ((uintptr_t)t67) - (uintptr_t)t66;
                                gm_b16_8 t71;
                                GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t64 + (uintptr_t)t66 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(t70));
                                gm_th2lqbo77pvsc t72;
                                GM_ST(uint8_t, (uint8_t *)&t72 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t72 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t72 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t72), t71);
                                ((uint8_t *)&t72)[16] = 1;
                                memmove(gm_out, ((uint8_t *)&t72), 24);
                                goto gm_fdone;
                            }
gm_lb10:;
                        }
gm_lb9:;
                        uint64_t t73 = gm_l2;
                        gm_l1 = t73;
                        goto gm_lb4;
                    }
gm_lb5:;
                    uint64_t t74 = gm_l1;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    if (o76) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 183, 13);
                    uint64_t t77 = r75;
                    gm_l1 = t77;
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_th2lqbo77pvsc t78;
    memset(&t78, 0, sizeof t78);
    memmove(gm_out, ((uint8_t *)&t78), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_fn_name_77uspqp_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_c_fn_name_77uspqp(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) uint64_t gm_f_code_bytes_u2ir6qv(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_before_fp800ft(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_before_fp800ft F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* an */
    gm_b16_8 gm_l3; /* bn */
    GM_STACK_CHECK_SHALLOW(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 48));
    void * t4 = gm_l1;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t4) + 48));
    uint8_t t6 = (!(t3 == t5));
    if (t6) {
        void * t7 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t7) + 48));
        void * t9 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 48));
        uint8_t t11 = (t8 < t10);
        F.ret = t11;
        goto gm_fret;
    }
    void * t12 = gm_l0;
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 52));
    void * t14 = gm_l1;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 52));
    uint8_t t16 = (!(t13 == t15));
    if (t16) {
        void * t17 = gm_l0;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 52));
        void * t19 = gm_l1;
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)t19) + 52));
        uint8_t t21 = (t18 < t20);
        F.ret = t21;
        goto gm_fret;
    }
    void * t22 = gm_l0;
    gm_b16_8 t23 = gm_f_sort_name_ro82p6b(t22);
    gm_l2 = t23;
    void * t24 = gm_l1;
    gm_b16_8 t25 = gm_f_sort_name_ro82p6b(t24);
    gm_l3 = t25;
    gm_b16_8 t26 = gm_l2;
    gm_b16_8 t27 = gm_l3;
    uint8_t t28 = (!gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
    if (t28) {
        gm_b16_8 t29 = gm_l2;
        gm_b16_8 t30 = gm_l3;
        uint8_t t31 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t29), ((uint8_t *)&t30));
        F.ret = t31;
        goto gm_fret;
    }
    void * t32 = gm_l0;
    uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t32) + 16) + 8));
    uint8_t t34 = ((t33 == ((uint64_t)0ULL)));
    void * t35 = gm_l1;
    uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t35) + 16) + 8));
    uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
    uint8_t t38 = (!(t34 == t37));
    if (t38) {
        void * t39 = gm_l1;
        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t39) + 16) + 8));
        uint8_t t41 = ((t40 == ((uint64_t)0ULL)));
        F.ret = t41;
        goto gm_fret;
    }
    void * t42 = gm_l0;
    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)t42));
    void * t44 = gm_l1;
    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)t44));
    uint8_t t46 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t45));
    F.ret = t46;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_decl_items_l330ksi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_decl_items_l330ksi F;
    gm_b16_8 gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_b16_8 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t5;
    gm_f_list_epvducg((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t5;
    F.l2 = ((uint64_t)0ULL);
    F.l3 = ((int32_t)0LL);
    F.l4 = ((uint64_t)0ULL);
    gm_b16_8 t6 = F.l0;
    gm_l5 = t6;
    {
        gm_node wn7 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlu84m640fm9, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l5;
        gm_b0_1 t11 = gm_f_lines_9uns0k2_sa157kc((void *)NULL, (&wn7), NULL, t10);
        if (gm_unw.kind) {
            goto gm_ll9;
        }
    }
    goto gm_ls12;
gm_ll9:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we8; }
    goto gm_lad;
gm_ls12:;
gm_lb1:;
we8:;
    {
        {
            uint64_t t13 = F.l2;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb3;
            }
            void * t16 = GM_LD(void *, ((uint8_t *)&F.l0));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
            uint64_t t18 = F.l2;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
            uint64_t t20 = (uintptr_t)t18;
            uint64_t t21 = (uintptr_t)t19;
            if (t20 > t21 || t21 > (uintptr_t)t17) gm_trap_slice(t20, t21, t17, gm_srccfhjr4r9mdef, 405, 38);
            uint64_t t22 = ((uintptr_t)t19) - (uintptr_t)t18;
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t16 + (uintptr_t)t18 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22));
            gm_b16_8 t24 = gm_f_trim_fdrfe6i(t23);
            gm_l6 = t24;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t26 = (t25 > ((uint64_t)0ULL));
            if (!t26) {
                goto gm_lb3;
            }
            void * t27 = (void *)((uint8_t *)&F.l1);
            void * t28 = GM_LD(void *, ((uint8_t *)&F.l0));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
            uint64_t t30 = F.l2;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
            uint64_t t32 = (uintptr_t)t30;
            uint64_t t33 = (uintptr_t)t31;
            if (t32 > t33 || t33 > (uintptr_t)t29) gm_trap_slice(t32, t33, t29, gm_srccfhjr4r9mdef, 405, 82);
            uint64_t t34 = ((uintptr_t)t31) - (uintptr_t)t30;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t28 + (uintptr_t)t30 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34));
            gm_tftb1odc739t5 t36 = gm_f_push_3ef746b(t27, t35);
            gm_l7 = t36;
            uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srccfhjr4r9mdef, 405, 71);
            goto gm_lb2;
        }
gm_lb3:;
    }
gm_lb2:;
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    F.ret = t38;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_declared_name_hovvm2b(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declared_name_hovvm2b F;
    gm_b16_8 gm_l0; /* item */
    gm_b16_8 gm_l1; /* text */
    uint64_t gm_l2; /* nl */
    gm_b16_8 gm_l3; /* rest */
    uint64_t gm_l4; /* j */
    gm_b16_8 gm_l5; /* name */
    int32_t gm_l6; /* depth */
    uint64_t gm_l7; /* i */
    uint8_t gm_l8; /* c */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 1082u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l10;
    uint64_t gm_l11;
    gm_b16_8 gm_l12;
    uint8_t gm_l13;
    gm_b16_8 t18 = gm_l0;
    gm_l1 = t18;
    {
        for (;;) {
            gm_b16_8 t19 = gm_l1;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_so7qmna0fo4so));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
            uint8_t t21 = gm_f_starts_with_6vvt01e(t19, t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                gm_b16_8 t22 = gm_l1;
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t24 = gm_f_find_nab29co(t22, t23);
                gm_l10 = t24;
                uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                if (t25) {
                    uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                    gm_l11 = t26;
                } else {
                    gm_b16_8 t27;
                    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(0));
                    F.ret = t27;
                    goto gm_fret;
                }
                uint64_t t28 = gm_l11;
                gm_l2 = t28;
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t31 = gm_l2;
                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                if (o33) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 473, 21);
                uint64_t t34 = r32;
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t36 = (uintptr_t)t34;
                uint64_t t37 = (uintptr_t)t35;
                if (t36 > t37 || t37 > (uintptr_t)t30) gm_trap_slice(t36, t37, t30, gm_srccfhjr4r9mdef, 473, 16);
                uint64_t t38 = ((uintptr_t)t35) - (uintptr_t)t34;
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t29 + (uintptr_t)t34 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
                gm_l1 = t39;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_b16_8 t40 = gm_l1;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_saalafjdd3tnp));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(8));
    uint8_t t42 = gm_f_starts_with_6vvt01e(t40, t41);
    if (t42) {
        void * t43 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_saalafjdd3tnp));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(8));
        gm_l12 = t45;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t48 = (uintptr_t)t46;
        uint64_t t49 = (uintptr_t)t47;
        if (t48 > t49 || t49 > (uintptr_t)t44) gm_trap_slice(t48, t49, t44, gm_srccfhjr4r9mdef, 476, 17);
        uint64_t t50 = ((uintptr_t)t47) - (uintptr_t)t46;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t43 + (uintptr_t)t46 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50));
        gm_l3 = t51;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t52 = gm_l4;
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t54 = (t52 < t53);
                if (!t54) {
                    goto gm_lb4;
                }
                void * t55 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t57 = gm_l4;
                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)t57 * 1));
                uint8_t t59 = gm_f_ident_char_pmpasvq(t58);
                if (!t59) {
                    goto gm_lb4;
                }
                {
                    uint64_t t60 = gm_l4;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    if (o62) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 478, 54);
                    uint64_t t63 = r61;
                    gm_l4 = t63;
                }
gm_lb6:;
            }
        }
gm_lb4:;
        void * t64 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t66 = gm_l4;
        uint64_t t67 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t68 = (uintptr_t)t66;
        if (t67 > t68 || t68 > (uintptr_t)t65) gm_trap_slice(t67, t68, t65, gm_srccfhjr4r9mdef, 479, 16);
        uint64_t t69 = ((uintptr_t)t66) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t64 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69));
        F.ret = t70;
        goto gm_fret;
    }
    gm_b16_8 t71 = gm_l1;
    gm_b16_8 t72;
    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_seccpktgmrevt));
    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(1));
    uint8_t t73 = gm_f_starts_with_6vvt01e(t71, t72);
    if (t73) {
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(0));
        F.ret = t74;
        goto gm_fret;
    }
    gm_b16_8 t75;
    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(0));
    gm_l5 = t75;
    gm_l6 = ((int32_t)0LL);
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t76 = gm_l7;
            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t78 = (t76 < t77);
            if (!t78) {
                goto gm_lb7;
            }
            {
                void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t81 = gm_l7;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)t81 * 1));
                gm_l8 = t82;
                uint8_t t83 = gm_l8;
                uint8_t t84 = ((t83 == ((uint8_t)34ULL)));
                if (t84) {
                    gm_l13 = ((uint8_t)1);
                } else {
                    uint8_t t85 = gm_l8;
                    uint8_t t86 = ((t85 == ((uint8_t)39ULL)));
                    gm_l13 = t86;
                }
                uint8_t t87 = gm_l13;
                if (t87) {
                    gm_b16_8 t88 = gm_l1;
                    uint64_t t89 = gm_l7;
                    uint64_t t90 = gm_f_skip_literal_17lq2p8(t88, t89);
                    gm_l7 = t90;
                    goto gm_lb9;
                }
                uint8_t t91 = gm_l8;
                gm_toh0ootodhjue t92;
                GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)40ULL));
                GM_ST(uint8_t, ((uint8_t *)&t92 + 1), ((uint8_t)91ULL));
                GM_ST(uint8_t, ((uint8_t *)&t92 + 2), ((uint8_t)123ULL));
                uint8_t t93 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t92) + 0, ((uint8_t *)&t91))) t93 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t92) + 1, ((uint8_t *)&t91))) t93 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t92) + 2, ((uint8_t *)&t91))) t93 = 1;
                uint8_t t94 = (uint8_t)(t93);
                if (t94) {
                    int32_t t95 = gm_l6;
                    int32_t r96; bool o97 = __builtin_add_overflow(t95, ((int32_t)1LL), &r96);
                    if (o97) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 492, 13);
                    int32_t t98 = r96;
                    gm_l6 = t98;
                } else {
                    uint8_t t99 = gm_l8;
                    gm_toh0ootodhjue t100;
                    GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)41ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t100 + 1), ((uint8_t)93ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t100 + 2), ((uint8_t)125ULL));
                    uint8_t t101 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t100) + 0, ((uint8_t *)&t99))) t101 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t100) + 1, ((uint8_t *)&t99))) t101 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t100) + 2, ((uint8_t *)&t99))) t101 = 1;
                    uint8_t t102 = (uint8_t)(t101);
                    if (t102) {
                        int32_t t103 = gm_l6;
                        int32_t r104; bool o105 = __builtin_sub_overflow(t103, ((int32_t)1LL), &r104);
                        if (o105) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 494, 13);
                        int32_t t106 = r104;
                        gm_l6 = t106;
                    } else {
                        {
                            {
                                uint8_t t107 = gm_l8;
                                uint8_t t108 = ((t107 == ((uint8_t)61ULL)));
                                if (!t108) {
                                    goto gm_lb11;
                                }
                                int32_t t109 = gm_l6;
                                uint8_t t110 = ((t109 == ((int32_t)0LL)));
                                if (!t110) {
                                    goto gm_lb11;
                                }
                                goto gm_lb7;
                            }
gm_lb11:;
                            uint8_t t111 = gm_l8;
                            uint8_t t112 = gm_f_ident_char_pmpasvq(t111);
                            if (t112) {
                                uint64_t t113 = gm_l7;
                                gm_l9 = t113;
                                {
                                    for (;;) {
                                        uint64_t t114 = gm_l9;
                                        uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint8_t t116 = (t114 < t115);
                                        if (!t116) {
                                            goto gm_lb12;
                                        }
                                        void * t117 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t119 = gm_l9;
                                        uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t117) + (uintptr_t)t119 * 1));
                                        uint8_t t121 = gm_f_ident_char_pmpasvq(t120);
                                        if (!t121) {
                                            goto gm_lb12;
                                        }
                                        {
                                            uint64_t t122 = gm_l9;
                                            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                                            if (o124) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 499, 58);
                                            uint64_t t125 = r123;
                                            gm_l9 = t125;
                                        }
gm_lb14:;
                                    }
                                }
gm_lb12:;
                                {
                                    {
                                        int32_t t126 = gm_l6;
                                        uint8_t t127 = ((t126 == ((int32_t)0LL)));
                                        if (!t127) {
                                            goto gm_lb16;
                                        }
                                        void * t128 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t130 = gm_l7;
                                        uint64_t t131 = gm_l9;
                                        uint64_t t132 = (uintptr_t)t130;
                                        uint64_t t133 = (uintptr_t)t131;
                                        if (t132 > t133 || t133 > (uintptr_t)t129) gm_trap_slice(t132, t133, t129, gm_srccfhjr4r9mdef, 500, 31);
                                        uint64_t t134 = ((uintptr_t)t131) - (uintptr_t)t130;
                                        gm_b16_8 t135;
                                        GM_ST(void *, (uint8_t *)&t135, (void *)(((uint8_t *)t128 + (uintptr_t)t130 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(t134));
                                        gm_b16_8 t136;
                                        GM_ST(void *, (uint8_t *)&t136, (void *)(gm_s7ipvt5onv6ht));
                                        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(3));
                                        uint8_t t137 = gm_f_starts_with_6vvt01e(t135, t136);
                                        if (!t137) {
                                            goto gm_lb16;
                                        }
                                        void * t138 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t140 = gm_l7;
                                        uint64_t t141 = gm_l9;
                                        uint64_t t142 = (uintptr_t)t140;
                                        uint64_t t143 = (uintptr_t)t141;
                                        if (t142 > t143 || t143 > (uintptr_t)t139) gm_trap_slice(t142, t143, t139, gm_srccfhjr4r9mdef, 500, 70);
                                        uint64_t t144 = ((uintptr_t)t141) - (uintptr_t)t140;
                                        gm_b16_8 t145;
                                        GM_ST(void *, (uint8_t *)&t145, (void *)(((uint8_t *)t138 + (uintptr_t)t140 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(t144));
                                        gm_l5 = t145;
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                }
gm_lb15:;
                                uint64_t t146 = gm_l9;
                                gm_l7 = t146;
                                goto gm_lb9;
                            }
                        }
gm_lb10:;
                    }
                }
                uint64_t t147 = gm_l7;
                uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                if (o149) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 504, 9);
                uint64_t t150 = r148;
                gm_l7 = t150;
            }
gm_lb9:;
        }
    }
gm_lb7:;
    gm_b16_8 t151 = gm_l5;
    F.ret = t151;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_decls_of_srep9du(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_decls_of_srep9du F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* text */
    uint8_t gm_l2; /* rank */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* item */
    GM_STACK_CHECK(&F, 320u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    gm_b16_8 t7 = gm_l1;
    gm_b16_8 t8 = gm_f_decl_items_l330ksi((void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t8;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t14 = gm_l4;
                {
                    {
                        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t12) + (uintptr_t)t14 * 16));
                        gm_l5 = t15;
                        void * t16 = gm_l0;
                        gm_b16_8 t17 = gm_l5;
                        gm_b16_8 t18 = gm_l5;
                        gm_b16_8 t19 = gm_f_declared_name_hovvm2b(t18);
                        gm_b16_8 t20 = gm_l5;
                        gm_b16_8 t21 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t20);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t22 = gm_l2;
                        gm_t84tkl8vf34ko t23;
                        GM_ST(uint8_t, (uint8_t *)&t23 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t23 + 50, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t17);
                        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t19);
                        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 32), t21);
                        GM_ST(uint8_t, ((uint8_t *)&t23 + 48), t22);
                        GM_ST(uint32_t, ((uint8_t *)&t23 + 52), ((uint32_t)0ULL));
                        gm_tftb1odc739t5 t24 = gm_f_push_rda4o9n(t16, &t23);
                        gm_l6 = t24;
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srccfhjr4r9mdef, 376, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 375, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l4;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l4 = t29;
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
inline __attribute__((aligned(4))) void gm_f_external_f65nm8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_external_f65nm8q F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* r */
    gm_b16_8 gm_l2; /* rest */
    gm_b16_8 gm_l3; /* r */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 645u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)gm_csu9mkbsfpo));
            gm_f_strip_prefix_b4949s8((void *)&gm_l5, t6, t7);
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l1 = t9;
            gm_b16_8 t10 = gm_l1;
            gm_th2lqbo77pvsc t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t11), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
    gm_l2 = t12;
    {
        {
            gm_b16_8 t13 = gm_l0;
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
            gm_f_strip_prefix_b4949s8((void *)&gm_l6, t13, t14);
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (!t15) {
                goto gm_lb4;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_l3 = t16;
            gm_b16_8 t17 = gm_l3;
            gm_l2 = t17;
            goto gm_lb3;
        }
gm_lb4:;
        gm_b16_8 t18 = gm_l0;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1oiqt105ulvb));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(19));
        gm_f_strip_prefix_b4949s8((void *)&gm_l7, t18, t19);
        uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
        if (t20) {
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_l8 = t21;
        } else {
            gm_th2lqbo77pvsc t22;
            memset(&t22, 0, sizeof t22);
            memmove(gm_out, ((uint8_t *)&t22), 24);
            goto gm_fdone;
        }
        gm_b16_8 t23 = gm_l8;
        gm_l4 = t23;
        gm_b16_8 t24 = gm_l4;
        gm_tnpifv20irnph t25;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
        gm_f_format_69nn7l3((void *)&gm_l9, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srccfhjr4r9mdef, 242, 16);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_l2 = t28;
    }
gm_lb3:;
    gm_b16_8 t29 = gm_l2;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_so0h0lkl6bp4l));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(38));
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(0));
    gm_f_replace_alloc_t7hndlq((void *)&gm_l10, (void *)NULL, cx, NULL, t29, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srccfhjr4r9mdef, 244, 12);
    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
    gm_th2lqbo77pvsc t35;
    GM_ST(uint8_t, (uint8_t *)&t35 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t35 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t35 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t35), t34);
    ((uint8_t *)&t35)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t35), 24);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_idfg0st(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_69vpqmq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_69vpqmq F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* d */
    uint64_t gm_l4; /* k */
    gm_b16_8 gm_l5; /* d */
    GM_STACK_CHECK(&F, 1352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l6;
    uint8_t gm_l7;
    gm_tf5srh4mp4b3i gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    gm_b16_8 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    uint64_t gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_b16_8 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_th2lqbo77pvsc gm_l20;
    gm_b16_8 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    {
        {
            gm_b16_8 t9 = gm_l0;
            gm_l1 = t9;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            if (t11) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    gm_b16_8 t12 = gm_l1;
                    gm_f_c_fn_name_77uspqp((void *)&gm_l6, t12);
                    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
                    if (!t13) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                    gm_l2 = t14;
                    void * t15 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l2);
                    gm_b16_8 t16 = gm_l2;
                    gm_tf5srh4mp4b3i t17 = gm_f_get_espvf96(t15, t16);
                    gm_l8 = t17;
                    {
                        {
                            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                            if (t18) {
                                goto gm_lb6;
                            }
                            gm_l7 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l7 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 557, 40);
                    }
gm_lb5:;
                    uint8_t t19 = gm_l7;
                    if (!t19) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t20 = gm_l1;
                    gm_b16_8 t21;
                    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_soa9mnl4ahjud));
                    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(7));
                    gm_f_strip_prefix_b4949s8((void *)&gm_l9, t20, t21);
                    uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
                    if (t22) {
                        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                        gm_l10 = t23;
                    } else {
                        gm_b16_8 t24 = gm_l1;
                        gm_l10 = t24;
                    }
                    gm_b16_8 t25 = gm_l10;
                    gm_l3 = t25;
                    gm_b16_8 t26 = gm_l3;
                    gm_b16_8 t27;
                    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sd1uj6gf5v8qh));
                    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(7));
                    uint8_t t28 = gm_f_contains_gkki4kq(t26, t27);
                    uint8_t t29 = (uint8_t)!t28;
                    if (t29) {
                        gm_b16_8 t30 = gm_l3;
                        gm_tnpifv20irnph t31;
                        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
                        gm_f_format_rse2efa((void *)&gm_l11, (void *)NULL, cx, NULL, t31);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srccfhjr4r9mdef, 559, 53);
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                        gm_l3 = t34;
                    }
                    void * t35 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
                    gm_b16_8 t36 = gm_l3;
                    gm_tnpifv20irnph t37;
                    GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
                    gm_f_format_uaq30dv((void *)&gm_l12, (void *)NULL, cx, NULL, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srccfhjr4r9mdef, 560, 35);
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                    gm_b16_8 t41 = gm_l2;
                    gm_b16_8 t42 = gm_l3;
                    gm_b16_8 t43 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t44 = GM_LD(uint8_t, ((uint8_t *)gm_c72j1g8d455));
                    gm_t84tkl8vf34ko t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 50, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t40);
                    GM_ST(gm_b16_8, ((uint8_t *)&t45 + 16), t41);
                    GM_ST(gm_b16_8, ((uint8_t *)&t45 + 32), t43);
                    GM_ST(uint8_t, ((uint8_t *)&t45 + 48), t44);
                    GM_ST(uint32_t, ((uint8_t *)&t45 + 52), ((uint32_t)0ULL));
                    gm_tftb1odc739t5 t46 = gm_f_push_rda4o9n(t35, &t45);
                    gm_l13 = t46;
                    uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                    if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srccfhjr4r9mdef, 560, 13);
                    void * t48 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l2);
                    gm_b16_8 t49 = gm_l2;
                    gm_tf5srh4mp4b3i t50 = gm_f_get_espvf96(t48, t49);
                    gm_l14 = t50;
                    uint8_t t51 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
                    if (t51) {
                        uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l15 = t52;
                    } else {
                        gm_trap("reached `@unreachable()`", gm_srccfhjr4r9mdef, 561, 46);
                    }
                    uint64_t t53 = gm_l15;
                    gm_l4 = t53;
                    void * t54 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l15));
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l15) + 8));
                    uint64_t t56 = gm_l4;
                    if ((uintptr_t)t56 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t56, t55, gm_srccfhjr4r9mdef, 562, 13);
                    void * t57 = (void *)(((uint8_t *)t54) + (uintptr_t)t56 * 40);
                    gm_b16_8 t58 = gm_l2;
                    gm_b16_8 t59 = gm_l1;
                    gm_f_external_f65nm8q((void *)&gm_l16, (void *)NULL, cx, NULL, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
                    if (t60) {
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                        gm_l17 = t61;
                    } else {
                        gm_b16_8 t62 = gm_l1;
                        gm_l17 = t62;
                    }
                    gm_b16_8 t63 = gm_l17;
                    gm_tnpifv20irnph t64;
                    GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
                    gm_f_format_u8fgvjp((void *)&gm_l18, (void *)NULL, cx, NULL, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srccfhjr4r9mdef, 562, 53);
                    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                    gm_t6j6smj7qoctu t68;
                    GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t58);
                    GM_ST(gm_b16_8, ((uint8_t *)&t68 + 16), t67);
                    gm_tftb1odc739t5 t69 = gm_f_push_0ek3ea4(t57, &t68);
                    gm_l19 = t69;
                    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srccfhjr4r9mdef, 562, 13);
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            gm_b16_8 t71 = gm_l1;
            gm_f_external_f65nm8q((void *)&gm_l20, (void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
            if (t72) {
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                gm_l21 = t73;
            } else {
                gm_b16_8 t74 = gm_l1;
                gm_l21 = t74;
            }
            gm_b16_8 t75 = gm_l21;
            gm_l5 = t75;
            void * t76 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
            gm_b16_8 t77 = gm_l5;
            gm_tnpifv20irnph t78;
            GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t77);
            gm_f_format_uaq30dv((void *)&gm_l22, (void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srccfhjr4r9mdef, 566, 31);
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
            gm_b16_8 t82 = gm_l5;
            gm_b16_8 t83 = gm_f_declared_name_hovvm2b(t82);
            gm_b16_8 t84 = gm_l5;
            gm_b16_8 t85 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t86 = GM_LD(uint8_t, ((uint8_t *)gm_c72j1g8d455));
            gm_t84tkl8vf34ko t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 50, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t87 + 0), t81);
            GM_ST(gm_b16_8, ((uint8_t *)&t87 + 16), t83);
            GM_ST(gm_b16_8, ((uint8_t *)&t87 + 32), t85);
            GM_ST(uint8_t, ((uint8_t *)&t87 + 48), t86);
            GM_ST(uint32_t, ((uint8_t *)&t87 + 52), ((uint32_t)0ULL));
            gm_tftb1odc739t5 t88 = gm_f_push_rda4o9n(t76, &t87);
            gm_l23 = t88;
            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srccfhjr4r9mdef, 566, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 555, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_fe8qj55 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* t */
    GM_STACK_CHECK(&F, 502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l4;
    uint8_t gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        {
            gm_b16_8 t8 = gm_l0;
            gm_l1 = t8;
            uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4));
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t r11; bool o12 = __builtin_add_overflow(t9, t10, &r11);
            if (o12) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 388, 16);
            uint64_t t13 = r11;
            gm_l2 = t13;
            uint64_t t14 = gm_l2;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8));
            uint8_t t16 = (t14 < t15);
            if (t16) {
                uint64_t t17 = gm_l2;
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 389, 29);
                uint64_t t20 = r18;
                gm_l2 = t20;
            }
            {
                {
                    int32_t t21 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
                    uint8_t t22 = ((t21 == ((int32_t)0LL)));
                    if (!t22) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t23 = gm_l1;
                    gm_b16_8 t24;
                    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_seccpktgmrevt));
                    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
                    uint8_t t25 = gm_f_starts_with_6vvt01e(t23, t24);
                    if (!t25) {
                        goto gm_lb4;
                    }
                    void * t26 = (void *)((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l1);
                    void * t27 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0));
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8));
                    uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2));
                    uint64_t t30 = gm_l2;
                    uint64_t t31 = (uintptr_t)t29;
                    uint64_t t32 = (uintptr_t)t30;
                    if (t31 > t32 || t32 > (uintptr_t)t28) gm_trap_slice(t31, t32, t28, gm_srccfhjr4r9mdef, 391, 24);
                    uint64_t t33 = ((uintptr_t)t30) - (uintptr_t)t29;
                    gm_b16_8 t34;
                    GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t27 + (uintptr_t)t29 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33));
                    gm_tftb1odc739t5 t35 = gm_f_push_3ef746b(t26, t34);
                    gm_l4 = t35;
                    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srccfhjr4r9mdef, 391, 13);
                    uint64_t t37 = gm_l2;
                    GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2), t37);
                    uint64_t t38 = gm_l2;
                    GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4), t38);
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            int32_t t39 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
            gm_b16_8 t40 = gm_l1;
            int32_t t41 = gm_f_brace_balance_aofv41t(t40);
            int32_t r42; bool o43 = __builtin_add_overflow(t39, t41, &r42);
            if (o43) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 396, 9);
            int32_t t44 = r42;
            GM_ST(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3), t44);
            gm_b16_8 t45 = gm_l1;
            gm_b16_8 t46 = gm_f_trim_end_6sjv6rg(t45);
            gm_l3 = t46;
            {
                {
                    int32_t t47 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
                    uint8_t t48 = (t47 <= ((int32_t)0LL));
                    if (!t48) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t49 = gm_l3;
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_spod3tmkh25up));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(1));
                    uint8_t t51 = gm_f_ends_with_biriu5l(t49, t50);
                    if (t51) {
                        gm_l5 = ((uint8_t)1);
                    } else {
                        gm_b16_8 t52 = gm_l3;
                        gm_b16_8 t53;
                        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s2cuflhqkkuaq));
                        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(1));
                        uint8_t t54 = gm_f_ends_with_biriu5l(t52, t53);
                        gm_l5 = t54;
                    }
                    uint8_t t55 = gm_l5;
                    if (!t55) {
                        goto gm_lb6;
                    }
                    void * t56 = (void *)((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l1);
                    void * t57 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0));
                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8));
                    uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2));
                    uint64_t t60 = gm_l2;
                    uint64_t t61 = (uintptr_t)t59;
                    uint64_t t62 = (uintptr_t)t60;
                    if (t61 > t62 || t62 > (uintptr_t)t58) gm_trap_slice(t61, t62, t58, gm_srccfhjr4r9mdef, 399, 24);
                    uint64_t t63 = ((uintptr_t)t60) - (uintptr_t)t59;
                    gm_b16_8 t64;
                    GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t57 + (uintptr_t)t59 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(t63));
                    gm_tftb1odc739t5 t65 = gm_f_push_3ef746b(t56, t64);
                    gm_l6 = t65;
                    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srccfhjr4r9mdef, 399, 13);
                    uint64_t t67 = gm_l2;
                    GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2), t67);
                    GM_ST(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3), ((int32_t)0LL));
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t68 = gm_l2;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4), t68);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 387, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_j11vulv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_j11vulv F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* d */
    GM_STACK_CHECK(&F, 717u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l4;
    uint8_t gm_l5;
    gm_ttsm01t0d8srg gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_th2lqbo77pvsc gm_l12;
    gm_b16_8 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    gm_b16_8 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    {
        {
            gm_b16_8 t9 = gm_l0;
            gm_l1 = t9;
            {
                {
                    gm_b16_8 t10 = gm_l1;
                    gm_f_c_fn_name_77uspqp((void *)&gm_l4, t10);
                    uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
                    if (!t11) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_l2 = t12;
                    void * t13 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l3);
                    gm_b16_8 t14 = gm_l2;
                    gm_ttsm01t0d8srg t15 = gm_f_get_ece4opn(t13, t14);
                    gm_l6 = t15;
                    {
                        {
                            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l6))[0] != 0);
                            if (t16) {
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
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 118, 40);
                    }
gm_lb5:;
                    uint8_t t17 = gm_l5;
                    if (!t17) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t18 = gm_l1;
                    gm_b16_8 t19;
                    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_soa9mnl4ahjud));
                    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(7));
                    gm_f_strip_prefix_b4949s8((void *)&gm_l7, t18, t19);
                    uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
                    if (t20) {
                        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                        gm_l8 = t21;
                    } else {
                        gm_b16_8 t22 = gm_l1;
                        gm_l8 = t22;
                    }
                    gm_b16_8 t23 = gm_l8;
                    gm_l3 = t23;
                    gm_b16_8 t24 = gm_l3;
                    gm_b16_8 t25;
                    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sd1uj6gf5v8qh));
                    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(7));
                    uint8_t t26 = gm_f_contains_gkki4kq(t24, t25);
                    uint8_t t27 = (uint8_t)!t26;
                    if (t27) {
                        gm_b16_8 t28 = gm_l3;
                        gm_tnpifv20irnph t29;
                        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
                        gm_f_format_rse2efa((void *)&gm_l9, (void *)NULL, cx, NULL, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srccfhjr4r9mdef, 120, 53);
                        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                        gm_l3 = t32;
                    }
                    void * t33 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
                    gm_b16_8 t34 = gm_l3;
                    gm_tftb1odc739t5 t35 = gm_f_extend_0cs85gs(t33, t34);
                    gm_l10 = t35;
                    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srccfhjr4r9mdef, 121, 13);
                    void * t37 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
                    gm_tftb1odc739t5 t38 = gm_f_push_t83rg2m(t37, ((uint8_t)10ULL));
                    gm_l11 = t38;
                    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srccfhjr4r9mdef, 122, 13);
                    void * t40 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l11);
                    gm_b16_8 t41 = gm_l1;
                    gm_f_external_f65nm8q((void *)&gm_l12, (void *)NULL, cx, NULL, t41);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
                    if (t42) {
                        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                        gm_l13 = t43;
                    } else {
                        gm_b16_8 t44 = gm_l1;
                        gm_l13 = t44;
                    }
                    gm_b16_8 t45 = gm_l13;
                    gm_tnpifv20irnph t46;
                    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
                    gm_tftb1odc739t5 t47 = gm_f_write_d8c7tpm(t40, t46);
                    gm_l14 = t47;
                    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srccfhjr4r9mdef, 123, 13);
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            void * t49 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_b16_8 t50 = gm_l1;
            gm_f_external_f65nm8q((void *)&gm_l15, (void *)NULL, cx, NULL, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t51 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
            if (t51) {
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                gm_l16 = t52;
            } else {
                gm_b16_8 t53 = gm_l1;
                gm_l16 = t53;
            }
            gm_b16_8 t54 = gm_l16;
            gm_tftb1odc739t5 t55 = gm_f_extend_0cs85gs(t49, t54);
            gm_l17 = t55;
            uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srccfhjr4r9mdef, 126, 9);
            void * t57 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_tftb1odc739t5 t58 = gm_f_push_t83rg2m(t57, ((uint8_t)10ULL));
            gm_l18 = t58;
            uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srccfhjr4r9mdef, 127, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 117, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_keqa9hq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_keqa9hq F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            gm_b16_8 t5 = gm_l1;
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_seccpktgmrevt));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
            uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
            uint8_t t8 = (uint8_t)!t7;
            if (t8) {
                uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_code_bytes_u2ir6qv *)((&F.h)->up))->l1));
                uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 208, 45);
                uint64_t t13 = r11;
                uint64_t r14; bool o15 = __builtin_add_overflow(t9, t13, &r14);
                if (o15) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 208, 40);
                uint64_t t16 = r14;
                GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_code_bytes_u2ir6qv *)((&F.h)->up))->l1), t16);
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 207, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n9765ef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_n9765ef F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* def */
    gm_t6j6smj7qoctu gm_l3; /* eq */
    gm_b16_8 gm_l4; /* head */
    gm_b16_8 gm_l5; /* d */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 1321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_b16_8 gm_l12;
    gm_b16_8 gm_l13;
    gm_ttu09lij36f8c gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    gm_b16_8 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l1 = t6;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
            if (t8) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t9 = gm_l1;
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_seccpktgmrevt));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
            uint8_t t11 = gm_f_starts_with_6vvt01e(t9, t10);
            if (t11) {
                gm_l8 = ((uint8_t)1);
            } else {
                gm_b16_8 t12 = gm_l1;
                gm_b16_8 t13;
                GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sasea0sobfkrs));
                GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
                uint8_t t14 = gm_f_starts_with_6vvt01e(t12, t13);
                gm_l8 = t14;
            }
            uint8_t t15 = gm_l8;
            if (t15) {
                gm_l7 = ((uint8_t)1);
            } else {
                gm_b16_8 t16 = gm_l1;
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_shv3ttlq9l359));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(14));
                uint8_t t18 = gm_f_starts_with_6vvt01e(t16, t17);
                gm_l7 = t18;
            }
            uint8_t t19 = gm_l7;
            if (t19) {
                void * t20 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
                gm_b16_8 t21 = gm_l1;
                gm_tnpifv20irnph t22;
                GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
                gm_f_format_uaq30dv((void *)&gm_l9, (void *)NULL, cx, NULL, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srccfhjr4r9mdef, 575, 35);
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                gm_b16_8 t26 = gm_l1;
                gm_b16_8 t27 = gm_f_declared_name_hovvm2b(t26);
                gm_b16_8 t28 = gm_l1;
                gm_b16_8 t29 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)gm_c4crumb5mc9));
                gm_t84tkl8vf34ko t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 50, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t25);
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 16), t27);
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 32), t29);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 48), t30);
                GM_ST(uint32_t, ((uint8_t *)&t31 + 52), ((uint32_t)0ULL));
                gm_tftb1odc739t5 t32 = gm_f_push_rda4o9n(t20, &t31);
                gm_l10 = t32;
                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srccfhjr4r9mdef, 575, 13);
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t34 = gm_l1;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(7));
            gm_f_strip_prefix_b4949s8((void *)&gm_l11, t34, t35);
            uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
            if (t36) {
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                gm_l12 = t37;
            } else {
                gm_ttu09lij36f8c t38;
                memset(&t38, 0, sizeof t38);
                uint8_t t39 = 0;
                ((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->ret = t38;
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t39;
                goto gm_lad;
            }
            gm_b16_8 t40 = gm_l12;
            gm_l2 = t40;
            {
                {
                    gm_b16_8 t41 = gm_l2;
                    gm_b16_8 t42;
                    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sao4mumvadoct));
                    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(3));
                    gm_f_split_once_mqcu1ce((void *)&gm_l14, t41, t42);
                    uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l14))[32] != 0);
                    if (!t43) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l14), 32);
                    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_l13 = t44;
                    goto gm_lb3;
                }
gm_lb4:;
                gm_b16_8 t45 = gm_l2;
                gm_b16_8 t46;
                GM_ST(void *, (uint8_t *)&t46, (void *)(gm_spod3tmkh25up));
                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(1));
                gm_f_strip_suffix_8155gbi((void *)&gm_l15, t45, t46);
                uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                if (t47) {
                    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                    gm_l16 = t48;
                } else {
                    gm_ttu09lij36f8c t49;
                    memset(&t49, 0, sizeof t49);
                    uint8_t t50 = 0;
                    ((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->ret = t49;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t50;
                    goto gm_lad;
                }
                gm_b16_8 t51 = gm_l16;
                gm_l13 = t51;
            }
gm_lb3:;
            gm_b16_8 t52 = gm_l13;
            gm_l4 = t52;
            gm_b16_8 t53 = gm_l4;
            gm_tnpifv20irnph t54;
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
            gm_f_format_8l8mb6d((void *)&gm_l17, (void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srccfhjr4r9mdef, 580, 14);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
            gm_l5 = t56;
            gm_b16_8 t57 = gm_l5;
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            gm_b16_8 t59 = gm_f_declared_name_hovvm2b(t58);
            gm_l6 = t59;
            void * t60 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
            gm_b16_8 t61 = gm_l5;
            gm_tnpifv20irnph t62;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
            gm_f_format_1u4jnnu((void *)&gm_l18, (void *)NULL, cx, NULL, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srccfhjr4r9mdef, 582, 31);
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_b16_8 t66 = gm_l6;
            gm_b16_8 t67 = gm_l5;
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
            gm_b16_8 t69 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)gm_c4crumb5mc9));
            gm_t84tkl8vf34ko t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 50, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t65);
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t66);
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 32), t69);
            GM_ST(uint8_t, ((uint8_t *)&t71 + 48), t70);
            GM_ST(uint32_t, ((uint8_t *)&t71 + 52), ((uint32_t)0ULL));
            gm_tftb1odc739t5 t72 = gm_f_push_rda4o9n(t60, &t71);
            gm_l19 = t72;
            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srccfhjr4r9mdef, 582, 9);
            void * t74 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l19));
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l19) + 8));
            gm_b16_8 t76 = gm_l6;
            uint64_t t77 = gm_f_hash64_apkb7bo(t76);
            if (((uint64_t)16ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 583, 23);
            uint64_t t78 = (uint64_t)(t77 % ((uint64_t)16ULL));
            uint64_t t79 = (uint64_t)t78;
            if ((uintptr_t)t79 >= (uintptr_t)(t75)) gm_trap_index((uintptr_t)t79, t75, gm_srccfhjr4r9mdef, 583, 9);
            void * t80 = (void *)(((uint8_t *)t74) + (uintptr_t)t79 * 40);
            gm_b16_8 t81 = gm_l2;
            gm_tftb1odc739t5 t82 = gm_f_push_3ef746b(t80, t81);
            gm_l20 = t82;
            uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srccfhjr4r9mdef, 583, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 572, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui0bpsf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_ui0bpsf F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* def */
    gm_t6j6smj7qoctu gm_l3; /* eq */
    gm_b16_8 gm_l4; /* head */
    GM_STACK_CHECK(&F, 768u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    uint8_t gm_l6;
    uint8_t gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_b16_8 gm_l11;
    gm_b16_8 gm_l12;
    gm_ttu09lij36f8c gm_l13;
    gm_th2lqbo77pvsc gm_l14;
    gm_b16_8 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l1 = t6;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
            if (t8) {
                gm_l7 = ((uint8_t)1);
            } else {
                gm_b16_8 t9 = gm_l1;
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_seccpktgmrevt));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
                uint8_t t11 = gm_f_starts_with_6vvt01e(t9, t10);
                gm_l7 = t11;
            }
            uint8_t t12 = gm_l7;
            if (t12) {
                gm_l6 = ((uint8_t)1);
            } else {
                gm_b16_8 t13 = gm_l1;
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sasea0sobfkrs));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
                uint8_t t15 = gm_f_starts_with_6vvt01e(t13, t14);
                gm_l6 = t15;
            }
            uint8_t t16 = gm_l6;
            if (t16) {
                gm_l5 = ((uint8_t)1);
            } else {
                gm_b16_8 t17 = gm_l1;
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(gm_shv3ttlq9l359));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
                uint8_t t19 = gm_f_starts_with_6vvt01e(t17, t18);
                gm_l5 = t19;
            }
            uint8_t t20 = gm_l5;
            if (t20) {
                void * t21 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
                gm_b16_8 t22 = gm_l1;
                gm_tftb1odc739t5 t23 = gm_f_extend_0cs85gs(t21, t22);
                gm_l8 = t23;
                uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srccfhjr4r9mdef, 131, 13);
                void * t25 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
                gm_tftb1odc739t5 t26 = gm_f_push_t83rg2m(t25, ((uint8_t)10ULL));
                gm_l9 = t26;
                uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srccfhjr4r9mdef, 132, 13);
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t28 = gm_l1;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(7));
            gm_f_strip_prefix_b4949s8((void *)&gm_l10, t28, t29);
            uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
            if (t30) {
                gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                gm_l11 = t31;
            } else {
                gm_ttu09lij36f8c t32;
                memset(&t32, 0, sizeof t32);
                uint8_t t33 = 0;
                ((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->ret = t32;
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t33;
                goto gm_lad;
            }
            gm_b16_8 t34 = gm_l11;
            gm_l2 = t34;
            {
                {
                    gm_b16_8 t35 = gm_l2;
                    gm_b16_8 t36;
                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sao4mumvadoct));
                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(3));
                    gm_f_split_once_mqcu1ce((void *)&gm_l13, t35, t36);
                    uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l13))[32] != 0);
                    if (!t37) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l13), 32);
                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_l12 = t38;
                    goto gm_lb3;
                }
gm_lb4:;
                gm_b16_8 t39 = gm_l2;
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_spod3tmkh25up));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(1));
                gm_f_strip_suffix_8155gbi((void *)&gm_l14, t39, t40);
                uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                if (t41) {
                    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                    gm_l15 = t42;
                } else {
                    gm_ttu09lij36f8c t43;
                    memset(&t43, 0, sizeof t43);
                    uint8_t t44 = 0;
                    ((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->ret = t43;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t44;
                    goto gm_lad;
                }
                gm_b16_8 t45 = gm_l15;
                gm_l12 = t45;
            }
gm_lb3:;
            gm_b16_8 t46 = gm_l12;
            gm_l4 = t46;
            void * t47 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_b16_8 t48 = gm_l4;
            gm_tnpifv20irnph t49;
            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
            gm_tftb1odc739t5 t50 = gm_f_write_815rgbg(t47, t49);
            gm_l16 = t50;
            uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srccfhjr4r9mdef, 137, 9);
            void * t52 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l10);
            gm_b16_8 t53 = gm_l2;
            gm_tftb1odc739t5 t54 = gm_f_extend_0cs85gs(t52, t53);
            gm_l17 = t54;
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srccfhjr4r9mdef, 138, 9);
            void * t56 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l10);
            gm_tftb1odc739t5 t57 = gm_f_push_t83rg2m(t56, ((uint8_t)10ULL));
            gm_l18 = t57;
            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srccfhjr4r9mdef, 139, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 129, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vn00ddp F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l1 = t6;
            {
                {
                    gm_b16_8 t7 = gm_l1;
                    gm_b16_8 t8 = gm_f_trim_fdrfe6i(t7);
                    gm_l2 = t8;
                    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint8_t t10 = (t9 > ((uint64_t)0ULL));
                    if (!t10) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t11 = gm_l1;
                    gm_b16_8 t12;
                    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_seccpktgmrevt));
                    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
                    uint8_t t13 = gm_f_starts_with_6vvt01e(t11, t12);
                    uint8_t t14 = (uint8_t)!t13;
                    if (!t14) {
                        goto gm_lb4;
                    }
                    gm_th2lqbo77pvsc t15;
                    memset(&t15, 0, sizeof t15);
                    uint8_t t16 = 0;
                    ((struct gm_fr_items_of_q103oiv *)((&F.h)->up))->ret = t15;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t16;
                    goto gm_lad;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 351, 5);
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
__attribute__((aligned(4))) void gm_f_function_o2lv8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_function_o2lv8g6 F;
    gm_b16_8 gm_l0; /* item */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* pre */
    gm_b16_8 gm_l4; /* text */
    uint64_t gm_l5; /* first_end */
    gm_b16_8 gm_l6; /* first */
    gm_b16_8 gm_l7; /* def */
    gm_b16_8 gm_l8; /* ext */
    gm_b16_8 gm_l9; /* body */
    int32_t gm_l10; /* depth */
    gm_tf5srh4mp4b3i gm_l11; /* open */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint8_t gm_l15; /* c */
    uint64_t gm_l16; /* at */
    gm_b16_8 gm_l17; /* header */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint8_t gm_l20; /* c */
    GM_STACK_CHECK(&F, 2132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l21;
    uint64_t gm_l22;
    gm_tf5srh4mp4b3i gm_l23;
    uint64_t gm_l24;
    gm_th2lqbo77pvsc gm_l25;
    gm_tabb710d9mq2e gm_l26;
    uint8_t gm_l27;
    gm_tf5srh4mp4b3i gm_l28;
    uint64_t gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l1;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            void * t22 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t24 = gm_l1;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1));
            uint8_t t26 = ((t25 == ((uint8_t)35ULL)));
            if (!t26) {
                goto gm_lb1;
            }
            {
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t29 = gm_l1;
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t31 = (uintptr_t)t29;
                uint64_t t32 = (uintptr_t)t30;
                if (t31 > t32 || t32 > (uintptr_t)t28) gm_trap_slice(t31, t32, t28, gm_srccfhjr4r9mdef, 252, 25);
                uint64_t t33 = ((uintptr_t)t30) - (uintptr_t)t29;
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t27 + (uintptr_t)t29 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33));
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t36 = gm_f_find_nab29co(t34, t35);
                gm_l21 = t36;
                uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l21))[8] != 0);
                if (t37) {
                    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                    gm_l22 = t38;
                } else {
                    goto gm_lb1;
                }
                uint64_t t39 = gm_l22;
                gm_l2 = t39;
                uint64_t t40 = gm_l1;
                uint64_t t41 = gm_l2;
                uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                if (o43) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 253, 18);
                uint64_t t44 = r42;
                uint64_t r45; bool o46 = __builtin_add_overflow(t40, t44, &r45);
                if (o46) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 253, 9);
                uint64_t t47 = r45;
                gm_l1 = t47;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t50 = gm_l1;
    uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t49) gm_trap_slice(t51, t52, t49, gm_srccfhjr4r9mdef, 255, 12);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t48 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l3 = t54;
    void * t55 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t57 = gm_l1;
    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t59 = (uintptr_t)t57;
    uint64_t t60 = (uintptr_t)t58;
    if (t59 > t60 || t60 > (uintptr_t)t56) gm_trap_slice(t59, t60, t56, gm_srccfhjr4r9mdef, 256, 13);
    uint64_t t61 = ((uintptr_t)t58) - (uintptr_t)t57;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t55 + (uintptr_t)t57 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(t61));
    gm_l4 = t62;
    gm_b16_8 t63 = gm_l4;
    gm_b16_8 t64;
    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_so514oj5nbhcu));
    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(1));
    gm_tf5srh4mp4b3i t65 = gm_f_find_nab29co(t63, t64);
    gm_l23 = t65;
    uint8_t t66 = (uint8_t)((((uint8_t *)&gm_l23))[8] != 0);
    if (t66) {
        uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&gm_l23));
        gm_l24 = t67;
    } else {
        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        gm_l24 = t68;
    }
    uint64_t t69 = gm_l24;
    gm_l5 = t69;
    void * t70 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint64_t t72 = gm_l5;
    uint64_t t73 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t74 = (uintptr_t)t72;
    if (t73 > t74 || t74 > (uintptr_t)t71) gm_trap_slice(t73, t74, t71, gm_srccfhjr4r9mdef, 258, 14);
    uint64_t t75 = ((uintptr_t)t72) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t76;
    GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t70 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(t75));
    gm_l6 = t76;
    gm_b16_8 t77 = gm_l0;
    gm_l7 = t77;
    {
        {
            gm_b16_8 t78 = gm_l6;
            gm_f_external_f65nm8q((void *)&gm_l25, (void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = (uint8_t)((((uint8_t *)&gm_l25))[16] != 0);
            if (!t79) {
                goto gm_lb5;
            }
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
            gm_l8 = t80;
            gm_b16_8 t81 = gm_l3;
            gm_b16_8 t82 = gm_l8;
            void * t83 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t t85 = gm_l5;
            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t t87 = (uintptr_t)t85;
            uint64_t t88 = (uintptr_t)t86;
            if (t87 > t88 || t88 > (uintptr_t)t84) gm_trap_slice(t87, t88, t84, gm_srccfhjr4r9mdef, 261, 46);
            uint64_t t89 = ((uintptr_t)t86) - (uintptr_t)t85;
            gm_b16_8 t90;
            GM_ST(void *, (uint8_t *)&t90, (void *)(((uint8_t *)t83 + (uintptr_t)t85 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(t89));
            gm_tfeab279c16rr t91;
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t81);
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 16), t82);
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 32), t90);
            gm_f_format_dsta2ql((void *)&gm_l26, (void *)NULL, cx, NULL, &t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
            if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srccfhjr4r9mdef, 261, 15);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_l7 = t94;
            goto gm_lb4;
        }
gm_lb5:;
        gm_b16_8 t95 = gm_l6;
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(7));
        uint8_t t97 = gm_f_starts_with_6vvt01e(t95, t96);
        if (t97) {
            gm_l27 = ((uint8_t)1);
        } else {
            gm_b16_8 t98 = gm_l6;
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_s1oiqt105ulvb));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(19));
            uint8_t t100 = gm_f_starts_with_6vvt01e(t98, t99);
            gm_l27 = t100;
        }
        uint8_t t101 = gm_l27;
        if (t101) {
            gm_ttu09lij36f8c t102;
            memset(&t102, 0, sizeof t102);
            memmove(gm_out, ((uint8_t *)&t102), 40);
            goto gm_fdone;
        }
    }
gm_lb4:;
    void * t103 = GM_LD(void *, ((uint8_t *)&gm_l7));
    uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
    uint64_t t107 = (uintptr_t)t105;
    uint64_t t108 = (uintptr_t)t106;
    if (t107 > t108 || t108 > (uintptr_t)t104) gm_trap_slice(t107, t108, t104, gm_srccfhjr4r9mdef, 266, 13);
    uint64_t t109 = ((uintptr_t)t106) - (uintptr_t)t105;
    gm_b16_8 t110;
    GM_ST(void *, (uint8_t *)&t110, (void *)(((uint8_t *)t103 + (uintptr_t)t105 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(t109));
    gm_l9 = t110;
    gm_l10 = ((int32_t)0LL);
    gm_tf5srh4mp4b3i t111;
    memset(&t111, 0, sizeof t111);
    gm_l11 = t111;
    gm_b16_8 t112 = gm_l9;
    gm_l12 = t112;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t113 = gm_l13;
            uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t115 = (t113 < t114);
            if (!t115) {
                goto gm_lb6;
            }
            {
                uint64_t t116 = gm_l13;
                void * t117 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t119 = gm_l13;
                uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t117) + (uintptr_t)t119 * 1));
                gm_tf5srh4mp4b3i t121;
                GM_ST(uint8_t, (uint8_t *)&t121 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t121 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t121 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t121 + 0), t116);
                GM_ST(uint8_t, ((uint8_t *)&t121 + 8), t120);
                gm_l28 = t121;
                {
                    {
                        uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                        gm_l14 = t122;
                        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 8));
                        gm_l15 = t123;
                        uint8_t t124 = gm_l15;
                        uint8_t t125 = ((t124 == ((uint8_t)40ULL)));
                        if (t125) {
                            int32_t t126 = gm_l10;
                            int32_t r127; bool o128 = __builtin_add_overflow(t126, ((int32_t)1LL), &r127);
                            if (o128) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 271, 13);
                            int32_t t129 = r127;
                            gm_l10 = t129;
                        } else {
                            uint8_t t130 = gm_l15;
                            uint8_t t131 = ((t130 == ((uint8_t)41ULL)));
                            if (t131) {
                                int32_t t132 = gm_l10;
                                int32_t r133; bool o134 = __builtin_sub_overflow(t132, ((int32_t)1LL), &r133);
                                if (o134) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 273, 13);
                                int32_t t135 = r133;
                                gm_l10 = t135;
                            } else {
                                {
                                    {
                                        uint8_t t136 = gm_l15;
                                        uint8_t t137 = ((t136 == ((uint8_t)123ULL)));
                                        if (!t137) {
                                            goto gm_lb12;
                                        }
                                        int32_t t138 = gm_l10;
                                        uint8_t t139 = ((t138 == ((int32_t)0LL)));
                                        if (!t139) {
                                            goto gm_lb12;
                                        }
                                        uint64_t t140 = gm_l14;
                                        gm_tf5srh4mp4b3i t141;
                                        GM_ST(uint8_t, (uint8_t *)&t141 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t141 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t141 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t141), t140);
                                        ((uint8_t *)&t141)[8] = 1;
                                        gm_l11 = t141;
                                        goto gm_lb6;
                                    }
gm_lb12:;
                                }
gm_lb11:;
                            }
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 269, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t142 = gm_l13;
            uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
            uint64_t t145 = r143;
            gm_l13 = t145;
        }
    }
gm_lb6:;
    uint8_t t146 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
    if (t146) {
        uint64_t t147 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
        gm_l29 = t147;
    } else {
        gm_ttu09lij36f8c t148;
        memset(&t148, 0, sizeof t148);
        memmove(gm_out, ((uint8_t *)&t148), 40);
        goto gm_fdone;
    }
    uint64_t t149 = gm_l29;
    gm_l16 = t149;
    void * t150 = GM_LD(void *, ((uint8_t *)&gm_l9));
    uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    uint64_t t152 = gm_l16;
    uint64_t t153 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t154 = (uintptr_t)t152;
    if (t153 > t154 || t154 > (uintptr_t)t151) gm_trap_slice(t153, t154, t151, gm_srccfhjr4r9mdef, 280, 28);
    uint64_t t155 = ((uintptr_t)t152) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t156;
    GM_ST(void *, (uint8_t *)&t156, (void *)(((uint8_t *)t150 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(t155));
    gm_b16_8 t157 = gm_f_trim_end_6sjv6rg(t156);
    gm_l17 = t157;
    gm_b16_8 t158 = gm_l17;
    gm_l18 = t158;
    gm_l19 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t159 = gm_l19;
            uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint8_t t161 = (t159 < t160);
            if (!t161) {
                goto gm_lb13;
            }
            {
                void * t162 = GM_LD(void *, ((uint8_t *)&gm_l18));
                uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint64_t t164 = gm_l19;
                {
                    {
                        uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)t162) + (uintptr_t)t164 * 1));
                        gm_l20 = t165;
                        uint8_t t166 = gm_l20;
                        gm_toh0ootodhjue t167;
                        GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)10ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t167 + 1), ((uint8_t)34ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t167 + 2), ((uint8_t)39ULL));
                        uint8_t t168 = 0;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t167) + 0, ((uint8_t *)&t166))) t168 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t167) + 1, ((uint8_t *)&t166))) t168 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t167) + 2, ((uint8_t *)&t166))) t168 = 1;
                        uint8_t t169 = (uint8_t)(t168);
                        if (t169) {
                            gm_ttu09lij36f8c t170;
                            memset(&t170, 0, sizeof t170);
                            memmove(gm_out, ((uint8_t *)&t170), 40);
                            goto gm_fdone;
                        }
                        goto gm_lb16;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 281, 5);
                }
gm_lb16:;
            }
gm_lb15:;
            uint64_t t171 = gm_l19;
            uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
            uint64_t t174 = r172;
            gm_l19 = t174;
        }
    }
gm_lb13:;
    gm_b16_8 t175 = gm_l17;
    gm_b16_8 t176;
    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_ssi4rrma2q00g));
    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(6));
    uint8_t t177 = gm_f_has_word_l11ltcj(t175, t176);
    if (t177) {
        gm_ttu09lij36f8c t178;
        memset(&t178, 0, sizeof t178);
        memmove(gm_out, ((uint8_t *)&t178), 40);
        goto gm_fdone;
    }
    gm_b16_8 t179 = gm_l6;
    gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)gm_csu9mkbsfpo));
    uint8_t t181 = gm_f_starts_with_6vvt01e(t179, t180);
    if (t181) {
        gm_b16_8 t182 = gm_l3;
        void * t183 = GM_LD(void *, ((uint8_t *)&gm_l7));
        uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        uint64_t t187 = (uintptr_t)t185;
        uint64_t t188 = (uintptr_t)t186;
        if (t187 > t188 || t188 > (uintptr_t)t184) gm_trap_slice(t187, t188, t184, gm_srccfhjr4r9mdef, 289, 46);
        uint64_t t189 = ((uintptr_t)t186) - (uintptr_t)t185;
        gm_b16_8 t190;
        GM_ST(void *, (uint8_t *)&t190, (void *)(((uint8_t *)t183 + (uintptr_t)t185 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(t189));
        gm_t6j6smj7qoctu t191;
        GM_ST(gm_b16_8, ((uint8_t *)&t191 + 0), t182);
        GM_ST(gm_b16_8, ((uint8_t *)&t191 + 16), t190);
        gm_f_format_stmnn5h((void *)&gm_l30, (void *)NULL, cx, NULL, &t191);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t192 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
        if (!t192) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srccfhjr4r9mdef, 289, 15);
        gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&t193));
        gm_l7 = t194;
    }
    gm_b16_8 t195 = gm_l17;
    gm_tnpifv20irnph t196;
    GM_ST(gm_b16_8, ((uint8_t *)&t196 + 0), t195);
    gm_f_format_dg0m8l8((void *)&gm_l31, (void *)NULL, cx, NULL, t196);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
    if (!t197) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srccfhjr4r9mdef, 291, 19);
    gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
    gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
    gm_b16_8 t200 = gm_l7;
    gm_t6j6smj7qoctu t201;
    GM_ST(gm_b16_8, ((uint8_t *)&t201 + 0), t199);
    GM_ST(gm_b16_8, ((uint8_t *)&t201 + 16), t200);
    gm_ttu09lij36f8c t202;
    GM_ST(uint8_t, (uint8_t *)&t202 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t202 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t202 + 36, 0);
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t202), t201);
    ((uint8_t *)&t202)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t202), 40);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_gm_names_6ner0lu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_gm_names_6ner0lu F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* c */
    uint64_t gm_l5; /* j */
    gm_b16_8 gm_l6; /* id */
    GM_STACK_CHECK(&F, 597u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_ttsm01t0d8srg gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    {
        gm_trqfvmjtt4sij t15;
        gm_f_list_epvducg((void *)&t15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t15;
        gm_t4c7ipjj9s3fp t16;
        gm_f_hash_map_m04clhi((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t16;
        gm_l3 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t17 = gm_l3;
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint8_t t19 = (t17 < t18);
                if (!t19) {
                    goto gm_lb2;
                }
                {
                    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t22 = gm_l3;
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t20) + (uintptr_t)t22 * 1));
                    gm_l4 = t23;
                    uint8_t t24 = gm_l4;
                    uint8_t t25 = ((t24 == ((uint8_t)34ULL)));
                    if (t25) {
                        gm_l7 = ((uint8_t)1);
                    } else {
                        uint8_t t26 = gm_l4;
                        uint8_t t27 = ((t26 == ((uint8_t)39ULL)));
                        gm_l7 = t27;
                    }
                    uint8_t t28 = gm_l7;
                    if (t28) {
                        gm_b16_8 t29 = gm_l0;
                        uint64_t t30 = gm_l3;
                        uint64_t t31 = gm_f_skip_literal_17lq2p8(t29, t30);
                        gm_l3 = t31;
                        goto gm_lb4;
                    }
                    uint8_t t32 = gm_l4;
                    uint8_t t33 = gm_f_ident_char_pmpasvq(t32);
                    if (t33) {
                        uint64_t t34 = gm_l3;
                        gm_l5 = t34;
                        {
                            for (;;) {
                                uint64_t t35 = gm_l5;
                                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t37 = (t35 < t36);
                                if (!t37) {
                                    goto gm_lb5;
                                }
                                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t40 = gm_l5;
                                uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t38) + (uintptr_t)t40 * 1));
                                uint8_t t42 = gm_f_ident_char_pmpasvq(t41);
                                if (!t42) {
                                    goto gm_lb5;
                                }
                                {
                                    uint64_t t43 = gm_l5;
                                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                                    if (o45) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 452, 58);
                                    uint64_t t46 = r44;
                                    gm_l5 = t46;
                                }
gm_lb7:;
                            }
                        }
gm_lb5:;
                        void * t47 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t49 = gm_l3;
                        uint64_t t50 = gm_l5;
                        uint64_t t51 = (uintptr_t)t49;
                        uint64_t t52 = (uintptr_t)t50;
                        if (t51 > t52 || t52 > (uintptr_t)t48) gm_trap_slice(t51, t52, t48, gm_srccfhjr4r9mdef, 453, 19);
                        uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t49;
                        gm_b16_8 t54;
                        GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t47 + (uintptr_t)t49 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
                        gm_l6 = t54;
                        {
                            {
                                gm_b16_8 t55 = gm_l6;
                                gm_b16_8 t56;
                                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s7ipvt5onv6ht));
                                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(3));
                                uint8_t t57 = gm_f_starts_with_6vvt01e(t55, t56);
                                if (!t57) {
                                    goto gm_lb9;
                                }
                                uint8_t t58 = gm_l4;
                                uint8_t t59 = (t58 >= ((uint8_t)48ULL));
                                if (t59) {
                                    uint8_t t60 = gm_l4;
                                    uint8_t t61 = (t60 <= ((uint8_t)57ULL));
                                    gm_l8 = t61;
                                } else {
                                    gm_l8 = ((uint8_t)0);
                                }
                                uint8_t t62 = gm_l8;
                                uint8_t t63 = (uint8_t)!t62;
                                if (!t63) {
                                    goto gm_lb9;
                                }
                                void * t64 = (void *)((uint8_t *)&F.l2);
                                gm_b16_8 t65 = gm_l6;
                                gm_ttsm01t0d8srg t66 = gm_f_get_ece4opn(t64, t65);
                                gm_l10 = t66;
                                {
                                    {
                                        uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l10))[0] != 0);
                                        if (t67) {
                                            goto gm_lb11;
                                        }
                                        gm_l9 = ((uint8_t)1);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    {
                                        gm_l9 = ((uint8_t)0);
                                        goto gm_lb10;
                                    }
gm_lb12:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 454, 74);
                                }
gm_lb10:;
                                uint8_t t68 = gm_l9;
                                if (!t68) {
                                    goto gm_lb9;
                                }
                                void * t69 = (void *)((uint8_t *)&F.l2);
                                gm_b16_8 t70 = gm_l6;
                                gm_tftb1odc739t5 t71 = gm_f_put_drsjd1t(t69, t70, GM_ZERO(gm_b0_1));
                                gm_l11 = t71;
                                uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srccfhjr4r9mdef, 455, 17);
                                void * t73 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t74 = gm_l6;
                                gm_tftb1odc739t5 t75 = gm_f_push_3ef746b(t73, t74);
                                gm_l12 = t75;
                                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srccfhjr4r9mdef, 456, 17);
                                goto gm_lb8;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        uint64_t t77 = gm_l5;
                        gm_l3 = t77;
                        goto gm_lb4;
                    }
                    uint64_t t78 = gm_l3;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    if (o80) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 461, 9);
                    uint64_t t81 = r79;
                    gm_l3 = t81;
                }
gm_lb4:;
            }
        }
gm_lb2:;
        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
        F.ret = t83;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld14_1;
    }
    {
        void * t84 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_lehld1n(t84);
    }
    goto gm_ls85;
gm_ld14_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t86 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_lehld1n(t86);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls85:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_word_l11ltcj(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_word_l11ltcj F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* from */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* at */
    uint64_t gm_l5; /* end */
    GM_STACK_CHECK(&F, 384u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l6;
    uint64_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t9 = (t7 < t8);
            if (!t9) {
                goto gm_lb1;
            }
            {
                void * t10 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t12 = gm_l2;
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t14 = (uintptr_t)t12;
                uint64_t t15 = (uintptr_t)t13;
                if (t14 > t15 || t15 > (uintptr_t)t11) gm_trap_slice(t14, t15, t11, gm_srccfhjr4r9mdef, 298, 23);
                uint64_t t16 = ((uintptr_t)t13) - (uintptr_t)t12;
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t10 + (uintptr_t)t12 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
                gm_b16_8 t18 = gm_l1;
                gm_tf5srh4mp4b3i t19 = gm_f_find_nab29co(t17, t18);
                gm_l6 = t19;
                uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
                if (t20) {
                    uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                    gm_l7 = t21;
                } else {
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                uint64_t t22 = gm_l7;
                gm_l3 = t22;
                uint64_t t23 = gm_l2;
                uint64_t t24 = gm_l3;
                uint64_t r25; bool o26 = __builtin_add_overflow(t23, t24, &r25);
                if (o26) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 299, 15);
                uint64_t t27 = r25;
                gm_l4 = t27;
                uint64_t t28 = gm_l4;
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 300, 16);
                uint64_t t32 = r30;
                gm_l5 = t32;
                {
                    {
                        uint64_t t33 = gm_l4;
                        uint8_t t34 = ((t33 == ((uint64_t)0ULL)));
                        if (t34) {
                            gm_l8 = ((uint8_t)1);
                        } else {
                            void * t35 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t37 = gm_l4;
                            uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
                            if (o39) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 301, 54);
                            uint64_t t40 = r38;
                            if ((uintptr_t)t40 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t40, t36, gm_srccfhjr4r9mdef, 301, 49);
                            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t35) + (uintptr_t)t40 * 1));
                            uint8_t t42 = gm_f_is_ident_cont_317fpdb(t41);
                            uint8_t t43 = (uint8_t)!t42;
                            gm_l8 = t43;
                        }
                        uint8_t t44 = gm_l8;
                        if (!t44) {
                            goto gm_lb5;
                        }
                        uint64_t t45 = gm_l5;
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint8_t t47 = (t45 >= t46);
                        if (t47) {
                            gm_l9 = ((uint8_t)1);
                        } else {
                            void * t48 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t50 = gm_l5;
                            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t48) + (uintptr_t)t50 * 1));
                            uint8_t t52 = gm_f_is_ident_cont_317fpdb(t51);
                            uint8_t t53 = (uint8_t)!t52;
                            gm_l9 = t53;
                        }
                        uint8_t t54 = gm_l9;
                        if (!t54) {
                            goto gm_lb5;
                        }
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t55 = gm_l4;
                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                if (o57) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 302, 16);
                uint64_t t58 = r56;
                gm_l2 = t58;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_ident_set_9ajalpn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_ident_set_9ajalpn F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* j */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l4;
    gm_t4c7ipjj9s3fp t8;
    gm_f_hash_map_m04clhi((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t8;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l2;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t14 = gm_l2;
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t12) + (uintptr_t)t14 * 1));
                uint8_t t16 = gm_f_ident_char_pmpasvq(t15);
                if (t16) {
                    uint64_t t17 = gm_l2;
                    gm_l3 = t17;
                    {
                        for (;;) {
                            uint64_t t18 = gm_l3;
                            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint8_t t20 = (t18 < t19);
                            if (!t20) {
                                goto gm_lb4;
                            }
                            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t23 = gm_l3;
                            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 1));
                            uint8_t t25 = gm_f_ident_char_pmpasvq(t24);
                            if (!t25) {
                                goto gm_lb4;
                            }
                            {
                                uint64_t t26 = gm_l3;
                                uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                                if (o28) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 727, 58);
                                uint64_t t29 = r27;
                                gm_l3 = t29;
                            }
gm_lb6:;
                        }
                    }
gm_lb4:;
                    void * t30 = (void *)((uint8_t *)&F.l1);
                    void * t31 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t33 = gm_l2;
                    uint64_t t34 = gm_l3;
                    uint64_t t35 = (uintptr_t)t33;
                    uint64_t t36 = (uintptr_t)t34;
                    if (t35 > t36 || t36 > (uintptr_t)t32) gm_trap_slice(t35, t36, t32, gm_srccfhjr4r9mdef, 728, 21);
                    uint64_t t37 = ((uintptr_t)t34) - (uintptr_t)t33;
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t31 + (uintptr_t)t33 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
                    gm_tftb1odc739t5 t39 = gm_f_put_drsjd1t(t30, t38, GM_ZERO(gm_b0_1));
                    gm_l4 = t39;
                    uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                    if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srccfhjr4r9mdef, 728, 13);
                    uint64_t t41 = gm_l3;
                    gm_l2 = t41;
                } else {
                    uint64_t t42 = gm_l2;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    if (o44) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 731, 13);
                    uint64_t t45 = r43;
                    gm_l2 = t45;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&F.l1), 56);
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
__attribute__((aligned(4))) void gm_f_inline_definition_6aokb9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_inline_definition_6aokb9d F;
    gm_b16_8 gm_l0; /* item */
    uint64_t gm_l1; /* small_bytes */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* end */
    gm_b16_8 gm_l4; /* text */
    uint64_t gm_l5; /* first_end */
    gm_b16_8 gm_l6; /* first */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 1161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l8;
    uint64_t gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    uint64_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    gm_th2lqbo77pvsc gm_l14;
    gm_b16_8 gm_l15;
    gm_b16_8 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t7 = (t5 < t6);
            if (!t7) {
                goto gm_lb1;
            }
            void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t10 = gm_l2;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t8) + (uintptr_t)t10 * 1));
            uint8_t t12 = ((t11 == ((uint8_t)35ULL)));
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t15 = gm_l2;
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t17 = (uintptr_t)t15;
                uint64_t t18 = (uintptr_t)t16;
                if (t17 > t18 || t18 > (uintptr_t)t14) gm_trap_slice(t17, t18, t14, gm_srccfhjr4r9mdef, 218, 25);
                uint64_t t19 = ((uintptr_t)t16) - (uintptr_t)t15;
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t13 + (uintptr_t)t15 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(t19));
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t22 = gm_f_find_nab29co(t20, t21);
                gm_l8 = t22;
                uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                if (t23) {
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                    gm_l9 = t24;
                } else {
                    goto gm_lb1;
                }
                uint64_t t25 = gm_l9;
                gm_l3 = t25;
                uint64_t t26 = gm_l2;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 219, 18);
                uint64_t t30 = r28;
                uint64_t r31; bool o32 = __builtin_add_overflow(t26, t30, &r31);
                if (o32) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 219, 9);
                uint64_t t33 = r31;
                gm_l2 = t33;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t34 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t36 = gm_l2;
    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t38 = (uintptr_t)t36;
    uint64_t t39 = (uintptr_t)t37;
    if (t38 > t39 || t39 > (uintptr_t)t35) gm_trap_slice(t38, t39, t35, gm_srccfhjr4r9mdef, 221, 13);
    uint64_t t40 = ((uintptr_t)t37) - (uintptr_t)t36;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t34 + (uintptr_t)t36 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40));
    gm_l4 = t41;
    gm_b16_8 t42 = gm_l4;
    gm_b16_8 t43;
    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_so514oj5nbhcu));
    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(1));
    gm_tf5srh4mp4b3i t44 = gm_f_find_nab29co(t42, t43);
    gm_l10 = t44;
    uint8_t t45 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
    if (t45) {
        uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
        gm_l11 = t46;
    } else {
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        gm_l11 = t47;
    }
    uint64_t t48 = gm_l11;
    gm_l5 = t48;
    void * t49 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint64_t t51 = gm_l5;
    uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t53 = (uintptr_t)t51;
    if (t52 > t53 || t53 > (uintptr_t)t50) gm_trap_slice(t52, t53, t50, gm_srccfhjr4r9mdef, 223, 14);
    uint64_t t54 = ((uintptr_t)t51) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t55;
    GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t49 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
    gm_l6 = t55;
    gm_b16_8 t56 = gm_l6;
    uint8_t t57 = gm_f_is_inline_aqdknmj(t56);
    uint8_t t58 = (uint8_t)!t57;
    if (t58) {
        uint64_t t59 = gm_l1;
        uint8_t t60 = ((t59 == ((uint64_t)0ULL)));
        if (t60) {
            gm_l13 = ((uint8_t)1);
        } else {
            gm_b16_8 t61 = gm_l6;
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)gm_csu9mkbsfpo));
            uint8_t t63 = gm_f_starts_with_6vvt01e(t61, t62);
            uint8_t t64 = (uint8_t)!t63;
            gm_l13 = t64;
        }
        uint8_t t65 = gm_l13;
        if (t65) {
            gm_l12 = ((uint8_t)1);
        } else {
            gm_b16_8 t66 = gm_l0;
            uint64_t t67 = gm_f_code_bytes_u2ir6qv(t66);
            uint64_t t68 = gm_l1;
            uint8_t t69 = (t67 > t68);
            gm_l12 = t69;
        }
        uint8_t t70 = gm_l12;
        if (t70) {
            gm_ttu09lij36f8c t71;
            memset(&t71, 0, sizeof t71);
            memmove(gm_out, ((uint8_t *)&t71), 40);
            goto gm_fdone;
        }
    }
    gm_b16_8 t72 = gm_l6;
    gm_f_c_fn_name_77uspqp((void *)&gm_l14, t72);
    uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
    if (t73) {
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l15 = t74;
    } else {
        gm_ttu09lij36f8c t75;
        memset(&t75, 0, sizeof t75);
        memmove(gm_out, ((uint8_t *)&t75), 40);
        goto gm_fdone;
    }
    gm_b16_8 t76 = gm_l15;
    gm_l7 = t76;
    void * t77 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t79 = gm_l2;
    uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t81 = (uintptr_t)t79;
    if (t80 > t81 || t81 > (uintptr_t)t78) gm_trap_slice(t80, t81, t78, gm_srccfhjr4r9mdef, 228, 37);
    uint64_t t82 = ((uintptr_t)t79) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t83;
    GM_ST(void *, (uint8_t *)&t83, (void *)(((uint8_t *)t77 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(t82));
    void * t84 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t86;
    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_soa9mnl4ahjud));
    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(7));
    gm_l16 = t86;
    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint64_t t89 = (uintptr_t)t87;
    uint64_t t90 = (uintptr_t)t88;
    if (t89 > t90 || t90 > (uintptr_t)t85) gm_trap_slice(t89, t90, t85, gm_srccfhjr4r9mdef, 228, 53);
    uint64_t t91 = ((uintptr_t)t88) - (uintptr_t)t87;
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(((uint8_t *)t84 + (uintptr_t)t87 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(t91));
    gm_t6j6smj7qoctu t93;
    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 0), t83);
    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 16), t92);
    gm_f_format_8n626pi((void *)&gm_l17, (void *)NULL, cx, NULL, &t93);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srccfhjr4r9mdef, 228, 18);
    gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
    gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
    gm_b16_8 t97 = gm_l7;
    gm_t6j6smj7qoctu t98;
    GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t96);
    GM_ST(gm_b16_8, ((uint8_t *)&t98 + 16), t97);
    gm_ttu09lij36f8c t99;
    GM_ST(uint8_t, (uint8_t *)&t99 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t99 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t99 + 36, 0);
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t99), t98);
    ((uint8_t *)&t99)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t99), 40);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_items_of_q103oiv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_items_of_q103oiv F;
    gm_b16_8 gm_l0; /* code */
    uint64_t gm_l2; /* start */
    int32_t gm_l3; /* depth */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* line_start */
    uint8_t gm_l6; /* c */
    gm_b16_8 gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 1072u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l8;
    uint64_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    {
        gm_trqfvmjtt4sij t28;
        gm_f_list_epvducg((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t28;
        gm_l2 = ((uint64_t)0ULL);
        gm_l3 = ((int32_t)0LL);
        gm_l4 = ((uint64_t)0ULL);
        gm_l5 = ((uint8_t)1);
        {
            for (;;) {
                uint64_t t30 = gm_l4;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint8_t t32 = (t30 < t31);
                if (!t32) {
                    goto gm_lb1;
                }
                {
                    void * t33 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t35 = gm_l4;
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)t35 * 1));
                    gm_l6 = t36;
                    {
                        {
                            uint8_t t37 = gm_l6;
                            uint8_t t38 = ((t37 == ((uint8_t)35ULL)));
                            if (!t38) {
                                goto gm_lb5;
                            }
                            uint8_t t39 = gm_l5;
                            if (!t39) {
                                goto gm_lb5;
                            }
                            {
                                for (;;) {
                                    uint64_t t41 = gm_l4;
                                    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint8_t t43 = (t41 < t42);
                                    if (!t43) {
                                        goto gm_lb6;
                                    }
                                    void * t44 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t46 = gm_l4;
                                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t44) + (uintptr_t)t46 * 1));
                                    uint8_t t48 = (!(t47 == ((uint8_t)10ULL)));
                                    if (!t48) {
                                        goto gm_lb6;
                                    }
                                    {
                                        uint64_t t49 = gm_l4;
                                        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                                        if (o51) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 318, 58);
                                        uint64_t t52 = r50;
                                        gm_l4 = t52;
                                    }
gm_lb8:;
                                }
                            }
gm_lb6:;
                            goto gm_lb3;
                        }
gm_lb5:;
                    }
gm_lb4:;
                    uint8_t t53 = gm_l6;
                    uint8_t t54 = ((t53 == ((uint8_t)34ULL)));
                    if (t54) {
                        gm_l8 = ((uint8_t)1);
                    } else {
                        uint8_t t55 = gm_l6;
                        uint8_t t56 = ((t55 == ((uint8_t)39ULL)));
                        gm_l8 = t56;
                    }
                    uint8_t t57 = gm_l8;
                    if (t57) {
                        uint64_t t58 = gm_l4;
                        uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                        if (o60) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 322, 13);
                        uint64_t t61 = r59;
                        gm_l4 = t61;
                        {
                            for (;;) {
                                uint64_t t63 = gm_l4;
                                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t65 = (t63 < t64);
                                if (!t65) {
                                    goto gm_lb9;
                                }
                                void * t66 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t68 = gm_l4;
                                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t66) + (uintptr_t)t68 * 1));
                                uint8_t t70 = gm_l6;
                                uint8_t t71 = (!(t69 == t70));
                                if (!t71) {
                                    goto gm_lb9;
                                }
                                {
                                    uint64_t t72 = gm_l4;
                                    void * t73 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t75 = gm_l4;
                                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)t73) + (uintptr_t)t75 * 1));
                                    uint8_t t77 = ((t76 == ((uint8_t)92ULL)));
                                    if (t77) {
                                        gm_l9 = ((uint64_t)2ULL);
                                    } else {
                                        gm_l9 = ((uint64_t)1ULL);
                                    }
                                    uint64_t t78 = gm_l9;
                                    uint64_t r79; bool o80 = __builtin_add_overflow(t72, t78, &r79);
                                    if (o80) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 324, 17);
                                    uint64_t t81 = r79;
                                    gm_l4 = t81;
                                }
gm_lb11:;
                            }
                        }
gm_lb9:;
                    } else {
                        {
                            {
                                uint8_t t82 = gm_l6;
                                uint8_t t83 = ((t82 == ((uint8_t)47ULL)));
                                if (!t83) {
                                    goto gm_lb13;
                                }
                                uint64_t t84 = gm_l4;
                                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                if (o86) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 326, 32);
                                uint64_t t87 = r85;
                                uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t89 = (t87 < t88);
                                if (!t89) {
                                    goto gm_lb13;
                                }
                                void * t90 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t92 = gm_l4;
                                uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                                if (o94) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 326, 58);
                                uint64_t t95 = r93;
                                if ((uintptr_t)t95 >= (uintptr_t)(t91)) gm_trap_index((uintptr_t)t95, t91, gm_srccfhjr4r9mdef, 326, 53);
                                uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t90) + (uintptr_t)t95 * 1));
                                uint8_t t97 = ((t96 == ((uint8_t)42ULL)));
                                if (!t97) {
                                    goto gm_lb13;
                                }
                                uint64_t t98 = gm_l4;
                                uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)2ULL), &r99);
                                if (o100) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 327, 13);
                                uint64_t t101 = r99;
                                gm_l4 = t101;
                                {
                                    for (;;) {
                                        uint64_t t103 = gm_l4;
                                        uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                                        if (o105) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 328, 19);
                                        uint64_t t106 = r104;
                                        uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                        uint8_t t108 = (t106 < t107);
                                        if (!t108) {
                                            goto gm_lb14;
                                        }
                                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                        uint64_t t111 = gm_l4;
                                        if ((uintptr_t)t111 >= (uintptr_t)(t110)) gm_trap_index((uintptr_t)t111, t110, gm_srccfhjr4r9mdef, 328, 45);
                                        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)t109) + (uintptr_t)t111 * 1));
                                        uint8_t t113 = ((t112 == ((uint8_t)42ULL)));
                                        if (t113) {
                                            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                            uint64_t t116 = gm_l4;
                                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                                            if (o118) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 328, 69);
                                            uint64_t t119 = r117;
                                            if ((uintptr_t)t119 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t119, t115, gm_srccfhjr4r9mdef, 328, 64);
                                            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)t119 * 1));
                                            uint8_t t121 = ((t120 == ((uint8_t)47ULL)));
                                            gm_l10 = t121;
                                        } else {
                                            gm_l10 = ((uint8_t)0);
                                        }
                                        uint8_t t122 = gm_l10;
                                        uint8_t t123 = (uint8_t)!t122;
                                        if (!t123) {
                                            goto gm_lb14;
                                        }
                                        {
                                            uint64_t t124 = gm_l4;
                                            uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                                            if (o126) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 328, 86);
                                            uint64_t t127 = r125;
                                            gm_l4 = t127;
                                        }
gm_lb16:;
                                    }
                                }
gm_lb14:;
                                uint64_t t128 = gm_l4;
                                uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                                if (o130) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 329, 13);
                                uint64_t t131 = r129;
                                gm_l4 = t131;
                                goto gm_lb12;
                            }
gm_lb13:;
                            {
                                {
                                    uint8_t t132 = gm_l6;
                                    uint8_t t133 = ((t132 == ((uint8_t)47ULL)));
                                    if (!t133) {
                                        goto gm_lb18;
                                    }
                                    uint64_t t134 = gm_l4;
                                    uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                                    if (o136) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 330, 32);
                                    uint64_t t137 = r135;
                                    uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint8_t t139 = (t137 < t138);
                                    if (!t139) {
                                        goto gm_lb18;
                                    }
                                    void * t140 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t142 = gm_l4;
                                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                                    if (o144) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 330, 58);
                                    uint64_t t145 = r143;
                                    if ((uintptr_t)t145 >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)t145, t141, gm_srccfhjr4r9mdef, 330, 53);
                                    uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)t140) + (uintptr_t)t145 * 1));
                                    uint8_t t147 = ((t146 == ((uint8_t)47ULL)));
                                    if (!t147) {
                                        goto gm_lb18;
                                    }
                                    {
                                        for (;;) {
                                            uint64_t t149 = gm_l4;
                                            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                                            if (o151) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 331, 19);
                                            uint64_t t152 = r150;
                                            uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                            uint8_t t154 = (t152 < t153);
                                            if (!t154) {
                                                goto gm_lb19;
                                            }
                                            void * t155 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                            uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                            uint64_t t157 = gm_l4;
                                            uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                                            if (o159) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 331, 45);
                                            uint64_t t160 = r158;
                                            if ((uintptr_t)t160 >= (uintptr_t)(t156)) gm_trap_index((uintptr_t)t160, t156, gm_srccfhjr4r9mdef, 331, 40);
                                            uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)t155) + (uintptr_t)t160 * 1));
                                            uint8_t t162 = (!(t161 == ((uint8_t)10ULL)));
                                            if (!t162) {
                                                goto gm_lb19;
                                            }
                                            {
                                                uint64_t t163 = gm_l4;
                                                uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                                                if (o165) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 331, 66);
                                                uint64_t t166 = r164;
                                                gm_l4 = t166;
                                            }
gm_lb21:;
                                        }
                                    }
gm_lb19:;
                                    goto gm_lb17;
                                }
gm_lb18:;
                                uint8_t t167 = gm_l6;
                                uint8_t t168 = ((t167 == ((uint8_t)123ULL)));
                                if (t168) {
                                    int32_t t169 = gm_l3;
                                    int32_t r170; bool o171 = __builtin_add_overflow(t169, ((int32_t)1LL), &r170);
                                    if (o171) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 333, 13);
                                    int32_t t172 = r170;
                                    gm_l3 = t172;
                                } else {
                                    uint8_t t173 = gm_l6;
                                    uint8_t t174 = ((t173 == ((uint8_t)125ULL)));
                                    if (t174) {
                                        int32_t t175 = gm_l3;
                                        int32_t r176; bool o177 = __builtin_sub_overflow(t175, ((int32_t)1LL), &r176);
                                        if (o177) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 335, 13);
                                        int32_t t178 = r176;
                                        gm_l3 = t178;
                                        int32_t t179 = gm_l3;
                                        uint8_t t180 = (t179 < ((int32_t)0LL));
                                        if (t180) {
                                            gm_th2lqbo77pvsc t181;
                                            memset(&t181, 0, sizeof t181);
                                            memmove(gm_out, ((uint8_t *)&t181), 24);
                                            goto gm_fdone;
                                        }
                                        {
                                            {
                                                int32_t t182 = gm_l3;
                                                uint8_t t183 = ((t182 == ((int32_t)0LL)));
                                                if (!t183) {
                                                    goto gm_lb23;
                                                }
                                                uint64_t t184 = gm_l4;
                                                uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                                                if (o186) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 337, 32);
                                                uint64_t t187 = r185;
                                                uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint8_t t189 = (t187 >= t188);
                                                if (t189) {
                                                    gm_l11 = ((uint8_t)1);
                                                } else {
                                                    void * t190 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                    uint64_t t191 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                    uint64_t t192 = gm_l4;
                                                    uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                                                    if (o194) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 337, 58);
                                                    uint64_t t195 = r193;
                                                    if ((uintptr_t)t195 >= (uintptr_t)(t191)) gm_trap_index((uintptr_t)t195, t191, gm_srccfhjr4r9mdef, 337, 53);
                                                    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)t190) + (uintptr_t)t195 * 1));
                                                    uint8_t t197 = ((t196 == ((uint8_t)10ULL)));
                                                    gm_l11 = t197;
                                                }
                                                uint8_t t198 = gm_l11;
                                                if (!t198) {
                                                    goto gm_lb23;
                                                }
                                                uint64_t t199 = gm_l4;
                                                uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)2ULL), &r200);
                                                if (o201) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 338, 22);
                                                uint64_t t202 = r200;
                                                uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint64_t t204 = (t202 < t203 ? t202 : t203);
                                                gm_l4 = t204;
                                                void * t205 = (void *)((uint8_t *)&F.l1);
                                                void * t206 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint64_t t208 = gm_l2;
                                                uint64_t t209 = gm_l4;
                                                uint64_t t210 = (uintptr_t)t208;
                                                uint64_t t211 = (uintptr_t)t209;
                                                if (t210 > t211 || t211 > (uintptr_t)t207) gm_trap_slice(t210, t211, t207, gm_srccfhjr4r9mdef, 339, 28);
                                                uint64_t t212 = ((uintptr_t)t209) - (uintptr_t)t208;
                                                gm_b16_8 t213;
                                                GM_ST(void *, (uint8_t *)&t213, (void *)(((uint8_t *)t206 + (uintptr_t)t208 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(t212));
                                                gm_tftb1odc739t5 t214 = gm_f_push_3ef746b(t205, t213);
                                                gm_l12 = t214;
                                                uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                                if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srccfhjr4r9mdef, 339, 17);
                                                uint64_t t216 = gm_l4;
                                                gm_l2 = t216;
                                                gm_l5 = ((uint8_t)1);
                                                goto gm_lb3;
                                            }
gm_lb23:;
                                        }
gm_lb22:;
                                    } else {
                                        {
                                            {
                                                uint8_t t217 = gm_l6;
                                                uint8_t t218 = ((t217 == ((uint8_t)59ULL)));
                                                if (!t218) {
                                                    goto gm_lb25;
                                                }
                                                int32_t t219 = gm_l3;
                                                uint8_t t220 = ((t219 == ((int32_t)0LL)));
                                                if (!t220) {
                                                    goto gm_lb25;
                                                }
                                                gm_th2lqbo77pvsc t221;
                                                memset(&t221, 0, sizeof t221);
                                                memmove(gm_out, ((uint8_t *)&t221), 24);
                                                goto gm_fdone;
                                            }
gm_lb25:;
                                        }
gm_lb24:;
                                    }
                                }
                            }
gm_lb17:;
                        }
gm_lb12:;
                    }
                    uint8_t t222 = gm_l6;
                    uint8_t t223 = ((t222 == ((uint8_t)10ULL)));
                    gm_l5 = t223;
                    uint64_t t224 = gm_l4;
                    uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                    if (o226) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 348, 9);
                    uint64_t t227 = r225;
                    gm_l4 = t227;
                }
gm_lb3:;
            }
        }
gm_lb1:;
    }
    {
        void * t228 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t230 = gm_l2;
        uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t232 = (uintptr_t)t230;
        uint64_t t233 = (uintptr_t)t231;
        if (t232 > t233 || t233 > (uintptr_t)t229) gm_trap_slice(t232, t233, t229, gm_srccfhjr4r9mdef, 351, 24);
        uint64_t t234 = ((uintptr_t)t231) - (uintptr_t)t230;
        gm_b16_8 t235;
        GM_ST(void *, (uint8_t *)&t235, (void *)(((uint8_t *)t228 + (uintptr_t)t230 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(t234));
        gm_l7 = t235;
        {
            gm_node wn236 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl8sdnavrdjn, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 5, .result = (void *)&F.w5, .fw = NULL, .next = cx};
            gm_b16_8 t239 = gm_l7;
            gm_b0_1 t240 = gm_f_lines_9uns0k2_snvhnj5((void *)NULL, (&wn236), NULL, t239);
            if (gm_unw.kind) {
                goto gm_ll238;
            }
        }
        goto gm_ls241;
gm_ll238:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 5) { gm_unw.kind = GM_NONE; goto we237; }
        goto gm_lad;
gm_ls241:;
gm_lb26:;
we237:;
        gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_th2lqbo77pvsc t243;
        GM_ST(uint8_t, (uint8_t *)&t243 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t243 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t243 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t243), t242);
        ((uint8_t *)&t243)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t243), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_line_reset_73tgnor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_line_reset_73tgnor F;
    gm_b16_8 gm_l0; /* def */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_tabb710d9mq2e gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spkrf9e7cr6h4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    uint8_t t4 = gm_f_contains_gkki4kq(t2, t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        gm_l1 = ((uint8_t)1);
    } else {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)gm_ccmgjp6th6r));
        gm_tnpifv20irnph t8;
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
        gm_f_format_uaq30dv((void *)&gm_l2, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srccfhjr4r9mdef, 631, 57);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        uint8_t t12 = gm_f_ends_with_biriu5l(t6, t11);
        gm_l1 = t12;
    }
    uint8_t t13 = gm_l1;
    if (t13) {
        gm_b16_8 t14 = gm_l0;
        F.ret = t14;
        goto gm_fret;
    }
    gm_b16_8 t15 = gm_l0;
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)gm_ccmgjp6th6r));
    gm_t6j6smj7qoctu t17;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
    gm_f_format_q8i7gsb((void *)&gm_l3, (void *)NULL, cx, NULL, &t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srccfhjr4r9mdef, 632, 12);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    F.ret = t20;
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
extern __attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_depths_1781b1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_set_depths_1781b1p F;
    gm_b16_8 gm_l0; /* decls */
    void * gm_l1; /* by_name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint32_t gm_l6; /* depth */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    GM_STACK_CHECK(&F, 852u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9re6l0g6jicn gm_l14;
    gm_tqetrgo0q24b5 gm_l15;
    gm_trqfvmjtt4sij gm_l16;
    gm_b16_8 t19 = gm_l0;
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    gm_l2 = t20;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l3;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                uint64_t t24 = gm_l3;
                void * t25 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t27 = gm_l3;
                gm_t84tkl8vf34ko t28 = GM_LD(gm_t84tkl8vf34ko, (((uint8_t *)t25) + (uintptr_t)t27 * 56));
                gm_t9re6l0g6jicn t29;
                GM_ST(uint64_t, ((uint8_t *)&t29 + 0), t24);
                GM_ST(gm_t84tkl8vf34ko, ((uint8_t *)&t29 + 8), t28);
                gm_l14 = t29;
                {
                    {
                        uint64_t t30 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l4 = t30;
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l14) + 8), 56);
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 48));
                        uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)gm_crpb2rekieb));
                        uint8_t t33 = (!(t31 == t32));
                        if (t33) {
                            goto gm_lb3;
                        }
                        gm_l6 = ((uint32_t)0ULL);
                        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
                        gm_l7 = t34;
                        gm_l8 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t35 = gm_l8;
                                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint8_t t37 = (t35 < t36);
                                if (!t37) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint64_t t40 = gm_l8;
                                    {
                                        {
                                            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t38) + (uintptr_t)t40 * 16));
                                            gm_l9 = t41;
                                            gm_b16_8 t42 = gm_l9;
                                            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                                            uint8_t t44 = (gm_eq0iqiubfm7e(((uint8_t *)&t42), ((uint8_t *)&t43)));
                                            if (t44) {
                                                goto gm_lb8;
                                            }
                                            void * t45 = gm_l1;
                                            gm_b16_8 t46 = gm_l9;
                                            gm_f_get_ohh8mo3((void *)&gm_l15, t45, t46);
                                            uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l15))[40] != 0);
                                            if (t47) {
                                                memmove(((uint8_t *)&gm_l16), ((uint8_t *)&gm_l15), 40);
                                            } else {
                                                goto gm_lb8;
                                            }
                                            memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l16), 40);
                                            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                            gm_l11 = t48;
                                            gm_l12 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t49 = gm_l12;
                                                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                                    uint8_t t51 = (t49 < t50);
                                                    if (!t51) {
                                                        goto gm_lb11;
                                                    }
                                                    {
                                                        void * t52 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                                        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                                        uint64_t t54 = gm_l12;
                                                        {
                                                            {
                                                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t52) + (uintptr_t)t54 * 8));
                                                                gm_l13 = t55;
                                                                {
                                                                    {
                                                                        uint64_t t56 = gm_l13;
                                                                        uint64_t t57 = gm_l4;
                                                                        uint8_t t58 = (t56 < t57);
                                                                        if (!t58) {
                                                                            goto gm_lb17;
                                                                        }
                                                                        void * t59 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                        uint64_t t61 = gm_l13;
                                                                        if ((uintptr_t)t61 >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)t61, t60, gm_srccfhjr4r9mdef, 645, 30);
                                                                        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t59) + (uintptr_t)t61 * 56) + 48));
                                                                        uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)gm_crpb2rekieb));
                                                                        uint8_t t64 = ((t62 == t63));
                                                                        if (!t64) {
                                                                            goto gm_lb17;
                                                                        }
                                                                        uint32_t t65 = gm_l6;
                                                                        void * t66 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                        uint64_t t68 = gm_l13;
                                                                        if ((uintptr_t)t68 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t68, t67, gm_srccfhjr4r9mdef, 645, 77);
                                                                        uint32_t t69 = GM_LD(uint32_t, ((((uint8_t *)t66) + (uintptr_t)t68 * 56) + 52));
                                                                        uint32_t r70; bool o71 = __builtin_add_overflow(t69, ((uint32_t)1ULL), &r70);
                                                                        if (o71) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 645, 77);
                                                                        uint32_t t72 = r70;
                                                                        uint32_t t73 = (t65 > t72 ? t65 : t72);
                                                                        gm_l6 = t73;
                                                                        goto gm_lb16;
                                                                    }
gm_lb17:;
                                                                }
gm_lb16:;
                                                                goto gm_lb14;
                                                            }
gm_lb15:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 644, 13);
                                                        }
gm_lb14:;
                                                    }
gm_lb13:;
                                                    uint64_t t74 = gm_l12;
                                                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                                                    uint64_t t77 = r75;
                                                    gm_l12 = t77;
                                                }
                                            }
gm_lb11:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 641, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t78 = gm_l8;
                                uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                                uint64_t t81 = r79;
                                gm_l8 = t81;
                            }
                        }
gm_lb6:;
                        void * t82 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t84 = gm_l4;
                        if ((uintptr_t)t84 >= (uintptr_t)(t83)) gm_trap_index((uintptr_t)t84, t83, gm_srccfhjr4r9mdef, 648, 9);
                        uint32_t t85 = gm_l6;
                        GM_ST(uint32_t, ((((uint8_t *)t82) + (uintptr_t)t84 * 56) + 52), t85);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 638, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t86 = gm_l3;
            uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
            uint64_t t89 = r87;
            gm_l3 = t89;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_skip_literal_17lq2p8(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_literal_17lq2p8 F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* q */
    uint64_t gm_l3; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l4;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t7 = gm_l1;
    if ((uintptr_t)t7 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t7, t6, gm_srccfhjr4r9mdef, 427, 10);
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)t7 * 1));
    gm_l2 = t8;
    uint64_t t9 = gm_l1;
    uint64_t r10; bool o11 = __builtin_add_overflow(t9, ((uint64_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 428, 10);
    uint64_t t12 = r10;
    gm_l3 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            void * t16 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t18 = gm_l3;
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)t18 * 1));
            uint8_t t20 = gm_l2;
            uint8_t t21 = (!(t19 == t20));
            if (!t21) {
                goto gm_lb1;
            }
            {
                uint64_t t22 = gm_l3;
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t25 = gm_l3;
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t23) + (uintptr_t)t25 * 1));
                uint8_t t27 = ((t26 == ((uint8_t)92ULL)));
                if (t27) {
                    gm_l4 = ((uint64_t)2ULL);
                } else {
                    gm_l4 = ((uint64_t)1ULL);
                }
                uint64_t t28 = gm_l4;
                uint64_t r29; bool o30 = __builtin_add_overflow(t22, t28, &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 430, 9);
                uint64_t t31 = r29;
                gm_l3 = t31;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t32 = gm_l3;
    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
    if (o34) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 432, 13);
    uint64_t t35 = r33;
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t37 = (t35 < t36 ? t35 : t36);
    F.ret = t37;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_sort_name_ro82p6b(void * a0);
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_split_by_file_hgjqgsp F;
    void * gm_l0; /* parts */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* o */
    gm_b16_8 gm_l8; /* all */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* item */
    gm_t6j6smj7qoctu gm_l12; /* d */
    gm_t6j6smj7qoctu gm_l13; /* f */
    gm_b16_8 gm_l14; /* name */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    void * gm_l29; /* l */
    gm_b16_8 gm_l32; /* first */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l39; /* def */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* k */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_t6j6smj7qoctu gm_l49; /* f */
    GM_STACK_CHECK(&F, 4524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l50;
    gm_b16_8 gm_l51;
    gm_ttu09lij36f8c gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_ttu09lij36f8c gm_l55;
    gm_t6j6smj7qoctu gm_l56;
    gm_th2lqbo77pvsc gm_l57;
    gm_b16_8 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tabb710d9mq2e gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_t9re6l0g6jicn gm_l65;
    gm_tftb1odc739t5 gm_l66;
    void * gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_tftb1odc739t5 gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_tftb1odc739t5 gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tmupb6k0asuti gm_l75;
    gm_tftb1odc739t5 gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tftb1odc739t5 gm_l78;
    gm_tftb1odc739t5 gm_l79;
    {
        {
            {
                {
                    void * t59 = gm_l0;
                    uint64_t t60 = GM_LD(uint64_t, ((((uint8_t *)t59) + 96) + 8));
                    uint8_t t61 = ((t60 == ((uint64_t)0ULL)));
                    if (!t61) {
                        goto gm_lb3;
                    }
                    void * t62 = gm_l0;
                    uint64_t t63 = GM_LD(uint64_t, ((((uint8_t *)t62) + 64) + 8));
                    uint8_t t64 = (t63 > ((uint64_t)0ULL));
                    if (!t64) {
                        goto gm_lb3;
                    }
                    gm_ttu09lij36f8c t65;
                    memset(&t65, 0, sizeof t65);
                    memmove(gm_out, ((uint8_t *)&t65), 40);
                    goto gm_fdone;
                }
gm_lb3:;
            }
gm_lb2:;
            gm_trqfvmjtt4sij t66;
            gm_f_list_016fo30((void *)&t66, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l1 = t66;
            void * t67 = (void *)((uint8_t *)&F.l1);
            void * t68 = gm_l0;
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 128));
            uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)gm_crpb2rekieb));
            gm_b0_1 t71 = gm_f_decls_of_srep9du((void *)NULL, cx, NULL, t67, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t72 = (void *)((uint8_t *)&F.l1);
            void * t73 = gm_l0;
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)t73) + 144));
            uint8_t t75 = GM_LD(uint8_t, ((uint8_t *)gm_cos4c858gsq));
            gm_b0_1 t76 = gm_f_decls_of_srep9du((void *)NULL, cx, NULL, t72, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4c7ipjj9s3fp t77;
            gm_f_hash_map_lljdp3h((void *)&t77, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t77;
            gm_trqfvmjtt4sij t78;
            gm_f_list_1g734tk((void *)&t78, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld58_1;
            }
            F.l3 = t78;
            void * t79 = gm_l0;
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)t79) + 96));
            gm_l4 = t80;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t82 = gm_l5;
                    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t84 = (t82 < t83);
                    if (!t84) {
                        goto gm_lb4;
                    }
                    {
                        void * t85 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t87 = gm_l5;
                        {
                            {
                                memmove(((uint8_t *)&gm_l6), (((uint8_t *)t85) + (uintptr_t)t87 * 32), 32);
                                gm_trqfvmjtt4sij t88;
                                gm_f_list_c8kt75n((void *)&t88, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll81;
                                }
                                F.l7 = t88;
                                gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                                gm_f_items_of_q103oiv((void *)&gm_l50, (void *)NULL, cx, NULL, t89);
                                if (gm_unw.kind) {
                                    goto gm_ll81;
                                }
                                uint8_t t90 = (uint8_t)((((uint8_t *)&gm_l50))[16] != 0);
                                if (t90) {
                                    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l50));
                                    gm_l51 = t91;
                                } else {
                                    gm_ttu09lij36f8c t92;
                                    memset(&t92, 0, sizeof t92);
                                    memmove(&F.ret, ((uint8_t *)&t92), 40);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                    goto gm_ll81;
                                }
                                gm_b16_8 t93 = gm_l51;
                                gm_l8 = t93;
                                gm_b16_8 t94 = gm_l8;
                                gm_l9 = t94;
                                gm_l10 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t96 = gm_l10;
                                        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint8_t t98 = (t96 < t97);
                                        if (!t98) {
                                            goto gm_lb9;
                                        }
                                        {
                                            void * t99 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                            uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                            uint64_t t101 = gm_l10;
                                            {
                                                {
                                                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)t99) + (uintptr_t)t101 * 16));
                                                    gm_l11 = t102;
                                                    {
                                                        {
                                                            gm_b16_8 t103 = gm_l11;
                                                            gm_f_inline_definition_6aokb9d((void *)&gm_l52, (void *)NULL, cx, NULL, t103, ((uint64_t)1600ULL));
                                                            if (gm_unw.kind) {
                                                                goto gm_ll95;
                                                            }
                                                            uint8_t t104 = (uint8_t)((((uint8_t *)&gm_l52))[32] != 0);
                                                            if (!t104) {
                                                                goto gm_lb15;
                                                            }
                                                            memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l52), 32);
                                                            void * t105 = (void *)((uint8_t *)&F.l2);
                                                            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                                            uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                                            gm_tftb1odc739t5 t108 = gm_f_put_qoojoqv(t105, t106, t107);
                                                            gm_l53 = t108;
                                                            uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                                            if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srccfhjr4r9mdef, 542, 17);
                                                            void * t110 = (void *)((uint8_t *)&F.l1);
                                                            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                                                            gm_b16_8 t112 = gm_f_line_reset_73tgnor((void *)NULL, cx, NULL, t111);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll95;
                                                            }
                                                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                                            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                                                            gm_b16_8 t115 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t114);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll95;
                                                            }
                                                            uint8_t t116 = GM_LD(uint8_t, ((uint8_t *)gm_c30tdq2p48i));
                                                            gm_t84tkl8vf34ko t117;
                                                            GM_ST(uint8_t, (uint8_t *)&t117 + 49, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t117 + 50, 0);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t117 + 0), t112);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t117 + 16), t113);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t117 + 32), t115);
                                                            GM_ST(uint8_t, ((uint8_t *)&t117 + 48), t116);
                                                            GM_ST(uint32_t, ((uint8_t *)&t117 + 52), ((uint32_t)0ULL));
                                                            gm_tftb1odc739t5 t118 = gm_f_push_rda4o9n(t110, &t117);
                                                            gm_l54 = t118;
                                                            uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                                            if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srccfhjr4r9mdef, 543, 17);
                                                            goto gm_lb11;
                                                        }
gm_lb15:;
                                                    }
gm_lb14:;
                                                    gm_b16_8 t120 = gm_l11;
                                                    gm_f_function_o2lv8g6((void *)&gm_l55, (void *)NULL, cx, NULL, t120);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll95;
                                                    }
                                                    uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l55))[32] != 0);
                                                    if (t121) {
                                                        memmove(((uint8_t *)&gm_l56), ((uint8_t *)&gm_l55), 32);
                                                    } else {
                                                        gm_ttu09lij36f8c t122;
                                                        memset(&t122, 0, sizeof t122);
                                                        memmove(&F.ret, ((uint8_t *)&t122), 40);
                                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                                        goto gm_ll95;
                                                    }
                                                    memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l56), 32);
                                                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                                    gm_f_c_fn_name_77uspqp((void *)&gm_l57, t123);
                                                    uint8_t t124 = (uint8_t)((((uint8_t *)&gm_l57))[16] != 0);
                                                    if (t124) {
                                                        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l57));
                                                        gm_l58 = t125;
                                                    } else {
                                                        gm_ttu09lij36f8c t126;
                                                        memset(&t126, 0, sizeof t126);
                                                        memmove(&F.ret, ((uint8_t *)&t126), 40);
                                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                                        goto gm_ll95;
                                                    }
                                                    gm_b16_8 t127 = gm_l58;
                                                    gm_l14 = t127;
                                                    void * t128 = (void *)((uint8_t *)&F.l7);
                                                    gm_b16_8 t129 = gm_l14;
                                                    gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 16));
                                                    gm_b16_8 t131 = gm_f_line_reset_73tgnor((void *)NULL, cx, NULL, t130);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll95;
                                                    }
                                                    gm_t6j6smj7qoctu t132;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t129);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t132 + 16), t131);
                                                    gm_tftb1odc739t5 t133 = gm_f_push_0ek3ea4(t128, &t132);
                                                    gm_l59 = t133;
                                                    uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                                    if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srccfhjr4r9mdef, 548, 13);
                                                    void * t135 = (void *)((uint8_t *)&F.l1);
                                                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                                    gm_tnpifv20irnph t137;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
                                                    gm_f_format_uaq30dv((void *)&gm_l60, (void *)NULL, cx, NULL, t137);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll95;
                                                    }
                                                    uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                                    if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_srccfhjr4r9mdef, 549, 35);
                                                    gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                                                    gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
                                                    gm_b16_8 t141 = gm_l14;
                                                    gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                                    gm_b16_8 t143 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t142);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll95;
                                                    }
                                                    uint8_t t144 = GM_LD(uint8_t, ((uint8_t *)gm_c72j1g8d455));
                                                    gm_t84tkl8vf34ko t145;
                                                    GM_ST(uint8_t, (uint8_t *)&t145 + 49, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t145 + 50, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t140);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t145 + 16), t141);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t145 + 32), t143);
                                                    GM_ST(uint8_t, ((uint8_t *)&t145 + 48), t144);
                                                    GM_ST(uint32_t, ((uint8_t *)&t145 + 52), ((uint32_t)0ULL));
                                                    gm_tftb1odc739t5 t146 = gm_f_push_rda4o9n(t135, &t145);
                                                    gm_l61 = t146;
                                                    uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                                    if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srccfhjr4r9mdef, 549, 13);
                                                    goto gm_lb12;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 540, 9);
                                            }
gm_lb12:;
                                        }
gm_lb11:;
                                        uint64_t t148 = gm_l10;
                                        uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                                        uint64_t t151 = r149;
                                        gm_l10 = t151;
                                    }
                                }
                                goto gm_ls152;
gm_ll95:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb9; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb11; }
                                }
                                goto gm_ll81;
gm_ls152:;
gm_lb9:;
                                void * t153 = (void *)((uint8_t *)&F.l3);
                                gm_tftb1odc739t5 t154 = gm_f_push_rlpbnuq(t153, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
                                gm_l62 = t154;
                                uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srccfhjr4r9mdef, 551, 9);
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 537, 5);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t156 = gm_l5;
                    uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                    uint64_t t159 = r157;
                    gm_l5 = t159;
                }
            }
            goto gm_ls160;
gm_ll81:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb6; }
            }
            goto gm_ld58_1;
gm_ls160:;
gm_lb4:;
        }
        {
            gm_trqfvmjtt4sij t161;
            gm_f_list_1g734tk((void *)&t161, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld58_1;
            }
            F.l15 = t161;
            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_l16 = t162;
            gm_l17 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t164 = gm_l17;
                    uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint8_t t166 = (t164 < t165);
                    if (!t166) {
                        goto gm_lb16;
                    }
                    {
                        void * t167 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t169 = gm_l17;
                        {
                            {
                                void * t170 = (void *)((uint8_t *)&F.l15);
                                gm_trqfvmjtt4sij t171;
                                gm_f_list_c8kt75n((void *)&t171, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll163;
                                }
                                gm_tftb1odc739t5 t172 = gm_f_push_rlpbnuq(t170, &t171);
                                gm_l63 = t172;
                                uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srccfhjr4r9mdef, 554, 29);
                                goto gm_lb19;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 554, 5);
                        }
gm_lb19:;
                    }
gm_lb18:;
                    uint64_t t174 = gm_l17;
                    uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                    uint64_t t177 = r175;
                    gm_l17 = t177;
                }
            }
            goto gm_ls178;
gm_ll163:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb16; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb18; }
            }
            goto gm_ld58_1;
gm_ls178:;
gm_lb16:;
            void * t179 = gm_l0;
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)t179) + 16));
            gm_l18 = t180;
            {
                gm_node wn181 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl1l1s81m49k, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 3, .result = (void *)&F.w3, .fw = NULL, .next = cx};
                gm_b16_8 t184 = gm_l18;
                gm_b0_1 t185 = gm_f_lines_9uns0k2_svejqbu((void *)NULL, (&wn181), NULL, t184);
                if (gm_unw.kind) {
                    goto gm_ll183;
                }
            }
            goto gm_ls186;
gm_ll183:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 3) { gm_unw.kind = GM_NONE; goto we182; }
            goto gm_ld58_1;
gm_ls186:;
gm_lb21:;
we182:;
            gm_trqfvmjtt4sij t187;
            gm_f_list_b7l80f8((void *)&t187, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld58_1;
            }
            F.l19 = t187;
            gm_l21 = ((uint64_t)0ULL);
            gm_l20 = ((uint64_t)16ULL);
            {
                for (;;) {
                    uint64_t t189 = gm_l21;
                    uint64_t t190 = gm_l20;
                    uint8_t t191 = (t189 < t190);
                    if (!t191) {
                        goto gm_lb22;
                    }
                    {
                        {
                            {
                                void * t192 = (void *)((uint8_t *)&F.l19);
                                gm_trqfvmjtt4sij t193;
                                gm_f_list_epvducg((void *)&t193, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll188;
                                }
                                gm_tftb1odc739t5 t194 = gm_f_push_vnejmrp(t192, &t193);
                                gm_l64 = t194;
                                uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srccfhjr4r9mdef, 571, 31);
                                goto gm_lb25;
                            }
gm_lb26:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 571, 5);
                        }
gm_lb25:;
                    }
gm_lb24:;
                    uint64_t t196 = gm_l21;
                    uint64_t r197; bool o198 = __builtin_add_overflow(t196, ((uint64_t)1ULL), &r197);
                    uint64_t t199 = r197;
                    gm_l21 = t199;
                }
            }
            goto gm_ls200;
gm_ll188:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb22; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb24; }
            }
            goto gm_ld58_1;
gm_ls200:;
gm_lb22:;
            void * t201 = gm_l0;
            gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)t201) + 32));
            gm_l22 = t202;
            {
                gm_node wn203 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlth8t37utgd, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 5, .result = (void *)&F.w5, .fw = NULL, .next = cx};
                gm_b16_8 t206 = gm_l22;
                gm_b0_1 t207 = gm_f_lines_9uns0k2_s1f7rak((void *)NULL, (&wn203), NULL, t206);
                if (gm_unw.kind) {
                    goto gm_ll205;
                }
            }
            goto gm_ls208;
gm_ll205:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 5) { gm_unw.kind = GM_NONE; goto we204; }
            goto gm_ld58_1;
gm_ls208:;
gm_lb27:;
we204:;
        }
        {
            gm_t4c7ipjj9s3fp t209;
            gm_f_hash_map_dklib0j((void *)&t209, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld58_1;
            }
            F.l23 = t209;
            gm_trqfvmjtt4sij t210;
            gm_f_list_5dhgg4i((void *)&t210, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld58_2;
            }
            F.l24 = t210;
            gm_b16_8 t211 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
            gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&t211));
            gm_l25 = t212;
            gm_l26 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t214 = gm_l26;
                    uint64_t t215 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                    uint8_t t216 = (t214 < t215);
                    if (!t216) {
                        goto gm_lb28;
                    }
                    {
                        uint64_t t217 = gm_l26;
                        void * t218 = GM_LD(void *, ((uint8_t *)&gm_l25));
                        uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                        uint64_t t220 = gm_l26;
                        gm_t84tkl8vf34ko t221 = GM_LD(gm_t84tkl8vf34ko, (((uint8_t *)t218) + (uintptr_t)t220 * 56));
                        gm_t9re6l0g6jicn t222;
                        GM_ST(uint64_t, ((uint8_t *)&t222 + 0), t217);
                        GM_ST(gm_t84tkl8vf34ko, ((uint8_t *)&t222 + 8), t221);
                        gm_l65 = t222;
                        {
                            {
                                uint64_t t223 = GM_LD(uint64_t, ((uint8_t *)&gm_l65));
                                gm_l27 = t223;
                                memmove(((uint8_t *)&F.l28), (((uint8_t *)&gm_l65) + 8), 56);
                                uint64_t t224 = GM_LD(uint64_t, ((((uint8_t *)&F.l28) + 16) + 8));
                                uint8_t t225 = ((t224 == ((uint64_t)0ULL)));
                                if (t225) {
                                    void * t226 = (void *)((uint8_t *)&F.l24);
                                    uint64_t t227 = gm_l27;
                                    gm_tftb1odc739t5 t228 = gm_f_push_ucgmlo2(t226, t227);
                                    gm_l66 = t228;
                                    uint8_t t229 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                    if (!t229) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srccfhjr4r9mdef, 590, 13);
                                    goto gm_lb30;
                                }
                                {
                                    {
                                        void * t230 = (void *)((uint8_t *)&F.l23);
                                        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 16));
                                        void * t232 = gm_f_get_ptr_53uc4fm(t230, t231);
                                        gm_l67 = t232;
                                        uint8_t t233 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l67)) != 0);
                                        if (!t233) {
                                            goto gm_lb34;
                                        }
                                        void * t234 = GM_LD(void *, ((uint8_t *)&gm_l67));
                                        gm_l29 = t234;
                                        void * t235 = gm_l29;
                                        uint64_t t236 = gm_l27;
                                        gm_tftb1odc739t5 t237 = gm_f_push_ucgmlo2(t235, t236);
                                        gm_l68 = t237;
                                        uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                        if (!t238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srccfhjr4r9mdef, 594, 13);
                                        goto gm_lb33;
                                    }
gm_lb34:;
                                    gm_trqfvmjtt4sij t239;
                                    gm_f_list_5dhgg4i((void *)&t239, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_ll213;
                                    }
                                    F.l30 = t239;
                                    void * t240 = (void *)((uint8_t *)&F.l30);
                                    uint64_t t241 = gm_l27;
                                    gm_tftb1odc739t5 t242 = gm_f_push_ucgmlo2(t240, t241);
                                    gm_l69 = t242;
                                    uint8_t t243 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                    if (!t243) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_srccfhjr4r9mdef, 597, 13);
                                    void * t244 = (void *)((uint8_t *)&F.l23);
                                    gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 16));
                                    gm_tftb1odc739t5 t246 = gm_f_put_7o5vpta(t244, t245, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l30)));
                                    gm_l70 = t246;
                                    uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                    if (!t247) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srccfhjr4r9mdef, 598, 13);
                                }
gm_lb33:;
                                goto gm_lb31;
                            }
gm_lb32:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 588, 5);
                        }
gm_lb31:;
                    }
gm_lb30:;
                    uint64_t t248 = gm_l26;
                    uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                    uint64_t t251 = r249;
                    gm_l26 = t251;
                }
            }
            goto gm_ls252;
gm_ll213:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb28; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb30; }
            }
            goto gm_ld58_2;
gm_ls252:;
gm_lb28:;
        }
        {
            gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
            void * t254 = (void *)((uint8_t *)&F.l23);
            gm_b0_1 t255 = gm_f_set_depths_1781b1p((void *)NULL, cx, NULL, t253, t254);
            if (gm_unw.kind) {
                goto gm_ld58_2;
            }
            gm_trqfvmjtt4sij t256;
            gm_f_list_epvducg((void *)&t256, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld58_2;
            }
            F.l31 = t256;
            void * t257 = gm_l0;
            gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)t257) + 48));
            void * t259 = gm_l0;
            gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)t259) + 80));
            gm_t6j6smj7qoctu t261;
            GM_ST(gm_b16_8, ((uint8_t *)&t261 + 0), t258);
            GM_ST(gm_b16_8, ((uint8_t *)&t261 + 16), t260);
            gm_f_format_8n626pi((void *)&gm_l71, (void *)NULL, cx, NULL, &t261);
            if (gm_unw.kind) {
                goto gm_ld58_2;
            }
            uint8_t t262 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
            if (!t262) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_srccfhjr4r9mdef, 603, 14);
            gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
            gm_l32 = t263;
            void * t264 = (void *)((uint8_t *)&F.l31);
            gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
            gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
            void * t267 = (void *)((uint8_t *)&F.l23);
            gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
            gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
            gm_b16_8 t270 = gm_l32;
            gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
            void * t272 = gm_l0;
            gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)t272) + 160));
            gm_b16_8 t274 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t266, t267, t269, t271, t273);
            if (gm_unw.kind) {
                goto gm_ld58_2;
            }
            gm_tftb1odc739t5 t275 = gm_f_push_3ef746b(t264, t274);
            gm_l72 = t275;
            uint8_t t276 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
            if (!t276) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_srccfhjr4r9mdef, 604, 5);
            gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
            gm_l33 = t277;
            gm_l34 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t279 = gm_l34;
                    uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint8_t t281 = (t279 < t280);
                    if (!t281) {
                        goto gm_lb35;
                    }
                    {
                        void * t282 = GM_LD(void *, ((uint8_t *)&gm_l33));
                        uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                        uint64_t t284 = gm_l34;
                        {
                            {
                                memmove(((uint8_t *)&F.l35), (((uint8_t *)t282) + (uintptr_t)t284 * 40), 40);
                                uint64_t t285 = GM_LD(uint64_t, (((uint8_t *)&F.l35) + 8));
                                uint8_t t286 = ((t285 == ((uint64_t)0ULL)));
                                if (t286) {
                                    goto gm_lb37;
                                }
                                gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                                gm_f_sort_skeree7(t287);
                                gm_trqfvmjtt4sij t288;
                                gm_f_list_uvfu1k1((void *)&t288, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll278;
                                }
                                F.l36 = t288;
                                gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                                gm_l37 = t289;
                                gm_l38 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t291 = gm_l38;
                                        uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                        uint8_t t293 = (t291 < t292);
                                        if (!t293) {
                                            goto gm_lb40;
                                        }
                                        {
                                            void * t294 = GM_LD(void *, ((uint8_t *)&gm_l37));
                                            uint64_t t295 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                            uint64_t t296 = gm_l38;
                                            {
                                                {
                                                    gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)t294) + (uintptr_t)t296 * 16));
                                                    gm_l39 = t297;
                                                    void * t298 = (void *)((uint8_t *)&F.l36);
                                                    gm_b16_8 t299 = gm_l39;
                                                    gm_tnpifv20irnph t300;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t300 + 0), t299);
                                                    gm_tftb1odc739t5 t301 = gm_f_write_cdouvhd(t298, t300);
                                                    gm_l73 = t301;
                                                    uint8_t t302 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                                                    if (!t302) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_srccfhjr4r9mdef, 610, 30);
                                                    goto gm_lb43;
                                                }
gm_lb44:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 610, 9);
                                            }
gm_lb43:;
                                        }
gm_lb42:;
                                        uint64_t t303 = gm_l38;
                                        uint64_t r304; bool o305 = __builtin_add_overflow(t303, ((uint64_t)1ULL), &r304);
                                        uint64_t t306 = r304;
                                        gm_l38 = t306;
                                    }
                                }
gm_lb40:;
                                void * t307 = (void *)((uint8_t *)&F.l31);
                                gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
                                gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                                void * t310 = (void *)((uint8_t *)&F.l23);
                                gm_b16_8 t311 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                gm_b16_8 t312 = GM_LD(gm_b16_8, ((uint8_t *)&t311));
                                gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&F.l36));
                                gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                                void * t315 = gm_l0;
                                gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)t315) + 160));
                                gm_b16_8 t317 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t309, t310, t312, t314, t316);
                                if (gm_unw.kind) {
                                    goto gm_ll278;
                                }
                                gm_tftb1odc739t5 t318 = gm_f_push_3ef746b(t307, t317);
                                gm_l74 = t318;
                                uint8_t t319 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                if (!t319) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srccfhjr4r9mdef, 611, 9);
                                goto gm_lb38;
                            }
gm_lb39:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 605, 5);
                        }
gm_lb38:;
                    }
gm_lb37:;
                    uint64_t t320 = gm_l34;
                    uint64_t r321; bool o322 = __builtin_add_overflow(t320, ((uint64_t)1ULL), &r321);
                    uint64_t t323 = r321;
                    gm_l34 = t323;
                }
            }
            goto gm_ls324;
gm_ll278:;
            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb35; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb37; }
            }
            goto gm_ld58_2;
gm_ls324:;
gm_lb35:;
        }
        {
            gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&t325));
            gm_l40 = t326;
            gm_l41 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t328 = gm_l41;
                    uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                    uint8_t t330 = (t328 < t329);
                    if (!t330) {
                        goto gm_lb45;
                    }
                    {
                        uint64_t t331 = gm_l41;
                        void * t332 = GM_LD(void *, ((uint8_t *)&gm_l40));
                        uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                        uint64_t t334 = gm_l41;
                        gm_trqfvmjtt4sij t335 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t332) + (uintptr_t)t334 * 40));
                        gm_tmupb6k0asuti t336;
                        GM_ST(uint64_t, ((uint8_t *)&t336 + 0), t331);
                        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t336 + 8), t335);
                        gm_l75 = t336;
                        {
                            {
                                uint64_t t337 = GM_LD(uint64_t, ((uint8_t *)&gm_l75));
                                gm_l42 = t337;
                                memmove(((uint8_t *)&F.l43), (((uint8_t *)&gm_l75) + 8), 40);
                                void * t338 = GM_LD(void *, ((uint8_t *)&F.l15));
                                uint64_t t339 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                uint64_t t340 = gm_l42;
                                if ((uintptr_t)t340 >= (uintptr_t)(t339)) gm_trap_index((uintptr_t)t340, t339, gm_srccfhjr4r9mdef, 614, 16);
                                memmove(((uint8_t *)&F.l44), (((uint8_t *)t338) + (uintptr_t)t340 * 40), 40);
                                {
                                    {
                                        uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)&F.l43) + 8));
                                        uint8_t t342 = ((t341 == ((uint64_t)0ULL)));
                                        if (!t342) {
                                            goto gm_lb51;
                                        }
                                        uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 8));
                                        uint8_t t344 = ((t343 == ((uint64_t)0ULL)));
                                        if (!t344) {
                                            goto gm_lb51;
                                        }
                                        goto gm_lb47;
                                    }
gm_lb51:;
                                }
gm_lb50:;
                                gm_trqfvmjtt4sij t345;
                                gm_f_list_c8kt75n((void *)&t345, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll327;
                                }
                                F.l45 = t345;
                                void * t346 = (void *)((uint8_t *)&F.l45);
                                gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                                gm_b16_8 t348 = GM_LD(gm_b16_8, ((uint8_t *)&t347));
                                gm_tftb1odc739t5 t349 = gm_f_extend_k51ik07(t346, t348);
                                gm_l76 = t349;
                                uint8_t t350 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                if (!t350) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 1, gm_srccfhjr4r9mdef, 617, 9);
                                void * t351 = (void *)((uint8_t *)&F.l45);
                                gm_b16_8 t352 = GM_LD(gm_b16_8, ((uint8_t *)&F.l44));
                                gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&t352));
                                gm_tftb1odc739t5 t354 = gm_f_extend_k51ik07(t351, t353);
                                gm_l77 = t354;
                                uint8_t t355 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                if (!t355) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srccfhjr4r9mdef, 618, 9);
                                gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&F.l45));
                                gm_closure t357 = ((gm_closure){GM_PTR_TAG(gm_f_fn_idfg0st), NULL, cx});
                                gm_f_sort_by_4j6ktaf(t356, t357);
                                if (gm_unw.kind) {
                                    goto gm_ll327;
                                }
                                gm_trqfvmjtt4sij t358;
                                gm_f_list_uvfu1k1((void *)&t358, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll327;
                                }
                                F.l46 = t358;
                                gm_b16_8 t359 = GM_LD(gm_b16_8, ((uint8_t *)&F.l45));
                                gm_l47 = t359;
                                gm_l48 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t361 = gm_l48;
                                        uint64_t t362 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                        uint8_t t363 = (t361 < t362);
                                        if (!t363) {
                                            goto gm_lb52;
                                        }
                                        {
                                            void * t364 = GM_LD(void *, ((uint8_t *)&gm_l47));
                                            uint64_t t365 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                            uint64_t t366 = gm_l48;
                                            {
                                                {
                                                    memmove(((uint8_t *)&gm_l49), (((uint8_t *)t364) + (uintptr_t)t366 * 32), 32);
                                                    void * t367 = (void *)((uint8_t *)&F.l46);
                                                    gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 16));
                                                    gm_tftb1odc739t5 t369 = gm_f_extend_0cs85gs(t367, t368);
                                                    gm_l78 = t369;
                                                    uint8_t t370 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                    if (!t370) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_srccfhjr4r9mdef, 621, 30);
                                                    goto gm_lb55;
                                                }
gm_lb56:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 621, 9);
                                            }
gm_lb55:;
                                        }
gm_lb54:;
                                        uint64_t t371 = gm_l48;
                                        uint64_t r372; bool o373 = __builtin_add_overflow(t371, ((uint64_t)1ULL), &r372);
                                        uint64_t t374 = r372;
                                        gm_l48 = t374;
                                    }
                                }
gm_lb52:;
                                void * t375 = (void *)((uint8_t *)&F.l31);
                                gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
                                gm_b16_8 t377 = GM_LD(gm_b16_8, ((uint8_t *)&t376));
                                void * t378 = (void *)((uint8_t *)&F.l23);
                                gm_b16_8 t379 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                gm_b16_8 t380 = GM_LD(gm_b16_8, ((uint8_t *)&t379));
                                gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&F.l46));
                                gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&t381));
                                void * t383 = gm_l0;
                                gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)t383) + 160));
                                gm_b16_8 t385 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t377, t378, t380, t382, t384);
                                if (gm_unw.kind) {
                                    goto gm_ll327;
                                }
                                gm_tftb1odc739t5 t386 = gm_f_push_3ef746b(t375, t385);
                                gm_l79 = t386;
                                uint8_t t387 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                if (!t387) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srccfhjr4r9mdef, 622, 9);
                                goto gm_lb48;
                            }
gm_lb49:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 613, 5);
                        }
gm_lb48:;
                    }
gm_lb47:;
                    uint64_t t388 = gm_l41;
                    uint64_t r389; bool o390 = __builtin_add_overflow(t388, ((uint64_t)1ULL), &r389);
                    uint64_t t391 = r389;
                    gm_l41 = t391;
                }
            }
            goto gm_ls392;
gm_ll327:;
            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb45; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb47; }
            }
            goto gm_ld58_2;
gm_ls392:;
gm_lb45:;
        }
        {
            void * t393 = gm_l0;
            gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)t393) + 112));
            gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
            gm_t6j6smj7qoctu t396;
            GM_ST(gm_b16_8, ((uint8_t *)&t396 + 0), t394);
            GM_ST(gm_b16_8, ((uint8_t *)&t396 + 16), t395);
            gm_ttu09lij36f8c t397;
            GM_ST(uint8_t, (uint8_t *)&t397 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t397 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t397 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t397), t396);
            ((uint8_t *)&t397)[32] = 1;
            memmove(&F.ret, ((uint8_t *)&t397), 40);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld58_2;
        }
    }
    {
        void * t398 = (void *)((uint8_t *)&F.l23);
        gm_f_deinit_vtu6170(t398);
    }
    {
        void * t399 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_nvuvq05(t399);
    }
    goto gm_ls400;
gm_ld58_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t401 = (void *)((uint8_t *)&F.l23);
        gm_f_deinit_vtu6170(t401);
        gm_unw = gm_saved;
    }
gm_ld58_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t402 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_nvuvq05(t402);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls400:;
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
inline __attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_split_jendjdm F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* n */
    gm_t6j6smj7qoctu gm_l2; /* s */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* u */
    GM_STACK_CHECK(&F, 504u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_ttu09lij36f8c gm_l7;
    gm_t6j6smj7qoctu gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t7 = gm_l0;
    uint64_t t8 = gm_l1;
    gm_f_split_shared_a91530r((void *)&gm_l7, (void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l7))[32] != 0);
    if (t9) {
        memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l7), 32);
    } else {
        gm_th2lqbo77pvsc t10;
        memset(&t10, 0, sizeof t10);
        memmove(gm_out, ((uint8_t *)&t10), 24);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l8), 32);
    gm_trqfvmjtt4sij t11;
    gm_f_list_epvducg((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t11;
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
    gm_l4 = t12;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l5;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t18 = gm_l5;
                {
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t16) + (uintptr_t)t18 * 16));
                        gm_l6 = t19;
                        void * t20 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                        gm_b16_8 t22 = gm_l6;
                        gm_t6j6smj7qoctu t23;
                        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
                        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t22);
                        gm_f_format_8n626pi((void *)&gm_l9, (void *)NULL, cx, NULL, &t23);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srccfhjr4r9mdef, 90, 33);
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                        gm_tftb1odc739t5 t27 = gm_f_push_3ef746b(t20, t26);
                        gm_l10 = t27;
                        uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srccfhjr4r9mdef, 90, 24);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 90, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l5;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l5 = t32;
        }
    }
gm_lb1:;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_th2lqbo77pvsc t34;
    GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t34), t33);
    ((uint8_t *)&t34)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t34), 24);
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
__attribute__((aligned(4))) void gm_f_split_shared_a91530r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_split_shared_a91530r F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* all */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* item */
    gm_t6j6smj7qoctu gm_l8; /* d */
    gm_b16_8 gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* item */
    gm_t6j6smj7qoctu gm_l18; /* f */
    gm_b16_8 gm_l19; /* shared */
    uint64_t gm_l20; /* total */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* item */
    uint64_t gm_l24; /* share */
    uint64_t gm_l27; /* size */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_b16_8 gm_l30; /* item */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    GM_STACK_CHECK(&F, 2525u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l35;
    gm_b16_8 gm_l36;
    gm_ttu09lij36f8c gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    uint8_t gm_l40;
    gm_ttu09lij36f8c gm_l41;
    gm_ttu09lij36f8c gm_l42;
    gm_t6j6smj7qoctu gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    {
        {
            void * t38 = gm_l0;
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 64));
            gm_f_items_of_q103oiv((void *)&gm_l35, (void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
            if (t40) {
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
                gm_l36 = t41;
            } else {
                gm_ttu09lij36f8c t42;
                memset(&t42, 0, sizeof t42);
                memmove(gm_out, ((uint8_t *)&t42), 40);
                goto gm_fdone;
            }
            gm_b16_8 t43 = gm_l36;
            gm_l2 = t43;
            gm_t4c7ipjj9s3fp t44;
            gm_f_hash_map_m04clhi((void *)&t44, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t44;
            gm_trqfvmjtt4sij t45;
            gm_f_list_uvfu1k1((void *)&t45, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l4 = t45;
            gm_b16_8 t46 = gm_l2;
            gm_l5 = t46;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t48 = gm_l6;
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t50 = (t48 < t49);
                    if (!t50) {
                        goto gm_lb2;
                    }
                    {
                        void * t51 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t53 = gm_l6;
                        {
                            {
                                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)t51) + (uintptr_t)t53 * 16));
                                gm_l7 = t54;
                                {
                                    {
                                        gm_b16_8 t55 = gm_l7;
                                        gm_f_inline_definition_6aokb9d((void *)&gm_l37, (void *)NULL, cx, NULL, t55, ((uint64_t)0ULL));
                                        if (gm_unw.kind) {
                                            goto gm_ll47;
                                        }
                                        uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l37))[32] != 0);
                                        if (!t56) {
                                            goto gm_lb8;
                                        }
                                        memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l37), 32);
                                        void * t57 = (void *)((uint8_t *)&F.l4);
                                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                                        gm_tftb1odc739t5 t59 = gm_f_extend_0cs85gs(t57, t58);
                                        gm_l38 = t59;
                                        uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                        if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srccfhjr4r9mdef, 109, 13);
                                        void * t61 = (void *)((uint8_t *)&F.l3);
                                        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 16));
                                        gm_tftb1odc739t5 t63 = gm_f_put_drsjd1t(t61, t62, GM_ZERO(gm_b0_1));
                                        gm_l39 = t63;
                                        uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                        if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srccfhjr4r9mdef, 110, 13);
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                }
gm_lb7:;
                                goto gm_lb5;
                            }
gm_lb6:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 107, 5);
                        }
gm_lb5:;
                    }
gm_lb4:;
                    uint64_t t65 = gm_l6;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l6 = t68;
                }
            }
            goto gm_ls69;
gm_ll47:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb2; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb4; }
            }
            goto gm_ld37_1;
gm_ls69:;
gm_lb2:;
        }
        {
            gm_trqfvmjtt4sij t70;
            gm_f_list_uvfu1k1((void *)&t70, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l9 = t70;
            gm_trqfvmjtt4sij t71;
            gm_f_list_uvfu1k1((void *)&t71, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l10 = t71;
            gm_trqfvmjtt4sij t72;
            gm_f_list_uvfu1k1((void *)&t72, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l11 = t72;
            void * t73 = gm_l0;
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)t73) + 16));
            gm_l12 = t74;
            {
                gm_node wn75 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlrddq2io9s1, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
                gm_b16_8 t78 = gm_l12;
                gm_b0_1 t79 = gm_f_lines_9uns0k2_s1hv6k0((void *)NULL, (&wn75), NULL, t78);
                if (gm_unw.kind) {
                    goto gm_ll77;
                }
            }
            goto gm_ls80;
gm_ll77:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we76; }
            goto gm_ld37_1;
gm_ls80:;
gm_lb9:;
we76:;
            void * t81 = gm_l0;
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)t81) + 32));
            gm_l13 = t82;
            {
                gm_node wn83 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlc7jtja1te5, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 2, .result = (void *)&F.w2, .fw = NULL, .next = cx};
                gm_b16_8 t86 = gm_l13;
                gm_b0_1 t87 = gm_f_lines_9uns0k2_sp8kip0((void *)NULL, (&wn83), NULL, t86);
                if (gm_unw.kind) {
                    goto gm_ll85;
                }
            }
            goto gm_ls88;
gm_ll85:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 2) { gm_unw.kind = GM_NONE; goto we84; }
            goto gm_ld37_1;
gm_ls88:;
gm_lb10:;
we84:;
            gm_trqfvmjtt4sij t89;
            gm_f_list_epvducg((void *)&t89, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l14 = t89;
            gm_b16_8 t90 = gm_l2;
            gm_l15 = t90;
            gm_l16 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t92 = gm_l16;
                    uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint8_t t94 = (t92 < t93);
                    if (!t94) {
                        goto gm_lb11;
                    }
                    {
                        void * t95 = GM_LD(void *, ((uint8_t *)&gm_l15));
                        uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint64_t t97 = gm_l16;
                        {
                            {
                                gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t95) + (uintptr_t)t97 * 16));
                                gm_l17 = t98;
                                gm_b16_8 t99 = gm_l17;
                                gm_f_inline_definition_6aokb9d((void *)&gm_l41, (void *)NULL, cx, NULL, t99, ((uint64_t)0ULL));
                                if (gm_unw.kind) {
                                    goto gm_ll91;
                                }
                                {
                                    {
                                        uint8_t t100 = (uint8_t)((((uint8_t *)&gm_l41))[32] != 0);
                                        if (t100) {
                                            goto gm_lb17;
                                        }
                                        gm_l40 = ((uint8_t)0);
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    {
                                        gm_l40 = ((uint8_t)1);
                                        goto gm_lb16;
                                    }
gm_lb18:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 143, 12);
                                }
gm_lb16:;
                                uint8_t t101 = gm_l40;
                                if (t101) {
                                    goto gm_lb13;
                                }
                                gm_b16_8 t102 = gm_l17;
                                gm_f_function_o2lv8g6((void *)&gm_l42, (void *)NULL, cx, NULL, t102);
                                if (gm_unw.kind) {
                                    goto gm_ll91;
                                }
                                uint8_t t103 = (uint8_t)((((uint8_t *)&gm_l42))[32] != 0);
                                if (t103) {
                                    memmove(((uint8_t *)&gm_l43), ((uint8_t *)&gm_l42), 32);
                                } else {
                                    gm_ttu09lij36f8c t104;
                                    memset(&t104, 0, sizeof t104);
                                    memmove(&F.ret, ((uint8_t *)&t104), 40);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                    goto gm_ll91;
                                }
                                memmove(((uint8_t *)&gm_l18), ((uint8_t *)&gm_l43), 32);
                                void * t105 = (void *)((uint8_t *)&F.l9);
                                gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                gm_tftb1odc739t5 t107 = gm_f_extend_0cs85gs(t105, t106);
                                gm_l44 = t107;
                                uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srccfhjr4r9mdef, 145, 9);
                                void * t109 = (void *)((uint8_t *)&F.l9);
                                gm_tftb1odc739t5 t110 = gm_f_push_t83rg2m(t109, ((uint8_t)10ULL));
                                gm_l45 = t110;
                                uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srccfhjr4r9mdef, 146, 9);
                                void * t112 = (void *)((uint8_t *)&F.l14);
                                gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 16));
                                gm_tftb1odc739t5 t114 = gm_f_push_3ef746b(t112, t113);
                                gm_l46 = t114;
                                uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srccfhjr4r9mdef, 147, 9);
                                goto gm_lb14;
                            }
gm_lb15:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 142, 5);
                        }
gm_lb14:;
                    }
gm_lb13:;
                    uint64_t t116 = gm_l16;
                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                    uint64_t t119 = r117;
                    gm_l16 = t119;
                }
            }
            goto gm_ls120;
gm_ll91:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb13; }
            }
            goto gm_ld37_1;
gm_ls120:;
gm_lb11:;
        }
        {
            void * t121 = gm_l0;
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)t121));
            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
            gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_tfeab279c16rr t125;
            GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t122);
            GM_ST(gm_b16_8, ((uint8_t *)&t125 + 16), t123);
            GM_ST(gm_b16_8, ((uint8_t *)&t125 + 32), t124);
            gm_f_format_c2lphrf((void *)&gm_l47, (void *)NULL, cx, NULL, &t125);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
            if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srccfhjr4r9mdef, 149, 15);
            gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
            gm_l19 = t127;
            gm_l20 = ((uint64_t)0ULL);
            gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
            gm_l21 = t128;
            gm_l22 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t130 = gm_l22;
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint8_t t132 = (t130 < t131);
                    if (!t132) {
                        goto gm_lb19;
                    }
                    {
                        void * t133 = GM_LD(void *, ((uint8_t *)&gm_l21));
                        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                        uint64_t t135 = gm_l22;
                        {
                            {
                                gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)t133) + (uintptr_t)t135 * 16));
                                gm_l23 = t136;
                                uint64_t t137 = gm_l20;
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                uint64_t r139; bool o140 = __builtin_add_overflow(t137, t138, &r139);
                                if (o140) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 151, 31);
                                uint64_t t141 = r139;
                                gm_l20 = t141;
                                goto gm_lb22;
                            }
gm_lb23:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 151, 5);
                        }
gm_lb22:;
                    }
gm_lb21:;
                    uint64_t t142 = gm_l22;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l22 = t145;
                }
            }
gm_lb19:;
            uint64_t t146 = gm_l20;
            uint64_t t147 = gm_l1;
            uint64_t t148 = (t147 > ((uint64_t)1ULL) ? t147 : ((uint64_t)1ULL));
            uint64_t r149; bool o150 = __builtin_add_overflow(t146, t148, &r149);
            if (o150) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 152, 16);
            uint64_t t151 = r149;
            uint64_t r152; bool o153 = __builtin_sub_overflow(t151, ((uint64_t)1ULL), &r152);
            if (o153) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 152, 16);
            uint64_t t154 = r152;
            uint64_t t155 = gm_l1;
            uint64_t t156 = (t155 > ((uint64_t)1ULL) ? t155 : ((uint64_t)1ULL));
            if (t156 == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 152, 15);
            uint64_t t157 = (uint64_t)(t154 / t156);
            uint64_t t158 = (t157 > ((uint64_t)1ULL) ? t157 : ((uint64_t)1ULL));
            gm_l24 = t158;
            gm_trqfvmjtt4sij t159;
            gm_f_list_lj5vg1g((void *)&t159, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l25 = t159;
            gm_trqfvmjtt4sij t160;
            gm_f_list_uvfu1k1((void *)&t160, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l26 = t160;
            void * t161 = (void *)((uint8_t *)&F.l26);
            void * t162 = gm_l0;
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)t162) + 48));
            gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
            gm_tfeab279c16rr t166;
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t163);
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 16), t164);
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 32), t165);
            gm_tftb1odc739t5 t167 = gm_f_write_qm3m7a0(t161, &t166);
            gm_l48 = t167;
            uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
            if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srccfhjr4r9mdef, 155, 5);
            void * t169 = (void *)((uint8_t *)&F.l25);
            gm_tftb1odc739t5 t170 = gm_f_push_97b8r3k(t169, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26)));
            gm_l49 = t170;
        }
        {
            uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
            if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srccfhjr4r9mdef, 156, 5);
            gm_l27 = ((uint64_t)0ULL);
            gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
            gm_l28 = t172;
            gm_l29 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t174 = gm_l29;
                    uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                    uint8_t t176 = (t174 < t175);
                    if (!t176) {
                        goto gm_lb24;
                    }
                    {
                        void * t177 = GM_LD(void *, ((uint8_t *)&gm_l28));
                        uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                        uint64_t t179 = gm_l29;
                        {
                            {
                                gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)t177) + (uintptr_t)t179 * 16));
                                gm_l30 = t180;
                                {
                                    {
                                        uint64_t t181 = gm_l27;
                                        uint64_t t182 = gm_l24;
                                        uint8_t t183 = (t181 >= t182);
                                        if (!t183) {
                                            goto gm_lb30;
                                        }
                                        uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                        uint64_t t185 = gm_l1;
                                        uint8_t t186 = (t184 < t185);
                                        if (!t186) {
                                            goto gm_lb30;
                                        }
                                        void * t187 = (void *)((uint8_t *)&F.l25);
                                        gm_trqfvmjtt4sij t188;
                                        gm_f_list_uvfu1k1((void *)&t188, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll173;
                                        }
                                        gm_tftb1odc739t5 t189 = gm_f_push_97b8r3k(t187, &t188);
                                        gm_l50 = t189;
                                        uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                        if (!t190) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srccfhjr4r9mdef, 160, 13);
                                        gm_l27 = ((uint64_t)0ULL);
                                        goto gm_lb29;
                                    }
gm_lb30:;
                                }
gm_lb29:;
                                uint64_t t191 = gm_l27;
                                uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                uint64_t r193; bool o194 = __builtin_add_overflow(t191, t192, &r193);
                                if (o194) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 163, 9);
                                uint64_t t195 = r193;
                                gm_l27 = t195;
                                void * t196 = GM_LD(void *, ((uint8_t *)&F.l25));
                                uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                uint64_t r199; bool o200 = __builtin_sub_overflow(t198, ((uint64_t)1ULL), &r199);
                                if (o200) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 164, 21);
                                uint64_t t201 = r199;
                                if ((uintptr_t)t201 >= (uintptr_t)(t197)) gm_trap_index((uintptr_t)t201, t197, gm_srccfhjr4r9mdef, 164, 9);
                                void * t202 = (void *)(((uint8_t *)t196) + (uintptr_t)t201 * 40);
                                gm_b16_8 t203 = gm_l30;
                                gm_tftb1odc739t5 t204 = gm_f_extend_0cs85gs(t202, t203);
                                gm_l51 = t204;
                                uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srccfhjr4r9mdef, 164, 9);
                                goto gm_lb27;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 158, 5);
                        }
gm_lb27:;
                    }
gm_lb26:;
                    uint64_t t206 = gm_l29;
                    uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                    uint64_t t209 = r207;
                    gm_l29 = t209;
                }
            }
            goto gm_ls210;
gm_ll173:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb24; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb26; }
            }
            goto gm_ld37_1;
gm_ls210:;
gm_lb24:;
            void * t211 = GM_LD(void *, ((uint8_t *)&F.l25));
            uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t212)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t212, gm_srccfhjr4r9mdef, 166, 5);
            void * t213 = (void *)(((uint8_t *)t211) + (uintptr_t)((uint64_t)0ULL) * 40);
            void * t214 = gm_l0;
            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)t214) + 80));
            gm_tftb1odc739t5 t216 = gm_f_extend_0cs85gs(t213, t215);
            gm_l52 = t216;
        }
        {
            uint8_t t217 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
            if (!t217) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srccfhjr4r9mdef, 166, 5);
            gm_trqfvmjtt4sij t218;
            gm_f_list_epvducg((void *)&t218, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld37_1;
            }
            F.l31 = t218;
            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
            gm_l32 = t219;
            gm_l33 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t221 = gm_l33;
                    uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                    uint8_t t223 = (t221 < t222);
                    if (!t223) {
                        goto gm_lb31;
                    }
                    {
                        void * t224 = GM_LD(void *, ((uint8_t *)&gm_l32));
                        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                        uint64_t t226 = gm_l33;
                        {
                            {
                                memmove(((uint8_t *)&F.l34), (((uint8_t *)t224) + (uintptr_t)t226 * 40), 40);
                                void * t227 = (void *)((uint8_t *)&F.l31);
                                gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                                gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
                                gm_tftb1odc739t5 t230 = gm_f_push_3ef746b(t227, t229);
                                gm_l53 = t230;
                                uint8_t t231 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                if (!t231) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srccfhjr4r9mdef, 168, 28);
                                goto gm_lb34;
                            }
gm_lb35:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 168, 5);
                        }
gm_lb34:;
                    }
gm_lb33:;
                    uint64_t t232 = gm_l33;
                    uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                    uint64_t t235 = r233;
                    gm_l33 = t235;
                }
            }
gm_lb31:;
            gm_b16_8 t236 = gm_l19;
            gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
            gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
            gm_t6j6smj7qoctu t239;
            GM_ST(gm_b16_8, ((uint8_t *)&t239 + 0), t237);
            GM_ST(gm_b16_8, ((uint8_t *)&t239 + 16), t238);
            gm_ttu09lij36f8c t240;
            GM_ST(uint8_t, (uint8_t *)&t240 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t240 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t240 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t240), t239);
            ((uint8_t *)&t240)[32] = 1;
            memmove(&F.ret, ((uint8_t *)&t240), 40);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld37_1;
        }
    }
    {
        void * t241 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_lehld1n(t241);
    }
    goto gm_ls242;
gm_ld37_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t243 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_lehld1n(t243);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls242:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_text_mmejmp8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_unit_text_mmejmp8 F;
    void * gm_l1; /* by_name */
    gm_b16_8 gm_l2; /* nameless */
    gm_b16_8 gm_l3; /* body */
    gm_b16_8 gm_l4; /* members */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* w */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* k */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* r */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* k */
    gm_b16_8 gm_l21; /* refs */
    uint8_t gm_l22; /* all */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* r */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* k */
    uint8_t gm_l30; /* t */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* k */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l40; /* m */
    GM_STACK_CHECK(&F, 2608u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_th2lqbo77pvsc gm_l43;
    uint8_t gm_l44;
    gm_ttsm01t0d8srg gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tqetrgo0q24b5 gm_l47;
    gm_trqfvmjtt4sij gm_l48;
    uint8_t gm_l49;
    gm_ttsm01t0d8srg gm_l50;
    gm_tftb1odc739t5 gm_l51;
    uint8_t gm_l52;
    gm_tqetrgo0q24b5 gm_l53;
    uint8_t gm_l54;
    gm_ttsm01t0d8srg gm_l55;
    gm_tf5srh4mp4b3i gm_l56;
    gm_tftb1odc739t5 gm_l57;
    uint8_t gm_l58;
    gm_ttsm01t0d8srg gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    uint8_t gm_l62;
    gm_ttsm01t0d8srg gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tabb710d9mq2e gm_l65;
    {
        {
            gm_trqfvmjtt4sij t67;
            gm_f_list_s83jn2q((void *)&t67, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l5 = t67;
            gm_b16_8 t68 = F.l0;
            gm_l6 = t68;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t70 = gm_l7;
                    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t72 = (t70 < t71);
                    if (!t72) {
                        goto gm_lb2;
                    }
                    {
                        void * t73 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t75 = gm_l7;
                        {
                            {
                                void * t76 = (void *)((uint8_t *)&F.l5);
                                gm_tftb1odc739t5 t77 = gm_f_push_vd1290p(t76, ((uint8_t)0));
                                gm_l41 = t77;
                                uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srccfhjr4r9mdef, 656, 22);
                                goto gm_lb5;
                            }
gm_lb6:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 656, 5);
                        }
gm_lb5:;
                    }
gm_lb4:;
                    uint64_t t79 = gm_l7;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l7 = t82;
                }
            }
gm_lb2:;
            gm_t4c7ipjj9s3fp t83;
            gm_f_hash_map_m04clhi((void *)&t83, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l8 = t83;
            gm_trqfvmjtt4sij t84;
            gm_f_list_epvducg((void *)&t84, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld66_1;
            }
            F.l9 = t84;
            void * t85 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t86 = gm_l3;
            gm_b16_8 t87 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t86);
            if (gm_unw.kind) {
                goto gm_ld66_1;
            }
            gm_tftb1odc739t5 t88 = gm_f_extend_vogsd72(t85, t87);
            gm_l42 = t88;
            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srccfhjr4r9mdef, 660, 5);
            {
                for (;;) {
                    void * t91 = (void *)((uint8_t *)&F.l9);
                    gm_f_pop_vr08sii((void *)&gm_l43, t91);
                    uint8_t t92 = (uint8_t)((((uint8_t *)&gm_l43))[16] != 0);
                    if (!t92) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l43));
                    gm_l10 = t93;
                    {
                        void * t94 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t95 = gm_l10;
                        gm_ttsm01t0d8srg t96 = gm_f_get_ece4opn(t94, t95);
                        gm_l45 = t96;
                        {
                            {
                                uint8_t t97 = (uint8_t)((((uint8_t *)&gm_l45))[0] != 0);
                                if (t97) {
                                    goto gm_lb11;
                                }
                                gm_l44 = ((uint8_t)0);
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                gm_l44 = ((uint8_t)1);
                                goto gm_lb10;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 662, 12);
                        }
gm_lb10:;
                        uint8_t t98 = gm_l44;
                        if (t98) {
                            goto gm_lb9;
                        }
                        void * t99 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t100 = gm_l10;
                        gm_tftb1odc739t5 t101 = gm_f_put_drsjd1t(t99, t100, GM_ZERO(gm_b0_1));
                        gm_l46 = t101;
                        uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                        if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srccfhjr4r9mdef, 663, 9);
                        void * t103 = gm_l1;
                        gm_b16_8 t104 = gm_l10;
                        gm_f_get_ohh8mo3((void *)&gm_l47, t103, t104);
                        uint8_t t105 = (uint8_t)((((uint8_t *)&gm_l47))[40] != 0);
                        if (t105) {
                            memmove(((uint8_t *)&gm_l48), ((uint8_t *)&gm_l47), 40);
                        } else {
                            goto gm_lb9;
                        }
                        memmove(((uint8_t *)&F.l11), ((uint8_t *)&gm_l48), 40);
                        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                        gm_l12 = t106;
                        gm_l13 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t108 = gm_l13;
                                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint8_t t110 = (t108 < t109);
                                if (!t110) {
                                    goto gm_lb13;
                                }
                                {
                                    void * t111 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint64_t t113 = gm_l13;
                                    {
                                        {
                                            uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)t111) + (uintptr_t)t113 * 8));
                                            gm_l14 = t114;
                                            void * t115 = GM_LD(void *, ((uint8_t *)&F.l5));
                                            uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                            uint64_t t117 = gm_l14;
                                            if ((uintptr_t)t117 >= (uintptr_t)(t116)) gm_trap_index((uintptr_t)t117, t116, gm_srccfhjr4r9mdef, 666, 16);
                                            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)t115) + (uintptr_t)t117 * 1));
                                            if (t118) {
                                                goto gm_lb15;
                                            }
                                            void * t119 = GM_LD(void *, ((uint8_t *)&F.l5));
                                            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                            uint64_t t121 = gm_l14;
                                            if ((uintptr_t)t121 >= (uintptr_t)(t120)) gm_trap_index((uintptr_t)t121, t120, gm_srccfhjr4r9mdef, 667, 13);
                                            GM_ST(uint8_t, (((uint8_t *)t119) + (uintptr_t)t121 * 1), ((uint8_t)1));
                                            void * t122 = GM_LD(void *, ((uint8_t *)&F.l0));
                                            uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
                                            uint64_t t124 = gm_l14;
                                            if ((uintptr_t)t124 >= (uintptr_t)(t123)) gm_trap_index((uintptr_t)t124, t123, gm_srccfhjr4r9mdef, 668, 22);
                                            gm_b16_8 t125 = GM_LD(gm_b16_8, ((((uint8_t *)t122) + (uintptr_t)t124 * 56) + 32));
                                            gm_l15 = t125;
                                            gm_l16 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t127 = gm_l16;
                                                    uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                    uint8_t t129 = (t127 < t128);
                                                    if (!t129) {
                                                        goto gm_lb18;
                                                    }
                                                    {
                                                        void * t130 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                        uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                        uint64_t t132 = gm_l16;
                                                        {
                                                            {
                                                                gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)t130) + (uintptr_t)t132 * 16));
                                                                gm_l17 = t133;
                                                                void * t134 = (void *)((uint8_t *)&F.l8);
                                                                gm_b16_8 t135 = gm_l17;
                                                                gm_ttsm01t0d8srg t136 = gm_f_get_ece4opn(t134, t135);
                                                                gm_l50 = t136;
                                                                {
                                                                    {
                                                                        uint8_t t137 = (uint8_t)((((uint8_t *)&gm_l50))[0] != 0);
                                                                        if (t137) {
                                                                            goto gm_lb24;
                                                                        }
                                                                        gm_l49 = ((uint8_t)1);
                                                                        goto gm_lb23;
                                                                    }
gm_lb24:;
                                                                    {
                                                                        gm_l49 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb25:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 668, 41);
                                                                }
gm_lb23:;
                                                                uint8_t t138 = gm_l49;
                                                                if (t138) {
                                                                    void * t139 = (void *)((uint8_t *)&F.l9);
                                                                    gm_b16_8 t140 = gm_l17;
                                                                    gm_tftb1odc739t5 t141 = gm_f_push_3ef746b(t139, t140);
                                                                    gm_l51 = t141;
                                                                    uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                                                    if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srccfhjr4r9mdef, 668, 66);
                                                                }
                                                                goto gm_lb21;
                                                            }
gm_lb22:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 668, 13);
                                                        }
gm_lb21:;
                                                    }
gm_lb20:;
                                                    uint64_t t143 = gm_l16;
                                                    uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                                                    uint64_t t146 = r144;
                                                    gm_l16 = t146;
                                                }
                                            }
gm_lb18:;
                                            goto gm_lb16;
                                        }
gm_lb17:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 665, 9);
                                    }
gm_lb16:;
                                }
gm_lb15:;
                                uint64_t t147 = gm_l13;
                                uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                                uint64_t t150 = r148;
                                gm_l13 = t150;
                            }
                        }
gm_lb13:;
                    }
gm_lb9:;
                }
            }
gm_lb7:;
        }
        {
            gm_b16_8 t151 = gm_l2;
            gm_l18 = t151;
            gm_l19 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t153 = gm_l19;
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint8_t t155 = (t153 < t154);
                    if (!t155) {
                        goto gm_lb26;
                    }
                    {
                        void * t156 = GM_LD(void *, ((uint8_t *)&gm_l18));
                        uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                        uint64_t t158 = gm_l19;
                        {
                            {
                                uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)t156) + (uintptr_t)t158 * 8));
                                gm_l20 = t159;
                                void * t160 = GM_LD(void *, ((uint8_t *)&F.l0));
                                uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
                                uint64_t t162 = gm_l20;
                                if ((uintptr_t)t162 >= (uintptr_t)(t161)) gm_trap_index((uintptr_t)t162, t161, gm_srccfhjr4r9mdef, 673, 17);
                                gm_b16_8 t163 = GM_LD(gm_b16_8, ((((uint8_t *)t160) + (uintptr_t)t162 * 56) + 32));
                                gm_l21 = t163;
                                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                uint8_t t165 = ((t164 == ((uint64_t)0ULL)));
                                if (t165) {
                                    goto gm_lb28;
                                }
                                gm_l22 = ((uint8_t)1);
                                gm_b16_8 t166 = gm_l21;
                                gm_l23 = t166;
                                gm_l24 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t168 = gm_l24;
                                        uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                        uint8_t t170 = (t168 < t169);
                                        if (!t170) {
                                            goto gm_lb31;
                                        }
                                        {
                                            void * t171 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                            uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                            uint64_t t173 = gm_l24;
                                            {
                                                {
                                                    gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)t171) + (uintptr_t)t173 * 16));
                                                    gm_l25 = t174;
                                                    {
                                                        {
                                                            void * t175 = gm_l1;
                                                            gm_b16_8 t176 = gm_l25;
                                                            gm_f_get_ohh8mo3((void *)&gm_l53, t175, t176);
                                                            {
                                                                {
                                                                    uint8_t t177 = (uint8_t)((((uint8_t *)&gm_l53))[40] != 0);
                                                                    if (t177) {
                                                                        goto gm_lb39;
                                                                    }
                                                                    gm_l52 = ((uint8_t)0);
                                                                    goto gm_lb38;
                                                                }
gm_lb39:;
                                                                {
                                                                    gm_l52 = ((uint8_t)1);
                                                                    goto gm_lb38;
                                                                }
gm_lb40:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 677, 16);
                                                            }
gm_lb38:;
                                                            uint8_t t178 = gm_l52;
                                                            if (!t178) {
                                                                goto gm_lb37;
                                                            }
                                                            void * t179 = (void *)((uint8_t *)&F.l8);
                                                            gm_b16_8 t180 = gm_l25;
                                                            gm_ttsm01t0d8srg t181 = gm_f_get_ece4opn(t179, t180);
                                                            gm_l55 = t181;
                                                            {
                                                                {
                                                                    uint8_t t182 = (uint8_t)((((uint8_t *)&gm_l55))[0] != 0);
                                                                    if (t182) {
                                                                        goto gm_lb42;
                                                                    }
                                                                    gm_l54 = ((uint8_t)1);
                                                                    goto gm_lb41;
                                                                }
gm_lb42:;
                                                                {
                                                                    gm_l54 = ((uint8_t)0);
                                                                    goto gm_lb41;
                                                                }
gm_lb43:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 677, 47);
                                                            }
gm_lb41:;
                                                            uint8_t t183 = gm_l54;
                                                            if (!t183) {
                                                                goto gm_lb37;
                                                            }
                                                            gm_l22 = ((uint8_t)0);
                                                            goto gm_lb36;
                                                        }
gm_lb37:;
                                                    }
gm_lb36:;
                                                    goto gm_lb34;
                                                }
gm_lb35:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 676, 9);
                                            }
gm_lb34:;
                                        }
gm_lb33:;
                                        uint64_t t184 = gm_l24;
                                        uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                                        uint64_t t187 = r185;
                                        gm_l24 = t187;
                                    }
                                }
gm_lb31:;
                                uint8_t t188 = gm_l22;
                                if (t188) {
                                    void * t189 = GM_LD(void *, ((uint8_t *)&F.l5));
                                    uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                    uint64_t t191 = gm_l20;
                                    if ((uintptr_t)t191 >= (uintptr_t)(t190)) gm_trap_index((uintptr_t)t191, t190, gm_srccfhjr4r9mdef, 679, 18);
                                    GM_ST(uint8_t, (((uint8_t *)t189) + (uintptr_t)t191 * 1), ((uint8_t)1));
                                }
                                goto gm_lb29;
                            }
gm_lb30:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 672, 5);
                        }
gm_lb29:;
                    }
gm_lb28:;
                    uint64_t t192 = gm_l19;
                    uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                    uint64_t t195 = r193;
                    gm_l19 = t195;
                }
            }
gm_lb26:;
        }
        {
            gm_trqfvmjtt4sij t196;
            gm_f_list_5dhgg4i((void *)&t196, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld66_1;
            }
            F.l26 = t196;
            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
            gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
            gm_l27 = t198;
            gm_l28 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t200 = gm_l28;
                    uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint8_t t202 = (t200 < t201);
                    if (!t202) {
                        goto gm_lb44;
                    }
                    {
                        uint64_t t203 = gm_l28;
                        void * t204 = GM_LD(void *, ((uint8_t *)&gm_l27));
                        uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                        uint64_t t206 = gm_l28;
                        uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)t204) + (uintptr_t)t206 * 1));
                        gm_tf5srh4mp4b3i t208;
                        GM_ST(uint8_t, (uint8_t *)&t208 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t208 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t208 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t208 + 0), t203);
                        GM_ST(uint8_t, ((uint8_t *)&t208 + 8), t207);
                        gm_l56 = t208;
                        {
                            {
                                uint64_t t209 = GM_LD(uint64_t, ((uint8_t *)&gm_l56));
                                gm_l29 = t209;
                                uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l56) + 8));
                                gm_l30 = t210;
                                uint8_t t211 = gm_l30;
                                if (t211) {
                                    void * t212 = (void *)((uint8_t *)&F.l26);
                                    uint64_t t213 = gm_l29;
                                    gm_tftb1odc739t5 t214 = gm_f_push_ucgmlo2(t212, t213);
                                    gm_l57 = t214;
                                    uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                    if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srccfhjr4r9mdef, 682, 49);
                                }
                                goto gm_lb47;
                            }
gm_lb48:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 682, 5);
                        }
gm_lb47:;
                    }
gm_lb46:;
                    uint64_t t216 = gm_l28;
                    uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                    uint64_t t219 = r217;
                    gm_l28 = t219;
                }
            }
gm_lb44:;
            gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
            gm_closure t221 = ((gm_closure){(void *)gm_f_fn_575gb0c, (void *)&F.h, cx});
            gm_f_sort_by_08k4kf2(t220, t221);
            if (gm_unw.kind) {
                goto gm_ld66_1;
            }
            gm_trqfvmjtt4sij t222;
            gm_f_list_uvfu1k1((void *)&t222, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld66_1;
            }
            F.l31 = t222;
            gm_t4c7ipjj9s3fp t223;
            gm_f_hash_map_m04clhi((void *)&t223, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld66_1;
            }
            F.l32 = t223;
            gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
            gm_l33 = t224;
            gm_l34 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t226 = gm_l34;
                    uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint8_t t228 = (t226 < t227);
                    if (!t228) {
                        goto gm_lb49;
                    }
                    {
                        void * t229 = GM_LD(void *, ((uint8_t *)&gm_l33));
                        uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                        uint64_t t231 = gm_l34;
                        {
                            {
                                uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)t229) + (uintptr_t)t231 * 8));
                                gm_l35 = t232;
                                void * t233 = (void *)((uint8_t *)&F.l32);
                                void * t234 = GM_LD(void *, ((uint8_t *)&F.l0));
                                uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
                                uint64_t t236 = gm_l35;
                                if ((uintptr_t)t236 >= (uintptr_t)(t235)) gm_trap_index((uintptr_t)t236, t235, gm_srccfhjr4r9mdef, 689, 21);
                                gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)t234) + (uintptr_t)t236 * 56));
                                gm_ttsm01t0d8srg t238 = gm_f_get_ece4opn(t233, t237);
                                gm_l59 = t238;
                                {
                                    {
                                        uint8_t t239 = (uint8_t)((((uint8_t *)&gm_l59))[0] != 0);
                                        if (t239) {
                                            goto gm_lb55;
                                        }
                                        gm_l58 = ((uint8_t)0);
                                        goto gm_lb54;
                                    }
gm_lb55:;
                                    {
                                        gm_l58 = ((uint8_t)1);
                                        goto gm_lb54;
                                    }
gm_lb56:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 689, 12);
                                }
gm_lb54:;
                                uint8_t t240 = gm_l58;
                                if (t240) {
                                    goto gm_lb51;
                                }
                                void * t241 = (void *)((uint8_t *)&F.l32);
                                void * t242 = GM_LD(void *, ((uint8_t *)&F.l0));
                                uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
                                uint64_t t244 = gm_l35;
                                if ((uintptr_t)t244 >= (uintptr_t)(t243)) gm_trap_index((uintptr_t)t244, t243, gm_srccfhjr4r9mdef, 690, 18);
                                gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)t242) + (uintptr_t)t244 * 56));
                                gm_tftb1odc739t5 t246 = gm_f_put_drsjd1t(t241, t245, GM_ZERO(gm_b0_1));
                                gm_l60 = t246;
                                uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                if (!t247) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srccfhjr4r9mdef, 690, 9);
                                void * t248 = (void *)((uint8_t *)&F.l31);
                                void * t249 = GM_LD(void *, ((uint8_t *)&F.l0));
                                uint64_t t250 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
                                uint64_t t251 = gm_l35;
                                if ((uintptr_t)t251 >= (uintptr_t)(t250)) gm_trap_index((uintptr_t)t251, t250, gm_srccfhjr4r9mdef, 691, 21);
                                gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)t249) + (uintptr_t)t251 * 56));
                                gm_tftb1odc739t5 t253 = gm_f_extend_0cs85gs(t248, t252);
                                gm_l61 = t253;
                                uint8_t t254 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                if (!t254) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srccfhjr4r9mdef, 691, 9);
                                goto gm_lb52;
                            }
gm_lb53:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 687, 5);
                        }
gm_lb52:;
                    }
gm_lb51:;
                    uint64_t t255 = gm_l34;
                    uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                    uint64_t t258 = r256;
                    gm_l34 = t258;
                }
            }
gm_lb49:;
        }
        {
            gm_trqfvmjtt4sij t259;
            gm_f_list_epvducg((void *)&t259, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld66_2;
            }
            F.l36 = t259;
            uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t261 = (t260 > ((uint64_t)0ULL));
            if (t261) {
                gm_b16_8 t262 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
                gm_t4c7ipjj9s3fp t264;
                gm_f_ident_set_9ajalpn((void *)&t264, (void *)NULL, cx, NULL, t263);
                if (gm_unw.kind) {
                    goto gm_ld66_2;
                }
                F.l37 = t264;
                gm_b16_8 t265 = gm_l4;
                gm_l38 = t265;
                gm_l39 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t267 = gm_l39;
                        uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                        uint8_t t269 = (t267 < t268);
                        if (!t269) {
                            goto gm_lb57;
                        }
                        {
                            void * t270 = GM_LD(void *, ((uint8_t *)&gm_l38));
                            uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                            uint64_t t272 = gm_l39;
                            {
                                {
                                    gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)t270) + (uintptr_t)t272 * 16));
                                    gm_l40 = t273;
                                    void * t274 = (void *)((uint8_t *)&F.l37);
                                    gm_b16_8 t275 = gm_l40;
                                    gm_ttsm01t0d8srg t276 = gm_f_get_ece4opn(t274, t275);
                                    gm_l63 = t276;
                                    {
                                        {
                                            uint8_t t277 = (uint8_t)((((uint8_t *)&gm_l63))[0] != 0);
                                            if (t277) {
                                                goto gm_lb63;
                                            }
                                            gm_l62 = ((uint8_t)0);
                                            goto gm_lb62;
                                        }
gm_lb63:;
                                        {
                                            gm_l62 = ((uint8_t)1);
                                            goto gm_lb62;
                                        }
gm_lb64:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 696, 31);
                                    }
gm_lb62:;
                                    uint8_t t278 = gm_l62;
                                    if (t278) {
                                        void * t279 = (void *)((uint8_t *)&F.l36);
                                        gm_b16_8 t280 = gm_l40;
                                        gm_tftb1odc739t5 t281 = gm_f_push_3ef746b(t279, t280);
                                        gm_l64 = t281;
                                        uint8_t t282 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                        if (!t282) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srccfhjr4r9mdef, 696, 59);
                                    }
                                    goto gm_lb60;
                                }
gm_lb61:;
                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 696, 9);
                            }
gm_lb60:;
                        }
gm_lb59:;
                        uint64_t t283 = gm_l39;
                        uint64_t r284; bool o285 = __builtin_add_overflow(t283, ((uint64_t)1ULL), &r284);
                        uint64_t t286 = r284;
                        gm_l39 = t286;
                    }
                }
gm_lb57:;
            }
            gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&F.l36));
            gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
            gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
            gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
            gm_b16_8 t291 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t288, t290);
            if (gm_unw.kind) {
                goto gm_ld66_2;
            }
            gm_b16_8 t292 = gm_l3;
            gm_t6j6smj7qoctu t293;
            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 0), t291);
            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 16), t292);
            gm_f_format_8n626pi((void *)&gm_l65, (void *)NULL, cx, NULL, &t293);
            if (gm_unw.kind) {
                goto gm_ld66_2;
            }
            uint8_t t294 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
            if (!t294) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srccfhjr4r9mdef, 698, 12);
        }
        {
            gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
            gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
            F.ret = t296;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld66_2;
        }
    }
    {
        void * t297 = (void *)((uint8_t *)&F.l32);
        gm_f_deinit_lehld1n(t297);
    }
    {
        void * t298 = (void *)((uint8_t *)&F.l8);
        gm_f_deinit_lehld1n(t298);
    }
    goto gm_ls299;
gm_ld66_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t300 = (void *)((uint8_t *)&F.l32);
        gm_f_deinit_lehld1n(t300);
        gm_unw = gm_saved;
    }
gm_ld66_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t301 = (void *)((uint8_t *)&F.l8);
        gm_f_deinit_lehld1n(t301);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls299:;
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
