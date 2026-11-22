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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csp8136jfauorf { unsigned __int128 f0; _Bool f1; _Bool f2; } gm_csp8136jfauorf;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t f2; uint8_t pe[14]; } gm_t18m13lmi7mp6;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csp8136jfauorf) == 32 && _Alignof(gm_csp8136jfauorf) == 16 && offsetof(gm_csp8136jfauorf, f0) == 0 && offsetof(gm_csp8136jfauorf, f1) == 16 && offsetof(gm_csp8136jfauorf, f2) == 17, "layout of `Arith`");
_Static_assert(sizeof(gm_t18m13lmi7mp6) == 32 && _Alignof(gm_t18m13lmi7mp6) == 16 && offsetof(gm_t18m13lmi7mp6, f0) == 0 && offsetof(gm_t18m13lmi7mp6, f1) == 16 && offsetof(gm_t18m13lmi7mp6, f2) == 17, "layout of `Arith`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint32_t v1; } f1; } gm_tcula66ipo514;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tcula66ipo514) == 32 && _Alignof(gm_tcula66ipo514) == 8 && offsetof(gm_tcula66ipo514, f0) == 0 && offsetof(gm_tcula66ipo514, f1) == 8, "layout of `Flow!u32`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
struct gm_fr_checked_overflow_ma1bm1g {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_div_zero_trap_ap1saqd {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_finish_int_jmjdqd4 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_format_bl27tn4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eruojoe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jp4udcs {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nacqkaa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra7pont {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_int_arith_is3k5b1 {
    gm_frame h;
    gm_t18m13lmi7mp6 ret;
};
struct gm_fr_int_binop_qrbogd9 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_cmp_kb3sm6u {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_int_hi_qa5fgoo {
    gm_frame h;
    __int128 ret;
};
struct gm_fr_int_lo_i8jbm6e {
    gm_frame h;
    __int128 ret;
};
struct gm_fr_int_pow_wrapped_5mjnoh4 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_int_shl_b8ne661 {
    gm_frame h;
    gm_t18m13lmi7mp6 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_overflow_result_vqlhh7t {
    gm_frame h;
    gm_tdlro53ljvkl0 ret;
};
struct gm_fr_overflow_trap_j2f8u18 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_pow_7je2uvi {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_sat_bits_ug4cjjm {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_shift_amount_aalipud {
    gm_frame h;
    gm_tcula66ipo514 ret;
};
struct gm_fr_shift_check_a7uaho4 {
    gm_frame h;
    gm_tcula66ipo514 ret;
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
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_finish_int_jmjdqd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, const void * a3, gm_t88rcucg1b7uk a4, gm_ttovhfee8mf28 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_bl27tn4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jp4udcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mnuav4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nacqkaa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_int_arith_is3k5b1(void *gm_out, gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4);
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_arith_is3k5b1_c(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4);
__attribute__((aligned(4))) void gm_f_int_binop_qrbogd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __int128 gm_f_int_hi_qa5fgoo(uint32_t a0);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __int128 gm_f_int_lo_i8jbm6e(uint32_t a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_int_pow_wrapped_5mjnoh4(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_int_shl_b8ne661(void *gm_out, uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3);
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_shl_b8ne661_c(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_overflow_result_vqlhh7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, unsigned __int128 a5, uint8_t a6, gm_b16_8 a7);
inline __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_int_iuioodt(void * a0, gm_ttovhfee8mf28 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_sat_bits_ug4cjjm(uint8_t a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_shift_amount_aalipud(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint32_t a5);
__attribute__((aligned(4))) void gm_f_shift_check_a7uaho4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, uint8_t a3, unsigned __int128 a4, uint32_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_wr_uint_rs9ado5(void * a0, gm_b16_8 a1, unsigned __int128 a2);
extern const uint8_t gm_s0k8sea1jhldg[21];
extern const uint8_t gm_s0q6kpmhsprri[33];
extern const uint8_t gm_s2hnapin8228t[21];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s5cmg64f6av8k[70];
extern const uint8_t gm_s5k1c58je5d4s[25];
extern const uint8_t gm_s9idfla83gb22[59];
extern const uint8_t gm_s9r2a03bnhj06[58];
extern const uint8_t gm_sckeklr60tlq3[34];
extern const uint8_t gm_sd70ob31745fn[4];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcqod65l394oc8[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_tmpl gm_tpl0j5bgvjcbn0u;
extern const gm_tmpl gm_tpl9qg0hlqqke7e;
extern const gm_tmpl gm_tplgqr018o660ue;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplrpvalpqjvipe;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t2) + 88));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t5 = (uint8_t)((t4 == ((uint8_t)3ULL)));
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)3ULL));
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l0));
    gm_l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = gm_l3;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l4;
                        gm_l5 = t12;
                        void * t13 = gm_l2;
                        uint64_t t14 = gm_l5;
                        uint8_t t15 = gm_l1;
                        GM_ST(uint8_t, (((uint8_t *)t13) + (uintptr_t)t14 * 1), t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l4;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l4 = t19;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_bl27tn4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bl27tn4 F;
    gm_tommf4j9upomc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sckeklr60tlq3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0j5bgvjcbn0u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jp4udcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jp4udcs F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0q6kpmhsprri));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrpvalpqjvipe), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nacqkaa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0) {
    struct gm_fr_format_nacqkaa F;
    gm_tsn2omssg6cpf gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tsn2omssg6cpf gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tsn2omssg6cpf t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd70ob31745fn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_mnuav4d((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9qg0hlqqke7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0) {
    struct gm_fr_format_ra7pont F;
    gm_tsn2omssg6cpf gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tsn2omssg6cpf gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tsn2omssg6cpf t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_mnuav4d((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) unsigned __int128 gm_f_sat_bits_ug4cjjm(uint8_t a0, uint32_t a1, uint8_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sat_bits_ug4cjjm F;
    uint8_t gm_l0; /* signed */
    uint32_t gm_l1; /* bits */
    uint8_t gm_l2; /* below */
    GM_STACK_CHECK_SHALLOW(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    __int128 gm_l3;
    unsigned __int128 gm_l4;
    uint8_t t2 = gm_l0;
    if (t2) {
        uint8_t t3 = gm_l2;
        if (t3) {
            uint32_t t4 = gm_l1;
            __int128 t5 = gm_f_int_lo_i8jbm6e(t4);
            gm_l3 = t5;
        } else {
            uint32_t t6 = gm_l1;
            __int128 t7 = gm_f_int_hi_qa5fgoo(t6);
            gm_l3 = t7;
        }
        __int128 t8 = gm_l3;
        unsigned __int128 t9;
        memset(&t9, 0, sizeof t9); memcpy(&t9, ((uint8_t *)&t8), sizeof t9 < 16 ? sizeof t9 : 16);
        F.ret = t9;
        goto gm_fret;
    }
    uint8_t t10 = gm_l2;
    if (t10) {
        gm_l4 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
    } else {
        uint32_t t11 = gm_l1;
        unsigned __int128 t12 = gm_f_num_mask_do3naah(t11);
        gm_l4 = t12;
    }
    unsigned __int128 t13 = gm_l4;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_checked_overflow_ma1bm1g F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* sym */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_f_fast_23em7sv(t2);
    if (t3) {
        gm_tcfa6l7reoosn t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
        ((uint8_t *)&t4)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 32);
        goto gm_fdone;
    }
    void * t5 = gm_l0;
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_b16_8 t7 = gm_l2;
    gm_t76123gfet73g t8;
    gm_f_overflow_trap_j2f8u18((void *)&t8, (void *)NULL, cx, NULL, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tcfa6l7reoosn t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    ((uint8_t *)&t9)[0] = 1;
    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t9 + 8), t8);
    memmove(gm_out, ((uint8_t *)&t9), 32);
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
extern __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_finish_int_jmjdqd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, const void * a3, gm_t88rcucg1b7uk a4, gm_ttovhfee8mf28 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7, gm_b16_8 a8) {
    struct gm_fr_finish_int_jmjdqd4 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_t18m13lmi7mp6 gm_l3; /* r */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* sym */
    gm_t88rcucg1b7uk gm_l7; /* res_ty */
    gm_b16_8 gm_l8; /* dst */
    unsigned __int128 gm_l9; /* low */
    gm_t51b2en4qp02s gm_l10; /* pb */
    uint64_t gm_l11; /* n */
    GM_STACK_CHECK(&F, 747u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    gm_tdlro53ljvkl0 gm_l12;
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
    unsigned __int128 t7 = GM_LD(unsigned __int128, ((uint8_t *)a3));
    unsigned __int128 t8 = gm_f_num_ext_348kmbu(t5, t6, t7);
    gm_l9 = t8;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)3ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a3) + 16));
            if (t11) {
                gm_b16_8 t12 = gm_l8;
                gm_f_fill_abmf5g5(t12, ((uint8_t)0ULL));
                gm_tcfa6l7reoosn t13;
                GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
                ((uint8_t *)&t13)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t13), 32);
                goto gm_fdone;
            }
            void * t14 = gm_l0;
            gm_t88rcucg1b7uk t15 = gm_l4;
            uint64_t t16 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t17 = gm_f_us_cuv01fp(t16);
            gm_l11 = t17;
            void * t18 = gm_l0;
            void * t19 = (void *)((uint8_t *)&gm_l10);
            uint64_t t20 = gm_l11;
            uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t22 = (uintptr_t)t20;
            if (t21 > t22 || t22 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t21, t22, ((uint64_t)16ULL), gm_srcqod65l394oc8, 149, 24);
            uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t19 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
            unsigned __int128 t25 = gm_l9;
            gm_f_wr_uint_rs9ado5(t18, t24, t25);
            void * t26 = gm_l0;
            gm_t88rcucg1b7uk t27 = gm_l7;
            gm_b16_8 t28 = gm_l8;
            void * t29 = (void *)((uint8_t *)&gm_l10);
            uint64_t t30 = gm_l11;
            uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t32 = (uintptr_t)t30;
            if (t31 > t32 || t32 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t31, t32, ((uint64_t)16ULL), gm_srcqod65l394oc8, 150, 38);
            uint64_t t33 = ((uintptr_t)t30) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t29 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33));
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            gm_b0_1 t36 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t26, t27, t28, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)a3) + 16));
            uint8_t t38 = (uint8_t)!t37;
            if (t38) {
                void * t39 = gm_l0;
                gm_b16_8 t40 = gm_l8;
                unsigned __int128 t41 = gm_l9;
                gm_f_wr_uint_rs9ado5(t39, t40, t41);
                gm_tcfa6l7reoosn t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t42 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t42 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t42 + 24, 0);
                ((uint8_t *)&t42)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t42 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t42), 32);
                goto gm_fdone;
            }
            void * t43 = gm_l0;
            gm_b16_8 t44 = gm_l8;
            void * t45 = gm_l0;
            gm_tl874t2atnlh3 t46 = gm_l1;
            gm_t9qkh0rsooc8d t47 = gm_l2;
            uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
            uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
            unsigned __int128 t50 = gm_l9;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)a3) + 17));
            gm_b16_8 t52 = gm_l6;
            gm_f_overflow_result_vqlhh7t((void *)&gm_l12, (void *)NULL, cx, NULL, t45, t46, t47, t48, t49, t50, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (t53) {
            } else {
                gm_t76123gfet73g t54 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l12) + 16));
                gm_tcfa6l7reoosn t55;
                GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                ((uint8_t *)&t55)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t55 + 8), t54);
                memmove(gm_out, ((uint8_t *)&t55), 32);
                goto gm_fdone;
            }
            unsigned __int128 t56 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l12) + 16));
            gm_f_wr_uint_rs9ado5(t43, t44, t56);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 141, 5);
    }
