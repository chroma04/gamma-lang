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
typedef struct { _Alignas(8) uint8_t b[0]; } gm_b0_8;
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t pe[6]; } gm_t553jgmqd1tlr;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 a[2]; } gm_t0kl141g5m2qf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t553jgmqd1tlr) == 16 && _Alignof(gm_t553jgmqd1tlr) == 8 && offsetof(gm_t553jgmqd1tlr, f0) == 0 && offsetof(gm_t553jgmqd1tlr, f1) == 8 && offsetof(gm_t553jgmqd1tlr, f2) == 9, "layout of `Policy`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 a[10]; } gm_thr4tus9rimig;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
_Static_assert(sizeof(gm_t0kl141g5m2qf) == 32 && _Alignof(gm_t0kl141g5m2qf) == 8 && offsetof(gm_t0kl141g5m2qf, a) == 0, "layout of `[2][]const string`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; } f1; } gm_t715ajl6o1ipo;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_trqfvmjtt4sij f4; } gm_t9j922kfqcdi9;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t pe[7]; } gm_tecsjmmgeeh52;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thr4tus9rimig) == 160 && _Alignof(gm_thr4tus9rimig) == 8 && offsetof(gm_thr4tus9rimig, a) == 0, "layout of `[10]string`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tu1089hhijadf f0; uint8_t f1; uint8_t pe[7]; } gm_tom1ad5bcqgi1;
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
typedef struct { gm_b16_8 f0; gm_ttu09lij36f8c f1; gm_th2lqbo77pvsc f2; } gm_t03ccdabio1fj;
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t5detta1b4gnk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
_Static_assert(sizeof(gm_t715ajl6o1ipo) == 32 && _Alignof(gm_t715ajl6o1ipo) == 8 && offsetof(gm_t715ajl6o1ipo, f0) == 0 && offsetof(gm_t715ajl6o1ipo, f1) == 8, "layout of `Taken`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t77ebp3ogdble;
_Static_assert(sizeof(gm_t9j922kfqcdi9) == 72 && _Alignof(gm_t9j922kfqcdi9) == 8 && offsetof(gm_t9j922kfqcdi9, f0) == 0 && offsetof(gm_t9j922kfqcdi9, f1) == 8 && offsetof(gm_t9j922kfqcdi9, f2) == 16 && offsetof(gm_t9j922kfqcdi9, f3) == 24 && offsetof(gm_t9j922kfqcdi9, f4) == 32, "layout of `CleanReport`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tecsjmmgeeh52 v1; } f1; } gm_tcl27d4j9lskg;
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tecsjmmgeeh52) == 88 && _Alignof(gm_tecsjmmgeeh52) == 8 && offsetof(gm_tecsjmmgeeh52, f0) == 0 && offsetof(gm_tecsjmmgeeh52, f1) == 40 && offsetof(gm_tecsjmmgeeh52, f2) == 80, "layout of `ScaffoldReport`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; gm_tom1ad5bcqgi1 v1; } f1; } gm_tis0irsd01rfp;
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_tom1ad5bcqgi1) == 64 && _Alignof(gm_tom1ad5bcqgi1) == 8 && offsetof(gm_tom1ad5bcqgi1, f0) == 0 && offsetof(gm_tom1ad5bcqgi1, f1) == 56, "layout of `?Version`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
_Static_assert(sizeof(gm_t03ccdabio1fj) == 80 && _Alignof(gm_t03ccdabio1fj) == 8 && offsetof(gm_t03ccdabio1fj, f0) == 0 && offsetof(gm_t03ccdabio1fj, f1) == 16 && offsetof(gm_t03ccdabio1fj, f2) == 56, "layout of `(url: string, reference: ?(key: string, value: string), subdir: ?string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tv37d3dpt2opu v1; } f1; } gm_t074vrl1hb4f6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t03ccdabio1fj v1; gm_tt008mgut8glb v2; } f1; } gm_t2jeshnsn30ep;
_Static_assert(sizeof(gm_t5detta1b4gnk) == 128 && _Alignof(gm_t5detta1b4gnk) == 8 && offsetof(gm_t5detta1b4gnk, f0) == 0 && offsetof(gm_t5detta1b4gnk, f1) == 8, "layout of `string!Extras`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_t77ebp3ogdble) == 128 && _Alignof(gm_t77ebp3ogdble) == 8 && offsetof(gm_t77ebp3ogdble, f0) == 0 && offsetof(gm_t77ebp3ogdble, f1) == 8, "layout of `Failure!Parsed`");
_Static_assert(sizeof(gm_tcl27d4j9lskg) == 96 && _Alignof(gm_tcl27d4j9lskg) == 8 && offsetof(gm_tcl27d4j9lskg, f0) == 0 && offsetof(gm_tcl27d4j9lskg, f1) == 8, "layout of `string!ScaffoldReport`");
_Static_assert(sizeof(gm_tis0irsd01rfp) == 72 && _Alignof(gm_tis0irsd01rfp) == 8 && offsetof(gm_tis0irsd01rfp, f0) == 0 && offsetof(gm_tis0irsd01rfp, f1) == 8, "layout of `Failure!?Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t074vrl1hb4f6) == 56 && _Alignof(gm_t074vrl1hb4f6) == 8 && offsetof(gm_t074vrl1hb4f6, f0) == 0 && offsetof(gm_t074vrl1hb4f6, f1) == 8, "layout of `string!?Resolution`");
_Static_assert(sizeof(gm_t2jeshnsn30ep) == 88 && _Alignof(gm_t2jeshnsn30ep) == 8 && offsetof(gm_t2jeshnsn30ep, f0) == 0 && offsetof(gm_t2jeshnsn30ep, f1) == 8, "layout of `NewDep`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
struct gm_fr_add_04lv2cc {
    gm_frame h;
    gm_thr4tus9rimig l1; /* keys */
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_trqfvmjtt4sij l8; /* refs */
    gm_t0uuhaljsf410 l9; /* <tmp> */
    gm_tdmi4fk9nhqa7 l10; /* <tmp> */
    gm_tfeab279c16rr l15; /* <tmp> */
    gm_t9bmh80edou9c l16; /* <tmp> */
    gm_t2jeshnsn30ep l20; /* dep */
    gm_ttu09lij36f8c l26; /* reference */
    gm_tu1089hhijadf l30; /* v */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_cache_command_7uqmos8 {
    gm_frame h;
    gm_tr8dvlu9ik4gj l9; /* p */
    gm_t9j922kfqcdi9 l15; /* report */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_contains_a7cccd4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pe7uj7v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fail_o80k3rv {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_fetch_l6q1k6p {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_trqfvmjtt4sij l4; /* given */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_find_root_kjnm6hf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_for_body_0m0ntf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_14t1s38 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_27unvt9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5uun4vl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6dqa07h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6ta43n5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7lopc28 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_da7bimt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dhjqjjt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e2683lk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fbigqms {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gl2aoqn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_is5aam9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j0ggm04 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n537vp2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6eprd4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pah1p62 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tffhbfn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_udqe6i2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ujusd29 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v71csbq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_flag_4n9tu40 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_package_command_ufjt5rp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g9fqnp2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_package_hf828qc {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* p */
    gm_b16_8 l5; /* dir */
    uint8_t l11; /* non_empty */
    uint8_t l12; /* missing */
    gm_tecsjmmgeeh52 l22; /* report */
    gm_t715ajl6o1ipo ret;
    gm_b0_1 w0;
};
struct gm_fr_newest_version_dsisujv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* tags */
    gm_tom1ad5bcqgi1 l3; /* best */
    gm_tu1089hhijadf l7; /* v */
    gm_tu1089hhijadf l8; /* b */
    gm_tis0irsd01rfp ret;
};
struct gm_fr_parse_args_7blm2mp {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t5detta1b4gnk ret;
};
struct gm_fr_push_2tc790g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8bt91lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_relative_path_3klciqu {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* f */
    gm_trqfvmjtt4sij l3; /* t */
    gm_trqfvmjtt4sij l5; /* parts */
    gm_b16_8 ret;
};
struct gm_fr_remove_bar6kga {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_root_of_cluda11 {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_run_command_s4fhqaq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_update_oi0ok0i {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_usage_failure_cgg6v1t {
    gm_frame h;
    gm_t5detta1b4gnk l0; /* r */
    gm_t77ebp3ogdble ret;
};
struct gm_fr_value_of_0km7vvp {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_04lv2cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_cache_command_7uqmos8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_clean_97pd952(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t553jgmqd1tlr a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_contains_a7cccd4(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pe7uj7v(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_suo216n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_fail_o80k3rv_c(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fetch_l6q1k6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_14t1s38(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_27unvt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6dqa07h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_6ta43n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7lopc28(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_da7bimt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_dhjqjjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e2683lk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_fbigqms(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gl2aoqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_is5aam9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_n537vp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tffhbfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_udqe6i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ujusd29(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_v71csbq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_has_flag_4n9tu40(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_package_command_ufjt5rp(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g9fqnp2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_all_q6phjan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_new_package_hf828qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_newest_version_dsisujv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_parse_args_7blm2mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_parse_int_8aueeu2(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2tc790g(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_text_g0qbj1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bt91lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_relative_path_3klciqu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_remote_tags_rssa7s7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_remove_bar6kga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_root_of_cluda11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_run_command_s4fhqaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_sanitize_name_3i9pjsp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_scaffold_ansfc6m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_summary_s3h16om(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_sync_ht164ep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_tag_version_aloeltn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_update_oi0ok0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t77ebp3ogdble gm_f_usage_failure_cgg6v1t_c(gm_t5detta1b4gnk a0);
__attribute__((aligned(4))) void gm_f_value_of_0km7vvp(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_value_of_0km7vvp_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_version_string_k02v4te(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_write_atomic_v74ncu3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf70j0q3sgt1(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cftc9ncjfm7[16] __attribute__((aligned(8)));
extern uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
extern void *const gm_hl87701o8eo1[];
extern const uint8_t gm_s0bmhg7t703ts[22];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0ri8hi6r25m8[37];
extern const uint8_t gm_s2n0v309tod60[70];
extern const uint8_t gm_s2n3d0ogcjggg[14];
extern const uint8_t gm_s30cnsbbn0c4o[4];
extern const uint8_t gm_s3lbmpmt95hvh[7];
extern const uint8_t gm_s3v0kr2fdpfld[5];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s46khuklhbvqd[21];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s4u8fr1tm8vl7[40];
extern const uint8_t gm_s4uu8ali3icr8[40];
extern const uint8_t gm_s5fgj0n2t69gl[8];
extern const uint8_t gm_s5i2d5hm88htd[4];
extern const uint8_t gm_s6pnfrub0rj1t[6];
extern const uint8_t gm_s77fggjckb5ln[41];
extern const uint8_t gm_s7ch91qn11t34[25];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_s7mqv7hf4fso0[27];
extern const uint8_t gm_s7spn02nsqs4u[18];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8f9rv6hrr024[11];
extern const uint8_t gm_s9m6kd6gec368[14];
extern const uint8_t gm_s9mhd8h7bolj3[48];
extern const uint8_t gm_s9pknf9g05okt[3];
extern const uint8_t gm_sa29cuhpvu442[54];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_saicih5tjo4fl[43];
extern const uint8_t gm_sankagtb3ecgn[58];
extern const uint8_t gm_saq7q763oq882[48];
extern const uint8_t gm_saqbkgqthogiq[30];
extern const uint8_t gm_sb9fu6ufbt9g1[17];
extern const uint8_t gm_sbcpd47bv0ru9[63];
extern const uint8_t gm_sbg03hl2obk72[10];
extern const uint8_t gm_sbvsnnbg6vc88[23];
extern const uint8_t gm_sd1smnnii4cig[7];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_se39cci44d9rt[35];
extern const uint8_t gm_se48mcej0nimj[16];
extern const uint8_t gm_seo2gq6ouvlf6[59];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgc7crh43q9tj[6];
extern const uint8_t gm_sgu3j1te831tl[24];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sivh46n0690a4[4];
extern const uint8_t gm_sj56dkk1peu3m[54];
extern const uint8_t gm_sjgng334veu6f[8];
extern const uint8_t gm_ske39klqfe71k[82];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skq2b15b87en9[63];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slrtg04n254p4[19];
extern const uint8_t gm_slskjtl6alfrd[6];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_smsc5gshqt6cd[20];
extern const uint8_t gm_snboa22b96bbc[4];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snl38trih7qf5[19];
extern const uint8_t gm_snmm68ee1rq48[41];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_snvisme5hp379[7];
extern const uint8_t gm_so3hs9kj1bue6[21];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so6e0kl9gkq21[46];
extern const uint8_t gm_so87vf8a6o4mm[11];
extern const uint8_t gm_so9n4mmd960c7[6];
extern const uint8_t gm_soohe0i8iafj3[20];
extern const uint8_t gm_spb7dvkqr71v2[77];
extern const uint8_t gm_sq99f4u8gmi6n[28];
extern const uint8_t gm_sqljkj8r253om[7];
extern const uint8_t gm_sr43s52rmecnf[33];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr88r273kmptc[21];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccr53ejqr1dde[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srgao1ir81l50[14];
extern const uint8_t gm_ss9d51lhe1orq[46];
extern const uint8_t gm_sshhi6fn3vfaa[45];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_suass5sid4b6e[5];
extern const uint8_t gm_sueadt05fevtm[18];
extern const uint8_t gm_suq3bgh3anb87[58];
extern const uint8_t gm_svdospumf3o67[14];
extern const uint8_t gm_svsegvhrrmf7h[18];
extern const gm_tmpl gm_tpl1dleoebfmvhp;
extern const gm_tmpl gm_tpl1lvejpvpq0qh;
extern const gm_tmpl gm_tpl4fm7lnhj31fn;
extern const gm_tmpl gm_tpl75h0vbo3vani;
extern const gm_tmpl gm_tpl8nqn0hncnfd4;
extern const gm_tmpl gm_tpl94ei7rae9si2;
extern const gm_tmpl gm_tpla0a5a633lqve;
extern const gm_tmpl gm_tplbmf4neusqbo5;
extern const gm_tmpl gm_tplc0d2186db2jt;
extern const gm_tmpl gm_tpligbj3kvvunpr;
extern const gm_tmpl gm_tpliqpvgtpo05sn;
extern const gm_tmpl gm_tplj6ss1j7sgouf;
extern const gm_tmpl gm_tpljq1be78i0ptp;
extern const gm_tmpl gm_tplkucr7969socr;
extern const gm_tmpl gm_tpll3bcqsckmobi;
extern const gm_tmpl gm_tplnrs6u0h2oc92;
extern const gm_tmpl gm_tplqipe054or34v;
extern const gm_tmpl gm_tpls4bcac20qnq6;
extern const gm_tmpl gm_tplsc9pqkp31v7e;
extern const gm_tmpl gm_tplu3lqf0bugia3;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pe7uj7v(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pe7uj7v F;
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
inline __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fail_o80k3rv F;
    gm_b16_8 gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_t3ujhu6qph7de t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 8), t2);
    memmove(gm_out, ((uint8_t *)&t3), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_kjnm6hf F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_th2lqbo77pvsc gm_l2;
    gm_b16_8 t5 = gm_l0;
    gm_f_canonicalize_epo7cln((void *)&gm_l2, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
            if (t6) {
                goto gm_lb2;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 11, 8);
    }
gm_lb1:;
    uint8_t t7 = gm_l1;
    if (t7) {
        gm_th2lqbo77pvsc t8;
        memset(&t8, 0, sizeof t8);
        memmove(gm_out, ((uint8_t *)&t8), 24);
        goto gm_fdone;
    }
    gm_b16_8 t9 = gm_l0;
    gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t9);
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
inline __attribute__((aligned(4))) void gm_f_format_14t1s38(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_14t1s38 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa29cuhpvu442));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu3lqf0bugia3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_27unvt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_27unvt9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7spn02nsqs4u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc0d2186db2jt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5uun4vl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s46khuklhbvqd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1lvejpvpq0qh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6dqa07h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6dqa07h F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0bmhg7t703ts));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsc9pqkp31v7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6ta43n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6ta43n5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj56dkk1peu3m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla0a5a633lqve), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7lopc28(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7lopc28 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s77fggjckb5ln));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl94ei7rae9si2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_da7bimt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_da7bimt F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9mhd8h7bolj3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkucr7969socr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dhjqjjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dhjqjjt F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5i2d5hm88htd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpligbj3kvvunpr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_e2683lk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_e2683lk F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4u8fr1tm8vl7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls4bcac20qnq6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fbigqms(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fbigqms F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snl38trih7qf5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpll3bcqsckmobi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gl2aoqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gl2aoqn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so3hs9kj1bue6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnrs6u0h2oc92), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_is5aam9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_is5aam9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svsegvhrrmf7h));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8nqn0hncnfd4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_j0ggm04 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soohe0i8iafj3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqipe054or34v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n537vp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n537vp2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr88r273kmptc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4fm7lnhj31fn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6eprd4 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbg03hl2obk72));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbmf4neusqbo5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pah1p62 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0ri8hi6r25m8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1dleoebfmvhp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tffhbfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tffhbfn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saq7q763oq882));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl75h0vbo3vani), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_udqe6i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_udqe6i2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skq2b15b87en9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj6ss1j7sgouf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ujusd29(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ujusd29 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ske39klqfe71k));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(81));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpliqpvgtpo05sn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_v71csbq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v71csbq F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spb7dvkqr71v2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljq1be78i0ptp), t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t2 = gm_l0;
    gm_b16_8 t3 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_2kkkskr F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_6sd3mnh F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tih6h4avv5nrb gm_l2;
    gm_t5j14m8ru132p gm_l3;
    gm_b16_8 t2 = gm_l0;
    gm_node *pn3 = gm_find(cx, 2126215853);
    if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 229, 13);
    gm_tih6h4avv5nrb t4;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l2) + 8), 32);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l3), 32);
    gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 8));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sb1m5es F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_g9fqnp2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g9fqnp2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_pe7uj7v();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2tc790g(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2tc790g F;
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
        gm_b16_8 t9 = gm_f_empty_slice_pe7uj7v();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8bt91lk(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bt91lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8bt91lk F;
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
inline __attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_usage_failure_cgg6v1t F;
    gm_b16_8 gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 577u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t77ebp3ogdble gm_l2;
    uint8_t t2 = (uint8_t)!((((uint8_t *)a0))[0] != 0);
    if (t2) {
        gm_tr8dvlu9ik4gj t3 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)a0) + 8));
        gm_t77ebp3ogdble t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        ((uint8_t *)&t4)[0] = 0;
        GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&t4 + 8), t3);
        gm_l2 = t4;
    } else {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l1 = t5;
        gm_b16_8 t6 = gm_l1;
        gm_t3ujhu6qph7de t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
        gm_t77ebp3ogdble t8;
        memset(&t8, 0, sizeof t8);
        ((uint8_t *)&t8)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t8 + 8), t7);
        gm_l2 = t8;
    }
    memmove(gm_out, ((uint8_t *)&gm_l2), 128);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_add_04lv2cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_add_04lv2cc F;
    gm_b16_8 gm_l0; /* args */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* problem */
    gm_b16_8 gm_l6; /* e */
    gm_b16_8 gm_l7; /* root */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* k */
    uint64_t gm_l14; /* sources */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* k */
    gm_b16_8 gm_l21; /* url */
    gm_b16_8 gm_l22; /* path */
    gm_b16_8 gm_l23; /* abs */
    gm_b16_8 gm_l24; /* url */
    gm_b16_8 gm_l25; /* problem */
    gm_b16_8 gm_l27; /* k */
    gm_b16_8 gm_l28; /* v */
    gm_b16_8 gm_l29; /* e */
    gm_b16_8 gm_l31; /* manifest_path */
    gm_b16_8 gm_l32; /* e */
    gm_b16_8 gm_l33; /* old_text */
    gm_tf1bjg81u0glv gm_l34; /* e */
    gm_b16_8 gm_l35; /* e */
    gm_b16_8 gm_l36; /* new_text */
    gm_tf1bjg81u0glv gm_l37; /* e */
    gm_b16_8 gm_l38; /* e */
    gm_b16_8 gm_l39; /* e */
    gm_b16_8 gm_l40; /* e2 */
    GM_STACK_CHECK(&F, 9066u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l41;
    gm_th2lqbo77pvsc gm_l42;
    gm_th2lqbo77pvsc gm_l43;
    gm_b16_8 gm_l44;
    gm_t3ujhu6qph7de gm_l45;
    gm_b16_8 gm_l46;
    uint8_t gm_l47;
    gm_th2lqbo77pvsc gm_l48;
    gm_tftb1odc739t5 gm_l49;
    uint8_t gm_l50;
    gm_th2lqbo77pvsc gm_l51;
    uint8_t gm_l52;
    gm_th2lqbo77pvsc gm_l53;
    uint8_t gm_l54;
    gm_th2lqbo77pvsc gm_l55;
    gm_th2lqbo77pvsc gm_l56;
    gm_th2lqbo77pvsc gm_l57;
    uint8_t gm_l58;
    uint8_t gm_l59;
    gm_th2lqbo77pvsc gm_l60;
    gm_th2lqbo77pvsc gm_l61;
    gm_b16_8 gm_l62;
    gm_tabb710d9mq2e gm_l63;
    gm_th2lqbo77pvsc gm_l64;
    gm_th2lqbo77pvsc gm_l65;
    gm_th2lqbo77pvsc gm_l66;
    gm_t62efos6ssciq gm_l67;
    gm_tin97tt4ud0s1 gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_tis0irsd01rfp gm_l70;
    gm_tom1ad5bcqgi1 gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_t3ujhu6qph7de gm_l73;
    gm_b16_8 gm_l74;
    gm_tjgmtdrk88t7m gm_l75;
    gm_tm43kqe3e8b4a gm_l76;
    gm_tabb710d9mq2e gm_l77;
    gm_t3ujhu6qph7de gm_l78;
    gm_b16_8 gm_l79;
    gm_tjgmtdrk88t7m gm_l80;
    gm_tm43kqe3e8b4a gm_l81;
    gm_tabb710d9mq2e gm_l82;
    gm_tl572d7579hns gm_l83;
    gm_t074vrl1hb4f6 gm_l84;
    gm_tv37d3dpt2opu gm_l85;
    gm_tl572d7579hns gm_l86;
    {
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(3));
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sd1smnnii4cig));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(3));
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(3));
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(7));
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sfvspsnv23gbt));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(6));
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(3));
        gm_thr4tus9rimig t51;
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t41);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 16), t42);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 32), t43);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 48), t44);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 64), t45);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 80), t46);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 96), t47);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 112), t48);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 128), t49);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 144), t50);
        F.l1 = t51;
        gm_b16_8 t52 = gm_l0;
        void * t53 = (void *)((uint8_t *)&F.l1);
        uint64_t t54 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t55 = (uintptr_t)((uint64_t)10ULL);
        if (t54 > t55 || t55 > (uintptr_t)((uint64_t)10ULL)) gm_trap_slice(t54, t55, ((uint64_t)10ULL), gm_srccr53ejqr1dde, 189, 45);
        uint64_t t56 = ((uintptr_t)((uint64_t)10ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t53 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        void * t59 = (void *)((uint8_t *)&gm_l2);
        uint64_t t60 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t61 = (uintptr_t)((uint64_t)0ULL);
        if (t60 > t61 || t61 > (uintptr_t)((uint64_t)0ULL)) gm_trap_slice(t60, t61, ((uint64_t)0ULL), gm_srccr53ejqr1dde, 189, 55);
        uint64_t t62 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t59 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62));
        gm_t5detta1b4gnk t64;
        gm_f_parse_args_7blm2mp((void *)&t64, (void *)NULL, cx, NULL, t52, t58, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_usage_failure_cgg6v1t((void *)&gm_l41, &t64);
        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
        if (t65) {
        } else {
            gm_t3ujhu6qph7de t66 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l41) + 8));
            gm_t715ajl6o1ipo t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            ((uint8_t *)&t67)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t67 + 8), t66);
            memmove(gm_out, ((uint8_t *)&t67), 32);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l41) + 8), 120);
        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        uint8_t t69 = (!(t68 == ((uint64_t)1ULL)));
        if (t69) {
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s4uu8ali3icr8));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(39));
            gm_t3ujhu6qph7de t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 8), t70);
            gm_t715ajl6o1ipo t72;
            GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
            ((uint8_t *)&t72)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t72 + 8), t71);
            memmove(gm_out, ((uint8_t *)&t72), 32);
            goto gm_fdone;
        }
        void * t73 = GM_LD(void *, ((uint8_t *)&F.l3));
        uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t74)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t74, gm_srccr53ejqr1dde, 191, 13);
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)t73) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_l4 = t75;
        {
            {
                gm_b16_8 t76 = gm_l4;
                gm_b16_8 t77;
                GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s8f9rv6hrr024));
                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(10));
                gm_f_name_problem_6r4mp11((void *)&gm_l42, (void *)NULL, cx, NULL, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l42))[16] != 0);
                if (!t78) {
                    goto gm_lb2;
                }
                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l42));
                gm_l5 = t79;
                gm_b16_8 t80 = gm_l5;
                gm_t3ujhu6qph7de t81;
                gm_f_fail_o80k3rv((void *)&t81, t80);
                gm_t715ajl6o1ipo t82;
                GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                ((uint8_t *)&t82)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t82 + 8), t81);
                memmove(gm_out, ((uint8_t *)&t82), 32);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t83 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(3));
        gm_f_value_of_0km7vvp((void *)&gm_l43, t83, t84);
        uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l43))[16] != 0);
        if (t85) {
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l43));
            gm_l44 = t86;
        } else {
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(1));
            gm_l44 = t87;
        }
    }
    {
        gm_b16_8 t88 = gm_l44;
        gm_f_root_of_cluda11((void *)&gm_l45, (void *)NULL, cx, NULL, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
        if (t89) {
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
            gm_l46 = t90;
        } else {
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
            gm_l6 = t91;
            gm_b16_8 t92 = gm_l6;
            gm_t3ujhu6qph7de t93;
            gm_f_fail_o80k3rv((void *)&t93, t92);
            gm_t715ajl6o1ipo t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            ((uint8_t *)&t94)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t94 + 8), t93);
            memmove(gm_out, ((uint8_t *)&t94), 32);
            goto gm_fdone;
        }
        gm_b16_8 t95 = gm_l46;
        gm_l7 = t95;
        gm_trqfvmjtt4sij t96;
        gm_f_list_epvducg((void *)&t96, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t96;
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(3));
        gm_b16_8 t98;
        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(6));
        gm_b16_8 t99;
        GM_ST(void *, (uint8_t *)&t99, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(3));
        gm_b16_8 t100;
        GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(7));
        gm_t0uuhaljsf410 t101;
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t97);
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 16), t98);
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 32), t99);
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 48), t100);
        F.l9 = t101;
        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 32));
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 48));
        gm_tdmi4fk9nhqa7 t106;
        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t102);
        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 16), t103);
        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 32), t104);
        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 48), t105);
        F.l10 = t106;
        void * t107 = (void *)((uint8_t *)&F.l10);
        uint64_t t108 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t109 = (uintptr_t)((uint64_t)4ULL);
        if (t108 > t109 || t109 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t108, t109, ((uint64_t)4ULL), gm_srccr53ejqr1dde, 195, 5);
        uint64_t t110 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t111;
        GM_ST(void *, (uint8_t *)&t111, (void *)(((uint8_t *)t107 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(t110));
        gm_l11 = t111;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t112 = gm_l12;
                uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t114 = (t112 < t113);
                if (!t114) {
                    goto gm_lb3;
                }
                {
                    void * t115 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t117 = gm_l12;
                    {
                        {
                            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)t115) + (uintptr_t)t117 * 16));
                            gm_l13 = t118;
                            void * t119 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t120 = gm_l13;
                            gm_f_value_of_0km7vvp((void *)&gm_l48, t119, t120);
                            {
                                {
                                    uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l48))[16] != 0);
                                    if (t121) {
                                        goto gm_lb9;
                                    }
                                    gm_l47 = ((uint8_t)0);
                                    goto gm_lb8;
                                }
gm_lb9:;
                                {
                                    gm_l47 = ((uint8_t)1);
                                    goto gm_lb8;
                                }
gm_lb10:;
                                gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 196, 12);
                            }
gm_lb8:;
                            uint8_t t122 = gm_l47;
                            if (t122) {
                                void * t123 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t124 = gm_l13;
                                gm_tftb1odc739t5 t125 = gm_f_push_3ef746b(t123, t124);
                                gm_l49 = t125;
                                uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srccr53ejqr1dde, 196, 42);
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 195, 5);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t127 = gm_l12;
                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                uint64_t t130 = r128;
                gm_l12 = t130;
            }
        }
gm_lb3:;
    }
    {
        gm_l14 = ((uint64_t)0ULL);
        gm_b16_8 t131;
        GM_ST(void *, (uint8_t *)&t131, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(4));
        gm_b16_8 t132;
        GM_ST(void *, (uint8_t *)&t132, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(3));
        gm_b16_8 t133;
        GM_ST(void *, (uint8_t *)&t133, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(3));
        gm_tfeab279c16rr t134;
        GM_ST(gm_b16_8, ((uint8_t *)&t134 + 0), t131);
        GM_ST(gm_b16_8, ((uint8_t *)&t134 + 16), t132);
        GM_ST(gm_b16_8, ((uint8_t *)&t134 + 32), t133);
        F.l15 = t134;
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 16));
        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 32));
        gm_t9bmh80edou9c t138;
        GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t135);
        GM_ST(gm_b16_8, ((uint8_t *)&t138 + 16), t136);
        GM_ST(gm_b16_8, ((uint8_t *)&t138 + 32), t137);
        F.l16 = t138;
        void * t139 = (void *)((uint8_t *)&F.l16);
        uint64_t t140 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t141 = (uintptr_t)((uint64_t)3ULL);
        if (t140 > t141 || t141 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t140, t141, ((uint64_t)3ULL), gm_srccr53ejqr1dde, 199, 5);
        uint64_t t142 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t143;
        GM_ST(void *, (uint8_t *)&t143, (void *)(((uint8_t *)t139 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(t142));
        gm_l17 = t143;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t144 = gm_l18;
                uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t146 = (t144 < t145);
                if (!t146) {
                    goto gm_lb11;
                }
                {
                    void * t147 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t149 = gm_l18;
                    {
                        {
                            gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)t147) + (uintptr_t)t149 * 16));
                            gm_l19 = t150;
                            void * t151 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t152 = gm_l19;
                            gm_f_value_of_0km7vvp((void *)&gm_l51, t151, t152);
                            {
                                {
                                    uint8_t t153 = (uint8_t)((((uint8_t *)&gm_l51))[16] != 0);
                                    if (t153) {
                                        goto gm_lb17;
                                    }
                                    gm_l50 = ((uint8_t)0);
                                    goto gm_lb16;
                                }
gm_lb17:;
                                {
                                    gm_l50 = ((uint8_t)1);
                                    goto gm_lb16;
                                }
gm_lb18:;
                                gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 200, 12);
                            }
gm_lb16:;
                            uint8_t t154 = gm_l50;
                            if (t154) {
                                uint64_t t155 = gm_l14;
                                uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                                if (o157) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 200, 42);
                                uint64_t t158 = r156;
                                gm_l14 = t158;
                            }
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 199, 5);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t159 = gm_l18;
                uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                uint64_t t162 = r160;
                gm_l18 = t162;
            }
        }
gm_lb11:;
        uint64_t t163 = gm_l14;
        uint8_t t164 = (t163 > ((uint64_t)1ULL));
        if (t164) {
            gm_b16_8 t165;
            GM_ST(void *, (uint8_t *)&t165, (void *)(gm_so6e0kl9gkq21));
            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(45));
            gm_t3ujhu6qph7de t166;
            GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 8), t165);
            gm_t715ajl6o1ipo t167;
            GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
            ((uint8_t *)&t167)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t167 + 8), t166);
            memmove(gm_out, ((uint8_t *)&t167), 32);
            goto gm_fdone;
        }
        {
            {
                void * t168 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t169;
                GM_ST(void *, (uint8_t *)&t169, (void *)(gm_sd1smnnii4cig));
                GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(6));
                gm_f_value_of_0km7vvp((void *)&gm_l53, t168, t169);
                {
                    {
                        uint8_t t170 = (uint8_t)((((uint8_t *)&gm_l53))[16] != 0);
                        if (t170) {
                            goto gm_lb22;
                        }
                        gm_l52 = ((uint8_t)0);
                        goto gm_lb21;
                    }
gm_lb22:;
                    {
                        gm_l52 = ((uint8_t)1);
                        goto gm_lb21;
                    }
gm_lb23:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 203, 8);
                }
gm_lb21:;
                uint8_t t171 = gm_l52;
                if (!t171) {
                    goto gm_lb20;
                }
                void * t172 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t173;
                GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sa2h9vonan0ma));
                GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(3));
                gm_f_value_of_0km7vvp((void *)&gm_l55, t172, t173);
                {
                    {
                        uint8_t t174 = (uint8_t)((((uint8_t *)&gm_l55))[16] != 0);
                        if (t174) {
                            goto gm_lb25;
                        }
                        gm_l54 = ((uint8_t)1);
                        goto gm_lb24;
                    }
gm_lb25:;
                    {
                        gm_l54 = ((uint8_t)0);
                        goto gm_lb24;
                    }
gm_lb26:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 203, 47);
                }
gm_lb24:;
                uint8_t t175 = gm_l54;
                if (!t175) {
                    goto gm_lb20;
                }
                gm_b16_8 t176;
                GM_ST(void *, (uint8_t *)&t176, (void *)(gm_s7ch91qn11t34));
                GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(24));
                gm_t3ujhu6qph7de t177;
                GM_ST(uint8_t, (uint8_t *)&t177 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t177 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t177 + 8), t176);
                gm_t715ajl6o1ipo t178;
                GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t178 + 4, 0);
                ((uint8_t *)&t178)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t178 + 8), t177);
                memmove(gm_out, ((uint8_t *)&t178), 32);
                goto gm_fdone;
            }
gm_lb20:;
        }
gm_lb19:;
    }
    {
        gm_b16_8 t179;
        GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(0));
        gm_t2jeshnsn30ep t180;
        memset(&t180, 0, sizeof t180);
        GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t180 + 8), t179);
        F.l20 = t180;
        {
            {
                void * t181 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t182;
                GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sa2h9vonan0ma));
                GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(3));
                gm_f_value_of_0km7vvp((void *)&gm_l56, t181, t182);
                uint8_t t183 = (uint8_t)((((uint8_t *)&gm_l56))[16] != 0);
                if (!t183) {
                    goto gm_lb28;
                }
                gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                gm_l21 = t184;
                uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                uint8_t t186 = (t185 > ((uint64_t)0ULL));
                if (t186) {
                    gm_b16_8 t187;
                    GM_ST(void *, (uint8_t *)&t187, (void *)(gm_suq3bgh3anb87));
                    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(57));
                    gm_t3ujhu6qph7de t188;
                    GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t188 + 8), t187);
                    gm_t715ajl6o1ipo t189;
                    GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                    ((uint8_t *)&t189)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t189 + 8), t188);
                    memmove(gm_out, ((uint8_t *)&t189), 32);
                    goto gm_fdone;
                }
                gm_b16_8 t190 = gm_l21;
                void * t191 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t192;
                GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sd1smnnii4cig));
                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(6));
                gm_th2lqbo77pvsc t193;
                gm_f_value_of_0km7vvp((void *)&t193, t191, t192);
                void * t194 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t195;
                GM_ST(void *, (uint8_t *)&t195, (void *)(gm_sfvspsnv23gbt));
                GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(6));
                gm_th2lqbo77pvsc t196;
                gm_f_value_of_0km7vvp((void *)&t196, t194, t195);
                gm_tt008mgut8glb t197;
                GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t190);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t197 + 16), t193);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t197 + 40), t196);
                gm_t2jeshnsn30ep t198;
                GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t198 + 72, 0);
                GM_ST(uint64_t, (uint8_t *)&t198 + 80, 0);
                GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)2ULL));
                GM_ST(gm_tt008mgut8glb, ((uint8_t *)&t198 + 8), t197);
                F.l20 = t198;
                goto gm_lb27;
            }