gm_lb1:;
    gm_tcfa6l7reoosn t57;
    GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t57 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t57 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t57 + 24, 0);
    ((uint8_t *)&t57)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t57 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t57), 32);
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
__attribute__((aligned(4))) void gm_f_int_arith_is3k5b1(void *gm_out, gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_arith_is3k5b1 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    uint8_t gm_l1; /* signed */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* a */
    unsigned __int128 gm_l4; /* c */
    unsigned __int128 gm_l5; /* mask */
    unsigned __int128 gm_l6; /* r */
    uint8_t gm_l7; /* over */
    unsigned __int128 gm_l8; /* p */
    unsigned __int128 gm_l9; /* p */
    uint8_t gm_l10; /* over */
    __int128 gm_l11; /* x */
    __int128 gm_l12; /* z */
    __int128 gm_l13; /* lo */
    __int128 gm_l14; /* hi */
    __int128 gm_l15; /* r */
    uint8_t gm_l16; /* over */
    __int128 gm_l17; /* r */
    uint8_t gm_l18; /* over */
    __int128 gm_l19; /* p */
    __int128 gm_l20; /* o */
    __int128 gm_l21; /* p */
    __int128 gm_l22; /* p */
    uint8_t gm_l23; /* over */
    GM_STACK_CHECK_SHALLOW(&F, 2847u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    __int128 gm_l34;
    uint8_t gm_l35;
    uint8_t gm_l36;
    uint8_t gm_l37;
    {
        uint8_t t10 = gm_l1;
        uint8_t t11 = (uint8_t)!t10;
        if (t11) {
            uint32_t t12 = gm_l2;
            unsigned __int128 t13 = gm_f_num_mask_do3naah(t12);
            gm_l5 = t13;
            {
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
                    if (!t15) {
                        goto gm_lb2;
                    }
                    unsigned __int128 t16 = gm_l3;
                    unsigned __int128 t17 = gm_l4;
                    unsigned __int128 r18; bool o19 = __builtin_add_overflow(t16, t17, &r18);
                    unsigned __int128 t20 = r18;
                    gm_l6 = t20;
                    uint32_t t21 = gm_l2;
                    uint8_t t22 = (t21 >= ((uint32_t)128ULL));
                    if (t22) {
                        unsigned __int128 t23 = gm_l6;
                        unsigned __int128 t24 = gm_l3;
                        uint8_t t25 = (t23 < t24);
                        gm_l24 = t25;
                    } else {
                        unsigned __int128 t26 = gm_l6;
                        unsigned __int128 t27 = gm_l5;
                        uint8_t t28 = (t26 > t27);
                        gm_l24 = t28;
                    }
                    uint8_t t29 = gm_l24;
                    gm_l7 = t29;
                    unsigned __int128 t30 = gm_l6;
                    unsigned __int128 t31 = gm_l5;
                    unsigned __int128 t32 = (unsigned __int128)(t30 & t31);
                    uint8_t t33 = gm_l7;
                    gm_t18m13lmi7mp6 t34;
                    GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
                    GM_ST(uint64_t, (uint8_t *)&t34 + 24, 0);
                    GM_ST(unsigned __int128, ((uint8_t *)&t34 + 0), t32);
                    GM_ST(uint8_t, ((uint8_t *)&t34 + 16), t33);
                    GM_ST(uint8_t, ((uint8_t *)&t34 + 17), ((uint8_t)0));
                    memmove(gm_out, ((uint8_t *)&t34), 32);
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                    if (!t36) {
                        goto gm_lb3;
                    }
                    unsigned __int128 t37 = gm_l3;
                    unsigned __int128 t38 = gm_l4;
                    unsigned __int128 r39; bool o40 = __builtin_sub_overflow(t37, t38, &r39);
                    unsigned __int128 t41 = r39;
                    unsigned __int128 t42 = gm_l5;
                    unsigned __int128 t43 = (unsigned __int128)(t41 & t42);
                    unsigned __int128 t44 = gm_l3;
                    unsigned __int128 t45 = gm_l4;
                    uint8_t t46 = (t44 < t45);
                    gm_t18m13lmi7mp6 t47;
                    GM_ST(uint16_t, (uint8_t *)&t47 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t47 + 20, 0);
                    GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
                    GM_ST(unsigned __int128, ((uint8_t *)&t47 + 0), t43);
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 16), t46);
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 17), ((uint8_t)1));
                    memmove(gm_out, ((uint8_t *)&t47), 32);
                    goto gm_fdone;
                }
gm_lb3:;
                {
                    uint32_t t48 = gm_l2;
                    uint8_t t49 = (t48 <= ((uint32_t)64ULL));
                    if (t49) {
                        unsigned __int128 t50 = gm_l3;
                        unsigned __int128 t51 = gm_l4;
                        unsigned __int128 r52; bool o53 = __builtin_mul_overflow(t50, t51, &r52);
                        if (o53) gm_trap("integer overflow in `*`", gm_srcqod65l394oc8, 82, 26);
                        unsigned __int128 t54 = r52;
                        gm_l8 = t54;
                        unsigned __int128 t55 = gm_l8;
                        unsigned __int128 t56 = gm_l5;
                        unsigned __int128 t57 = (unsigned __int128)(t55 & t56);
                        unsigned __int128 t58 = gm_l8;
                        unsigned __int128 t59 = gm_l5;
                        uint8_t t60 = (t58 > t59);
                        gm_t18m13lmi7mp6 t61;
                        GM_ST(uint16_t, (uint8_t *)&t61 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t61 + 20, 0);
                        GM_ST(uint64_t, (uint8_t *)&t61 + 24, 0);
                        GM_ST(unsigned __int128, ((uint8_t *)&t61 + 0), t57);
                        GM_ST(uint8_t, ((uint8_t *)&t61 + 16), t60);
                        GM_ST(uint8_t, ((uint8_t *)&t61 + 17), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t61), 32);
                        goto gm_fdone;
                    }
                    unsigned __int128 t62 = gm_l3;
                    unsigned __int128 t63 = gm_l4;
                    unsigned __int128 r64; bool o65 = __builtin_mul_overflow(t62, t63, &r64);
                    unsigned __int128 t66 = r64;
                    gm_l9 = t66;
                    unsigned __int128 t67 = gm_l3;
                    uint8_t t68 = (!(t67 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                    if (t68) {
                        unsigned __int128 t69 = gm_l9;
                        unsigned __int128 t70 = gm_l3;
                        if (t70 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 86, 41);
                        unsigned __int128 t71 = (unsigned __int128)(t69 / t70);
                        unsigned __int128 t72 = gm_l4;
                        uint8_t t73 = (!(t71 == t72));
                        if (t73) {
                            gm_l26 = ((uint8_t)1);
                        } else {
                            unsigned __int128 t74 = gm_l9;
                            unsigned __int128 t75 = gm_l5;
                            uint8_t t76 = (t74 > t75);
                            gm_l26 = t76;
                        }
                        uint8_t t77 = gm_l26;
                        gm_l25 = t77;
                    } else {
                        gm_l25 = ((uint8_t)0);
                    }
                    uint8_t t78 = gm_l25;
                    gm_l10 = t78;
                    unsigned __int128 t79 = gm_l9;
                    unsigned __int128 t80 = gm_l5;
                    unsigned __int128 t81 = (unsigned __int128)(t79 & t80);
                    uint8_t t82 = gm_l10;
                    gm_t18m13lmi7mp6 t83;
                    GM_ST(uint16_t, (uint8_t *)&t83 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t83 + 20, 0);
                    GM_ST(uint64_t, (uint8_t *)&t83 + 24, 0);
                    GM_ST(unsigned __int128, ((uint8_t *)&t83 + 0), t81);
                    GM_ST(uint8_t, ((uint8_t *)&t83 + 16), t82);
                    GM_ST(uint8_t, ((uint8_t *)&t83 + 17), ((uint8_t)0));
                    memmove(gm_out, ((uint8_t *)&t83), 32);
                    goto gm_fdone;
                }
gm_lb4:;
                gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 73, 9);
            }