gm_lb28:;
            {
                {
                    void * t199 = (void *)((uint8_t *)&F.l3);
                    gm_b16_8 t200;
                    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_smimi08sfrg0n));
                    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(4));
                    gm_f_value_of_0km7vvp((void *)&gm_l57, t199, t200);
                    uint8_t t201 = (uint8_t)((((uint8_t *)&gm_l57))[16] != 0);
                    if (!t201) {
                        goto gm_lb30;
                    }
                    gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l57));
                    gm_l22 = t202;
                    uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                    uint8_t t204 = (t203 > ((uint64_t)0ULL));
                    if (t204) {
                        gm_l58 = ((uint8_t)1);
                    } else {
                        void * t205 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t206;
                        GM_ST(void *, (uint8_t *)&t206, (void *)(gm_sfvspsnv23gbt));
                        GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(6));
                        gm_f_value_of_0km7vvp((void *)&gm_l60, t205, t206);
                        {
                            {
                                uint8_t t207 = (uint8_t)((((uint8_t *)&gm_l60))[16] != 0);
                                if (t207) {
                                    goto gm_lb32;
                                }
                                gm_l59 = ((uint8_t)0);
                                goto gm_lb31;
                            }
gm_lb32:;
                            {
                                gm_l59 = ((uint8_t)1);
                                goto gm_lb31;
                            }
gm_lb33:;
                            gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 211, 34);
                        }
gm_lb31:;
                        uint8_t t208 = gm_l59;
                        gm_l58 = t208;
                    }
                    uint8_t t209 = gm_l58;
                    if (t209) {
                        gm_b16_8 t210;
                        GM_ST(void *, (uint8_t *)&t210, (void *)(gm_s2n0v309tod60));
                        GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(69));
                        gm_t3ujhu6qph7de t211;
                        GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 8), t210);
                        gm_t715ajl6o1ipo t212;
                        GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                        ((uint8_t *)&t212)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t212 + 8), t211);
                        memmove(gm_out, ((uint8_t *)&t212), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t213 = gm_l22;
                    gm_f_canonicalize_epo7cln((void *)&gm_l61, (void *)NULL, cx, NULL, t213);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t214 = (uint8_t)((((uint8_t *)&gm_l61))[16] != 0);
                    if (t214) {
                        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l61));
                        gm_l62 = t215;
                    } else {
                        gm_b16_8 t216 = gm_l22;
                        gm_tnpifv20irnph t217;
                        GM_ST(gm_b16_8, ((uint8_t *)&t217 + 0), t216);
                        gm_f_format_udqe6i2((void *)&gm_l63, (void *)NULL, cx, NULL, t217);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t218 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                        if (!t218) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_srccr53ejqr1dde, 215, 26);
                        gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                        gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                        gm_t3ujhu6qph7de t221;
                        gm_f_fail_o80k3rv((void *)&t221, t220);
                        gm_t715ajl6o1ipo t222;
                        GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                        ((uint8_t *)&t222)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t222 + 8), t221);
                        memmove(gm_out, ((uint8_t *)&t222), 32);
                        goto gm_fdone;
                    }
                    gm_b16_8 t223 = gm_l62;
                    gm_l23 = t223;
                    gm_b16_8 t224 = gm_l7;
                    gm_b16_8 t225 = gm_l23;
                    gm_b16_8 t226 = gm_f_relative_path_3klciqu((void *)NULL, cx, NULL, t224, t225);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2jeshnsn30ep t227;
                    memset(&t227, 0, sizeof t227);
                    GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t227 + 8), t226);
                    F.l20 = t227;
                    goto gm_lb29;
                }
gm_lb30:;
                {
                    {
                        void * t228 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t229;
                        GM_ST(void *, (uint8_t *)&t229, (void *)(gm_s0k6ioi39f878));
                        GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(3));
                        gm_f_value_of_0km7vvp((void *)&gm_l64, t228, t229);
                        uint8_t t230 = (uint8_t)((((uint8_t *)&gm_l64))[16] != 0);
                        if (!t230) {
                            goto gm_lb35;
                        }
                        gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l64));
                        gm_l24 = t231;
                        {
                            {
                                gm_b16_8 t232 = gm_l24;
                                gm_f_url_problem_38kdh3a((void *)&gm_l65, (void *)NULL, cx, NULL, t232);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t233 = (uint8_t)((((uint8_t *)&gm_l65))[16] != 0);
                                if (!t233) {
                                    goto gm_lb37;
                                }
                                gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l65));
                                gm_l25 = t234;
                                gm_b16_8 t235 = gm_l25;
                                gm_t3ujhu6qph7de t236;
                                GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t236 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t236 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t236 + 8), t235);
                                gm_t715ajl6o1ipo t237;
                                GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                                ((uint8_t *)&t237)[0] = 1;
                                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t237 + 8), t236);
                                memmove(gm_out, ((uint8_t *)&t237), 32);
                                goto gm_fdone;
                            }
gm_lb37:;
                        }
gm_lb36:;
                        uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                        uint8_t t239 = (t238 > ((uint64_t)1ULL));
                        if (t239) {
                            gm_b16_8 t240;
                            GM_ST(void *, (uint8_t *)&t240, (void *)(gm_sankagtb3ecgn));
                            GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(57));
                            gm_t3ujhu6qph7de t241;
                            GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t241 + 8), t240);
                            gm_t715ajl6o1ipo t242;
                            GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
                            ((uint8_t *)&t242)[0] = 1;
                            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t242 + 8), t241);
                            memmove(gm_out, ((uint8_t *)&t242), 32);
                            goto gm_fdone;
                        }
                        gm_ttu09lij36f8c t243;
                        memset(&t243, 0, sizeof t243);
                        F.l26 = t243;
                        uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                        uint8_t t245 = ((t244 == ((uint64_t)1ULL)));
                        if (t245) {
                            void * t246 = GM_LD(void *, ((uint8_t *)&F.l8));
                            uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t247)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t247, gm_srccr53ejqr1dde, 223, 18);
                            gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)t246) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_l27 = t248;
                            void * t249 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t250 = gm_l27;
                            gm_f_value_of_0km7vvp((void *)&gm_l66, t249, t250);
                            uint8_t t251 = (uint8_t)((((uint8_t *)&gm_l66))[16] != 0);
                            if (!t251) gm_trap("unwrap of null", gm_srccr53ejqr1dde, 224, 18);
                            gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l66));
                            gm_l28 = t252;
                            gm_b16_8 t253 = gm_l27;
                            gm_b16_8 t254;
                            GM_ST(void *, (uint8_t *)&t254, (void *)(gm_sla3alt5340mf));
                            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(7));
                            uint8_t t255 = (gm_eq0iqiubfm7e(((uint8_t *)&t253), ((uint8_t *)&t254)));
                            if (t255) {
                                gm_b16_8 t256 = gm_l28;
                                gm_b16_8 t257 = gm_f_trim_ascii_c9j36d1(t256);
                                gm_f_parse_version_req_alipvfi((void *)&gm_l67, (void *)NULL, cx, NULL, t257);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t258 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                if (t258) {
                                    memmove(((uint8_t *)&gm_l68), (((uint8_t *)&gm_l67) + 8), 40);
                                } else {
                                    gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                    gm_l29 = t259;
                                    gm_b16_8 t260 = gm_l28;
                                    gm_b16_8 t261 = gm_l29;
                                    gm_t6j6smj7qoctu t262;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t262 + 0), t260);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t262 + 16), t261);
                                    gm_f_format_pah1p62((void *)&gm_l69, (void *)NULL, cx, NULL, &t262);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t263 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                    if (!t263) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_srccr53ejqr1dde, 227, 34);
                                    gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                                    gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                                    gm_t3ujhu6qph7de t266;
                                    gm_f_fail_o80k3rv((void *)&t266, t265);
                                    gm_t715ajl6o1ipo t267;
                                    GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
                                    ((uint8_t *)&t267)[0] = 1;
                                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t267 + 8), t266);
                                    memmove(gm_out, ((uint8_t *)&t267), 32);
                                    goto gm_fdone;
                                }
                            }
                            gm_b16_8 t268 = gm_l27;
                            gm_b16_8 t269 = gm_l28;
                            gm_t6j6smj7qoctu t270;
                            GM_ST(gm_b16_8, ((uint8_t *)&t270 + 0), t268);
                            GM_ST(gm_b16_8, ((uint8_t *)&t270 + 16), t269);
                            gm_ttu09lij36f8c t271;
                            GM_ST(uint8_t, (uint8_t *)&t271 + 33, 0);
                            GM_ST(uint16_t, (uint8_t *)&t271 + 34, 0);
                            GM_ST(uint32_t, (uint8_t *)&t271 + 36, 0);
                            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t271), t270);
                            ((uint8_t *)&t271)[32] = 1;
                            F.l26 = t271;
                        } else {
                            {
                                {
                                    gm_b16_8 t272 = gm_l24;
                                    gm_f_newest_version_dsisujv((void *)&gm_l70, (void *)NULL, cx, NULL, t272);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t273 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                    if (t273) {
                                    } else {
                                        gm_t3ujhu6qph7de t274 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l70) + 8));
                                        gm_t715ajl6o1ipo t275;
                                        GM_ST(uint8_t, (uint8_t *)&t275 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t275 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
                                        ((uint8_t *)&t275)[0] = 1;
                                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t275 + 8), t274);
                                        memmove(gm_out, ((uint8_t *)&t275), 32);
                                        goto gm_fdone;
                                    }
                                    memmove(((uint8_t *)&gm_l71), (((uint8_t *)&gm_l70) + 8), 64);
                                    uint8_t t276 = (uint8_t)((((uint8_t *)&gm_l71))[56] != 0);
                                    if (!t276) {
                                        goto gm_lb39;
                                    }
                                    memmove(((uint8_t *)&F.l30), ((uint8_t *)&gm_l71), 56);
                                    gm_b16_8 t277;
                                    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_sla3alt5340mf));
                                    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(7));
                                    gm_b16_8 t278 = gm_f_version_string_k02v4te((void *)NULL, cx, NULL, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l30)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t279;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t279 + 0), t278);
                                    gm_f_format_dhjqjjt((void *)&gm_l72, (void *)NULL, cx, NULL, t279);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                    if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_srccr53ejqr1dde, 232, 49);
                                    gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                                    gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                                    gm_t6j6smj7qoctu t283;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t277);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t283 + 16), t282);
                                    gm_ttu09lij36f8c t284;
                                    GM_ST(uint8_t, (uint8_t *)&t284 + 33, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t284 + 34, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t284 + 36, 0);
                                    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t284), t283);
                                    ((uint8_t *)&t284)[32] = 1;
                                    F.l26 = t284;
                                    goto gm_lb38;
                                }
gm_lb39:;
                            }
gm_lb38:;
                        }
                        gm_b16_8 t285 = gm_l24;
                        void * t286 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t287;
                        GM_ST(void *, (uint8_t *)&t287, (void *)(gm_sfvspsnv23gbt));
                        GM_ST(uintptr_t, (uint8_t *)&t287 + 8, (uintptr_t)(6));
                        gm_ttu09lij36f8c t288 = GM_LD(gm_ttu09lij36f8c, ((uint8_t *)&F.l26));
                        gm_th2lqbo77pvsc t289;
                        gm_f_value_of_0km7vvp((void *)&t289, t286, t287);
                        gm_t03ccdabio1fj t290;
                        GM_ST(gm_b16_8, ((uint8_t *)&t290 + 0), t285);
                        GM_ST(gm_ttu09lij36f8c, ((uint8_t *)&t290 + 16), t288);
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t290 + 56), t289);
                        gm_t2jeshnsn30ep t291;
                        GM_ST(uint8_t, (uint8_t *)&t291 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t291 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t291 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t291 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t03ccdabio1fj, ((uint8_t *)&t291 + 8), t290);
                        F.l20 = t291;
                        goto gm_lb34;
                    }
gm_lb35:;
                    gm_b16_8 t292;
                    GM_ST(void *, (uint8_t *)&t292, (void *)(gm_seo2gq6ouvlf6));
                    GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(58));
                    gm_t3ujhu6qph7de t293;
                    GM_ST(uint8_t, (uint8_t *)&t293 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t293 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t293 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t293 + 8), t292);
                    gm_t715ajl6o1ipo t294;
                    GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                    ((uint8_t *)&t294)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t294 + 8), t293);
                    memmove(gm_out, ((uint8_t *)&t294), 32);
                    goto gm_fdone;
                }
gm_lb34:;
            }
gm_lb29:;
        }
gm_lb27:;
    }
    {
        gm_b16_8 t295 = gm_l7;
        gm_b16_8 t296;
        GM_ST(void *, (uint8_t *)&t296, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t296 + 8, (uintptr_t)(10));
        gm_b16_8 t297 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t295, t296);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l31 = t297;
        gm_b16_8 t298 = gm_l31;
        gm_f_read_text_g0qbj1t((void *)&gm_l73, (void *)NULL, cx, NULL, t298);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t299 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
        if (t299) {
            gm_b16_8 t300 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
            gm_l74 = t300;
        } else {
            gm_b16_8 t301 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
            gm_l32 = t301;
            gm_b16_8 t302 = gm_l32;
            gm_t3ujhu6qph7de t303;
            gm_f_fail_o80k3rv((void *)&t303, t302);
            gm_t715ajl6o1ipo t304;
            GM_ST(uint8_t, (uint8_t *)&t304 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t304 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t304 + 4, 0);
            ((uint8_t *)&t304)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t304 + 8), t303);
            memmove(gm_out, ((uint8_t *)&t304), 32);
            goto gm_fdone;
        }
        gm_b16_8 t305 = gm_l74;
        gm_l33 = t305;
        gm_b16_8 t306 = gm_l33;
        gm_f_parse_manifest_b61s3ln((void *)&gm_l75, (void *)NULL, cx, NULL, t306);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
        if (t307) {
            memmove(((uint8_t *)&gm_l76), (((uint8_t *)&gm_l75) + 8), 320);
        } else {
            memmove(((uint8_t *)&gm_l34), (((uint8_t *)&gm_l75) + 8), 24);
            gm_b16_8 t308 = gm_l31;
            uint64_t t309 = GM_LD(uint64_t, ((uint8_t *)&gm_l34));
            gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
            gm_tq3m5qgvkpje8 t311;
            GM_ST(gm_b16_8, ((uint8_t *)&t311 + 0), t308);
            GM_ST(uint64_t, ((uint8_t *)&t311 + 16), t309);
            GM_ST(gm_b16_8, ((uint8_t *)&t311 + 24), t310);
            gm_f_format_n6eprd4((void *)&gm_l77, (void *)NULL, cx, NULL, &t311);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t312 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
            if (!t312) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_srccr53ejqr1dde, 241, 22);
            gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
            gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
            gm_t3ujhu6qph7de t315;
            gm_f_fail_o80k3rv((void *)&t315, t314);
            gm_t715ajl6o1ipo t316;
            GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t316 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t316 + 4, 0);
            ((uint8_t *)&t316)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t316 + 8), t315);
            memmove(gm_out, ((uint8_t *)&t316), 32);
            goto gm_fdone;
        }
        gm_b16_8 t317 = gm_l33;
        gm_b16_8 t318 = gm_l4;
        gm_tm43kqe3e8b4a t319 = GM_LD(gm_tm43kqe3e8b4a, ((uint8_t *)&gm_l76));
        gm_t2jeshnsn30ep t320 = GM_LD(gm_t2jeshnsn30ep, ((uint8_t *)&F.l20));
        gm_f_add_dependency_jmjffrp((void *)&gm_l78, (void *)NULL, cx, NULL, t317, t318, &t320);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t321 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
        if (t321) {
            gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
            gm_l79 = t322;
        } else {
            gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
            gm_l35 = t323;
            gm_b16_8 t324 = gm_l35;
            gm_t3ujhu6qph7de t325;
            gm_f_fail_o80k3rv((void *)&t325, t324);
            gm_t715ajl6o1ipo t326;
            GM_ST(uint8_t, (uint8_t *)&t326 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t326 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t326 + 4, 0);
            ((uint8_t *)&t326)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t326 + 8), t325);
            memmove(gm_out, ((uint8_t *)&t326), 32);
            goto gm_fdone;
        }
        gm_b16_8 t327 = gm_l79;
        gm_l36 = t327;
        gm_b16_8 t328 = gm_l36;
        gm_f_parse_manifest_b61s3ln((void *)&gm_l80, (void *)NULL, cx, NULL, t328);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t329 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
        if (t329) {
            memmove(((uint8_t *)&gm_l81), (((uint8_t *)&gm_l80) + 8), 320);
        } else {
            memmove(((uint8_t *)&gm_l37), (((uint8_t *)&gm_l80) + 8), 24);
            gm_b16_8 t330 = gm_l31;
            uint64_t t331 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
            gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
            gm_tq3m5qgvkpje8 t333;
            GM_ST(gm_b16_8, ((uint8_t *)&t333 + 0), t330);
            GM_ST(uint64_t, ((uint8_t *)&t333 + 16), t331);
            GM_ST(gm_b16_8, ((uint8_t *)&t333 + 24), t332);
            gm_f_format_n6eprd4((void *)&gm_l82, (void *)NULL, cx, NULL, &t333);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t334 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
            if (!t334) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 8, gm_srccr53ejqr1dde, 246, 22);
            gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
            gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
            gm_t3ujhu6qph7de t337;
            gm_f_fail_o80k3rv((void *)&t337, t336);
            gm_t715ajl6o1ipo t338;
            GM_ST(uint8_t, (uint8_t *)&t338 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t338 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t338 + 4, 0);
            ((uint8_t *)&t338)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t338 + 8), t337);
            memmove(gm_out, ((uint8_t *)&t338), 32);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t339 = gm_l31;
        gm_b16_8 t340 = gm_l36;
        gm_tm43kqe3e8b4a t341 = GM_LD(gm_tm43kqe3e8b4a, ((uint8_t *)&gm_l81));
        gm_f_write_atomic_v74ncu3((void *)&gm_l83, (void *)NULL, cx, NULL, t339, t340);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t342 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
        if (t342) {
        } else {
            gm_b16_8 t343 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
            gm_l38 = t343;
            gm_b16_8 t344 = gm_l38;
            gm_t3ujhu6qph7de t345;
            gm_f_fail_o80k3rv((void *)&t345, t344);
            gm_t715ajl6o1ipo t346;
            GM_ST(uint8_t, (uint8_t *)&t346 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t346 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t346 + 4, 0);
            ((uint8_t *)&t346)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t346 + 8), t345);
            memmove(gm_out, ((uint8_t *)&t346), 32);
            goto gm_fdone;
        }
        gm_b16_8 t347 = gm_l7;
        gm_tqs5p4n2tq19v t348;
        GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t348 + 1), ((uint8_t)0));
        gm_tl572d7579hns t349;
        GM_ST(uint8_t, (uint8_t *)&t349 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t349 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t349 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t349 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t349 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t349 + 0), ((uint8_t)0ULL));
        gm_f_sync_ht164ep((void *)&gm_l84, (void *)NULL, cx, NULL, t347, t348, &t349, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t350 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
        if (t350) {
            memmove(((uint8_t *)&gm_l85), (((uint8_t *)&gm_l84) + 8), 48);
        } else {
            gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l84) + 8));
            gm_l39 = t351;
            gm_b16_8 t352 = gm_l31;
            gm_b16_8 t353 = gm_l33;
            gm_f_write_atomic_v74ncu3((void *)&gm_l86, (void *)NULL, cx, NULL, t352, t353);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t354 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
            if (t354) {
            } else {
                gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 8));
                gm_l40 = t355;
                gm_b16_8 t356 = gm_l40;
                gm_t3ujhu6qph7de t357;
                gm_f_fail_o80k3rv((void *)&t357, t356);
                gm_t715ajl6o1ipo t358;
                GM_ST(uint8_t, (uint8_t *)&t358 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t358 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t358 + 4, 0);
                ((uint8_t *)&t358)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t358 + 8), t357);
                memmove(gm_out, ((uint8_t *)&t358), 32);
                goto gm_fdone;
            }
            gm_b16_8 t359 = gm_l39;
            gm_t3ujhu6qph7de t360;
            gm_f_fail_o80k3rv((void *)&t360, t359);
            gm_t715ajl6o1ipo t361;
            GM_ST(uint8_t, (uint8_t *)&t361 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t361 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t361 + 4, 0);
            ((uint8_t *)&t361)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t361 + 8), t360);
            memmove(gm_out, ((uint8_t *)&t361), 32);
            goto gm_fdone;
        }
        gm_b16_8 t362 = gm_l4;
        gm_tv37d3dpt2opu t363 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)&gm_l85));
        if (gm_pf454qfrpt6r(cx, gm_svdospumf3o67, 13, gm_srccr53ejqr1dde, 253, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t362) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t362)), GM_LD(uintptr_t, ((uint8_t *)&t362) + 8), gm_srccr53ejqr1dde, 253, 5))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sueadt05fevtm, 17, gm_srccr53ejqr1dde, 253, 5)) {
            goto gm_lad;
        }
        gm_t715ajl6o1ipo t364;
        GM_ST(uint8_t, (uint8_t *)&t364 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t364 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t364 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t364 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t364 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t364 + 24, 0);
        ((uint8_t *)&t364)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t364 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t364), 32);
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
__attribute__((aligned(4))) void gm_f_cache_command_7uqmos8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_cache_command_7uqmos8 F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* sub */
    gm_b16_8 gm_l2; /* rest */
    gm_th2lqbo77pvsc gm_l3; /* dir */
    gm_b0_8 gm_l4; /* <temporary array> */
    gm_b0_8 gm_l5; /* <temporary array> */
    gm_b16_8 gm_l6; /* d */
    gm_tpmdr2hmdhsqr gm_l7; /* <temporary array> */
    gm_t0kl141g5m2qf gm_l8; /* <temporary array> */
    uint64_t gm_l10; /* days */
    gm_b16_8 gm_l11; /* v */
    gm_b16_8 gm_l12; /* d */
    uint64_t gm_l13; /* secs */
    gm_t553jgmqd1tlr gm_l14; /* policy */
    GM_STACK_CHECK(&F, 3071u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l16;
    gm_b16_8 gm_l17;
    gm_t77ebp3ogdble gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_tutnubt6u4hoq gm_l20;
    uint64_t gm_l21;
    gm_tabb710d9mq2e gm_l22;
    uint8_t gm_l23;
    gm_th2lqbo77pvsc gm_l24;
    gm_b16_8 gm_l25;
    gm_tf5srh4mp4b3i gm_l26;
    uint64_t gm_l27;
    gm_tf5srh4mp4b3i gm_l28;
    uint64_t gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tabb710d9mq2e gm_l31;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
    if (t14) {
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sshhi6fn3vfaa));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(44));
        gm_t3ujhu6qph7de t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
        gm_t715ajl6o1ipo t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        ((uint8_t *)&t17)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t17 + 8), t16);
        memmove(gm_out, ((uint8_t *)&t17), 32);
        goto gm_fdone;
    }
    void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t19, gm_srccr53ejqr1dde, 292, 12);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t18) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_l1 = t20;
    void * t21 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t23 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t22) gm_trap_slice(t23, t24, t22, gm_srccr53ejqr1dde, 293, 13);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)1ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    gm_l2 = t26;
    gm_f_cache_dir_81h90qu((void *)&gm_l3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            gm_b16_8 t27 = gm_l1;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_snboa22b96bbc));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
            uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
            if (!t29) {
                goto gm_lb2;
            }
            gm_b16_8 t30 = gm_l2;
            void * t31 = (void *)((uint8_t *)&gm_l4);
            uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
            if (t32 > t33 || t33 > (uintptr_t)((uint64_t)0ULL)) gm_trap_slice(t32, t33, ((uint64_t)0ULL), gm_srccr53ejqr1dde, 298, 52);
            uint64_t t34 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t31 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34));
            void * t36 = (void *)((uint8_t *)&gm_l5);
            uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
            if (t37 > t38 || t38 > (uintptr_t)((uint64_t)0ULL)) gm_trap_slice(t37, t38, ((uint64_t)0ULL), gm_srccr53ejqr1dde, 298, 69);
            uint64_t t39 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t36 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39));
            gm_t5detta1b4gnk t41;
            gm_f_parse_args_7blm2mp((void *)&t41, (void *)NULL, cx, NULL, t30, t35, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_usage_failure_cgg6v1t((void *)&gm_l16, &t41);
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (t42) {
            } else {
                gm_t3ujhu6qph7de t43 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l16) + 8));
                gm_t715ajl6o1ipo t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t44 + 8), t43);
                memmove(gm_out, ((uint8_t *)&t44), 32);
                goto gm_fdone;
            }
            uint8_t t45 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            gm_tr8dvlu9ik4gj t46 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&gm_l16) + 8));
            if (t45) {
                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                gm_l17 = t47;
            } else {
                gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)gm_cftc9ncjfm7));
                gm_t3ujhu6qph7de t49;
                gm_f_fail_o80k3rv((void *)&t49, t48);
                gm_t715ajl6o1ipo t50;
                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                ((uint8_t *)&t50)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t50 + 8), t49);
                memmove(gm_out, ((uint8_t *)&t50), 32);
                goto gm_fdone;
            }
            gm_b16_8 t51 = gm_l17;
            gm_l6 = t51;
            gm_b16_8 t52 = gm_l6;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t52) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t52)), GM_LD(uintptr_t, ((uint8_t *)&t52) + 8), gm_srccr53ejqr1dde, 300, 13))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 300, 13)) {
                goto gm_lad;
            }
            gm_t715ajl6o1ipo t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 24, 0);
            ((uint8_t *)&t53)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t53 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t53), 32);
            goto gm_fdone;
        }
gm_lb2:;
        {
            gm_b16_8 t54 = gm_l1;
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_slskjtl6alfrd));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(5));
            uint8_t t56 = (gm_eq0iqiubfm7e(((uint8_t *)&t54), ((uint8_t *)&t55)));
            if (!t56) {
                goto gm_lb3;
            }
            gm_b16_8 t57 = gm_l2;
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_so87vf8a6o4mm));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(10));
            gm_tpmdr2hmdhsqr t59;
            GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
            gm_l7 = t59;
            void * t60 = (void *)((uint8_t *)&gm_l7);
            uint64_t t61 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t62 = (uintptr_t)((uint64_t)1ULL);
            if (t61 > t62 || t62 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t61, t62, ((uint64_t)1ULL), gm_srccr53ejqr1dde, 304, 53);
            uint64_t t63 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t60 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(t63));
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s0nf1rg04f3m5));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(3));
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sjgng334veu6f));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(7));
            gm_t0kl141g5m2qf t67;
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t65);
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 16), t66);
            gm_l8 = t67;
            void * t68 = (void *)((uint8_t *)&gm_l8);
            uint64_t t69 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t70 = (uintptr_t)((uint64_t)2ULL);
            if (t69 > t70 || t70 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t69, t70, ((uint64_t)2ULL), gm_srccr53ejqr1dde, 304, 82);
            uint64_t t71 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t68 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71));
            gm_t5detta1b4gnk t73;
            gm_f_parse_args_7blm2mp((void *)&t73, (void *)NULL, cx, NULL, t57, t64, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_usage_failure_cgg6v1t((void *)&gm_l18, &t73);
            uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (t74) {
            } else {
                gm_t3ujhu6qph7de t75 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l18) + 8));
                gm_t715ajl6o1ipo t76;
                GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
                ((uint8_t *)&t76)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t76 + 8), t75);
                memmove(gm_out, ((uint8_t *)&t76), 32);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l18) + 8), 120);
            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
            uint8_t t78 = (t77 > ((uint64_t)0ULL));
            if (t78) {
                gm_b16_8 t79;
                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sr43s52rmecnf));
                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(32));
                gm_t3ujhu6qph7de t80;
                GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t80 + 8), t79);
                gm_t715ajl6o1ipo t81;
                GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                ((uint8_t *)&t81)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t81 + 8), t80);
                memmove(gm_out, ((uint8_t *)&t81), 32);
                goto gm_fdone;
            }
            gm_l10 = ((uint64_t)30ULL);
            {
                {
                    void * t82 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_so87vf8a6o4mm));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(10));
                    gm_f_value_of_0km7vvp((void *)&gm_l19, t82, t83);
                    uint8_t t84 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                    if (!t84) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
                    gm_l11 = t85;
                    gm_b16_8 t86 = gm_l11;
                    gm_f_parse_int_8aueeu2((void *)&gm_l20, t86);
                    uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (t87) {
                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        gm_l21 = t88;
                    } else {
                        gm_b16_8 t89 = gm_l11;
                        gm_tnpifv20irnph t90;
                        GM_ST(gm_b16_8, ((uint8_t *)&t90 + 0), t89);
                        gm_f_format_da7bimt((void *)&gm_l22, (void *)NULL, cx, NULL, t90);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srccr53ejqr1dde, 309, 43);
                        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                        gm_t3ujhu6qph7de t94;
                        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 8), t93);
                        gm_t715ajl6o1ipo t95;
                        GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                        ((uint8_t *)&t95)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t95 + 8), t94);
                        memmove(gm_out, ((uint8_t *)&t95), 32);
                        goto gm_fdone;
                    }
                    uint64_t t96 = gm_l21;
                    gm_l10 = t96;
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            {
                {
                    void * t97 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t98;
                    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s0nf1rg04f3m5));
                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(3));
                    uint8_t t99 = gm_f_has_flag_4n9tu40(t97, t98);
                    if (!t99) {
                        goto gm_lb7;
                    }
                    void * t100 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t101;
                    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_so87vf8a6o4mm));
                    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(10));
                    gm_f_value_of_0km7vvp((void *)&gm_l24, t100, t101);
                    {
                        {
                            uint8_t t102 = (uint8_t)((((uint8_t *)&gm_l24))[16] != 0);
                            if (t102) {
                                goto gm_lb9;
                            }
                            gm_l23 = ((uint8_t)0);
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            gm_l23 = ((uint8_t)1);
                            goto gm_lb8;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 312, 40);
                    }
gm_lb8:;
                    uint8_t t103 = gm_l23;
                    if (!t103) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t104;
                    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_ss9d51lhe1orq));
                    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(45));
                    gm_t3ujhu6qph7de t105;
                    GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t105 + 8), t104);
                    gm_t715ajl6o1ipo t106;
                    GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                    ((uint8_t *)&t106)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t106 + 8), t105);
                    memmove(gm_out, ((uint8_t *)&t106), 32);
                    goto gm_fdone;
                }
gm_lb7:;
            }
gm_lb6:;
            uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t107) {
                gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                gm_l25 = t108;
            } else {
                gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)gm_cftc9ncjfm7));
                gm_t3ujhu6qph7de t110;
                gm_f_fail_o80k3rv((void *)&t110, t109);
                gm_t715ajl6o1ipo t111;
                GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                ((uint8_t *)&t111)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t111 + 8), t110);
                memmove(gm_out, ((uint8_t *)&t111), 32);
                goto gm_fdone;
            }
            gm_b16_8 t112 = gm_l25;
            gm_l12 = t112;
            uint64_t t113 = gm_l10;
            uint64_t r114; bool o115 = __builtin_mul_overflow(t113, ((uint64_t)86400ULL), &r114);
            gm_tf5srh4mp4b3i t116;
            if (o115) {
            gm_tf5srh4mp4b3i t117;
            memset(&t117, 0, sizeof t117);
                t116 = t117;
            } else {
            uint64_t t118 = r114;
            gm_tf5srh4mp4b3i t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t119), t118);
            ((uint8_t *)&t119)[8] = 1;
                t116 = t119;
            }
            gm_l26 = t116;
            uint8_t t120 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
            if (t120) {
                uint64_t t121 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                gm_l27 = t121;
            } else {
                if (((uint64_t)1000000000ULL) == 0) gm_trap("division by zero", gm_srccr53ejqr1dde, 316, 40);
                uint64_t t122 = (uint64_t)(((uint64_t)18446744073709551615ULL) / ((uint64_t)1000000000ULL));
                gm_l27 = t122;
            }
            uint64_t t123 = gm_l27;
            gm_l13 = t123;
            uint64_t t124 = gm_l13;
            uint64_t r125; bool o126 = __builtin_mul_overflow(t124, ((uint64_t)1000000000ULL), &r125);
            gm_tf5srh4mp4b3i t127;
            if (o126) {
            gm_tf5srh4mp4b3i t128;
            memset(&t128, 0, sizeof t128);
                t127 = t128;
            } else {
            uint64_t t129 = r125;
            gm_tf5srh4mp4b3i t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t130), t129);
            ((uint8_t *)&t130)[8] = 1;
                t127 = t130;
            }
            gm_l28 = t127;
            uint8_t t131 = (uint8_t)((((uint8_t *)&gm_l28))[8] != 0);
            if (t131) {
                uint64_t t132 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                gm_l29 = t132;
            } else {
                gm_l29 = ((uint64_t)18446744073709551615ULL);
            }
            uint64_t t133 = gm_l29;
            void * t134 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t135;
            GM_ST(void *, (uint8_t *)&t135, (void *)(gm_s0nf1rg04f3m5));
            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(3));
            uint8_t t136 = gm_f_has_flag_4n9tu40(t134, t135);
            void * t137 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t138;
            GM_ST(void *, (uint8_t *)&t138, (void *)(gm_sjgng334veu6f));
            GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(7));
            uint8_t t139 = gm_f_has_flag_4n9tu40(t137, t138);
            gm_t553jgmqd1tlr t140;
            GM_ST(uint16_t, (uint8_t *)&t140 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t140 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t140 + 0), t133);
            GM_ST(uint8_t, ((uint8_t *)&t140 + 8), t136);
            GM_ST(uint8_t, ((uint8_t *)&t140 + 9), t139);
            gm_l14 = t140;
            gm_b16_8 t141 = gm_l12;
            gm_t553jgmqd1tlr t142 = gm_l14;
            gm_t9j922kfqcdi9 t143;
            gm_f_clean_97pd952((void *)&t143, (void *)NULL, cx, NULL, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l15 = t143;
            void * t144 = (void *)((uint8_t *)&F.l15);
            uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 9));
            gm_b16_8 t146 = gm_f_summary_s3h16om((void *)NULL, cx, NULL, t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t147 = gm_l12;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t146) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t146)), GM_LD(uintptr_t, ((uint8_t *)&t146) + 8), gm_srccr53ejqr1dde, 323, 13))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7hkl5mqa15gm, 4, gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t147) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t147)), GM_LD(uintptr_t, ((uint8_t *)&t147) + 8), gm_srccr53ejqr1dde, 323, 13))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
            uint64_t t148 = GM_LD(uint64_t, ((((uint8_t *)&F.l15) + 32) + 8));
            uint8_t t149 = (t148 > ((uint64_t)0ULL));
            if (t149) {
                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 32));
                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                gm_b16_8 t152;
                GM_ST(void *, (uint8_t *)&t152, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(1));
                gm_f_join_sfknljb((void *)&gm_l30, (void *)NULL, cx, NULL, t151, t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srccr53ejqr1dde, 324, 61);
                gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                gm_t3ujhu6qph7de t156;
                gm_f_fail_o80k3rv((void *)&t156, t155);
                gm_t715ajl6o1ipo t157;
                GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
                ((uint8_t *)&t157)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t157 + 8), t156);
                memmove(gm_out, ((uint8_t *)&t157), 32);
                goto gm_fdone;
            }
            gm_t715ajl6o1ipo t158;
            GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 24, 0);
            ((uint8_t *)&t158)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t158 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t158), 32);
            goto gm_fdone;
        }