gm_lb1:;
        }
    }
    {
        unsigned __int128 t84 = gm_l3;
        __int128 t85;
        memset(&t85, 0, sizeof t85); memcpy(&t85, ((uint8_t *)&t84), sizeof t85 < 16 ? sizeof t85 : 16);
        gm_l11 = t85;
        unsigned __int128 t86 = gm_l4;
        __int128 t87;
        memset(&t87, 0, sizeof t87); memcpy(&t87, ((uint8_t *)&t86), sizeof t87 < 16 ? sizeof t87 : 16);
        gm_l12 = t87;
        uint32_t t88 = gm_l2;
        __int128 t89 = gm_f_int_lo_i8jbm6e(t88);
        gm_l13 = t89;
        uint32_t t90 = gm_l2;
        __int128 t91 = gm_f_int_hi_qa5fgoo(t90);
        gm_l14 = t91;
        {
            {
                uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                uint8_t t93 = (uint8_t)(t92 == ((uint8_t)0ULL));
                if (!t93) {
                    goto gm_lb6;
                }
                __int128 t94 = gm_l11;
                __int128 t95 = gm_l12;
                __int128 r96; bool o97 = __builtin_add_overflow(t94, t95, &r96);
                __int128 t98 = r96;
                gm_l15 = t98;
                uint32_t t99 = gm_l2;
                uint8_t t100 = (t99 >= ((uint32_t)128ULL));
                if (t100) {
                    __int128 t101 = gm_l11;
                    __int128 t102 = gm_l15;
                    __int128 t103 = (__int128)(t101 ^ t102);
                    __int128 t104 = gm_l12;
                    __int128 t105 = gm_l15;
                    __int128 t106 = (__int128)(t104 ^ t105);
                    __int128 t107 = (__int128)(t103 & t106);
                    uint8_t t108 = (t107 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    gm_l27 = t108;
                } else {
                    __int128 t109 = gm_l15;
                    __int128 t110 = gm_l13;
                    uint8_t t111 = (t109 < t110);
                    if (t111) {
                        gm_l28 = ((uint8_t)1);
                    } else {
                        __int128 t112 = gm_l15;
                        __int128 t113 = gm_l14;
                        uint8_t t114 = (t112 > t113);
                        gm_l28 = t114;
                    }
                    uint8_t t115 = gm_l28;
                    gm_l27 = t115;
                }
                uint8_t t116 = gm_l27;
                gm_l16 = t116;
                uint32_t t117 = gm_l2;
                __int128 t118 = gm_l15;
                unsigned __int128 t119;
                memset(&t119, 0, sizeof t119); memcpy(&t119, ((uint8_t *)&t118), sizeof t119 < 16 ? sizeof t119 : 16);
                unsigned __int128 t120 = gm_f_num_ext_348kmbu(((uint8_t)1), t117, t119);
                uint8_t t121 = gm_l16;
                __int128 t122 = gm_l12;
                uint8_t t123 = (t122 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                gm_t18m13lmi7mp6 t124;
                GM_ST(uint16_t, (uint8_t *)&t124 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t124 + 20, 0);
                GM_ST(uint64_t, (uint8_t *)&t124 + 24, 0);
                GM_ST(unsigned __int128, ((uint8_t *)&t124 + 0), t120);
                GM_ST(uint8_t, ((uint8_t *)&t124 + 16), t121);
                GM_ST(uint8_t, ((uint8_t *)&t124 + 17), t123);
                memmove(gm_out, ((uint8_t *)&t124), 32);
                goto gm_fdone;
            }
gm_lb6:;
            {
                uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                uint8_t t126 = (uint8_t)(t125 == ((uint8_t)1ULL));
                if (!t126) {
                    goto gm_lb7;
                }
                __int128 t127 = gm_l11;
                __int128 t128 = gm_l12;
                __int128 r129; bool o130 = __builtin_sub_overflow(t127, t128, &r129);
                __int128 t131 = r129;
                gm_l17 = t131;
                uint32_t t132 = gm_l2;
                uint8_t t133 = (t132 >= ((uint32_t)128ULL));
                if (t133) {
                    __int128 t134 = gm_l11;
                    __int128 t135 = gm_l12;
                    __int128 t136 = (__int128)(t134 ^ t135);
                    __int128 t137 = gm_l11;
                    __int128 t138 = gm_l17;
                    __int128 t139 = (__int128)(t137 ^ t138);
                    __int128 t140 = (__int128)(t136 & t139);
                    uint8_t t141 = (t140 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    gm_l29 = t141;
                } else {
                    __int128 t142 = gm_l17;
                    __int128 t143 = gm_l13;
                    uint8_t t144 = (t142 < t143);
                    if (t144) {
                        gm_l30 = ((uint8_t)1);
                    } else {
                        __int128 t145 = gm_l17;
                        __int128 t146 = gm_l14;
                        uint8_t t147 = (t145 > t146);
                        gm_l30 = t147;
                    }
                    uint8_t t148 = gm_l30;
                    gm_l29 = t148;
                }
                uint8_t t149 = gm_l29;
                gm_l18 = t149;
                uint32_t t150 = gm_l2;
                __int128 t151 = gm_l17;
                unsigned __int128 t152;
                memset(&t152, 0, sizeof t152); memcpy(&t152, ((uint8_t *)&t151), sizeof t152 < 16 ? sizeof t152 : 16);
                unsigned __int128 t153 = gm_f_num_ext_348kmbu(((uint8_t)1), t150, t152);
                uint8_t t154 = gm_l18;
                __int128 t155 = gm_l12;
                uint8_t t156 = (t155 > ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                gm_t18m13lmi7mp6 t157;
                GM_ST(uint16_t, (uint8_t *)&t157 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t157 + 20, 0);
                GM_ST(uint64_t, (uint8_t *)&t157 + 24, 0);
                GM_ST(unsigned __int128, ((uint8_t *)&t157 + 0), t153);
                GM_ST(uint8_t, ((uint8_t *)&t157 + 16), t154);
                GM_ST(uint8_t, ((uint8_t *)&t157 + 17), t156);
                memmove(gm_out, ((uint8_t *)&t157), 32);
                goto gm_fdone;
            }
gm_lb7:;
            {
                uint32_t t158 = gm_l2;
                uint8_t t159 = (t158 <= ((uint32_t)63ULL));
                if (t159) {
                    __int128 t160 = gm_l11;
                    __int128 t161 = gm_l12;
                    __int128 r162; bool o163 = __builtin_mul_overflow(t160, t161, &r162);
                    if (o163) gm_trap("integer overflow in `*`", gm_srcqod65l394oc8, 108, 22);
                    __int128 t164 = r162;
                    gm_l19 = t164;
                    uint32_t t165 = gm_l2;
                    __int128 t166 = gm_l19;
                    unsigned __int128 t167;
                    memset(&t167, 0, sizeof t167); memcpy(&t167, ((uint8_t *)&t166), sizeof t167 < 16 ? sizeof t167 : 16);
                    unsigned __int128 t168 = gm_f_num_ext_348kmbu(((uint8_t)1), t165, t167);
                    __int128 t169 = gm_l19;
                    __int128 t170 = gm_l13;
                    uint8_t t171 = (t169 < t170);
                    if (t171) {
                        gm_l31 = ((uint8_t)1);
                    } else {
                        __int128 t172 = gm_l19;
                        __int128 t173 = gm_l14;
                        uint8_t t174 = (t172 > t173);
                        gm_l31 = t174;
                    }
                    uint8_t t175 = gm_l31;
                    __int128 t176 = gm_l19;
                    uint8_t t177 = (t176 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    gm_t18m13lmi7mp6 t178;
                    GM_ST(uint16_t, (uint8_t *)&t178 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t178 + 20, 0);
                    GM_ST(uint64_t, (uint8_t *)&t178 + 24, 0);
                    GM_ST(unsigned __int128, ((uint8_t *)&t178 + 0), t168);
                    GM_ST(uint8_t, ((uint8_t *)&t178 + 16), t175);
                    GM_ST(uint8_t, ((uint8_t *)&t178 + 17), t177);
                    memmove(gm_out, ((uint8_t *)&t178), 32);
                    goto gm_fdone;
                }
                __int128 t179 = gm_l11;
                uint8_t t180 = ((t179 == ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                if (t180) {
                    gm_l32 = ((uint8_t)1);
                } else {
                    __int128 t181 = gm_l12;
                    uint8_t t182 = ((t181 == ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                    gm_l32 = t182;
                }
                uint8_t t183 = gm_l32;
                if (t183) {
                    gm_t18m13lmi7mp6 t184;
                    GM_ST(uint16_t, (uint8_t *)&t184 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t184 + 20, 0);
                    GM_ST(uint64_t, (uint8_t *)&t184 + 24, 0);
                    GM_ST(unsigned __int128, ((uint8_t *)&t184 + 0), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    GM_ST(uint8_t, ((uint8_t *)&t184 + 16), ((uint8_t)0));
                    GM_ST(uint8_t, ((uint8_t *)&t184 + 17), ((uint8_t)0));
                    memmove(gm_out, ((uint8_t *)&t184), 32);
                    goto gm_fdone;
                }
                __int128 t185 = gm_l11;
                uint8_t t186 = ((t185 == ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 18446744073709551615ULL))));
                if (t186) {
                    gm_l33 = ((uint8_t)1);
                } else {
                    __int128 t187 = gm_l12;
                    uint8_t t188 = ((t187 == ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 18446744073709551615ULL))));
                    gm_l33 = t188;
                }
                uint8_t t189 = gm_l33;
                if (t189) {
                    __int128 t190 = gm_l11;
                    uint8_t t191 = ((t190 == ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 18446744073709551615ULL))));
                    if (t191) {
                        __int128 t192 = gm_l12;
                        gm_l34 = t192;
                    } else {
                        __int128 t193 = gm_l11;
                        gm_l34 = t193;
                    }
                    __int128 t194 = gm_l34;
                    gm_l20 = t194;
                    __int128 t195 = gm_l20;
                    __int128 t196 = gm_f_int_lo_i8jbm6e(((uint32_t)128ULL));
                    uint8_t t197 = ((t195 == t196));
                    if (t197) {
                        __int128 t198 = gm_l20;
                        unsigned __int128 t199;
                        memset(&t199, 0, sizeof t199); memcpy(&t199, ((uint8_t *)&t198), sizeof t199 < 16 ? sizeof t199 : 16);
                        gm_t18m13lmi7mp6 t200;
                        GM_ST(uint16_t, (uint8_t *)&t200 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t200 + 20, 0);
                        GM_ST(uint64_t, (uint8_t *)&t200 + 24, 0);
                        GM_ST(unsigned __int128, ((uint8_t *)&t200 + 0), t199);
                        GM_ST(uint8_t, ((uint8_t *)&t200 + 16), ((uint8_t)1));
                        GM_ST(uint8_t, ((uint8_t *)&t200 + 17), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t200), 32);
                        goto gm_fdone;
                    }
                    __int128 t201 = gm_l20;
                    if (t201 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 115, 22);
                    __int128 t202 = (__int128)((unsigned __int128)0 - (unsigned __int128)t201);
                    gm_l21 = t202;
                    uint32_t t203 = gm_l2;
                    __int128 t204 = gm_l21;
                    unsigned __int128 t205;
                    memset(&t205, 0, sizeof t205); memcpy(&t205, ((uint8_t *)&t204), sizeof t205 < 16 ? sizeof t205 : 16);
                    unsigned __int128 t206 = gm_f_num_ext_348kmbu(((uint8_t)1), t203, t205);
                    __int128 t207 = gm_l21;
                    __int128 t208 = gm_l13;
                    uint8_t t209 = (t207 < t208);
                    if (t209) {
                        gm_l35 = ((uint8_t)1);
                    } else {
                        __int128 t210 = gm_l21;
                        __int128 t211 = gm_l14;
                        uint8_t t212 = (t210 > t211);
                        gm_l35 = t212;
                    }
                    uint8_t t213 = gm_l35;
                    __int128 t214 = gm_l21;
                    uint8_t t215 = (t214 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    gm_t18m13lmi7mp6 t216;
                    GM_ST(uint16_t, (uint8_t *)&t216 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t216 + 20, 0);
                    GM_ST(uint64_t, (uint8_t *)&t216 + 24, 0);
                    GM_ST(unsigned __int128, ((uint8_t *)&t216 + 0), t206);
                    GM_ST(uint8_t, ((uint8_t *)&t216 + 16), t213);
                    GM_ST(uint8_t, ((uint8_t *)&t216 + 17), t215);
                    memmove(gm_out, ((uint8_t *)&t216), 32);
                    goto gm_fdone;
                }
                __int128 t217 = gm_l11;
                __int128 t218 = gm_l12;
                __int128 r219; bool o220 = __builtin_mul_overflow(t217, t218, &r219);
                __int128 t221 = r219;
                gm_l22 = t221;
                __int128 t222 = gm_l22;
                __int128 t223 = gm_l11;
                if (t223 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 119, 21);
                if (t222 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t223 == (__int128)-1) gm_trap("integer overflow in `/`", gm_srcqod65l394oc8, 119, 21);
                __int128 t224 = (__int128)(t222 / t223);
                __int128 t225 = gm_l12;
                uint8_t t226 = (!(t224 == t225));
                if (t226) {
                    gm_l37 = ((uint8_t)1);
                } else {
                    __int128 t227 = gm_l22;
                    __int128 t228 = gm_l13;
                    uint8_t t229 = (t227 < t228);
                    gm_l37 = t229;
                }
                uint8_t t230 = gm_l37;
                if (t230) {
                    gm_l36 = ((uint8_t)1);
                } else {
                    __int128 t231 = gm_l22;
                    __int128 t232 = gm_l14;
                    uint8_t t233 = (t231 > t232);
                    gm_l36 = t233;
                }
                uint8_t t234 = gm_l36;
                gm_l23 = t234;
                uint32_t t235 = gm_l2;
                __int128 t236 = gm_l22;
                unsigned __int128 t237;
                memset(&t237, 0, sizeof t237); memcpy(&t237, ((uint8_t *)&t236), sizeof t237 < 16 ? sizeof t237 : 16);
                unsigned __int128 t238 = gm_f_num_ext_348kmbu(((uint8_t)1), t235, t237);
                uint8_t t239 = gm_l23;
                __int128 t240 = gm_l11;
                uint8_t t241 = (t240 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                __int128 t242 = gm_l12;
                uint8_t t243 = (t242 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                uint8_t t244 = (!(t241 == t243));
                gm_t18m13lmi7mp6 t245;
                GM_ST(uint16_t, (uint8_t *)&t245 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t245 + 20, 0);
                GM_ST(uint64_t, (uint8_t *)&t245 + 24, 0);
                GM_ST(unsigned __int128, ((uint8_t *)&t245 + 0), t238);
                GM_ST(uint8_t, ((uint8_t *)&t245 + 16), t239);
                GM_ST(uint8_t, ((uint8_t *)&t245 + 17), t244);
                memmove(gm_out, ((uint8_t *)&t245), 32);
                goto gm_fdone;
            }
gm_lb8:;
            gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 95, 5);
        }
gm_lb5:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_arith_is3k5b1_c(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4) { gm_t18m13lmi7mp6 gm_r; gm_f_int_arith_is3k5b1(&gm_r, a0, a1, a2, a3, a4); return GM_CONV(gm_csp8136jfauorf, gm_r); }
__attribute__((aligned(4))) void gm_f_int_binop_qrbogd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8) {
    struct gm_fr_int_binop_qrbogd9 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_t88rcucg1b7uk gm_l4; /* rt */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* dst */
    gm_ttovhfee8mf28 gm_l9; /* info */
    unsigned __int128 gm_l10; /* a */
    gm_t9qkh0rsooc8d gm_l11; /* fl */
    gm_t9qkh0rsooc8d gm_l12; /* fl */
    gm_t9qkh0rsooc8d gm_l13; /* fl */
    unsigned __int128 gm_l14; /* c */
    gm_t9qkh0rsooc8d gm_l15; /* fl */
    uint32_t gm_l16; /* n */
    uint32_t gm_l17; /* n */
    unsigned __int128 gm_l18; /* c */
    uint8_t gm_l19; /* is_div */
    __int128 gm_l20; /* p */
    __int128 gm_l21; /* q */
    GM_STACK_CHECK(&F, 2463u);
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
    gm_l8 = a8;
    gm_tg5lql6itufk7 gm_l22;
    gm_ttovhfee8mf28 gm_l23;
    gm_tcula66ipo514 gm_l24;
    gm_tcula66ipo514 gm_l25;
    unsigned __int128 gm_l26;
    unsigned __int128 gm_l27;
    __int128 gm_l28;
    {
        void * t18 = gm_l0;
        gm_t88rcucg1b7uk t19 = gm_l3;
        gm_tg5lql6itufk7 t20 = gm_f_int_info_ulcevdv(t18, GM_CONV(gm_csguankko4lmhd, t19));
        gm_l22 = t20;
        uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l22))[8] != 0);
        if (t21) {
            gm_ttovhfee8mf28 t22 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l22));
            gm_l23 = t22;
        } else {
            void * t23 = gm_l0;
            gm_tl874t2atnlh3 t24 = gm_l1;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s5cmg64f6av8k));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(69));
            gm_t76123gfet73g t26;
            gm_f_trap_qh84l9d((void *)&t26, (void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), t26);
            memmove(gm_out, ((uint8_t *)&t27), 32);
            goto gm_fdone;
        }
        gm_ttovhfee8mf28 t28 = gm_l23;
        gm_l9 = t28;
        void * t29 = gm_l0;
        gm_ttovhfee8mf28 t30 = gm_l9;
        gm_b16_8 t31 = gm_l6;
        unsigned __int128 t32 = gm_f_rd_int_iuioodt(t29, t30, t31);
        gm_l10 = t32;
        {
            {
                {
                    {
                        {
                            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                            if (!t34) {
                                goto gm_lb4;
                            }
                            gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
                            gm_l11 = t35;
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)1ULL));
                            if (!t37) {
                                goto gm_lb5;
                            }
                            gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
                            gm_l11 = t38;
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)2ULL));
                            if (!t40) {
                                goto gm_lb6;
                            }
                            gm_t9qkh0rsooc8d t41 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
                            gm_l11 = t41;
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t42 = gm_l0;
                    gm_ttovhfee8mf28 t43 = gm_l9;
                    gm_b16_8 t44 = gm_l7;
                    unsigned __int128 t45 = gm_f_rd_int_iuioodt(t42, t43, t44);
                    gm_l14 = t45;
                    void * t46 = gm_l0;
                    gm_tl874t2atnlh3 t47 = gm_l1;
                    gm_t9qkh0rsooc8d t48 = gm_l11;
                    gm_ts6oqppjh0ba3 t49 = gm_l2;
                    uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
                    uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                    unsigned __int128 t52 = gm_l10;
                    unsigned __int128 t53 = gm_l14;
                    gm_t18m13lmi7mp6 t54;
                    gm_f_int_arith_is3k5b1((void *)&t54, t49, t50, t51, t52, t53);
                    gm_t88rcucg1b7uk t55 = gm_l3;
                    gm_ttovhfee8mf28 t56 = gm_l9;
                    gm_ts6oqppjh0ba3 t57 = gm_l2;
                    gm_b16_8 t58 = gm_f_op_sym_6olbepb(t57);
                    gm_t88rcucg1b7uk t59 = gm_l5;
                    gm_b16_8 t60 = gm_l8;
                    gm_f_finish_int_jmjdqd4((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t46, t47, t48, &t54, t55, t56, t58, t59, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)3ULL));
                    if (!t62) {
                        goto gm_lb7;
                    }
                    gm_t9qkh0rsooc8d t63 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
                    gm_l15 = t63;
                    void * t64 = gm_l0;
                    gm_tl874t2atnlh3 t65 = gm_l1;
                    gm_t88rcucg1b7uk t66 = gm_l3;
                    gm_t88rcucg1b7uk t67 = gm_l4;
                    gm_b16_8 t68 = gm_l7;
                    uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                    gm_f_shift_amount_aalipud((void *)&gm_l24, (void *)NULL, cx, NULL, t64, t65, t66, t67, t68, t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (t70) {
                    } else {
                        gm_t76123gfet73g t71 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l24) + 8));
                        gm_tcfa6l7reoosn t72;
                        GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                        ((uint8_t *)&t72)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t72 + 8), t71);
                        memmove(gm_out, ((uint8_t *)&t72), 32);
                        goto gm_fdone;
                    }
                    uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 8));
                    gm_l16 = t73;
                    void * t74 = gm_l0;
                    gm_tl874t2atnlh3 t75 = gm_l1;
                    gm_t9qkh0rsooc8d t76 = gm_l15;
                    uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
                    uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                    unsigned __int128 t79 = gm_l10;
                    uint32_t t80 = gm_l16;
                    gm_t18m13lmi7mp6 t81;
                    gm_f_int_shl_b8ne661((void *)&t81, t77, t78, t79, t80);
                    gm_t88rcucg1b7uk t82 = gm_l3;
                    gm_ttovhfee8mf28 t83 = gm_l9;
                    gm_b16_8 t84;
                    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_si62ejtp0vbct));
                    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(2));
                    gm_t88rcucg1b7uk t85 = gm_l5;
                    gm_b16_8 t86 = gm_l8;
                    gm_f_finish_int_jmjdqd4((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t74, t75, t76, &t81, t82, t83, t84, t85, t86);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)6ULL));
                    if (!t88) {
                        goto gm_lb8;
                    }
                    void * t89 = gm_l0;
                    gm_tl874t2atnlh3 t90 = gm_l1;
                    gm_t88rcucg1b7uk t91 = gm_l3;
                    gm_t88rcucg1b7uk t92 = gm_l4;
                    gm_b16_8 t93 = gm_l7;
                    uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                    gm_f_shift_amount_aalipud((void *)&gm_l25, (void *)NULL, cx, NULL, t89, t90, t91, t92, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                    if (t95) {
                    } else {
                        gm_t76123gfet73g t96 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l25) + 8));
                        gm_tcfa6l7reoosn t97;
                        GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
                        ((uint8_t *)&t97)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t97 + 8), t96);
                        memmove(gm_out, ((uint8_t *)&t97), 32);
                        goto gm_fdone;
                    }
                    uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 8));
                    gm_l17 = t98;
                    uint8_t t99 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
                    if (t99) {
                        void * t100 = gm_l0;
                        gm_b16_8 t101 = gm_l8;
                        unsigned __int128 t102 = gm_l10;
                        __int128 t103;
                        memset(&t103, 0, sizeof t103); memcpy(&t103, ((uint8_t *)&t102), sizeof t103 < 16 ? sizeof t103 : 16);
                        uint32_t t104 = gm_l17;
                        unsigned __int128 n105 = (unsigned __int128)t104;
                        if (n105 >= 128) gm_trap_shift((uint64_t)n105, "i128", gm_srcqod65l394oc8, 187, 34);
                        __int128 t106 = (__int128)(t103 >> (int)n105);
                        unsigned __int128 t107;
                        memset(&t107, 0, sizeof t107); memcpy(&t107, ((uint8_t *)&t106), sizeof t107 < 16 ? sizeof t107 : 16);
                        gm_f_wr_uint_rs9ado5(t100, t101, t107);
                    } else {
                        void * t108 = gm_l0;
                        gm_b16_8 t109 = gm_l8;
                        unsigned __int128 t110 = gm_l10;
                        uint32_t t111 = gm_l17;
                        unsigned __int128 t112 = (unsigned __int128)t111;
                        unsigned __int128 n113 = (unsigned __int128)t112;
                        if (n113 >= 128) gm_trap_shift((uint64_t)n113, "u128", gm_srcqod65l394oc8, 189, 33);
                        unsigned __int128 t114 = (unsigned __int128)(t110 >> (int)n113);
                        gm_f_wr_uint_rs9ado5(t108, t109, t114);
                    }
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    {
                        {
                            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t116 = (uint8_t)(t115 == ((uint8_t)4ULL));
                            if (!t116) {
                                goto gm_lb11;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)5ULL));
                            if (!t118) {
                                goto gm_lb12;
                            }
                            goto gm_lb10;
                        }