gm_lb3:;
        {
            gm_b16_8 t159 = gm_l1;
            gm_tnpifv20irnph t160;
            GM_ST(gm_b16_8, ((uint8_t *)&t160 + 0), t159);
            gm_f_format_6ta43n5((void *)&gm_l31, (void *)NULL, cx, NULL, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t161 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t161) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srccr53ejqr1dde, 327, 36);
            gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
            gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
            gm_t3ujhu6qph7de t164;
            GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t164 + 8), t163);
            gm_t715ajl6o1ipo t165;
            GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
            ((uint8_t *)&t165)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t165 + 8), t164);
            memmove(gm_out, ((uint8_t *)&t165), 32);
            goto gm_fdone;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 296, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_a7cccd4(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_a7cccd4 F;
    gm_b16_8 gm_l0; /* names */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t7 = gm_l0;
    gm_l2 = t7;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t13 = gm_l3;
                {
                    {
                        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)t13 * 16));
                        gm_l4 = t14;
                        gm_b16_8 t15 = gm_l4;
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 61, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t18 = gm_l3;
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            uint64_t t21 = r19;
            gm_l3 = t21;
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
extern __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_fail_o80k3rv_c(gm_b16_8 a0) { gm_t3ujhu6qph7de gm_r; gm_f_fail_o80k3rv(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_fetch_l6q1k6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fetch_l6q1k6p F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_t0kl141g5m2qf gm_l2; /* <temporary array> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* d */
    gm_b16_8 gm_l8; /* d */
    gm_b16_8 gm_l9; /* dir */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* root */
    gm_tqs5p4n2tq19v gm_l12; /* opts */
    gm_b16_8 gm_l13; /* e */
    GM_STACK_CHECK(&F, 1778u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_th2lqbo77pvsc gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_b16_8 gm_l20;
    gm_t3ujhu6qph7de gm_l21;
    gm_b16_8 gm_l22;
    gm_t074vrl1hb4f6 gm_l23;
    gm_tv37d3dpt2opu gm_l24;
    gm_b16_8 t9 = gm_l0;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_snboa22b96bbc));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(3));
    gm_tpmdr2hmdhsqr t11;
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
    gm_l1 = t11;
    void * t12 = (void *)((uint8_t *)&gm_l1);
    uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
    if (t13 > t14 || t14 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t13, t14, ((uint64_t)1ULL), gm_srccr53ejqr1dde, 153, 45);
    uint64_t t15 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t12 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15));
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s5fgj0n2t69gl));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(7));
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_snvisme5hp379));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
    gm_t0kl141g5m2qf t19;
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t17);
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t18);
    gm_l2 = t19;
    void * t20 = (void *)((uint8_t *)&gm_l2);
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)((uint64_t)2ULL);
    if (t21 > t22 || t22 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t21, t22, ((uint64_t)2ULL), gm_srccr53ejqr1dde, 153, 67);
    uint64_t t23 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    gm_t5detta1b4gnk t25;
    gm_f_parse_args_7blm2mp((void *)&t25, (void *)NULL, cx, NULL, t9, t16, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_usage_failure_cgg6v1t((void *)&gm_l14, &t25);
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (t26) {
    } else {
        gm_t3ujhu6qph7de t27 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l14) + 8));
        gm_t715ajl6o1ipo t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        ((uint8_t *)&t28)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t28 + 8), t27);
        memmove(gm_out, ((uint8_t *)&t28), 32);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l14) + 8), 120);
    gm_trqfvmjtt4sij t29;
    gm_f_list_epvducg((void *)&t29, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t29;
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_l5 = t30;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l6;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb1;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t36 = gm_l6;
                {
                    {
                        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t34) + (uintptr_t)t36 * 16));
                        gm_l7 = t37;
                        void * t38 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t39 = gm_l7;
                        gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t38, t39);
                        gm_l15 = t40;
                        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srccr53ejqr1dde, 156, 28);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 156, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t42 = gm_l6;
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            uint64_t t45 = r43;
            gm_l6 = t45;
        }
    }
gm_lb1:;
    {
        {
            void * t46 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_snboa22b96bbc));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(3));
            gm_f_value_of_0km7vvp((void *)&gm_l16, t46, t47);
            uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
            if (!t48) {
                goto gm_lb7;
            }
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
            gm_l8 = t49;
            void * t50 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t51 = gm_l8;
            gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t50, t51);
            gm_l17 = t52;
            uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srccr53ejqr1dde, 157, 35);
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint8_t t55 = (t54 > ((uint64_t)1ULL));
    if (t55) {
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_saqbkgqthogiq));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(29));
        gm_t3ujhu6qph7de t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 8), t56);
        gm_t715ajl6o1ipo t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        ((uint8_t *)&t58)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t58 + 8), t57);
        memmove(gm_out, ((uint8_t *)&t58), 32);
        goto gm_fdone;
    }
    void * t59 = GM_LD(void *, ((uint8_t *)&F.l4));
    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint8_t t61 = (((uint64_t)0ULL) < t60);
    if (t61) {
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t59) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_th2lqbo77pvsc t63;
        GM_ST(uint8_t, (uint8_t *)&t63 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t63 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t63 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t63), t62);
        ((uint8_t *)&t63)[16] = 1;
        gm_l18 = t63;
    } else {
        gm_th2lqbo77pvsc t64;
        memset(&t64, 0, sizeof t64);
        gm_l18 = t64;
    }
    memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 24);
    uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
    if (t65) {
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
        gm_l20 = t66;
    } else {
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(1));
        gm_l20 = t67;
    }
    gm_b16_8 t68 = gm_l20;
    gm_l9 = t68;
    gm_b16_8 t69 = gm_l9;
    gm_f_root_of_cluda11((void *)&gm_l21, (void *)NULL, cx, NULL, t69);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (t70) {
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_l22 = t71;
    } else {
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_l10 = t72;
        gm_b16_8 t73 = gm_l10;
        gm_t3ujhu6qph7de t74;
        gm_f_fail_o80k3rv((void *)&t74, t73);
        gm_t715ajl6o1ipo t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
        ((uint8_t *)&t75)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t75 + 8), t74);
        memmove(gm_out, ((uint8_t *)&t75), 32);
        goto gm_fdone;
    }
    gm_b16_8 t76 = gm_l22;
    gm_l11 = t76;
    void * t77 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t78;
    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s5fgj0n2t69gl));
    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(7));
    uint8_t t79 = gm_f_has_flag_4n9tu40(t77, t78);
    void * t80 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t81;
    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_snvisme5hp379));
    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(6));
    uint8_t t82 = gm_f_has_flag_4n9tu40(t80, t81);
    gm_tqs5p4n2tq19v t83;
    GM_ST(uint8_t, ((uint8_t *)&t83 + 0), t79);
    GM_ST(uint8_t, ((uint8_t *)&t83 + 1), t82);
    gm_l12 = t83;
    gm_b16_8 t84 = gm_l11;
    gm_tqs5p4n2tq19v t85 = gm_l12;
    gm_tl572d7579hns t86;
    GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t86 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t86 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
    gm_f_sync_ht164ep((void *)&gm_l23, (void *)NULL, cx, NULL, t84, t85, &t86, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (t87) {
        memmove(((uint8_t *)&gm_l24), (((uint8_t *)&gm_l23) + 8), 48);
    } else {
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_l13 = t88;
        gm_b16_8 t89 = gm_l13;
        gm_t3ujhu6qph7de t90;
        gm_f_fail_o80k3rv((void *)&t90, t89);
        gm_t715ajl6o1ipo t91;
        GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
        ((uint8_t *)&t91)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t91 + 8), t90);
        memmove(gm_out, ((uint8_t *)&t91), 32);
        goto gm_fdone;
    }
    gm_t715ajl6o1ipo t92;
    GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t92 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t92 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t92 + 24, 0);
    ((uint8_t *)&t92)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t92 + 8), GM_ZERO(gm_b0_1));
    gm_tv37d3dpt2opu t93 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)&gm_l24));
    memmove(gm_out, ((uint8_t *)&t92), 32);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_0m0ntf2 F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* e */
    GM_STACK_CHECK(&F, 333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tibupt4dmcqai gm_l3;
    gm_tabb710d9mq2e gm_l4;
    {
        {
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l0), 32);
            uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
            if (t4) {
                memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l1) + 8), 24);
            } else {
                gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l1) + 8));
                gm_l2 = t5;
                gm_toe5fmr3vvf4d t6 = gm_l2;
                uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
                uint8_t t8 = (uint8_t)((t7 == ((uint8_t)0ULL)));
                gm_toe5fmr3vvf4d t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
                if (t8) {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l12), ((uint8_t)1));
                } else {
                    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l5));
                    gm_toe5fmr3vvf4d t11 = gm_l2;
                    gm_b16_8 t12 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t11);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t13;
                    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t10);
                    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t12);
                    gm_f_format_is5aam9((void *)&gm_l4, (void *)NULL, cx, NULL, &t13);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srccr53ejqr1dde, 362, 34);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
                    gm_t3ujhu6qph7de t17;
                    gm_f_fail_o80k3rv((void *)&t17, t16);
                    gm_t715ajl6o1ipo t18;
                    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                    ((uint8_t *)&t18)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t18 + 8), t17);
                    uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
                    ((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->ret = t18;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t19;
                    goto gm_lad;
                }
                gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
                goto gm_lad;
            }
            gm_tibupt4dmcqai t20 = GM_LD(gm_tibupt4dmcqai, ((uint8_t *)&gm_l3));
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l11), ((uint8_t)1));
            gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
            goto gm_lad;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 357, 9);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_flag_4n9tu40(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_flag_4n9tu40 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 80));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                {
                    {
                        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t12) + (uintptr_t)t14 * 16));
                        gm_l4 = t15;
                        gm_b16_8 t16 = gm_l4;
                        gm_b16_8 t17 = gm_l1;
                        uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
                        if (t18) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 54, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_package_command_ufjt5rp(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_package_command_ufjt5rp F;
    gm_b16_8 gm_l0; /* command */
    GM_STACK_CHECK_SHALLOW(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6pnfrub0rj1t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3lbmpmt95hvh));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s4o70d89183c2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sqljkj8r253om));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sivh46n0690a4));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_suass5sid4b6e));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(4));
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_so9n4mmd960c7));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    gm_th2d9c7aju8hp t10;
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 16), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 32), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 48), t6);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 64), t7);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 80), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 96), t9);
    uint8_t t11 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 0, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 16, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 32, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 48, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 64, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 80, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 96, ((uint8_t *)&t2))) t11 = 1;
    uint8_t t12 = (uint8_t)(t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_new_package_hf828qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1) {
    struct gm_fr_new_package_hf828qc F;
    gm_b16_8 gm_l0; /* args */
    uint8_t gm_l1; /* create */
    gm_t0kl141g5m2qf gm_l2; /* <temporary array> */
    gm_b0_8 gm_l3; /* <temporary array> */
    uint8_t gm_l6; /* git_init */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* n */
    gm_b16_8 gm_l9; /* problem */
    uint8_t gm_l10; /* created_here */
    gm_b16_8 gm_l13; /* <tmp> */
    gm_toe5fmr3vvf4d gm_l14; /* e */
    gm_th2lqbo77pvsc gm_l15; /* derived_from */
    gm_b16_8 gm_l16; /* name */
    gm_b16_8 gm_l17; /* n */
    gm_b16_8 gm_l18; /* abs */
    gm_b16_8 gm_l19; /* dir_name */
    gm_b16_8 gm_l20; /* problem */
    gm_b16_8 gm_l21; /* e */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* f */
    uint8_t gm_l26; /* existing_manifest */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* f */
    gm_b16_8 gm_l30; /* d */
    GM_STACK_CHECK(&F, 3805u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t77ebp3ogdble gm_l31;
    gm_th2lqbo77pvsc gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_th2lqbo77pvsc gm_l34;
    gm_th2lqbo77pvsc gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_ttln0d0hd70en gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_th2lqbo77pvsc gm_l40;
    gm_th2lqbo77pvsc gm_l41;
    gm_b16_8 gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_th2lqbo77pvsc gm_l44;
    gm_b16_8 gm_l45;
    gm_th2lqbo77pvsc gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tcl27d4j9lskg gm_l48;
    gm_tecsjmmgeeh52 gm_l49;
    gm_tabb710d9mq2e gm_l50;
    {
        gm_b16_8 t29 = gm_l0;
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sius9pki5u4e3));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(4));
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s30cnsbbn0c4o));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(3));
        gm_t0kl141g5m2qf t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t31);
        gm_l2 = t32;
        void * t33 = (void *)((uint8_t *)&gm_l2);
        uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t35 = (uintptr_t)((uint64_t)2ULL);
        if (t34 > t35 || t35 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t34, t35, ((uint64_t)2ULL), gm_srccr53ejqr1dde, 332, 45);
        uint64_t t36 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t33 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36));
        void * t38 = (void *)((uint8_t *)&gm_l3);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = (uintptr_t)((uint64_t)0ULL);
        if (t39 > t40 || t40 > (uintptr_t)((uint64_t)0ULL)) gm_trap_slice(t39, t40, ((uint64_t)0ULL), gm_srccr53ejqr1dde, 332, 75);
        uint64_t t41 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t38 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41));
        gm_t5detta1b4gnk t43;
        gm_f_parse_args_7blm2mp((void *)&t43, (void *)NULL, cx, NULL, t29, t37, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_usage_failure_cgg6v1t((void *)&gm_l31, &t43);
        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (t44) {
        } else {
            gm_t3ujhu6qph7de t45 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l31) + 8));
            gm_t715ajl6o1ipo t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            ((uint8_t *)&t46)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t46 + 8), t45);
            memmove(gm_out, ((uint8_t *)&t46), 32);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l31) + 8), 120);
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(1));
        F.l5 = t47;
        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
        uint8_t t49 = ((t48 == ((uint64_t)1ULL)));
        if (t49) {
            void * t50 = GM_LD(void *, ((uint8_t *)&F.l4));
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t51)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t51, gm_srccr53ejqr1dde, 335, 15);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t50) + (uintptr_t)((uint64_t)0ULL) * 16));
            F.l5 = t52;
        } else {
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
            uint8_t t54 = ((t53 == ((uint64_t)0ULL)));
            if (t54) {
                uint8_t t55 = gm_l1;
                if (t55) {
                    gm_b16_8 t56;
                    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sgu3j1te831tl));
                    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(23));
                    gm_t3ujhu6qph7de t57;
                    GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t57 + 8), t56);
                    gm_t715ajl6o1ipo t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t58 + 8), t57);
                    memmove(gm_out, ((uint8_t *)&t58), 32);
                    goto gm_fdone;
                }
            } else {
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_saqbkgqthogiq));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(29));
                gm_t3ujhu6qph7de t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t60 + 8), t59);
                gm_t715ajl6o1ipo t61;
                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                ((uint8_t *)&t61)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t61 + 8), t60);
                memmove(gm_out, ((uint8_t *)&t61), 32);
                goto gm_fdone;
            }
        }
        gm_l6 = ((uint8_t)1);
        {
            {
                void * t62 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t63;
                GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s30cnsbbn0c4o));
                GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(3));
                gm_f_value_of_0km7vvp((void *)&gm_l32, t62, t63);
                uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l32))[16] != 0);
                if (!t64) {
                    goto gm_lb2;
                }
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l32));
                gm_l7 = t65;
                {
                    {
                        gm_b16_8 t66 = gm_l7;
                        gm_b16_8 t67;
                        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s0k6ioi39f878));
                        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(3));
                        uint8_t t68 = (gm_eq0iqiubfm7e(((uint8_t *)&t66), ((uint8_t *)&t67)));
                        if (!t68) {
                            goto gm_lb4;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_b16_8 t69 = gm_l7;
                        gm_b16_8 t70;
                        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s3v0kr2fdpfld));
                        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
                        uint8_t t71 = (gm_eq0iqiubfm7e(((uint8_t *)&t69), ((uint8_t *)&t70)));
                        if (!t71) {
                            goto gm_lb5;
                        }
                        gm_l6 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        gm_b16_8 t72 = gm_l7;
                        gm_tnpifv20irnph t73;
                        GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                        gm_f_format_e2683lk((void *)&gm_l33, (void *)NULL, cx, NULL, t73);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                        if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srccr53ejqr1dde, 346, 40);
                        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                        gm_t3ujhu6qph7de t77;
                        GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t77 + 8), t76);
                        gm_t715ajl6o1ipo t78;
                        GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                        ((uint8_t *)&t78)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t78 + 8), t77);
                        memmove(gm_out, ((uint8_t *)&t78), 32);
                        goto gm_fdone;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 343, 9);
                }
gm_lb3:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                void * t79 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sius9pki5u4e3));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(4));
                gm_f_value_of_0km7vvp((void *)&gm_l34, t79, t80);
                uint8_t t81 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
                if (!t81) {
                    goto gm_lb8;
                }
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
                gm_l8 = t82;
                {
                    {
                        gm_b16_8 t83 = gm_l8;
                        gm_b16_8 t84;
                        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_snqvo5bgc2fch));
                        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(7));
                        gm_f_name_problem_6r4mp11((void *)&gm_l35, (void *)NULL, cx, NULL, t83, t84);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
                        if (!t85) {
                            goto gm_lb10;
                        }
                        gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
                        gm_l9 = t86;
                        gm_b16_8 t87 = gm_l9;
                        gm_t3ujhu6qph7de t88;
                        gm_f_fail_o80k3rv((void *)&t88, t87);
                        gm_t715ajl6o1ipo t89;
                        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                        ((uint8_t *)&t89)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t89 + 8), t88);
                        memmove(gm_out, ((uint8_t *)&t89), 32);
                        goto gm_fdone;
                    }
gm_lb10:;
                }
gm_lb9:;
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
        gm_l10 = ((uint8_t)0);
        uint8_t t90 = gm_l1;
        if (t90) {
            F.l11 = ((uint8_t)0);
            F.l12 = ((uint8_t)0);
            gm_b16_8 t91 = F.l5;
            gm_l13 = t91;
            {
                gm_node wn92 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl87701o8eo1, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
                gm_b16_8 t95 = gm_l13;
                gm_b0_1 t96 = gm_f_entries_44o5n9f_suo216n((void *)NULL, (&wn92), NULL, t95);
                if (gm_unw.kind) {
                    goto gm_ll94;
                }
            }
            goto gm_ls97;
gm_ll94:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we93; }
            goto gm_lad;
gm_ls97:;
gm_lb11:;
we93:;
            uint8_t t98 = F.l11;
            if (t98) {
                gm_b16_8 t99 = F.l5;
                gm_tnpifv20irnph t100;
                GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t99);
                gm_f_format_7lopc28((void *)&gm_l36, (void *)NULL, cx, NULL, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srccr53ejqr1dde, 369, 37);
                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                gm_t3ujhu6qph7de t104;
                gm_f_fail_o80k3rv((void *)&t104, t103);
                gm_t715ajl6o1ipo t105;
                GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                ((uint8_t *)&t105)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t105 + 8), t104);
                memmove(gm_out, ((uint8_t *)&t105), 32);
                goto gm_fdone;
            }
            uint8_t t106 = F.l12;
            if (t106) {
                gm_b16_8 t107 = F.l5;
                gm_ttln0d0hd70en t108 = gm_f_make_dir_all_q6phjan((void *)NULL, cx, NULL, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t108;
                uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                if (t109) {
                } else {
                    gm_toe5fmr3vvf4d t110 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l37) + 4));
                    gm_l14 = t110;
                    gm_b16_8 t111 = F.l5;
                    gm_toe5fmr3vvf4d t112 = gm_l14;
                    gm_b16_8 t113 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t112);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t114;
                    GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t111);
                    GM_ST(gm_b16_8, ((uint8_t *)&t114 + 16), t113);
                    gm_f_format_5uun4vl((void *)&gm_l38, (void *)NULL, cx, NULL, &t114);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                    if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srccr53ejqr1dde, 371, 59);
                    gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                    gm_t3ujhu6qph7de t118;
                    gm_f_fail_o80k3rv((void *)&t118, t117);
                    gm_t715ajl6o1ipo t119;
                    GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                    ((uint8_t *)&t119)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t119 + 8), t118);
                    memmove(gm_out, ((uint8_t *)&t119), 32);
                    goto gm_fdone;
                }
                gm_l10 = ((uint8_t)1);
            }
        } else {
            gm_b16_8 t120 = F.l5;
            uint8_t t121 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t122 = (uint8_t)!t121;
            if (t122) {
                gm_b16_8 t123 = F.l5;
                gm_tnpifv20irnph t124;
                GM_ST(gm_b16_8, ((uint8_t *)&t124 + 0), t123);
                gm_f_format_tffhbfn((void *)&gm_l39, (void *)NULL, cx, NULL, t124);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_srccr53ejqr1dde, 375, 22);
                gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                gm_t3ujhu6qph7de t128;
                gm_f_fail_o80k3rv((void *)&t128, t127);
                gm_t715ajl6o1ipo t129;
                GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                ((uint8_t *)&t129)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t129 + 8), t128);
                memmove(gm_out, ((uint8_t *)&t129), 32);
                goto gm_fdone;
            }
        }
    }
    {
        gm_th2lqbo77pvsc t130;
        memset(&t130, 0, sizeof t130);
        gm_l15 = t130;
        gm_b16_8 t131;
        GM_ST(void *, (uint8_t *)&t131, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(0));
        gm_l16 = t131;
        {
            {
                void * t132 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t133;
                GM_ST(void *, (uint8_t *)&t133, (void *)(gm_sius9pki5u4e3));
                GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(4));
                gm_f_value_of_0km7vvp((void *)&gm_l40, t132, t133);
                uint8_t t134 = (uint8_t)((((uint8_t *)&gm_l40))[16] != 0);
                if (!t134) {
                    goto gm_lb13;
                }
                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l40));
                gm_l17 = t135;
                gm_b16_8 t136 = gm_l17;
                gm_l16 = t136;
                goto gm_lb12;
            }
gm_lb13:;
            gm_b16_8 t137 = F.l5;
            gm_f_canonicalize_epo7cln((void *)&gm_l41, (void *)NULL, cx, NULL, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l41))[16] != 0);
            if (t138) {
                gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                gm_l42 = t139;
            } else {
                gm_b16_8 t140 = F.l5;
                gm_tnpifv20irnph t141;
                GM_ST(gm_b16_8, ((uint8_t *)&t141 + 0), t140);
                gm_f_format_14t1s38((void *)&gm_l43, (void *)NULL, cx, NULL, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_srccr53ejqr1dde, 383, 26);
                gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                gm_t3ujhu6qph7de t145;
                gm_f_fail_o80k3rv((void *)&t145, t144);
                gm_t715ajl6o1ipo t146;
                GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                ((uint8_t *)&t146)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t146 + 8), t145);
                memmove(gm_out, ((uint8_t *)&t146), 32);
                goto gm_fdone;
            }
            gm_b16_8 t147 = gm_l42;
            gm_l18 = t147;
            gm_b16_8 t148 = gm_l18;
            gm_f_file_name_em7v8lv((void *)&gm_l44, (void *)NULL, cx, NULL, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t149 = (uint8_t)((((uint8_t *)&gm_l44))[16] != 0);
            if (t149) {
                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l44));
                gm_l45 = t150;
            } else {
                gm_b16_8 t151;
                GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(0));
                gm_l45 = t151;
            }
            gm_b16_8 t152 = gm_l45;
            gm_l19 = t152;
            gm_b16_8 t153 = gm_l19;
            gm_b16_8 t154 = gm_f_sanitize_name_3i9pjsp((void *)NULL, cx, NULL, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t154;
            {
                {
                    gm_b16_8 t155 = gm_l16;
                    gm_b16_8 t156;
                    GM_ST(void *, (uint8_t *)&t156, (void *)(gm_snqvo5bgc2fch));
                    GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(7));
                    gm_f_name_problem_6r4mp11((void *)&gm_l46, (void *)NULL, cx, NULL, t155, t156);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t157 = (uint8_t)((((uint8_t *)&gm_l46))[16] != 0);
                    if (!t157) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l46));
                    gm_l20 = t158;
                    uint8_t t159 = gm_l10;
                    if (t159) {
                        gm_b16_8 t160 = F.l5;
                        gm_node *pn161 = gm_find(cx, 2126215853);
                        if (!pn161) gm_trap("no handler installed for effect `Fs`", gm_srccr53ejqr1dde, 388, 35);
                        gm_ttln0d0hd70en t162;
                        t162 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn161->clauses[9])(pn161->env, pn161->hctx, pn161, t160);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_b16_8 t163 = gm_l19;
                    gm_b16_8 t164 = gm_l16;
                    gm_b16_8 t165 = gm_l20;
                    gm_tfeab279c16rr t166;
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t163);
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 16), t164);
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 32), t165);
                    gm_f_format_ujusd29((void *)&gm_l47, (void *)NULL, cx, NULL, &t166);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srccr53ejqr1dde, 389, 26);
                    gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                    gm_t3ujhu6qph7de t170;
                    gm_f_fail_o80k3rv((void *)&t170, t169);
                    gm_t715ajl6o1ipo t171;
                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                    ((uint8_t *)&t171)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t171 + 8), t170);
                    memmove(gm_out, ((uint8_t *)&t171), 32);
                    goto gm_fdone;
                }
gm_lb15:;
            }
gm_lb14:;
            gm_b16_8 t172 = gm_l16;
            gm_b16_8 t173 = gm_l19;
            uint8_t t174 = (!gm_eq0iqiubfm7e(((uint8_t *)&t172), ((uint8_t *)&t173)));
            if (t174) {
                gm_b16_8 t175 = gm_l19;
                gm_th2lqbo77pvsc t176;
                GM_ST(uint8_t, (uint8_t *)&t176 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t176 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t176 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t176), t175);
                ((uint8_t *)&t176)[16] = 1;
                gm_l15 = t176;
            }
        }
gm_lb12:;
    }
    {
        gm_b16_8 t177 = F.l5;
        gm_b16_8 t178 = gm_l16;
        uint8_t t179 = gm_l6;
        gm_f_scaffold_ansfc6m((void *)&gm_l48, (void *)NULL, cx, NULL, t177, t178, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
        if (t180) {
            memmove(((uint8_t *)&gm_l49), (((uint8_t *)&gm_l48) + 8), 88);
        } else {
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
            gm_l21 = t181;
            gm_b16_8 t182 = gm_l21;
            gm_t3ujhu6qph7de t183;
            gm_f_fail_o80k3rv((void *)&t183, t182);
            gm_t715ajl6o1ipo t184;
            GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
            ((uint8_t *)&t184)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t184 + 8), t183);
            memmove(gm_out, ((uint8_t *)&t184), 32);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l49), 88);
        gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 40));
        gm_l23 = t185;
        gm_l24 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t187 = gm_l24;
                uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                uint8_t t189 = (t187 < t188);
                if (!t189) {
                    goto gm_lb16;
                }
                {
                    void * t190 = GM_LD(void *, ((uint8_t *)&gm_l23));
                    uint64_t t191 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                    uint64_t t192 = gm_l24;
                    {
                        {
                            gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)t190) + (uintptr_t)t192 * 16));
                            gm_l25 = t193;
                            gm_b16_8 t194 = gm_l25;
                            if (gm_pf1m4b8q4e78(cx, gm_srgao1ir81l50, 13, gm_srccr53ejqr1dde, 394, 37)) {
                                goto gm_ll186;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t194) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t194)), GM_LD(uintptr_t, ((uint8_t *)&t194) + 8), gm_srccr53ejqr1dde, 394, 37))) {
                                goto gm_ll186;
                            }
                            if (gm_pf1m4b8q4e78(cx, gm_slrtg04n254p4, 18, gm_srccr53ejqr1dde, 394, 37)) {
                                goto gm_ll186;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 394, 5);
                    }
gm_lb19:;
                }
gm_lb18:;
                uint64_t t195 = gm_l24;
                uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                uint64_t t198 = r196;
                gm_l24 = t198;
            }
        }
        goto gm_ls199;
gm_ll186:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb16; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb18; }
        }
        goto gm_lad;
gm_ls199:;
gm_lb16:;
        uint64_t t200 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
        uint8_t t201 = ((t200 == ((uint64_t)0ULL)));
        if (t201) {
            gm_b16_8 t202 = F.l5;
            if (gm_pf1m4b8q4e78(cx, gm_s7mqv7hf4fso0, 26, gm_srccr53ejqr1dde, 396, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t202) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t202)), GM_LD(uintptr_t, ((uint8_t *)&t202) + 8), gm_srccr53ejqr1dde, 396, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 396, 9)) {
                goto gm_lad;
            }
            gm_t715ajl6o1ipo t203;
            GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t203 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t203 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t203 + 24, 0);
            ((uint8_t *)&t203)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t203 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t203), 32);
            goto gm_fdone;
        }
        gm_l26 = ((uint8_t)0);
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 40));
        gm_l27 = t204;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t206 = gm_l28;
                uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t208 = (t206 < t207);
                if (!t208) {
                    goto gm_lb21;
                }
                {
                    void * t209 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t211 = gm_l28;
                    {
                        {
                            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)t209) + (uintptr_t)t211 * 16));
                            gm_l29 = t212;
                            gm_b16_8 t213 = gm_l29;
                            gm_b16_8 t214;
                            GM_ST(void *, (uint8_t *)&t214, (void *)(gm_se24c7hpp401n));
                            GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(10));
                            uint8_t t215 = (gm_eq0iqiubfm7e(((uint8_t *)&t213), ((uint8_t *)&t214)));
                            if (t215) {
                                gm_l26 = ((uint8_t)1);
                            }
                            goto gm_lb24;
                        }
gm_lb25:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 400, 5);
                    }
gm_lb24:;
                }
gm_lb23:;
                uint64_t t216 = gm_l28;
                uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                uint64_t t219 = r217;
                gm_l28 = t219;
            }
        }
gm_lb21:;
    }
    {
        uint8_t t220 = gm_l26;
        if (t220) {
            gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
            gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
            gm_b16_8 t223;
            GM_ST(void *, (uint8_t *)&t223, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l50, (void *)NULL, cx, NULL, t222, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
            if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srccr53ejqr1dde, 404, 81);
            gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
            gm_b16_8 t226 = F.l5;
            if (gm_pf1m4b8q4e78(cx, gm_s2n3d0ogcjggg, 13, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_tybihgc69jv3, ((uint8_t *)&t225), gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7hkl5mqa15gm, 4, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t226) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t226)), GM_LD(uintptr_t, ((uint8_t *)&t226) + 8), gm_srccr53ejqr1dde, 404, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_snmm68ee1rq48, 40, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
        } else {
            gm_b16_8 t227 = gm_l16;
            gm_b16_8 t228 = F.l5;
            if (gm_pf1m4b8q4e78(cx, gm_sbvsnnbg6vc88, 22, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t227) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t227)), GM_LD(uintptr_t, ((uint8_t *)&t227) + 8), gm_srccr53ejqr1dde, 406, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_sgc7crh43q9tj, 5, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t228) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t228)), GM_LD(uintptr_t, ((uint8_t *)&t228) + 8), gm_srccr53ejqr1dde, 406, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
        }
        {
            {
                uint8_t t229 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                if (!t229) {
                    goto gm_lb27;
                }
                gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                gm_l30 = t230;
                gm_b16_8 t231 = gm_l30;
                if (gm_pf1m4b8q4e78(cx, gm_se48mcej0nimj, 15, gm_srccr53ejqr1dde, 409, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t231) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t231)), GM_LD(uintptr_t, ((uint8_t *)&t231) + 8), gm_srccr53ejqr1dde, 409, 9))) {
                    goto gm_lad;
                }
                if (gm_pf1m4b8q4e78(cx, gm_sbcpd47bv0ru9, 62, gm_srccr53ejqr1dde, 409, 9)) {
                    goto gm_lad;
                }
                goto gm_lb26;
            }
gm_lb27:;
        }
gm_lb26:;
        uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
        if (t232) {
            if (gm_pf1m4b8q4e78(cx, gm_se39cci44d9rt, 34, gm_srccr53ejqr1dde, 411, 33)) {
                goto gm_lad;
            }
        }
        gm_b16_8 t233 = F.l5;
        gm_b16_8 t234 = F.l5;
        if (gm_pf1m4b8q4e78(cx, gm_sq99f4u8gmi6n, 27, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t233) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t233)), GM_LD(uintptr_t, ((uint8_t *)&t233) + 8), gm_srccr53ejqr1dde, 412, 5))) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_sb9fu6ufbt9g1, 16, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t234) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t234)), GM_LD(uintptr_t, ((uint8_t *)&t234) + 8), gm_srccr53ejqr1dde, 412, 5))) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        gm_t715ajl6o1ipo t235;
        GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t235 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t235 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t235 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t235 + 24, 0);
        ((uint8_t *)&t235)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t235 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t235), 32);
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
inline __attribute__((aligned(4))) void gm_f_newest_version_dsisujv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_newest_version_dsisujv F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* t */
    GM_STACK_CHECK(&F, 866u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t470aqkia5212 gm_l9;
    gm_trqfvmjtt4sij gm_l10;
    gm_tom1ad5bcqgi1 gm_l11;
    gm_tu1089hhijadf gm_l12;
    gm_b16_8 t9 = gm_l0;
    gm_f_remote_tags_rssa7s7((void *)&gm_l9, (void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t10) {
        memmove(((uint8_t *)&gm_l10), (((uint8_t *)&gm_l9) + 8), 40);
    } else {
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_l1 = t11;
        gm_b16_8 t12 = gm_l1;
        gm_t3ujhu6qph7de t13;
        gm_f_fail_o80k3rv((void *)&t13, t12);
        gm_tis0irsd01rfp t14;
        memset(&t14, 0, sizeof t14);
        ((uint8_t *)&t14)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t14 + 8), t13);
        memmove(gm_out, ((uint8_t *)&t14), 72);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l10), 40);
    gm_tom1ad5bcqgi1 t15;
    memset(&t15, 0, sizeof t15);
    F.l3 = t15;
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_l4 = t16;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l5;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t22 = gm_l5;
                {
                    {
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t20) + (uintptr_t)t22 * 16));
                        gm_l6 = t23;
                        gm_b16_8 t24 = gm_l6;
                        gm_f_tag_version_aloeltn((void *)&gm_l11, (void *)NULL, cx, NULL, t24);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l11))[56] != 0);
                        if (t25) {
                            memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l11), 56);
                        } else {
                            goto gm_lb3;
                        }
                        memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l12), 56);
                        uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)&F.l7) + 24) + 8));
                        uint8_t t27 = (t26 > ((uint64_t)0ULL));
                        if (t27) {
                            goto gm_lb3;
                        }
                        {
                            {
                                uint8_t t28 = (uint8_t)((((uint8_t *)&F.l3))[56] != 0);
                                if (!t28) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l8), ((uint8_t *)&F.l3), 56);
                                int32_t t29 = gm_f_cmp_version_0oibvro(&(*(gm_tu1089hhijadf *)((uint8_t *)&F.l7)), &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l8)));
                                uint8_t t30 = (t29 < ((int32_t)0LL));
                                if (t30) {
                                    goto gm_lb3;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        gm_tu1089hhijadf t31 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&F.l7));
                        gm_tom1ad5bcqgi1 t32;
                        GM_ST(uint8_t, (uint8_t *)&t32 + 57, 0);
                        GM_ST(uint16_t, (uint8_t *)&t32 + 58, 0);
                        GM_ST(uint32_t, (uint8_t *)&t32 + 60, 0);
                        GM_ST(gm_tu1089hhijadf, ((uint8_t *)&t32), t31);
                        ((uint8_t *)&t32)[56] = 1;
                        F.l3 = t32;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 261, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t33 = gm_l5;
            uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
            uint64_t t36 = r34;
            gm_l5 = t36;
        }
    }