gm_lb12:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    void * t119 = gm_l0;
                    gm_ttovhfee8mf28 t120 = gm_l9;
                    gm_b16_8 t121 = gm_l7;
                    unsigned __int128 t122 = gm_f_rd_int_iuioodt(t119, t120, t121);
                    gm_l18 = t122;
                    unsigned __int128 t123 = gm_l18;
                    uint8_t t124 = ((t123 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                    if (t124) {
                        void * t125 = gm_l0;
                        gm_tl874t2atnlh3 t126 = gm_l1;
                        gm_t76123gfet73g t127;
                        gm_f_div_zero_trap_ap1saqd((void *)&t127, (void *)NULL, cx, NULL, t125, t126);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t128;
                        GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                        ((uint8_t *)&t128)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t128 + 8), t127);
                        memmove(gm_out, ((uint8_t *)&t128), 32);
                        goto gm_fdone;
                    }
                    gm_ts6oqppjh0ba3 t129 = gm_l2;
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t129) + 0));
                    uint8_t t131 = (uint8_t)((t130 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t132;
                    GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)4ULL));
                    gm_l19 = t131;
                    uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
                    uint8_t t134 = (uint8_t)!t133;
                    if (t134) {
                        void * t135 = gm_l0;
                        gm_b16_8 t136 = gm_l8;
                        uint8_t t137 = gm_l19;
                        if (t137) {
                            unsigned __int128 t138 = gm_l10;
                            unsigned __int128 t139 = gm_l18;
                            if (t139 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 197, 45);
                            unsigned __int128 t140 = (unsigned __int128)(t138 / t139);
                            gm_l26 = t140;
                        } else {
                            unsigned __int128 t141 = gm_l10;
                            unsigned __int128 t142 = gm_l18;
                            if (t142 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 197, 60);
                            unsigned __int128 t143 = (unsigned __int128)(t141 % t142);
                            gm_l26 = t143;
                        }
                        unsigned __int128 t144 = gm_l26;
                        gm_f_wr_uint_rs9ado5(t135, t136, t144);
                        gm_tcfa6l7reoosn t145;
                        GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t145 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t145 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t145 + 24, 0);
                        ((uint8_t *)&t145)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t145 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t145), 32);
                        goto gm_fdone;
                    }
                    unsigned __int128 t146 = gm_l10;
                    __int128 t147;
                    memset(&t147, 0, sizeof t147); memcpy(&t147, ((uint8_t *)&t146), sizeof t147 < 16 ? sizeof t147 : 16);
                    gm_l20 = t147;
                    unsigned __int128 t148 = gm_l18;
                    __int128 t149;
                    memset(&t149, 0, sizeof t149); memcpy(&t149, ((uint8_t *)&t148), sizeof t149 < 16 ? sizeof t149 : 16);
                    gm_l21 = t149;
                    __int128 t150 = gm_l21;
                    uint8_t t151 = ((t150 == ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 18446744073709551615ULL))));
                    if (t151) {
                        __int128 t152 = gm_l20;
                        uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                        __int128 t154 = gm_f_int_lo_i8jbm6e(t153);
                        uint8_t t155 = ((t152 == t154));
                        if (t155) {
                            void * t156 = gm_l0;
                            gm_tl874t2atnlh3 t157 = gm_l1;
                            gm_ts6oqppjh0ba3 t158 = gm_l2;
                            gm_b16_8 t159 = gm_f_op_sym_6olbepb(t158);
                            gm_t76123gfet73g t160;
                            gm_f_overflow_trap_j2f8u18((void *)&t160, (void *)NULL, cx, NULL, t156, t157, t159);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tcfa6l7reoosn t161;
                            GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                            ((uint8_t *)&t161)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t161 + 8), t160);
                            memmove(gm_out, ((uint8_t *)&t161), 32);
                            goto gm_fdone;
                        }
                        void * t162 = gm_l0;
                        gm_b16_8 t163 = gm_l8;
                        uint8_t t164 = gm_l19;
                        if (t164) {
                            __int128 t165 = gm_l20;
                            if (t165 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 204, 46);
                            __int128 t166 = (__int128)((unsigned __int128)0 - (unsigned __int128)t165);
                            unsigned __int128 t167;
                            memset(&t167, 0, sizeof t167); memcpy(&t167, ((uint8_t *)&t166), sizeof t167 < 16 ? sizeof t167 : 16);
                            gm_l27 = t167;
                        } else {
                            gm_l27 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                        }
                        unsigned __int128 t168 = gm_l27;
                        gm_f_wr_uint_rs9ado5(t162, t163, t168);
                        gm_tcfa6l7reoosn t169;
                        GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t169 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t169 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t169 + 24, 0);
                        ((uint8_t *)&t169)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t169 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t169), 32);
                        goto gm_fdone;
                    }
                    void * t170 = gm_l0;
                    gm_b16_8 t171 = gm_l8;
                    uint8_t t172 = gm_l19;
                    if (t172) {
                        __int128 t173 = gm_l20;
                        __int128 t174 = gm_l21;
                        if (t174 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 207, 42);
                        if (t173 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t174 == (__int128)-1) gm_trap("integer overflow in `/`", gm_srcqod65l394oc8, 207, 42);
                        __int128 t175 = (__int128)(t173 / t174);
                        gm_l28 = t175;
                    } else {
                        __int128 t176 = gm_l20;
                        __int128 t177 = gm_l21;
                        if (t177 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 207, 57);
                        if (t176 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t177 == (__int128)-1) gm_trap("integer overflow in `%`", gm_srcqod65l394oc8, 207, 57);
                        __int128 t178 = (__int128)(t176 % t177);
                        gm_l28 = t178;
                    }
                    __int128 t179 = gm_l28;
                    unsigned __int128 t180;
                    memset(&t180, 0, sizeof t180); memcpy(&t180, ((uint8_t *)&t179), sizeof t180 < 16 ? sizeof t180 : 16);
                    gm_f_wr_uint_rs9ado5(t170, t171, t180);
                    goto gm_lb1;
                }