gm_lb1:;
    gm_tom1ad5bcqgi1 t37 = GM_LD(gm_tom1ad5bcqgi1, ((uint8_t *)&F.l3));
    gm_tis0irsd01rfp t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
    ((uint8_t *)&t38)[0] = 0;
    GM_ST(gm_tom1ad5bcqgi1, ((uint8_t *)&t38 + 8), t37);
    memmove(gm_out, ((uint8_t *)&t38), 72);
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
__attribute__((aligned(4))) void gm_f_parse_args_7blm2mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_parse_args_7blm2mp F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* value_opts */
    gm_b16_8 gm_l2; /* flag_opts */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* rest */
    gm_b16_8 gm_l9; /* opt */
    gm_b16_8 gm_l10; /* name */
    gm_th2lqbo77pvsc gm_l11; /* inline */
    gm_t6j6smj7qoctu gm_l12; /* split */
    gm_b16_8 gm_l13; /* v */
    GM_STACK_CHECK(&F, 2789u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    gm_ttu09lij36f8c gm_l16;
    uint8_t gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_b16_8 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    uint8_t gm_l22;
    gm_th2lqbo77pvsc gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_trqfvmjtt4sij t24;
    gm_f_list_epvducg((void *)&t24, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t25;
    gm_f_list_g9fqnp2((void *)&t25, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t26;
    gm_f_list_epvducg((void *)&t26, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tr8dvlu9ik4gj t27;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27 + 0), t24);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27 + 40), t25);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27 + 80), t26);
    F.l3 = t27;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l4;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t33 = gm_l4;
                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t31) + (uintptr_t)t33 * 16));
                gm_l5 = t34;
                uint64_t t35 = gm_l4;
                uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                if (o37) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 72, 9);
                uint64_t t38 = r36;
                gm_l4 = t38;
                gm_b16_8 t39 = gm_l5;
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s9pknf9g05okt));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
                if (t41) {
                    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t44 = gm_l4;
                    uint64_t t45 = (uintptr_t)t44;
                    uint64_t t46 = (uintptr_t)t43;
                    if (t45 > t46 || t46 > (uintptr_t)t43) gm_trap_slice(t45, t46, t43, gm_srccr53ejqr1dde, 75, 25);
                    uint64_t t47 = ((uintptr_t)t43) - (uintptr_t)t44;
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t42 + (uintptr_t)t44 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
                    gm_l6 = t48;
                    gm_l7 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t49 = gm_l7;
                            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t51 = (t49 < t50);
                            if (!t51) {
                                goto gm_lb4;
                            }
                            {
                                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t54 = gm_l7;
                                {
                                    {
                                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t52) + (uintptr_t)t54 * 16));
                                        gm_l8 = t55;
                                        void * t56 = (void *)((uint8_t *)&F.l3);
                                        gm_b16_8 t57 = gm_l8;
                                        gm_tftb1odc739t5 t58 = gm_f_push_3ef746b(t56, t57);
                                        gm_l14 = t58;
                                        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srccr53ejqr1dde, 75, 37);
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 75, 13);
                                }
gm_lb7:;
                            }
gm_lb6:;
                            uint64_t t60 = gm_l7;
                            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                            uint64_t t63 = r61;
                            gm_l7 = t63;
                        }
                    }
gm_lb4:;
                    goto gm_lb1;
                }
                {
                    {
                        gm_b16_8 t64 = gm_l5;
                        gm_b16_8 t65;
                        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s9pknf9g05okt));
                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(2));
                        gm_f_strip_prefix_b4949s8((void *)&gm_l15, t64, t65);
                        uint8_t t66 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                        if (!t66) {
                            goto gm_lb10;
                        }
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                        gm_l9 = t67;
                        gm_b16_8 t68 = gm_l9;
                        gm_l10 = t68;
                        gm_th2lqbo77pvsc t69;
                        memset(&t69, 0, sizeof t69);
                        gm_l11 = t69;
                        {
                            {
                                gm_b16_8 t70 = gm_l9;
                                gm_b16_8 t71;
                                GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sh0j2ggs1vvoa));
                                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(1));
                                gm_f_split_once_mqcu1ce((void *)&gm_l16, t70, t71);
                                uint8_t t72 = (uint8_t)((((uint8_t *)&gm_l16))[32] != 0);
                                if (!t72) {
                                    goto gm_lb12;
                                }
                                memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l16), 32);
                                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                                gm_l10 = t73;
                                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                gm_th2lqbo77pvsc t75;
                                GM_ST(uint8_t, (uint8_t *)&t75 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t75 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t75 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t75), t74);
                                ((uint8_t *)&t75)[16] = 1;
                                gm_l11 = t75;
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        {
                            {
                                gm_b16_8 t76 = gm_l2;
                                gm_b16_8 t77 = gm_l10;
                                uint8_t t78 = gm_f_contains_a7cccd4(t76, t77);
                                if (!t78) {
                                    goto gm_lb14;
                                }
                                {
                                    {
                                        uint8_t t79 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
                                        if (t79) {
                                            goto gm_lb16;
                                        }
                                        gm_l17 = ((uint8_t)0);
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    {
                                        gm_l17 = ((uint8_t)1);
                                        goto gm_lb15;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 85, 46);
                                }
gm_lb15:;
                                uint8_t t80 = gm_l17;
                                if (!t80) {
                                    goto gm_lb14;
                                }
                                gm_b16_8 t81 = gm_l10;
                                gm_tnpifv20irnph t82;
                                GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t81);
                                gm_f_format_6dqa07h((void *)&gm_l18, (void *)NULL, cx, NULL, t82);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srccr53ejqr1dde, 86, 25);
                                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                                gm_t5detta1b4gnk t86;
                                memset(&t86, 0, sizeof t86);
                                ((uint8_t *)&t86)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t86 + 8), t85);
                                memmove(gm_out, ((uint8_t *)&t86), 128);
                                goto gm_fdone;
                            }
gm_lb14:;
                            gm_b16_8 t87 = gm_l2;
                            gm_b16_8 t88 = gm_l10;
                            uint8_t t89 = gm_f_contains_a7cccd4(t87, t88);
                            if (t89) {
                                void * t90 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t91 = gm_l10;
                                uint8_t t92 = gm_f_has_flag_4n9tu40(t90, t91);
                                uint8_t t93 = (uint8_t)!t92;
                                if (t93) {
                                    void * t94 = (void *)(((uint8_t *)&F.l3) + 80);
                                    gm_b16_8 t95 = gm_l10;
                                    gm_tftb1odc739t5 t96 = gm_f_push_3ef746b(t94, t95);
                                    gm_l19 = t96;
                                    uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                    if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srccr53ejqr1dde, 88, 45);
                                }
                            } else {
                                gm_b16_8 t98 = gm_l1;
                                gm_b16_8 t99 = gm_l10;
                                uint8_t t100 = gm_f_contains_a7cccd4(t98, t99);
                                if (t100) {
                                    uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
                                    if (t101) {
                                        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                                        gm_l20 = t102;
                                    } else {
                                        uint64_t t103 = gm_l4;
                                        uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                        uint8_t t105 = (t103 >= t104);
                                        if (t105) {
                                            gm_b16_8 t106 = gm_l10;
                                            gm_tnpifv20irnph t107;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t106);
                                            gm_f_format_n537vp2((void *)&gm_l21, (void *)NULL, cx, NULL, t107);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                            if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srccr53ejqr1dde, 91, 48);
                                            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                                            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                                            gm_t5detta1b4gnk t111;
                                            memset(&t111, 0, sizeof t111);
                                            ((uint8_t *)&t111)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
                                            memmove(gm_out, ((uint8_t *)&t111), 128);
                                            goto gm_fdone;
                                        }
                                        uint64_t t112 = gm_l4;
                                        uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                                        if (o114) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 92, 21);
                                        uint64_t t115 = r113;
                                        gm_l4 = t115;
                                        void * t116 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                        uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                        uint64_t t118 = gm_l4;
                                        uint64_t r119; bool o120 = __builtin_sub_overflow(t118, ((uint64_t)1ULL), &r119);
                                        if (o120) gm_trap("integer overflow in `-`", gm_srccr53ejqr1dde, 93, 26);
                                        uint64_t t121 = r119;
                                        if ((uintptr_t)t121 >= (uintptr_t)(t117)) gm_trap_index((uintptr_t)t121, t117, gm_srccr53ejqr1dde, 93, 21);
                                        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)t116) + (uintptr_t)t121 * 16));
                                        gm_l20 = t122;
                                    }
                                    gm_b16_8 t123 = gm_l20;
                                    gm_l13 = t123;
                                    void * t124 = (void *)((uint8_t *)&F.l3);
                                    gm_b16_8 t125 = gm_l10;
                                    gm_f_value_of_0km7vvp((void *)&gm_l23, t124, t125);
                                    {
                                        {
                                            uint8_t t126 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                                            if (t126) {
                                                goto gm_lb19;
                                            }
                                            gm_l22 = ((uint8_t)0);
                                            goto gm_lb18;
                                        }
gm_lb19:;
                                        {
                                            gm_l22 = ((uint8_t)1);
                                            goto gm_lb18;
                                        }
gm_lb20:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 95, 20);
                                    }
gm_lb18:;
                                    uint8_t t127 = gm_l22;
                                    if (t127) {
                                        gm_b16_8 t128 = gm_l10;
                                        gm_tnpifv20irnph t129;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
                                        gm_f_format_fbigqms((void *)&gm_l24, (void *)NULL, cx, NULL, t129);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                        if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srccr53ejqr1dde, 95, 61);
                                        gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                                        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                                        gm_t5detta1b4gnk t133;
                                        memset(&t133, 0, sizeof t133);
                                        ((uint8_t *)&t133)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t133 + 8), t132);
                                        memmove(gm_out, ((uint8_t *)&t133), 128);
                                        goto gm_fdone;
                                    }
                                    void * t134 = (void *)(((uint8_t *)&F.l3) + 40);
                                    gm_b16_8 t135 = gm_l10;
                                    gm_b16_8 t136 = gm_l13;
                                    gm_t6j6smj7qoctu t137;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t135);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t137 + 16), t136);
                                    gm_tftb1odc739t5 t138 = gm_f_push_2tc790g(t134, &t137);
                                    gm_l25 = t138;
                                    uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                    if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srccr53ejqr1dde, 96, 17);
                                } else {
                                    gm_b16_8 t140 = gm_l5;
                                    gm_tnpifv20irnph t141;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t141 + 0), t140);
                                    gm_f_format_j0ggm04((void *)&gm_l26, (void *)NULL, cx, NULL, t141);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                    if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srccr53ejqr1dde, 98, 25);
                                    gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                    gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                                    gm_t5detta1b4gnk t145;
                                    memset(&t145, 0, sizeof t145);
                                    ((uint8_t *)&t145)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t145 + 8), t144);
                                    memmove(gm_out, ((uint8_t *)&t145), 128);
                                    goto gm_fdone;
                                }
                            }
                        }
gm_lb13:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        {
                            gm_b16_8 t146 = gm_l5;
                            gm_b16_8 t147;
                            GM_ST(void *, (uint8_t *)&t147, (void *)(gm_skolmnfc0ccbd));
                            GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(1));
                            uint8_t t148 = gm_f_starts_with_6vvt01e(t146, t147);
                            if (!t148) {
                                goto gm_lb22;
                            }
                            uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint8_t t150 = (t149 > ((uint64_t)1ULL));
                            if (!t150) {
                                goto gm_lb22;
                            }
                            gm_b16_8 t151 = gm_l5;
                            gm_tnpifv20irnph t152;
                            GM_ST(gm_b16_8, ((uint8_t *)&t152 + 0), t151);
                            gm_f_format_j0ggm04((void *)&gm_l27, (void *)NULL, cx, NULL, t152);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                            if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srccr53ejqr1dde, 101, 21);
                            gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                            gm_t5detta1b4gnk t156;
                            memset(&t156, 0, sizeof t156);
                            ((uint8_t *)&t156)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t156 + 8), t155);
                            memmove(gm_out, ((uint8_t *)&t156), 128);
                            goto gm_fdone;
                        }
gm_lb22:;
                        void * t157 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t158 = gm_l5;
                        gm_tftb1odc739t5 t159 = gm_f_push_3ef746b(t157, t158);
                        gm_l28 = t159;
                        uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srccr53ejqr1dde, 103, 13);
                    }
gm_lb21:;
                }
gm_lb9:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tr8dvlu9ik4gj t161 = GM_LD(gm_tr8dvlu9ik4gj, ((uint8_t *)&F.l3));
    gm_t5detta1b4gnk t162;
    GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
    ((uint8_t *)&t162)[0] = 0;
    GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&t162 + 8), t161);
    memmove(gm_out, ((uint8_t *)&t162), 128);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_relative_path_3klciqu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_relative_path_3klciqu F;
    gm_b16_8 gm_l0; /* from */
    gm_b16_8 gm_l1; /* target */
    uint64_t gm_l4; /* common */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* x */
    GM_STACK_CHECK(&F, 817u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tl7hpf5nlilqq gm_l11;
    gm_tl7hpf5nlilqq gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_b16_8 t15 = gm_l0;
    gm_f_split_cbk4ks8((void *)&gm_l11, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l11) + 16), 40);
    gm_b16_8 t16 = gm_l1;
    gm_f_split_cbk4ks8((void *)&gm_l12, (void *)NULL, cx, NULL, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l12) + 16), 40);
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l4;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            uint64_t t20 = gm_l4;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            void * t23 = GM_LD(void *, ((uint8_t *)&F.l2));
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            uint64_t t25 = gm_l4;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t23) + (uintptr_t)t25 * 16));
            void * t27 = GM_LD(void *, ((uint8_t *)&F.l3));
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            uint64_t t29 = gm_l4;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t27) + (uintptr_t)t29 * 16));
            uint8_t t31 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t30)));
            if (!t31) {
                goto gm_lb1;
            }
            {
                uint64_t t32 = gm_l4;
                uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 179, 98);
                uint64_t t35 = r33;
                gm_l4 = t35;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t36;
    gm_f_list_epvducg((void *)&t36, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t36;
    uint64_t t37 = gm_l4;
    gm_l7 = t37;
    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    gm_l6 = t38;
    {
        for (;;) {
            uint64_t t39 = gm_l7;
            uint64_t t40 = gm_l6;
            uint8_t t41 = (t39 < t40);
            if (!t41) {
                goto gm_lb4;
            }
            {
                {
                    {
                        void * t42 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_snenkhfev166s));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t44 = gm_f_push_3ef746b(t42, t43);
                        gm_l13 = t44;
                        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srccr53ejqr1dde, 181, 36);
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 181, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t46 = gm_l7;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l7 = t49;
        }
    }
gm_lb4:;
    void * t50 = GM_LD(void *, ((uint8_t *)&F.l3));
    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint64_t t52 = gm_l4;
    uint64_t t53 = (uintptr_t)t52;
    uint64_t t54 = (uintptr_t)t51;
    if (t53 > t54 || t54 > (uintptr_t)t51) gm_trap_slice(t53, t54, t51, gm_srccr53ejqr1dde, 182, 14);
    uint64_t t55 = ((uintptr_t)t51) - (uintptr_t)t52;
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t50 + (uintptr_t)t52 * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55));
    gm_l8 = t56;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t57 = gm_l9;
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t59 = (t57 < t58);
            if (!t59) {
                goto gm_lb9;
            }
            {
                void * t60 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t62 = gm_l9;
                {
                    {
                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t60) + (uintptr_t)t62 * 16));
                        gm_l10 = t63;
                        void * t64 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t65 = gm_l10;
                        gm_tftb1odc739t5 t66 = gm_f_push_3ef746b(t64, t65);
                        gm_l14 = t66;
                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srccr53ejqr1dde, 182, 34);
                        goto gm_lb12;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 182, 5);
                }
gm_lb12:;
            }
gm_lb11:;
            uint64_t t68 = gm_l9;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l9 = t71;
        }
    }
gm_lb9:;
    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
    uint8_t t73 = ((t72 == ((uint64_t)0ULL)));
    if (t73) {
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
        F.ret = t74;
        goto gm_fret;
    }
    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l15, (void *)NULL, cx, NULL, t76, t77);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srccr53ejqr1dde, 184, 12);
    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
    F.ret = t80;
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
__attribute__((aligned(4))) void gm_f_remove_bar6kga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_bar6kga F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* e */
    gm_b16_8 gm_l6; /* root */
    gm_b16_8 gm_l7; /* manifest_path */
    gm_b16_8 gm_l8; /* e */
    gm_b16_8 gm_l9; /* old_text */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* new_text */
    gm_b16_8 gm_l12; /* e */
    gm_b16_8 gm_l13; /* e */
    gm_b16_8 gm_l14; /* e2 */
    GM_STACK_CHECK(&F, 2091u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_b16_8 gm_l17;
    gm_t3ujhu6qph7de gm_l18;
    gm_b16_8 gm_l19;
    gm_t3ujhu6qph7de gm_l20;
    gm_b16_8 gm_l21;
    gm_t3ujhu6qph7de gm_l22;
    gm_b16_8 gm_l23;
    gm_tl572d7579hns gm_l24;
    gm_t074vrl1hb4f6 gm_l25;
    gm_tv37d3dpt2opu gm_l26;
    gm_tl572d7579hns gm_l27;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snboa22b96bbc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_tpmdr2hmdhsqr t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_l1 = t4;
    void * t5 = (void *)((uint8_t *)&gm_l1);
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)1ULL);
    if (t6 > t7 || t7 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t6, t7, ((uint64_t)1ULL), gm_srccr53ejqr1dde, 273, 45);
    uint64_t t8 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    void * t10 = (void *)((uint8_t *)&gm_l2);
    uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
    if (t11 > t12 || t12 > (uintptr_t)((uint64_t)0ULL)) gm_trap_slice(t11, t12, ((uint64_t)0ULL), gm_srccr53ejqr1dde, 273, 67);
    uint64_t t13 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t10 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    gm_t5detta1b4gnk t15;
    gm_f_parse_args_7blm2mp((void *)&t15, (void *)NULL, cx, NULL, t2, t9, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_usage_failure_cgg6v1t((void *)&gm_l15, &t15);
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (t16) {
    } else {
        gm_t3ujhu6qph7de t17 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l15) + 8));
        gm_t715ajl6o1ipo t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        ((uint8_t *)&t18)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t18 + 8), t17);
        memmove(gm_out, ((uint8_t *)&t18), 32);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l15) + 8), 120);
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint8_t t20 = (!(t19 == ((uint64_t)1ULL)));
    if (t20) {
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_saicih5tjo4fl));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(42));
        gm_t3ujhu6qph7de t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
        gm_t715ajl6o1ipo t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t23 + 8), t22);
        memmove(gm_out, ((uint8_t *)&t23), 32);
        goto gm_fdone;
    }
    void * t24 = GM_LD(void *, ((uint8_t *)&F.l3));
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t25, gm_srccr53ejqr1dde, 275, 13);
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t24) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_l4 = t26;
    void * t27 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_snboa22b96bbc));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
    gm_f_value_of_0km7vvp((void *)&gm_l16, t27, t28);
    uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
    if (t29) {
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
        gm_l17 = t30;
    } else {
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
        gm_l17 = t31;
    }
    gm_b16_8 t32 = gm_l17;
    gm_f_root_of_cluda11((void *)&gm_l18, (void *)NULL, cx, NULL, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (t33) {
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_l19 = t34;
    } else {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_l5 = t35;
        gm_b16_8 t36 = gm_l5;
        gm_t3ujhu6qph7de t37;
        gm_f_fail_o80k3rv((void *)&t37, t36);
        gm_t715ajl6o1ipo t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
        ((uint8_t *)&t38)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t38 + 8), t37);
        memmove(gm_out, ((uint8_t *)&t38), 32);
        goto gm_fdone;
    }
    gm_b16_8 t39 = gm_l19;
    gm_l6 = t39;
    gm_b16_8 t40 = gm_l6;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_se24c7hpp401n));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(10));
    gm_b16_8 t42 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t40, t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t42;
    gm_b16_8 t43 = gm_l7;
    gm_f_read_text_g0qbj1t((void *)&gm_l20, (void *)NULL, cx, NULL, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (t44) {
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_l21 = t45;
    } else {
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_l8 = t46;
        gm_b16_8 t47 = gm_l8;
        gm_t3ujhu6qph7de t48;
        gm_f_fail_o80k3rv((void *)&t48, t47);
        gm_t715ajl6o1ipo t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
        ((uint8_t *)&t49)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t49 + 8), t48);
        memmove(gm_out, ((uint8_t *)&t49), 32);
        goto gm_fdone;
    }
    gm_b16_8 t50 = gm_l21;
    gm_l9 = t50;
    gm_b16_8 t51 = gm_l9;
    gm_b16_8 t52 = gm_l4;
    gm_f_remove_dependency_4ju7b0h((void *)&gm_l22, (void *)NULL, cx, NULL, t51, t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (t53) {
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_l23 = t54;
    } else {
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_l10 = t55;
        gm_b16_8 t56 = gm_l10;
        gm_t3ujhu6qph7de t57;
        gm_f_fail_o80k3rv((void *)&t57, t56);
        gm_t715ajl6o1ipo t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        ((uint8_t *)&t58)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t58 + 8), t57);
        memmove(gm_out, ((uint8_t *)&t58), 32);
        goto gm_fdone;
    }
    gm_b16_8 t59 = gm_l23;
    gm_l11 = t59;
    gm_b16_8 t60 = gm_l7;
    gm_b16_8 t61 = gm_l11;
    gm_f_write_atomic_v74ncu3((void *)&gm_l24, (void *)NULL, cx, NULL, t60, t61);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (t62) {
    } else {
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
        gm_l12 = t63;
        gm_b16_8 t64 = gm_l12;
        gm_t3ujhu6qph7de t65;
        gm_f_fail_o80k3rv((void *)&t65, t64);
        gm_t715ajl6o1ipo t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        ((uint8_t *)&t66)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t66 + 8), t65);
        memmove(gm_out, ((uint8_t *)&t66), 32);
        goto gm_fdone;
    }
    gm_b16_8 t67 = gm_l6;
    gm_tqs5p4n2tq19v t68;
    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t68 + 1), ((uint8_t)0));
    gm_tl572d7579hns t69;
    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t69 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t69 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
    gm_f_sync_ht164ep((void *)&gm_l25, (void *)NULL, cx, NULL, t67, t68, &t69, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (t70) {
        memmove(((uint8_t *)&gm_l26), (((uint8_t *)&gm_l25) + 8), 48);
    } else {
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
        gm_l13 = t71;
        gm_b16_8 t72 = gm_l7;
        gm_b16_8 t73 = gm_l9;
        gm_f_write_atomic_v74ncu3((void *)&gm_l27, (void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
        if (t74) {
        } else {
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
            gm_l14 = t75;
            gm_b16_8 t76 = gm_l14;
            gm_t3ujhu6qph7de t77;
            gm_f_fail_o80k3rv((void *)&t77, t76);
            gm_t715ajl6o1ipo t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            ((uint8_t *)&t78)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t78 + 8), t77);
            memmove(gm_out, ((uint8_t *)&t78), 32);
            goto gm_fdone;
        }
        gm_b16_8 t79 = gm_l13;
        gm_t3ujhu6qph7de t80;
        gm_f_fail_o80k3rv((void *)&t80, t79);
        gm_t715ajl6o1ipo t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
        ((uint8_t *)&t81)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t81 + 8), t80);
        memmove(gm_out, ((uint8_t *)&t81), 32);
        goto gm_fdone;
    }
    gm_b16_8 t82 = gm_l4;
    gm_tv37d3dpt2opu t83 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)&gm_l26));
    if (gm_pf454qfrpt6r(cx, gm_s9m6kd6gec368, 13, gm_srccr53ejqr1dde, 285, 5)) {
        goto gm_lad;
    }
    if ((GM_LD(uintptr_t, ((uint8_t *)&t82) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t82)), GM_LD(uintptr_t, ((uint8_t *)&t82) + 8), gm_srccr53ejqr1dde, 285, 5))) {
        goto gm_lad;
    }
    if (gm_pf454qfrpt6r(cx, gm_smsc5gshqt6cd, 19, gm_srccr53ejqr1dde, 285, 5)) {
        goto gm_lad;
    }
    gm_t715ajl6o1ipo t84;
    GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t84 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t84 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t84 + 24, 0);
    ((uint8_t *)&t84)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t84 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t84), 32);
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
inline __attribute__((aligned(4))) void gm_f_root_of_cluda11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_root_of_cluda11 F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_tih6h4avv5nrb gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_th2lqbo77pvsc gm_l4;
    gm_t3ujhu6qph7de gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_b16_8 t5 = gm_l0;
    gm_node *pn6 = gm_find(cx, 2126215853);
    if (!pn6) gm_trap("no handler installed for effect `Fs`", gm_srccr53ejqr1dde, 110, 8);
    gm_tih6h4avv5nrb t7;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn6->clauses[5])((void *)&t7, pn6->env, pn6->hctx, pn6, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t7;
    {
        {
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (t8) {
                goto gm_lb2;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 110, 8);
    }
gm_lb1:;
    uint8_t t9 = gm_l1;
    if (t9) {
        gm_b16_8 t10 = gm_l0;
        gm_tnpifv20irnph t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
        gm_f_format_27unvt9((void *)&gm_l3, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srccr53ejqr1dde, 110, 37);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_t3ujhu6qph7de t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        ((uint8_t *)&t15)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
        memmove(gm_out, ((uint8_t *)&t15), 24);
        goto gm_fdone;
    }
    gm_b16_8 t16 = gm_l0;
    gm_f_find_root_kjnm6hf((void *)&gm_l4, (void *)NULL, cx, NULL, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
    if (t17) {
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_t3ujhu6qph7de t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
        gm_l5 = t19;
    } else {
        gm_b16_8 t20 = gm_l0;
        gm_tnpifv20irnph t21;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
        gm_f_format_v71csbq((void *)&gm_l6, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srccr53ejqr1dde, 111, 33);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_t3ujhu6qph7de t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        ((uint8_t *)&t25)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 8), t24);
        gm_l5 = t25;
    }
    memmove(gm_out, ((uint8_t *)&gm_l5), 24);
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
inline __attribute__((aligned(4))) uint8_t gm_f_run_command_s4fhqaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_run_command_s4fhqaq F;
    gm_b16_8 gm_l0; /* tool */
    gm_b16_8 gm_l1; /* command */
    gm_b16_8 gm_l2; /* args */
    gm_t715ajl6o1ipo gm_l3; /* result */
    gm_b16_8 gm_l4; /* e */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 752u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t715ajl6o1ipo gm_l6;
    gm_tabb710d9mq2e gm_l7;
    {
        {
            gm_b16_8 t15 = gm_l1;
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s6pnfrub0rj1t));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
            uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
            if (!t17) {
                goto gm_lb2;
            }
            gm_b16_8 t18 = gm_l2;
            gm_f_fetch_l6q1k6p((void *)&gm_l6, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t19 = gm_l1;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3lbmpmt95hvh));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
            uint8_t t21 = (gm_eq0iqiubfm7e(((uint8_t *)&t19), ((uint8_t *)&t20)));
            if (!t21) {
                goto gm_lb3;
            }
            gm_b16_8 t22 = gm_l2;
            gm_f_update_oi0ok0i((void *)&gm_l6, (void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t23 = gm_l1;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s4o70d89183c2));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
            if (!t25) {
                goto gm_lb4;
            }
            gm_b16_8 t26 = gm_l2;
            gm_f_add_04lv2cc((void *)&gm_l6, (void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_b16_8 t27 = gm_l1;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sqljkj8r253om));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(6));
            uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
            if (!t29) {
                goto gm_lb5;
            }
            gm_b16_8 t30 = gm_l2;
            gm_f_remove_bar6kga((void *)&gm_l6, (void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_b16_8 t31 = gm_l1;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sivh46n0690a4));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
            uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
            if (!t33) {
                goto gm_lb6;
            }
            gm_b16_8 t34 = gm_l2;
            gm_f_new_package_hf828qc((void *)&gm_l6, (void *)NULL, cx, NULL, t34, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_b16_8 t35 = gm_l1;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_suass5sid4b6e));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(4));
            uint8_t t37 = (gm_eq0iqiubfm7e(((uint8_t *)&t35), ((uint8_t *)&t36)));
            if (!t37) {
                goto gm_lb7;
            }
            gm_b16_8 t38 = gm_l2;
            gm_f_new_package_hf828qc((void *)&gm_l6, (void *)NULL, cx, NULL, t38, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb7:;
        {
            gm_b16_8 t39 = gm_l1;
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_so9n4mmd960c7));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(5));
            uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
            if (!t41) {
                goto gm_lb8;
            }
            gm_b16_8 t42 = gm_l2;
            gm_f_cache_command_7uqmos8((void *)&gm_l6, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            gm_b16_8 t43 = gm_l1;
            gm_tnpifv20irnph t44;
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
            gm_f_format_gl2aoqn((void *)&gm_l7, (void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srccr53ejqr1dde, 131, 29);
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
            gm_t3ujhu6qph7de t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 8), t47);
            gm_t715ajl6o1ipo t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            ((uint8_t *)&t49)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t49 + 8), t48);
            gm_l6 = t49;
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 123, 26);
    }
gm_lb1:;
    memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l6), 32);
    {
        {
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (t50) {
                goto gm_lb11;
            }
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)0ULL));
            if (!t52) {
                goto gm_lb11;
            }
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l3) + 8) + 8));
            gm_l4 = t53;
            gm_b16_8 t54 = gm_l0;
            gm_b16_8 t55 = gm_l4;
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)gm_crqb3tjd618));
            if ((GM_LD(uintptr_t, ((uint8_t *)&t54) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t54)), GM_LD(uintptr_t, ((uint8_t *)&t54) + 8), gm_srccr53ejqr1dde, 135, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t55) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t55)), GM_LD(uintptr_t, ((uint8_t *)&t55) + 8), gm_srccr53ejqr1dde, 135, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s8dbdqj1sqibj, 2, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t56) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t56)), GM_LD(uintptr_t, ((uint8_t *)&t56) + 8), gm_srccr53ejqr1dde, 135, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)2ULL);
            goto gm_fret;
        }
gm_lb11:;
        {
            uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (t57) {
                goto gm_lb12;
            }
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)1ULL));
            if (!t59) {
                goto gm_lb12;
            }
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l3) + 8) + 8));
            gm_l5 = t60;
            gm_b16_8 t61 = gm_l0;
            gm_b16_8 t62 = gm_l5;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t61) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t61)), GM_LD(uintptr_t, ((uint8_t *)&t61) + 8), gm_srccr53ejqr1dde, 139, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t62) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t62)), GM_LD(uintptr_t, ((uint8_t *)&t62) + 8), gm_srccr53ejqr1dde, 139, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
gm_lb12:;
        {
            F.ret = ((uint8_t)0ULL);
            goto gm_fret;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 133, 5);
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
inline __attribute__((aligned(4))) void gm_f_update_oi0ok0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_update_oi0ok0i F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* e */
    gm_b16_8 gm_l5; /* root */
    gm_tl572d7579hns gm_l6; /* refresh */
    gm_b16_8 gm_l7; /* e */
    GM_STACK_CHECK(&F, 1263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    gm_b16_8 gm_l10;
    gm_t3ujhu6qph7de gm_l11;
    gm_b16_8 gm_l12;
    gm_tl572d7579hns gm_l13;
    gm_t074vrl1hb4f6 gm_l14;
    gm_tv37d3dpt2opu gm_l15;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snboa22b96bbc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_tpmdr2hmdhsqr t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_l1 = t4;
    void * t5 = (void *)((uint8_t *)&gm_l1);
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)1ULL);
    if (t6 > t7 || t7 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t6, t7, ((uint64_t)1ULL), gm_srccr53ejqr1dde, 167, 45);
    uint64_t t8 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    void * t10 = (void *)((uint8_t *)&gm_l2);
    uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
    if (t11 > t12 || t12 > (uintptr_t)((uint64_t)0ULL)) gm_trap_slice(t11, t12, ((uint64_t)0ULL), gm_srccr53ejqr1dde, 167, 67);
    uint64_t t13 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t10 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    gm_t5detta1b4gnk t15;
    gm_f_parse_args_7blm2mp((void *)&t15, (void *)NULL, cx, NULL, t2, t9, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_usage_failure_cgg6v1t((void *)&gm_l8, &t15);
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t16) {
    } else {
        gm_t3ujhu6qph7de t17 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l8) + 8));
        gm_t715ajl6o1ipo t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        ((uint8_t *)&t18)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t18 + 8), t17);
        memmove(gm_out, ((uint8_t *)&t18), 32);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l8) + 8), 120);
    void * t19 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_snboa22b96bbc));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
    gm_f_value_of_0km7vvp((void *)&gm_l9, t19, t20);
    uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
    if (t21) {
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
        gm_l10 = t22;
    } else {
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
        gm_l10 = t23;
    }
    gm_b16_8 t24 = gm_l10;
    gm_f_root_of_cluda11((void *)&gm_l11, (void *)NULL, cx, NULL, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t25) {
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_l12 = t26;
    } else {
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_l4 = t27;
        gm_b16_8 t28 = gm_l4;
        gm_t3ujhu6qph7de t29;
        gm_f_fail_o80k3rv((void *)&t29, t28);
        gm_t715ajl6o1ipo t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        ((uint8_t *)&t30)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t30 + 8), t29);
        memmove(gm_out, ((uint8_t *)&t30), 32);
        goto gm_fdone;
    }
    gm_b16_8 t31 = gm_l12;
    gm_l5 = t31;
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
    if (t33) {
        gm_tl572d7579hns t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t34 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t34 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1ULL));
        gm_l13 = t34;
    } else {
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_tl572d7579hns t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t37 + 8), t36);
        gm_l13 = t37;
    }
    memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l13), 24);
    gm_b16_8 t38 = gm_l5;
    gm_tqs5p4n2tq19v t39;
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t39 + 1), ((uint8_t)0));
    gm_tl572d7579hns t40 = GM_LD(gm_tl572d7579hns, ((uint8_t *)&gm_l6));
    gm_f_sync_ht164ep((void *)&gm_l14, (void *)NULL, cx, NULL, t38, t39, &t40, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (t41) {
        memmove(((uint8_t *)&gm_l15), (((uint8_t *)&gm_l14) + 8), 48);
    } else {
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_l7 = t42;
        gm_b16_8 t43 = gm_l7;
        gm_t3ujhu6qph7de t44;
        gm_f_fail_o80k3rv((void *)&t44, t43);
        gm_t715ajl6o1ipo t45;
        GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
        ((uint8_t *)&t45)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t45 + 8), t44);
        memmove(gm_out, ((uint8_t *)&t45), 32);
        goto gm_fdone;
    }
    gm_t715ajl6o1ipo t46;
    GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t46 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t46 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t46 + 24, 0);
    ((uint8_t *)&t46)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t46 + 8), GM_ZERO(gm_b0_1));
    gm_tv37d3dpt2opu t47 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)&gm_l15));
    memmove(gm_out, ((uint8_t *)&t46), 32);
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
extern __attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t77ebp3ogdble gm_f_usage_failure_cgg6v1t_c(gm_t5detta1b4gnk a0) { gm_t77ebp3ogdble gm_r; gm_f_usage_failure_cgg6v1t(&gm_r, &a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_value_of_0km7vvp(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_of_0km7vvp F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 258u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 40));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t12) + (uintptr_t)t14 * 32), 32);
                        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                            gm_th2lqbo77pvsc t19;
                            GM_ST(uint8_t, (uint8_t *)&t19 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t19 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t19 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t19), t18);
                            ((uint8_t *)&t19)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t19), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 47, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l3;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l3 = t23;
        }
    }
gm_lb1:;
    gm_th2lqbo77pvsc t24;
    memset(&t24, 0, sizeof t24);
    memmove(gm_out, ((uint8_t *)&t24), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_value_of_0km7vvp_c(void * a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_value_of_0km7vvp(&gm_r, a0, a1); return gm_r; }