gm_lb9:;
            }
            {
                {
                    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)7ULL));
                    if (!t182) {
                        goto gm_lb13;
                    }
                    void * t183 = gm_l0;
                    gm_b16_8 t184 = gm_l8;
                    unsigned __int128 t185 = gm_l10;
                    void * t186 = gm_l0;
                    gm_ttovhfee8mf28 t187 = gm_l9;
                    gm_b16_8 t188 = gm_l7;
                    unsigned __int128 t189 = gm_f_rd_int_iuioodt(t186, t187, t188);
                    unsigned __int128 t190 = (unsigned __int128)(t185 & t189);
                    gm_f_wr_uint_rs9ado5(t183, t184, t190);
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)8ULL));
                    if (!t192) {
                        goto gm_lb14;
                    }
                    void * t193 = gm_l0;
                    gm_b16_8 t194 = gm_l8;
                    unsigned __int128 t195 = gm_l10;
                    void * t196 = gm_l0;
                    gm_ttovhfee8mf28 t197 = gm_l9;
                    gm_b16_8 t198 = gm_l7;
                    unsigned __int128 t199 = gm_f_rd_int_iuioodt(t196, t197, t198);
                    unsigned __int128 t200 = (unsigned __int128)(t195 | t199);
                    gm_f_wr_uint_rs9ado5(t193, t194, t200);
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t202 = (uint8_t)(t201 == ((uint8_t)9ULL));
                    if (!t202) {
                        goto gm_lb15;
                    }
                    void * t203 = gm_l0;
                    gm_b16_8 t204 = gm_l8;
                    unsigned __int128 t205 = gm_l10;
                    void * t206 = gm_l0;
                    gm_ttovhfee8mf28 t207 = gm_l9;
                    gm_b16_8 t208 = gm_l7;
                    unsigned __int128 t209 = gm_f_rd_int_iuioodt(t206, t207, t208);
                    unsigned __int128 t210 = (unsigned __int128)(t205 ^ t209);
                    gm_f_wr_uint_rs9ado5(t203, t204, t210);
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    void * t211 = gm_l0;
                    gm_tl874t2atnlh3 t212 = gm_l1;
                    gm_b16_8 t213;
                    GM_ST(void *, (uint8_t *)&t213, (void *)(gm_s9idfla83gb22));
                    GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(58));
                    gm_t76123gfet73g t214;
                    gm_f_trap_qh84l9d((void *)&t214, (void *)NULL, cx, NULL, t211, t212, t213);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t215;
                    GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t215 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
                    ((uint8_t *)&t215)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t215 + 8), t214);
                    memmove(gm_out, ((uint8_t *)&t215), 32);
                    goto gm_fdone;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 175, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tcfa6l7reoosn t216;
        GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t216 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t216 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t216 + 24, 0);
        ((uint8_t *)&t216)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t216 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t216), 32);
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
inline __attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_cmp_kb3sm6u F;
    uint8_t gm_l0; /* signed */
    unsigned __int128 gm_l1; /* x */
    unsigned __int128 gm_l2; /* y */
    __int128 gm_l3; /* p */
    __int128 gm_l4; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 302u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int32_t gm_l5;
    int32_t gm_l6;
    int32_t gm_l7;
    int32_t gm_l8;
    uint8_t t2 = gm_l0;
    if (t2) {
        unsigned __int128 t3 = gm_l1;
        __int128 t4;
        memset(&t4, 0, sizeof t4); memcpy(&t4, ((uint8_t *)&t3), sizeof t4 < 16 ? sizeof t4 : 16);
        gm_l3 = t4;
        unsigned __int128 t5 = gm_l2;
        __int128 t6;
        memset(&t6, 0, sizeof t6); memcpy(&t6, ((uint8_t *)&t5), sizeof t6 < 16 ? sizeof t6 : 16);
        gm_l4 = t6;
        __int128 t7 = gm_l3;
        __int128 t8 = gm_l4;
        uint8_t t9 = (t7 < t8);
        if (t9) {
            gm_l5 = ((int32_t)-1LL);
        } else {
            __int128 t10 = gm_l3;
            __int128 t11 = gm_l4;
            uint8_t t12 = (t10 > t11);
            if (t12) {
                gm_l6 = ((int32_t)1LL);
            } else {
                gm_l6 = ((int32_t)0LL);
            }
            int32_t t13 = gm_l6;
            gm_l5 = t13;
        }
        int32_t t14 = gm_l5;
        F.ret = t14;
        goto gm_fret;
    }
    unsigned __int128 t15 = gm_l1;
    unsigned __int128 t16 = gm_l2;
    uint8_t t17 = (t15 < t16);
    if (t17) {
        gm_l7 = ((int32_t)-1LL);
    } else {
        unsigned __int128 t18 = gm_l1;
        unsigned __int128 t19 = gm_l2;
        uint8_t t20 = (t18 > t19);
        if (t20) {
            gm_l8 = ((int32_t)1LL);
        } else {
            gm_l8 = ((int32_t)0LL);
        }
        int32_t t21 = gm_l8;
        gm_l7 = t21;
    }
    int32_t t22 = gm_l7;
    F.ret = t22;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) __int128 gm_f_int_hi_qa5fgoo(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_hi_qa5fgoo F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint32_t)128ULL));
    if (t3) {
        unsigned __int128 t4 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
        unsigned __int128 n5 = (unsigned __int128)((uint64_t)1ULL);
        if (n5 >= 128) gm_trap_shift((uint64_t)n5, "u128", gm_srcqod65l394oc8, 12, 30);
        unsigned __int128 t6 = (unsigned __int128)(t4 >> (int)n5);
        __int128 t7;
        memset(&t7, 0, sizeof t7); memcpy(&t7, ((uint8_t *)&t6), sizeof t7 < 16 ? sizeof t7 : 16);
        F.ret = t7;
        goto gm_fret;
    }
    uint32_t t8 = gm_l0;
    uint32_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint32_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 13, 25);
    uint32_t t11 = r9;
    unsigned __int128 n12 = (unsigned __int128)t11;
    if (n12 >= 128) gm_trap_shift((uint64_t)n12, "i128", gm_srcqod65l394oc8, 13, 13);
    __int128 r13 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n12);
    bool o14 = (__int128)(r13 >> (int)n12) != ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL));
    if (o14) gm_trap("integer overflow in `<<`", gm_srcqod65l394oc8, 13, 13);
    __int128 t15 = r13;
    __int128 r16; bool o17 = __builtin_sub_overflow(t15, ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 13, 12);
    __int128 t18 = r16;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) __int128 gm_f_int_lo_i8jbm6e(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_lo_i8jbm6e F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint32_t)128ULL));
    if (t3) {
        unsigned __int128 n4 = (unsigned __int128)((uint64_t)127ULL);
        if (n4 >= 128) gm_trap_shift((uint64_t)n4, "u128", gm_srcqod65l394oc8, 6, 30);
        unsigned __int128 r5 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n4);
        bool o6 = (unsigned __int128)(r5 >> (int)n4) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
        if (o6) gm_trap("integer overflow in `<<`", gm_srcqod65l394oc8, 6, 30);
        unsigned __int128 t7 = r5;
        __int128 t8;
        memset(&t8, 0, sizeof t8); memcpy(&t8, ((uint8_t *)&t7), sizeof t8 < 16 ? sizeof t8 : 16);
        F.ret = t8;
        goto gm_fret;
    }
    uint32_t t9 = gm_l0;
    uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 7, 26);
    uint32_t t12 = r10;
    unsigned __int128 n13 = (unsigned __int128)t12;
    if (n13 >= 128) gm_trap_shift((uint64_t)n13, "i128", gm_srcqod65l394oc8, 7, 14);
    __int128 r14 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n13);
    bool o15 = (__int128)(r14 >> (int)n13) != ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL));
    if (o15) gm_trap("integer overflow in `<<`", gm_srcqod65l394oc8, 7, 14);
    __int128 t16 = r14;
    if (t16 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 7, 12);
    __int128 t17 = (__int128)((unsigned __int128)0 - (unsigned __int128)t16);
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_int_pow_wrapped_5mjnoh4(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_pow_wrapped_5mjnoh4 F;
    uint8_t gm_l0; /* signed */
    uint32_t gm_l1; /* bits */
    unsigned __int128 gm_l2; /* base */
    uint32_t gm_l3; /* n */
    gm_ts6oqppjh0ba3 gm_l4; /* mulop */
    unsigned __int128 gm_l5; /* result */
    unsigned __int128 gm_l6; /* b */
    uint32_t gm_l7; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t18m13lmi7mp6 gm_l8;
    gm_t18m13lmi7mp6 gm_l9;
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 1), t5);
    gm_l4 = t6;
    gm_l5 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
    unsigned __int128 t7 = gm_l2;
    gm_l6 = t7;
    uint32_t t8 = gm_l3;
    gm_l7 = t8;
    {
        for (;;) {
            uint32_t t9 = gm_l7;
            uint8_t t10 = (t9 > ((uint32_t)0ULL));
            if (!t10) {
                goto gm_lb1;
            }
            {
                uint32_t t11 = gm_l7;
                uint32_t t12 = (uint32_t)(t11 & ((uint32_t)1ULL));
                uint8_t t13 = ((t12 == ((uint32_t)1ULL)));
                if (t13) {
                    gm_ts6oqppjh0ba3 t14 = gm_l4;
                    uint8_t t15 = gm_l0;
                    uint32_t t16 = gm_l1;
                    unsigned __int128 t17 = gm_l5;
                    unsigned __int128 t18 = gm_l6;
                    gm_f_int_arith_is3k5b1((void *)&gm_l8, t14, t15, t16, t17, t18);
                    unsigned __int128 t19 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l8));
                    gm_l5 = t19;
                }
                gm_ts6oqppjh0ba3 t20 = gm_l4;
                uint8_t t21 = gm_l0;
                uint32_t t22 = gm_l1;
                unsigned __int128 t23 = gm_l6;
                unsigned __int128 t24 = gm_l6;
                gm_f_int_arith_is3k5b1((void *)&gm_l9, t20, t21, t22, t23, t24);
                unsigned __int128 t25 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l9));
                gm_l6 = t25;
                uint32_t t26 = gm_l7;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)1ULL);
                if (n27 >= 32) gm_trap_shift((uint64_t)n27, "u32", gm_srcqod65l394oc8, 290, 9);
                uint32_t t28 = (uint32_t)(t26 >> (int)n27);
                gm_l7 = t28;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint8_t t29 = gm_l0;
    uint32_t t30 = gm_l1;
    unsigned __int128 t31 = gm_l5;
    unsigned __int128 t32 = gm_f_num_ext_348kmbu(t29, t30, t31);
    F.ret = t32;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_int_shl_b8ne661(void *gm_out, uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_shl_b8ne661 F;
    uint8_t gm_l0; /* signed */
    uint32_t gm_l1; /* bits */
    unsigned __int128 gm_l2; /* a */
    uint32_t gm_l3; /* n */
    unsigned __int128 gm_l4; /* k */
    unsigned __int128 gm_l5; /* low */
    __int128 gm_l6; /* v */
    __int128 gm_l7; /* w */
    __int128 gm_l8; /* wrapped */
    GM_STACK_CHECK_SHALLOW(&F, 667u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    __int128 gm_l9;
    uint32_t t2 = gm_l3;
    unsigned __int128 t3 = (unsigned __int128)t2;
    gm_l4 = t3;
    uint8_t t4 = gm_l0;
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        unsigned __int128 t6 = gm_l2;
        unsigned __int128 t7 = gm_l4;
        unsigned __int128 n8 = (unsigned __int128)t7;
        if (n8 >= 128) gm_trap_shift((uint64_t)n8, "u128", gm_srcqod65l394oc8, 129, 17);
        unsigned __int128 r9 = (unsigned __int128)((unsigned __int128)t6 << (int)n8);
        bool o10 = (unsigned __int128)(r9 >> (int)n8) != t6;
        unsigned __int128 t11 = r9;
        uint32_t t12 = gm_l1;
        unsigned __int128 t13 = gm_f_num_mask_do3naah(t12);
        unsigned __int128 t14 = (unsigned __int128)(t11 & t13);
        gm_l5 = t14;
        unsigned __int128 t15 = gm_l5;
        unsigned __int128 t16 = gm_l5;
        unsigned __int128 t17 = gm_l4;
        unsigned __int128 n18 = (unsigned __int128)t17;
        if (n18 >= 128) gm_trap_shift((uint64_t)n18, "u128", gm_srcqod65l394oc8, 130, 39);
        unsigned __int128 t19 = (unsigned __int128)(t16 >> (int)n18);
        unsigned __int128 t20 = gm_l2;
        uint8_t t21 = (!(t19 == t20));
        gm_t18m13lmi7mp6 t22;
        GM_ST(uint16_t, (uint8_t *)&t22 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 20, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
        GM_ST(unsigned __int128, ((uint8_t *)&t22 + 0), t15);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 16), t21);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 17), ((uint8_t)0));
        memmove(gm_out, ((uint8_t *)&t22), 32);
        goto gm_fdone;
    }
    unsigned __int128 t23 = gm_l2;
    __int128 t24;
    memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 16 ? sizeof t24 : 16);
    gm_l6 = t24;
    __int128 t25 = gm_l6;
    unsigned __int128 t26 = gm_l4;
    unsigned __int128 n27 = (unsigned __int128)t26;
    if (n27 >= 128) gm_trap_shift((uint64_t)n27, "i128", gm_srcqod65l394oc8, 133, 10);
    __int128 r28 = (__int128)((unsigned __int128)t25 << (int)n27);
    bool o29 = (__int128)(r28 >> (int)n27) != t25;
    __int128 t30 = r28;
    gm_l7 = t30;
    uint32_t t31 = gm_l1;
    uint8_t t32 = (t31 >= ((uint32_t)128ULL));
    if (t32) {
        __int128 t33 = gm_l7;
        gm_l9 = t33;
    } else {
        __int128 t34 = gm_l7;
        uint32_t t35 = gm_l1;
        uint32_t r36; bool o37 = __builtin_sub_overflow(((uint32_t)128ULL), t35, &r36);
        if (o37) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 134, 52);
        uint32_t t38 = r36;
        unsigned __int128 n39 = (unsigned __int128)t38;
        if (n39 >= 128) gm_trap_shift((uint64_t)n39, "i128", gm_srcqod65l394oc8, 134, 45);
        __int128 r40 = (__int128)((unsigned __int128)t34 << (int)n39);
        bool o41 = (__int128)(r40 >> (int)n39) != t34;
        __int128 t42 = r40;
        uint32_t t43 = gm_l1;
        uint32_t r44; bool o45 = __builtin_sub_overflow(((uint32_t)128ULL), t43, &r44);
        if (o45) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 134, 69);
        uint32_t t46 = r44;
        unsigned __int128 n47 = (unsigned __int128)t46;
        if (n47 >= 128) gm_trap_shift((uint64_t)n47, "i128", gm_srcqod65l394oc8, 134, 44);
        __int128 t48 = (__int128)(t42 >> (int)n47);
        gm_l9 = t48;
    }
    __int128 t49 = gm_l9;
    gm_l8 = t49;
    __int128 t50 = gm_l8;
    unsigned __int128 t51;
    memset(&t51, 0, sizeof t51); memcpy(&t51, ((uint8_t *)&t50), sizeof t51 < 16 ? sizeof t51 : 16);
    __int128 t52 = gm_l8;
    unsigned __int128 t53 = gm_l4;
    unsigned __int128 n54 = (unsigned __int128)t53;
    if (n54 >= 128) gm_trap_shift((uint64_t)n54, "i128", gm_srcqod65l394oc8, 135, 50);
    __int128 t55 = (__int128)(t52 >> (int)n54);
    __int128 t56 = gm_l6;
    uint8_t t57 = (!(t55 == t56));
    __int128 t58 = gm_l6;
    uint8_t t59 = (t58 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
    gm_t18m13lmi7mp6 t60;
    GM_ST(uint16_t, (uint8_t *)&t60 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t60 + 20, 0);
    GM_ST(uint64_t, (uint8_t *)&t60 + 24, 0);
    GM_ST(unsigned __int128, ((uint8_t *)&t60 + 0), t51);
    GM_ST(uint8_t, ((uint8_t *)&t60 + 16), t57);
    GM_ST(uint8_t, ((uint8_t *)&t60 + 17), t59);
    memmove(gm_out, ((uint8_t *)&t60), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_shl_b8ne661_c(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3) { gm_t18m13lmi7mp6 gm_r; gm_f_int_shl_b8ne661(&gm_r, a0, a1, a2, a3); return GM_CONV(gm_csp8136jfauorf, gm_r); }
inline __attribute__((aligned(4))) void gm_f_overflow_result_vqlhh7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, unsigned __int128 a5, uint8_t a6, gm_b16_8 a7) {
    struct gm_fr_overflow_result_vqlhh7t F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    uint8_t gm_l3; /* signed */
    uint32_t gm_l4; /* bits */
    unsigned __int128 gm_l5; /* low */
    uint8_t gm_l6; /* below */
    gm_b16_8 gm_l7; /* sym */
    GM_STACK_CHECK(&F, 338u);
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
    gm_tcfa6l7reoosn gm_l8;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)2ULL));
            if (!t7) {
                goto gm_lb2;
            }
            uint8_t t8 = gm_l3;
            uint32_t t9 = gm_l4;
            uint8_t t10 = gm_l6;
            unsigned __int128 t11 = gm_f_sat_bits_ug4cjjm(t8, t9, t10);
            gm_tdlro53ljvkl0 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 40, 0);
            ((uint8_t *)&t12)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t12 + 16), t11);
            memmove(gm_out, ((uint8_t *)&t12), 48);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb3;
            }
            void * t15 = gm_l0;
            gm_tl874t2atnlh3 t16 = gm_l1;
            gm_b16_8 t17 = gm_l7;
            gm_f_checked_overflow_ma1bm1g((void *)&gm_l8, (void *)NULL, cx, NULL, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (t18) {
            } else {
                gm_t76123gfet73g t19 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l8) + 8));
                gm_tdlro53ljvkl0 t20;
                GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t20 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t20 + 40, 0);
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t20 + 16), t19);
                memmove(gm_out, ((uint8_t *)&t20), 48);
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 39, 5);
    }
gm_lb1:;
    unsigned __int128 t21 = gm_l5;
    gm_tdlro53ljvkl0 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t22 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t22 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t22 + 40, 0);
    ((uint8_t *)&t22)[0] = 0;
    GM_ST(unsigned __int128, ((uint8_t *)&t22 + 16), t21);
    memmove(gm_out, ((uint8_t *)&t22), 48);
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
extern __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8) {
    struct gm_fr_pow_7je2uvi F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* res_ty */
    gm_t88rcucg1b7uk gm_l5; /* ety */
    gm_b16_8 gm_l6; /* bb */
    gm_b16_8 gm_l7; /* nb */
    gm_b16_8 gm_l8; /* dst */
    gm_ttovhfee8mf28 gm_l9; /* info */
    uint8_t gm_l10; /* signed */
    uint32_t gm_l11; /* bits */
    unsigned __int128 gm_l12; /* base */
    gm_ttovhfee8mf28 gm_l13; /* einfo */
    unsigned __int128 gm_l14; /* nraw */
    uint32_t gm_l15; /* n */
    gm_t86h6hhm6d76n gm_l16; /* bval */
    gm_ts6oqppjh0ba3 gm_l17; /* mulop */
    unsigned __int128 gm_l18; /* acc */
    uint8_t gm_l19; /* over */
    uint32_t gm_l20; /* <tmp> */
    uint32_t gm_l21; /* <tmp> */
    gm_t18m13lmi7mp6 gm_l22; /* r */
    unsigned __int128 gm_l23; /* v */
    gm_t51b2en4qp02s gm_l24; /* pb */
    uint64_t gm_l25; /* sz */
    GM_STACK_CHECK(&F, 1736u);
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
    gm_l8 = a8;
    gm_tg5lql6itufk7 gm_l26;
    gm_ttovhfee8mf28 gm_l27;
    gm_tg5lql6itufk7 gm_l28;
    gm_ttovhfee8mf28 gm_l29;
    uint8_t gm_l30;
    unsigned __int128 gm_l31;
    uint8_t gm_l32;
    gm_tabb710d9mq2e gm_l33;
    void * t14 = gm_l0;
    gm_t88rcucg1b7uk t15 = gm_l3;
    gm_tg5lql6itufk7 t16 = gm_f_int_info_ulcevdv(t14, GM_CONV(gm_csguankko4lmhd, t15));
    gm_l26 = t16;
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
    if (t17) {
        gm_ttovhfee8mf28 t18 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l26));
        gm_l27 = t18;
    } else {
        void * t19 = gm_l0;
        gm_tl874t2atnlh3 t20 = gm_l1;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s2hnapin8228t));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(20));
        gm_t76123gfet73g t22;
        gm_f_trap_qh84l9d((void *)&t22, (void *)NULL, cx, NULL, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t23 + 8), t22);
        memmove(gm_out, ((uint8_t *)&t23), 32);
        goto gm_fdone;
    }
    gm_ttovhfee8mf28 t24 = gm_l27;
    gm_l9 = t24;
    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
    gm_l10 = t25;
    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
    gm_l11 = t26;
    void * t27 = gm_l0;
    gm_t88rcucg1b7uk t28 = gm_l3;
    gm_b16_8 t29 = gm_l6;
    unsigned __int128 t30 = gm_f_get_int_pg7p4gc(t27, GM_CONV(gm_csguankko4lmhd, t28), t29);
    gm_l12 = t30;
    void * t31 = gm_l0;
    gm_t88rcucg1b7uk t32 = gm_l5;
    gm_tg5lql6itufk7 t33 = gm_f_int_info_ulcevdv(t31, GM_CONV(gm_csguankko4lmhd, t32));
    gm_l28 = t33;
    uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l28))[8] != 0);
    if (t34) {
        gm_ttovhfee8mf28 t35 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l28));
        gm_l29 = t35;
    } else {
        void * t36 = gm_l0;
        gm_tl874t2atnlh3 t37 = gm_l1;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s0k8sea1jhldg));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(20));
        gm_t76123gfet73g t39;
        gm_f_trap_qh84l9d((void *)&t39, (void *)NULL, cx, NULL, t36, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t40 + 8), t39);
        memmove(gm_out, ((uint8_t *)&t40), 32);
        goto gm_fdone;
    }
    gm_ttovhfee8mf28 t41 = gm_l29;
    gm_l13 = t41;
    void * t42 = gm_l0;
    gm_t88rcucg1b7uk t43 = gm_l5;
    gm_b16_8 t44 = gm_l7;
    unsigned __int128 t45 = gm_f_get_int_pg7p4gc(t42, GM_CONV(gm_csguankko4lmhd, t43), t44);
    gm_l14 = t45;
    gm_l15 = ((uint32_t)0ULL);
    uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
    if (t46) {
        unsigned __int128 t47 = gm_l14;
        unsigned __int128 n48 = (unsigned __int128)((uint64_t)127ULL);
        if (n48 >= 128) gm_trap_shift((uint64_t)n48, "u128", gm_srcqod65l394oc8, 236, 31);
        unsigned __int128 t49 = (unsigned __int128)(t47 >> (int)n48);
        uint8_t t50 = ((t49 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
        gm_l30 = t50;
    } else {
        gm_l30 = ((uint8_t)0);
    }
    uint8_t t51 = gm_l30;
    uint8_t t52 = (uint8_t)!t51;
    if (t52) {
        unsigned __int128 t53 = gm_l14;
        uint32_t t54 = (uint32_t)~((uint32_t)0ULL);
        unsigned __int128 t55 = (unsigned __int128)t54;
        unsigned __int128 t56 = (t53 < t55 ? t53 : t55);
        uint32_t t57 = (uint32_t)t56;
        gm_l15 = t57;
    }
    uint8_t t58 = gm_l10;
    unsigned __int128 t59 = gm_l12;
    gm_f_num_val_i3upeur((void *)&gm_l16, t58, t59);
    gm_t9qkh0rsooc8d t60;
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t61;
    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t61 + 1), t60);
    gm_l17 = t61;
    gm_l18 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
    gm_l19 = ((uint8_t)0);
    uint32_t t62 = gm_l15;
    uint8_t t63 = ((t62 == ((uint32_t)0ULL)));
    if (t63) {
        gm_l18 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
    } else {
        unsigned __int128 t64 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l16) + 16));
        uint8_t t65 = ((t64 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
        if (t65) {
            gm_l18 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
        } else {
            unsigned __int128 t66 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l16) + 16));
            uint8_t t67 = ((t66 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
            if (t67) {
                {
                    {
                        uint8_t t68 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
                        if (!t68) {
                            goto gm_lb2;
                        }
                        uint32_t t69 = gm_l15;
                        if (((uint32_t)2ULL) == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 247, 31);
                        uint32_t t70 = (uint32_t)(t69 % ((uint32_t)2ULL));
                        uint8_t t71 = ((t70 == ((uint32_t)1ULL)));
                        if (!t71) {
                            goto gm_lb2;
                        }
                        unsigned __int128 r72; bool o73 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r72);
                        unsigned __int128 t74 = r72;
                        gm_l31 = t74;
                        goto gm_lb1;
                    }
gm_lb2:;
                    gm_l31 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                }
gm_lb1:;
                unsigned __int128 t75 = gm_l31;
                gm_l18 = t75;
            } else {
                gm_l21 = ((uint32_t)0ULL);
                uint32_t t76 = gm_l15;
                gm_l20 = t76;
                {
                    for (;;) {
                        uint32_t t77 = gm_l21;
                        uint32_t t78 = gm_l20;
                        uint8_t t79 = (t77 < t78);
                        if (!t79) {
                            goto gm_lb3;
                        }
                        {
                            {
                                {
                                    gm_ts6oqppjh0ba3 t80 = gm_l17;
                                    uint8_t t81 = gm_l10;
                                    uint32_t t82 = gm_l11;
                                    unsigned __int128 t83 = gm_l18;
                                    unsigned __int128 t84 = gm_l12;
                                    gm_f_int_arith_is3k5b1((void *)&gm_l22, t80, t81, t82, t83, t84);
                                    unsigned __int128 t85 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l22));
                                    gm_l18 = t85;
                                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 16));
                                    if (t86) {
                                        gm_l19 = ((uint8_t)1);
                                        goto gm_lb3;
                                    }
                                    goto gm_lb6;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 249, 9);
                            }
gm_lb6:;
                        }
gm_lb5:;
                        uint32_t t87 = gm_l21;
                        uint32_t r88; bool o89 = __builtin_add_overflow(t87, ((uint32_t)1ULL), &r88);
                        uint32_t t90 = r88;
                        gm_l21 = t90;
                    }
                }
gm_lb3:;
            }
        }
    }
    uint8_t t91 = gm_l19;
    uint8_t t92 = (uint8_t)!t91;
    if (t92) {
        uint8_t t93 = gm_l10;
        uint32_t t94 = gm_l11;
        unsigned __int128 t95 = gm_l18;
        unsigned __int128 t96 = gm_f_num_ext_348kmbu(t93, t94, t95);
        gm_l23 = t96;
        gm_t9qkh0rsooc8d t97 = gm_l2;
        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t97) + 0));
        uint8_t t99 = (uint8_t)((t98 == ((uint8_t)3ULL)));
        gm_t9qkh0rsooc8d t100;
        GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)3ULL));
        if (t99) {
            void * t101 = gm_l0;
            gm_t88rcucg1b7uk t102 = gm_l3;
            uint64_t t103 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t101, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t104 = gm_f_us_cuv01fp(t103);
            gm_l25 = t104;
            void * t105 = gm_l0;
            void * t106 = (void *)((uint8_t *)&gm_l24);
            uint64_t t107 = gm_l25;
            uint64_t t108 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t109 = (uintptr_t)t107;
            if (t108 > t109 || t109 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t108, t109, ((uint64_t)16ULL), gm_srcqod65l394oc8, 263, 24);
            uint64_t t110 = ((uintptr_t)t107) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(((uint8_t *)t106 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(t110));
            unsigned __int128 t112 = gm_l23;
            gm_f_put_uint_cddin99(t105, t111, t112);
            void * t113 = gm_l0;
            gm_t88rcucg1b7uk t114 = gm_l4;
            gm_b16_8 t115 = gm_l8;
            void * t116 = (void *)((uint8_t *)&gm_l24);
            uint64_t t117 = gm_l25;
            uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t119 = (uintptr_t)t117;
            if (t118 > t119 || t119 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t118, t119, ((uint64_t)16ULL), gm_srcqod65l394oc8, 264, 38);
            uint64_t t120 = ((uintptr_t)t117) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t121;
            GM_ST(void *, (uint8_t *)&t121, (void *)(((uint8_t *)t116 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(t120));
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
            gm_b0_1 t123 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t113, t114, t115, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t124 = gm_l0;
            gm_b16_8 t125 = gm_l8;
            unsigned __int128 t126 = gm_l23;
            gm_f_put_uint_cddin99(t124, t125, t126);
        }
        gm_tcfa6l7reoosn t127;
        GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t127 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t127 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t127 + 24, 0);
        ((uint8_t *)&t127)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t127 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t127), 32);
        goto gm_fdone;
    }
    {
        {
            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t129 = (uint8_t)(t128 == ((uint8_t)3ULL));
            if (!t129) {
                goto gm_lb9;
            }
            gm_b16_8 t130 = gm_l8;
            gm_f_fill_abmf5g5(t130, ((uint8_t)0ULL));
            goto gm_lb8;
        }
gm_lb9:;
        {
            uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t132 = (uint8_t)(t131 == ((uint8_t)2ULL));
            if (!t132) {
                goto gm_lb10;
            }
            void * t133 = gm_l0;
            gm_b16_8 t134 = gm_l8;
            uint8_t t135 = gm_l10;
            uint32_t t136 = gm_l11;
            uint8_t t137 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
            if (t137) {
                uint32_t t138 = gm_l15;
                if (((uint32_t)2ULL) == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 272, 69);
                uint32_t t139 = (uint32_t)(t138 % ((uint32_t)2ULL));
                uint8_t t140 = ((t139 == ((uint32_t)1ULL)));
                gm_l32 = t140;
            } else {
                gm_l32 = ((uint8_t)0);
            }
            uint8_t t141 = gm_l32;
            unsigned __int128 t142 = gm_f_sat_bits_ug4cjjm(t135, t136, t141);
            gm_f_put_uint_cddin99(t133, t134, t142);
            goto gm_lb8;
        }
gm_lb10:;
        {
            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t144 = (uint8_t)(t143 == ((uint8_t)1ULL));
            if (!t144) {
                goto gm_lb11;
            }
            void * t145 = gm_l0;
            gm_b16_8 t146 = gm_l8;
            uint8_t t147 = gm_l10;
            uint32_t t148 = gm_l11;
            unsigned __int128 t149 = gm_l12;
            uint32_t t150 = gm_l15;
            unsigned __int128 t151 = gm_f_int_pow_wrapped_5mjnoh4(t147, t148, t149, t150);
            gm_f_put_uint_cddin99(t145, t146, t151);
            goto gm_lb8;
        }
gm_lb11:;
        {
            uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t153 = (uint8_t)(t152 == ((uint8_t)0ULL));
            if (!t153) {
                goto gm_lb12;
            }
            void * t154 = gm_l0;
            uint8_t t155 = gm_f_fast_23em7sv(t154);
            uint8_t t156 = (uint8_t)!t155;
            if (t156) {
                void * t157 = gm_l0;
                gm_tl874t2atnlh3 t158 = gm_l1;
                void * t159 = gm_l0;
                gm_t88rcucg1b7uk t160 = gm_l3;
                gm_b16_8 t161 = gm_l6;
                gm_b16_8 t162 = gm_f_int_text_vrjnuu2((void *)NULL, cx, NULL, t159, t160, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t163 = gm_l15;
                gm_tommf4j9upomc t164;
                GM_ST(uint32_t, (uint8_t *)&t164 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t164 + 0), t162);
                GM_ST(uint32_t, ((uint8_t *)&t164 + 16), t163);
                gm_f_format_bl27tn4((void *)&gm_l33, (void *)NULL, cx, NULL, &t164);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcqod65l394oc8, 275, 52);
                gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                gm_t76123gfet73g t168;
                gm_f_trap_qh84l9d((void *)&t168, (void *)NULL, cx, NULL, t157, t158, t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t169;
                GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                ((uint8_t *)&t169)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t169 + 8), t168);
                memmove(gm_out, ((uint8_t *)&t169), 32);
                goto gm_fdone;
            }
            void * t170 = gm_l0;
            gm_b16_8 t171 = gm_l8;
            uint8_t t172 = gm_l10;
            uint32_t t173 = gm_l11;
            unsigned __int128 t174 = gm_l12;
            uint32_t t175 = gm_l15;
            unsigned __int128 t176 = gm_f_int_pow_wrapped_5mjnoh4(t172, t173, t174, t175);
            gm_f_put_uint_cddin99(t170, t171, t176);
            goto gm_lb8;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 270, 5);
    }
gm_lb8:;
    gm_tcfa6l7reoosn t177;
    GM_ST(uint8_t, (uint8_t *)&t177 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t177 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t177 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t177 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t177 + 24, 0);
    ((uint8_t *)&t177)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t177 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t177), 32);
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
extern __attribute__((aligned(4))) unsigned __int128 gm_f_sat_bits_ug4cjjm(uint8_t a0, uint32_t a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_shift_amount_aalipud(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint32_t a5) {
    struct gm_fr_shift_amount_aalipud F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* lt */
    gm_t88rcucg1b7uk gm_l3; /* rt */
    gm_b16_8 gm_l4; /* y */
    uint32_t gm_l5; /* bits */
    gm_ttovhfee8mf28 gm_l6; /* rinfo */
    unsigned __int128 gm_l7; /* v */
    uint8_t gm_l8; /* neg */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tg5lql6itufk7 gm_l9;
    gm_ttovhfee8mf28 gm_l10;
    uint8_t gm_l11;
    unsigned __int128 gm_l12;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l3;
    gm_tg5lql6itufk7 t4 = gm_f_int_info_ulcevdv(t2, GM_CONV(gm_csguankko4lmhd, t3));
    gm_l9 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
    if (t5) {
        gm_ttovhfee8mf28 t6 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l9));
        gm_l10 = t6;
    } else {
        void * t7 = gm_l0;
        gm_tl874t2atnlh3 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s9r2a03bnhj06));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(57));
        gm_t76123gfet73g t10;
        gm_f_trap_qh84l9d((void *)&t10, (void *)NULL, cx, NULL, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcula66ipo514 t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        ((uint8_t *)&t11)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t11 + 8), t10);
        memmove(gm_out, ((uint8_t *)&t11), 32);
        goto gm_fdone;
    }
    gm_ttovhfee8mf28 t12 = gm_l10;
    gm_l6 = t12;
    void * t13 = gm_l0;
    gm_t88rcucg1b7uk t14 = gm_l3;
    gm_b16_8 t15 = gm_l4;
    unsigned __int128 t16 = gm_f_get_int_pg7p4gc(t13, GM_CONV(gm_csguankko4lmhd, t14), t15);
    gm_l7 = t16;
    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
    if (t17) {
        unsigned __int128 t18 = gm_l7;
        unsigned __int128 n19 = (unsigned __int128)((uint64_t)127ULL);
        if (n19 >= 128) gm_trap_shift((uint64_t)n19, "u128", gm_srcqod65l394oc8, 167, 30);
        unsigned __int128 t20 = (unsigned __int128)(t18 >> (int)n19);
        uint8_t t21 = ((t20 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
        gm_l11 = t21;
    } else {
        gm_l11 = ((uint8_t)0);
    }
    uint8_t t22 = gm_l11;
    gm_l8 = t22;
    void * t23 = gm_l0;
    gm_tl874t2atnlh3 t24 = gm_l1;
    gm_t88rcucg1b7uk t25 = gm_l2;
    uint8_t t26 = gm_l8;
    uint8_t t27 = gm_l8;
    if (t27) {
        unsigned __int128 t28 = gm_l7;
        unsigned __int128 r29; bool o30 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t28, &r29);
        unsigned __int128 t31 = r29;
        gm_l12 = t31;
    } else {
        unsigned __int128 t32 = gm_l7;
        gm_l12 = t32;
    }
    unsigned __int128 t33 = gm_l12;
    uint32_t t34 = gm_l5;
    gm_f_shift_check_a7uaho4((void *)&(*(gm_tcula66ipo514 *)gm_out), (void *)NULL, cx, NULL, t23, t24, t25, t26, t33, t34);
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
inline __attribute__((aligned(4))) void gm_f_shift_check_a7uaho4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, uint8_t a3, unsigned __int128 a4, uint32_t a5) {
    struct gm_fr_shift_check_a7uaho4 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* lt */
    uint8_t gm_l3; /* neg */
    unsigned __int128 gm_l4; /* mag */
    uint32_t gm_l5; /* bits */
    gm_b16_8 gm_l6; /* text */
    GM_STACK_CHECK(&F, 710u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_b16_8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    {
        {
            uint8_t t4 = gm_l3;
            uint8_t t5 = (uint8_t)!t4;
            if (!t5) {
                goto gm_lb2;
            }
            unsigned __int128 t6 = gm_l4;
            uint32_t t7 = gm_l5;
            unsigned __int128 t8 = (unsigned __int128)t7;
            uint8_t t9 = (t6 < t8);
            if (!t9) {
                goto gm_lb2;
            }
            unsigned __int128 t10 = gm_l4;
            uint32_t t11 = (uint32_t)t10;
            gm_tcula66ipo514 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
            ((uint8_t *)&t12)[0] = 0;
            GM_ST(uint32_t, ((uint8_t *)&t12 + 8), t11);
            memmove(gm_out, ((uint8_t *)&t12), 32);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t13 = gm_l0;
    uint8_t t14 = gm_f_fast_23em7sv(t13);
    if (t14) {
        uint8_t t15 = gm_l3;
        if (t15) {
            gm_tcula66ipo514 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t16 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t16 + 24, 0);
            ((uint8_t *)&t16)[0] = 0;
            GM_ST(uint32_t, ((uint8_t *)&t16 + 8), ((uint32_t)0ULL));
            memmove(gm_out, ((uint8_t *)&t16), 32);
            goto gm_fdone;
        }
        unsigned __int128 t17 = gm_l4;
        uint64_t t18 = (uint64_t)~((uint64_t)0ULL);
        unsigned __int128 t19 = (unsigned __int128)t18;
        unsigned __int128 t20 = (t17 < t19 ? t17 : t19);
        uint64_t t21 = (uint64_t)t20;
        uint32_t t22 = gm_l5;
        uint64_t t23 = (uint64_t)t22;
        if (t23 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 54, 17);
        uint64_t t24 = (uint64_t)(t21 % t23);
        uint32_t t25 = (uint32_t)t24;
        gm_tcula66ipo514 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
        ((uint8_t *)&t26)[0] = 0;
        GM_ST(uint32_t, ((uint8_t *)&t26 + 8), t25);
        memmove(gm_out, ((uint8_t *)&t26), 32);
        goto gm_fdone;
    }
    uint8_t t27 = gm_l3;
    if (t27) {
        unsigned __int128 t28 = gm_l4;
        gm_tsn2omssg6cpf t29;
        GM_ST(unsigned __int128, ((uint8_t *)&t29 + 0), t28);
        gm_f_format_nacqkaa((void *)&gm_l8, (void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcqod65l394oc8, 56, 22);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_l7 = t31;
    } else {
        unsigned __int128 t32 = gm_l4;
        gm_tsn2omssg6cpf t33;
        GM_ST(unsigned __int128, ((uint8_t *)&t33 + 0), t32);
        gm_f_format_ra7pont((void *)&gm_l9, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcqod65l394oc8, 56, 55);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_l7 = t35;
    }
    gm_b16_8 t36 = gm_l7;
    gm_l6 = t36;
    void * t37 = gm_l0;
    gm_tl874t2atnlh3 t38 = gm_l1;
    gm_b16_8 t39 = gm_l6;
    void * t40 = gm_l0;
    void * t41 = GM_LD(void *, (((uint8_t *)t40) + 8));
    gm_t88rcucg1b7uk t42 = gm_l2;
    gm_b16_8 t43 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6j6smj7qoctu t44;
    GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t39);
    GM_ST(gm_b16_8, ((uint8_t *)&t44 + 16), t43);
    gm_f_format_jp4udcs((void *)&gm_l10, (void *)NULL, cx, NULL, &t44);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcqod65l394oc8, 57, 26);
    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
    gm_t76123gfet73g t48;
    gm_f_trap_qh84l9d((void *)&t48, (void *)NULL, cx, NULL, t37, t38, t47);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tcula66ipo514 t49;
    GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
    ((uint8_t *)&t49)[0] = 1;
    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t49 + 8), t48);
    memmove(gm_out, ((uint8_t *)&t49), 32);
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
