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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csia7oevr2u0fk { _Bool f0; unsigned __int128 f1; } gm_csia7oevr2u0fk;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csia7oevr2u0fk) == 32 && _Alignof(gm_csia7oevr2u0fk) == 16 && offsetof(gm_csia7oevr2u0fk, f0) == 0 && offsetof(gm_csia7oevr2u0fk, f1) == 16, "layout of `NumVal`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { gm_t86h6hhm6d76n f0; uint8_t f1; uint8_t pe[15]; } gm_tsp9ul70rnkg6;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { gm_tvdg94bk7aa22 f0; } gm_th9nl3oso747e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsp9ul70rnkg6) == 48 && _Alignof(gm_tsp9ul70rnkg6) == 16 && offsetof(gm_tsp9ul70rnkg6, f0) == 0 && offsetof(gm_tsp9ul70rnkg6, f1) == 32, "layout of `?NumVal`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_th9nl3oso747e) == 12 && _Alignof(gm_th9nl3oso747e) == 4 && offsetof(gm_th9nl3oso747e, f0) == 0, "layout of `(Builtin,)`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_apply_builtin_41dffti {
    gm_frame h;
    gm_tckdobunaulee l32; /* d */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_array_ptr_to_slice_7dg3m1v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_coerce_bytes_9kf3u24 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_convert_is6an2k {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_format_46el73v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8k5eh95 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_985clkf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9qvncot {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fv5tfj6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h8e7ps1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj15nqp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l7c15sv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pe0ujmq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra7pont {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ven7kt5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_hash_bytes_2qnf0cp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_int_26mf5j0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_u64_32bripn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_val_nr2e99r {
    gm_frame h;
    gm_t2jeg3bg7sfqa l17; /* s */
    gm_tpautr4br7h3l l31; /* def */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_text_vrjnuu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_bits_onl28vp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_f16_to_f64_h70suv4 {
    gm_frame h;
    double ret;
};
struct gm_fr_num_f64_to_f16_3g33m05 {
    gm_frame h;
    uint16_t ret;
};
struct gm_fr_num_is_nan_b0ma7mo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_round_2pna7m3 {
    gm_frame h;
    double ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_opt_inner_q4ga84u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_align_lavuopj {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint16_t gm_f_f16_bits_2us9irg(double a0);
__attribute__((aligned(4))) double gm_f_f16_value_42vkfhl(uint16_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8k5eh95(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_985clkf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0);
inline __attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
__attribute__((aligned(4))) void gm_f_format_b3uc2q7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hmhrs8a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_mnuav4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_pe0ujmq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
inline __attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0);
inline __attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0);
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh(const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_float_int_ba5kc1t(void *gm_out, double a0, gm_tvdg94bk7aa22 a1);
__attribute__((aligned(4))) double gm_f_num_fmax_0qau2jp(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_fmin_1k5uqp0(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6(const void * a0, uint16_t a1);
__attribute__((aligned(4))) uint8_t gm_f_num_integral_qbts4a6(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
extern const uint8_t gm_s064htvgs8b59[69];
extern const uint8_t gm_s26c80h2pm6a7[26];
extern const uint8_t gm_s2jl146j9vpnj[20];
extern const uint8_t gm_s3ergh2il1tjf[43];
extern const uint8_t gm_s3gnqv0up712m[61];
extern const uint8_t gm_s4bovq0eevpad[23];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s9h8lp3gkedth[29];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_sartclcojin4f[52];
extern const uint8_t gm_sb9352gc5i144[21];
extern const uint8_t gm_sbel6qookv0gl[54];
extern const uint8_t gm_sbjj41cb5lph3[60];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sc6st5mdtp49u[16];
extern const uint8_t gm_sciu02cqahje3[52];
extern const uint8_t gm_sd4ibvv64si9n[45];
extern const uint8_t gm_sge745dj3cnrb[73];
extern const uint8_t gm_sge7ihldgqi49[20];
extern const uint8_t gm_sh01rb1u2jpfs[19];
extern const uint8_t gm_sl9mgk5ho4kmr[29];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_spqf88ftq0gqk[29];
extern const uint8_t gm_sq87o4fbigdsl[17];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sti4tkgslrkgo[26];
extern const uint8_t gm_stk678hojgu6q[24];
extern const uint8_t gm_su88ia0hdam4d[66];
extern const gm_tmpl gm_tpl15v8rvh46nek;
extern const gm_tmpl gm_tpl1lj3ah7qeq42;
extern const gm_tmpl gm_tplav8bdvvt8rem;
extern const gm_tmpl gm_tplcktokqd7n6fo;
extern const gm_tmpl gm_tpljicvo0ni8aba;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplmm8hld32929h;
extern const gm_tmpl gm_tplmsb1klg4n6ap;
extern const gm_tmpl gm_tplt1lu36t604pk;
extern const gm_tmpl gm_tplvl1jqbb33ag2;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t4 = ((t2 == t3));
        if (t4) {
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            { gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5); }
        }
    }
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_46el73v F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slnkfiltk5b43));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplav8bdvvt8rem), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8k5eh95(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8k5eh95 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2jl146j9vpnj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljicvo0ni8aba), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_985clkf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0) {
    struct gm_fr_format_985clkf F;
    gm_th9nl3oso747e gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_th9nl3oso747e gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th9nl3oso747e t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq87o4fbigdsl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_hmhrs8a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt1lu36t604pk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0) {
    struct gm_fr_format_9qvncot F;
    gm_tpl3q6e8s9qo1 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tpl3q6e8s9qo1 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tpl3q6e8s9qo1 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_b3uc2q7((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fv5tfj6 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl9mgk5ho4kmr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1lj3ah7qeq42), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h8e7ps1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6st5mdtp49u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvl1jqbb33ag2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kj15nqp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sge7ihldgqi49));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmsb1klg4n6ap), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_l7c15sv F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su88ia0hdam4d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmm8hld32929h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pe0ujmq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pe0ujmq F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4bovq0eevpad));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcktokqd7n6fo), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ven7kt5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sciu02cqahje3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl15v8rvh46nek), t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_int_26mf5j0 F;
    void * gm_l0; /* h */
    gm_t86h6hhm6d76n gm_l1; /* x */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* low */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    unsigned __int128 t2 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)a1)));
    gm_l3 = t2;
    void * t3 = gm_l0;
    unsigned __int128 t4 = gm_l3;
    uint64_t t5 = (uint64_t)t4;
    gm_f_hash_u64_32bripn(t3, t5);
    uint32_t t6 = gm_l2;
    uint8_t t7 = (t6 > ((uint32_t)64ULL));
    if (t7) {
        void * t8 = gm_l0;
        unsigned __int128 t9 = gm_l3;
        unsigned __int128 n10 = (unsigned __int128)((uint64_t)64ULL);
        if (n10 >= 128) gm_trap_shift((uint64_t)n10, "u128", gm_srcl8pn2pjtmj6k, 427, 33);
        unsigned __int128 t11 = (unsigned __int128)(t9 >> (int)n10);
        uint64_t t12 = (uint64_t)t11;
        gm_f_hash_u64_32bripn(t8, t12);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t4, GM_CONV(gm_csguankko4lmhd, t5));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 360) + 40));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 360) + 48));
    gm_t2lqu8jsj511d t6;
    GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_nullable_pointer_nkie06s(t2, GM_CONV(gm_csguankko4lmhd, t3));
    F.ret = t4;
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
inline __attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_f16_to_f64_h70suv4 F;
    uint16_t gm_l0; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint16_t t2 = gm_l0;
    double t3 = gm_f_f16_value_42vkfhl(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_f64_to_f16_3g33m05 F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t2 = gm_l0;
    uint16_t t3 = gm_f_f16_bits_2us9irg(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_is_nan_b0ma7mo F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t2 = gm_l0;
    double t3 = gm_l0;
    uint8_t t4 = (!(t2 == t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_round_2pna7m3 F;
    double gm_l0; /* x */
    uint16_t gm_l1; /* width */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint16_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint16_t)32ULL)));
    if (t3) {
        double t4 = gm_l0;
        float t5 = (float)((double)t4);
        double t6 = (double)t5;
        F.ret = t6;
        goto gm_fret;
    }
    uint16_t t7 = gm_l1;
    uint8_t t8 = ((t7 == ((uint16_t)16ULL)));
    if (t8) {
        double t9 = gm_l0;
        uint16_t t10 = gm_f_num_f64_to_f16_3g33m05(t9);
        double t11 = gm_f_num_f16_to_f64_h70suv4(t10);
        F.ret = t11;
        goto gm_fret;
    }
    double t12 = gm_l0;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_of_e6hfj1r((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_inner_q4ga84u F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_kind_htv6093((void *)&gm_l3, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)13ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t8;
            gm_t88rcucg1b7uk t9 = gm_l2;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
        gm_t88rcucg1b7uk t10 = gm_l1;
        F.ret = t10;
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t6 = (t5 > ((uint64_t)1ULL));
    if (t6) {
        void * t7 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)((uint64_t)1ULL) * 8));
        gm_l3 = t9;
    } else {
        gm_l3 = ((uint64_t)0ULL);
    }
    uint64_t t10 = gm_l3;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 104, 68);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t2, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 360);
    gm_l2 = t3;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    void * t7 = gm_l2;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 40));
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srcpit8mdn0781q, 387, 14);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)t8 * 16));
    gm_l3 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t11 = (uintptr_t)t10;
    gm_l4 = t11;
    uint64_t t12 = gm_l4;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
    uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t20 = r18;
    uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
    uint64_t t22 = (uint64_t)~t21;
    uint64_t t23 = (uint64_t)(t20 & t22);
    uint64_t t24 = gm_l4;
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 391, 12);
    uint64_t t27 = r25;
    gm_l5 = t27;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_f_us_cuv01fp(t28);
    gm_l6 = t29;
    uint64_t t30 = gm_l1;
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l6;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 18);
        uint64_t t36 = r34;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t38 = (t36 > t37);
        gm_l8 = t38;
    }
    uint8_t t39 = gm_l8;
    if (t39) {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t42;
        goto gm_fret;
    }
    void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t45 = gm_l5;
    uint64_t t46 = gm_l5;
    uint64_t t47 = gm_l6;
    uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 21);
    uint64_t t50 = r48;
    uint64_t t51 = (uintptr_t)t45;
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t44) gm_trap_slice(t51, t52, t44, gm_srcpit8mdn0781q, 394, 10);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t45;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t43 + (uintptr_t)t45 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l7 = t54;
    void * t55 = gm_l2;
    uint64_t t56 = gm_l5;
    uint64_t t57 = gm_l6;
    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
    if (o59) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 395, 14);
    uint64_t t60 = r58;
    GM_ST(uint64_t, (((uint8_t *)t55) + 48), t60);
    gm_b16_8 t61 = gm_l7;
    gm_f_fill_abmf5g5(t61, ((uint8_t)0ULL));
    gm_b16_8 t62 = gm_l7;
    F.ret = t62;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
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
    gm_f_format_46el73v((void *)&gm_l3, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcpit8mdn0781q, 438, 107);
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
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_apply_builtin_41dffti F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* res_ty */
    gm_t88rcucg1b7uk gm_l4; /* t0 */
    gm_t88rcucg1b7uk gm_l5; /* t1 */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* dst */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* al */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    __int128 gm_l12; /* stride */
    __int128 gm_l13; /* p */
    uint64_t gm_l14; /* pbits */
    uint64_t gm_l15; /* mask */
    int64_t gm_l16; /* d */
    int64_t gm_l17; /* q */
    gm_ttovhfee8mf28 gm_l18; /* info */
    unsigned __int128 gm_l19; /* raw */
    __int128 gm_l20; /* n */
    __int128 gm_l21; /* off */
    __int128 gm_l22; /* r */
    gm_tcmkompbl09il gm_l23; /* u */
    uint64_t gm_l24; /* n */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* def */
    gm_ttovhfee8mf28 gm_l27; /* info */
    gm_t86h6hhm6d76n gm_l28; /* x */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    unsigned __int128 gm_l33; /* tb */
    unsigned __int128 gm_l34; /* mag */
    gm_t2lqu8jsj511d gm_l35; /* mk */
    gm_b16_8 gm_l36; /* vb */
    double gm_l37; /* x */
    double gm_l38; /* y */
    double gm_l39; /* r */
    gm_ttovhfee8mf28 gm_l40; /* info */
    unsigned __int128 gm_l41; /* x */
    unsigned __int128 gm_l42; /* y */
    uint64_t gm_l43; /* pick */
    unsigned __int128 gm_l44; /* hi */
    gm_ttovhfee8mf28 gm_l45; /* info */
    unsigned __int128 gm_l46; /* x */
    unsigned __int128 gm_l47; /* r */
    gm_ttovhfee8mf28 gm_l48; /* info */
    unsigned __int128 gm_l49; /* x */
    unsigned __int128 gm_l50; /* y */
    unsigned __int128 gm_l51; /* d */
    gm_ttovhfee8mf28 gm_l52; /* r */
    gm_t9qkh0rsooc8d gm_l53; /* fl */
    double gm_l54; /* x */
    uint64_t gm_l55; /* raw */
    uint64_t gm_l56; /* exp */
    uint16_t gm_l57; /* w */
    double gm_l58; /* x */
    double gm_l59; /* y */
    double gm_l60; /* z */
    gm_ttovhfee8mf28 gm_l61; /* info */
    gm_ttovhfee8mf28 gm_l62; /* ainfo */
    uint32_t gm_l63; /* n */
    unsigned __int128 gm_l64; /* x */
    unsigned __int128 gm_l65; /* araw */
    uint32_t gm_l66; /* k */
    unsigned __int128 gm_l67; /* r */
    gm_ttovhfee8mf28 gm_l68; /* info */
    uint32_t gm_l69; /* n */
    unsigned __int128 gm_l70; /* x */
    unsigned __int128 gm_l71; /* r */
    uint32_t gm_l72; /* <tmp> */
    uint32_t gm_l73; /* <tmp> */
    uint32_t gm_l74; /* i */
    uint32_t gm_l75; /* <tmp> */
    uint32_t gm_l76; /* <tmp> */
    uint32_t gm_l77; /* i */
    gm_ttovhfee8mf28 gm_l78; /* info */
    gm_t86h6hhm6d76n gm_l79; /* seed */
    uint64_t gm_l80; /* h */
    GM_STACK_CHECK(&F, 11274u);
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
    gm_tabb710d9mq2e gm_l81;
    gm_t53at80pfdus3 gm_l82;
    uint64_t gm_l83;
    int64_t gm_l84;
    gm_tg5lql6itufk7 gm_l85;
    gm_ttovhfee8mf28 gm_l86;
    __int128 gm_l87;
    gm_t57euh612ligr gm_l88;
    gm_tcmkompbl09il gm_l89;
    gm_t57euh612ligr gm_l90;
    gm_tcmkompbl09il gm_l91;
    gm_tg5lql6itufk7 gm_l92;
    gm_ttovhfee8mf28 gm_l93;
    gm_t3l8idb2m6c1r gm_l94;
    uint8_t gm_l95;
    unsigned __int128 gm_l96;
    gm_t53at80pfdus3 gm_l97;
    double gm_l98;
    gm_tg5lql6itufk7 gm_l99;
    gm_ttovhfee8mf28 gm_l100;
    gm_tabb710d9mq2e gm_l101;
    uint64_t gm_l102;
    uint64_t gm_l103;
    gm_t53at80pfdus3 gm_l104;
    gm_tg5lql6itufk7 gm_l105;
    gm_ttovhfee8mf28 gm_l106;
    uint8_t gm_l107;
    gm_tcfa6l7reoosn gm_l108;
    gm_tg5lql6itufk7 gm_l109;
    gm_ttovhfee8mf28 gm_l110;
    gm_tg5lql6itufk7 gm_l111;
    uint8_t gm_l112;
    gm_t53at80pfdus3 gm_l113;
    uint8_t gm_l114;
    uint8_t gm_l115;
    uint8_t gm_l116;
    uint8_t gm_l117;
    gm_t53at80pfdus3 gm_l118;
    gm_tg5lql6itufk7 gm_l119;
    gm_ttovhfee8mf28 gm_l120;
    gm_tg5lql6itufk7 gm_l121;
    gm_ttovhfee8mf28 gm_l122;
    uint8_t gm_l123;
    unsigned __int128 gm_l124;
    gm_tg5lql6itufk7 gm_l125;
    gm_ttovhfee8mf28 gm_l126;
    unsigned __int128 gm_l127;
    gm_tabb710d9mq2e gm_l128;
    gm_tg5lql6itufk7 gm_l129;
    gm_ttovhfee8mf28 gm_l130;
    gm_tcfa6l7reoosn gm_l131;
    gm_tabb710d9mq2e gm_l132;
    {
        {
            {
                {
                    {
                        {
                            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t135 = (uint8_t)(t134 == ((uint8_t)0ULL));
                            if (!t135) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t137 = (uint8_t)(t136 == ((uint8_t)1ULL));
                            if (!t137) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t139 = (uint8_t)(t138 == ((uint8_t)2ULL));
                            if (!t139) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t141 = (uint8_t)(t140 == ((uint8_t)3ULL));
                            if (!t141) {
                                goto gm_lb7;
                            }
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t143 = (uint8_t)(t142 == ((uint8_t)4ULL));
                            if (!t143) {
                                goto gm_lb8;
                            }
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t145 = (uint8_t)(t144 == ((uint8_t)5ULL));
                            if (!t145) {
                                goto gm_lb9;
                            }
                            goto gm_lb3;
                        }
gm_lb9:;
                        {
                            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t147 = (uint8_t)(t146 == ((uint8_t)6ULL));
                            if (!t147) {
                                goto gm_lb10;
                            }
                            goto gm_lb3;
                        }
gm_lb10:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t148 = gm_l0;
                    gm_tl874t2atnlh3 t149 = gm_l1;
                    gm_tvdg94bk7aa22 t150 = gm_l2;
                    gm_t88rcucg1b7uk t151 = gm_l4;
                    gm_t88rcucg1b7uk t152 = gm_l3;
                    void * t153 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t154, gm_srcl8pn2pjtmj6k, 192, 107);
                    gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)t153) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
                    gm_b16_8 t157 = gm_l7;
                    gm_f_convert_is6an2k((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t148, t149, t150, t151, t152, t156, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t159 = (uint8_t)(t158 == ((uint8_t)7ULL));
                    if (!t159) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t160 = gm_l7;
                    gm_f_fill_abmf5g5(t160, ((uint8_t)0ULL));
                    uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    void * t162 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t163)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t163, gm_srcl8pn2pjtmj6k, 195, 30);
                    uint64_t t164 = GM_LD(uint64_t, ((((uint8_t *)t162) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    uint64_t t165 = (t161 < t164 ? t161 : t164);
                    gm_l8 = t165;
                    void * t166 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t168 = gm_l8;
                    uint64_t t169 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t170 = (uintptr_t)t168;
                    if (t169 > t170 || t170 > (uintptr_t)t167) gm_trap_slice(t169, t170, t167, gm_srcl8pn2pjtmj6k, 196, 13);
                    uint64_t t171 = ((uintptr_t)t168) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t172;
                    GM_ST(void *, (uint8_t *)&t172, (void *)(((uint8_t *)t166 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(t171));
                    void * t173 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t174)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t174, gm_srcl8pn2pjtmj6k, 196, 33);
                    void * t175 = GM_LD(void *, (((uint8_t *)t173) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t176 = GM_LD(uint64_t, ((((uint8_t *)t173) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    uint64_t t177 = gm_l8;
                    uint64_t t178 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t179 = (uintptr_t)t177;
                    if (t178 > t179 || t179 > (uintptr_t)t176) gm_trap_slice(t178, t179, t176, gm_srcl8pn2pjtmj6k, 196, 33);
                    uint64_t t180 = ((uintptr_t)t177) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t181;
                    GM_ST(void *, (uint8_t *)&t181, (void *)(((uint8_t *)t175 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(t180));
                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                    gm_f_copy_from_f7j6p05(t172, t182);
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)8ULL));
                    if (!t184) {
                        goto gm_lb12;
                    }
                    void * t185 = gm_l0;
                    void * t186 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t187)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t187, gm_srcl8pn2pjtmj6k, 199, 30);
                    gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)t186) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                    uint64_t t190 = gm_f_ptr_of_b77b4c9(t185, t189);
                    gm_l9 = t190;
                    void * t191 = gm_l0;
                    gm_t88rcucg1b7uk t192 = gm_l3;
                    uint64_t t193 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t191, t192);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t193;
                    {
                        {
                            uint64_t t194 = gm_l9;
                            uint8_t t195 = (!(t194 == ((uint64_t)0ULL)));
                            if (!t195) {
                                goto gm_lb14;
                            }
                            uint64_t t196 = gm_l10;
                            uint8_t t197 = (t196 > ((uint64_t)1ULL));
                            if (!t197) {
                                goto gm_lb14;
                            }
                            uint64_t t198 = gm_l9;
                            uint64_t t199 = gm_l10;
                            if (t199 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 201, 45);
                            uint64_t t200 = (uint64_t)(t198 % t199);
                            uint8_t t201 = (!(t200 == ((uint64_t)0ULL)));
                            if (!t201) {
                                goto gm_lb14;
                            }
                            void * t202 = gm_l0;
                            gm_t88rcucg1b7uk t203 = gm_l4;
                            uint64_t t204 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t202, t203);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t205 = gm_l10;
                            uint8_t t206 = (t204 < t205);
                            if (!t206) {
                                goto gm_lb14;
                            }
                            void * t207 = gm_l0;
                            uint8_t t208 = gm_f_fast_23em7sv(t207);
                            uint8_t t209 = (uint8_t)!t208;
                            if (!t209) {
                                goto gm_lb14;
                            }
                            void * t210 = gm_l0;
                            gm_tl874t2atnlh3 t211 = gm_l1;
                            uint64_t t212 = gm_l9;
                            uint64_t t213 = gm_l10;
                            gm_t2lqu8jsj511d t214;
                            GM_ST(uint64_t, ((uint8_t *)&t214 + 0), t212);
                            GM_ST(uint64_t, ((uint8_t *)&t214 + 8), t213);
                            gm_f_format_l7c15sv((void *)&gm_l81, (void *)NULL, cx, NULL, t214);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                            if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_srcl8pn2pjtmj6k, 202, 38);
                            gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                            gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
                            gm_t76123gfet73g t218;
                            gm_f_trap_qh84l9d((void *)&t218, (void *)NULL, cx, NULL, t210, t211, t217);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tcfa6l7reoosn t219;
                            GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                            ((uint8_t *)&t219)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t219 + 8), t218);
                            memmove(gm_out, ((uint8_t *)&t219), 32);
                            goto gm_fdone;
                        }
gm_lb14:;
                    }
gm_lb13:;
                    gm_b16_8 t220 = gm_l7;
                    void * t221 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t222)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t222, gm_srcl8pn2pjtmj6k, 204, 27);
                    gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)t221) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&t223));
                    gm_f_copy_from_f7j6p05(t220, t224);
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    {
                        {
                            uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t226 = (uint8_t)(t225 == ((uint8_t)9ULL));
                            if (!t226) {
                                goto gm_lb17;
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        {
                            uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t228 = (uint8_t)(t227 == ((uint8_t)13ULL));
                            if (!t228) {
                                goto gm_lb18;
                            }
                            goto gm_lb16;
                        }
gm_lb18:;
                        {
                            uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t230 = (uint8_t)(t229 == ((uint8_t)35ULL));
                            if (!t230) {
                                goto gm_lb19;
                            }
                            goto gm_lb16;
                        }
gm_lb19:;
                        goto gm_lb15;
                    }
gm_lb16:;
                    gm_b16_8 t231 = gm_l7;
                    void * t232 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t233)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t233, gm_srcl8pn2pjtmj6k, 206, 51);
                    gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)t232) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                    gm_f_copy_from_f7j6p05(t231, t235);
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    {
                        {
                            uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t237 = (uint8_t)(t236 == ((uint8_t)10ULL));
                            if (!t237) {
                                goto gm_lb22;
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        {
                            uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t239 = (uint8_t)(t238 == ((uint8_t)11ULL));
                            if (!t239) {
                                goto gm_lb23;
                            }
                            goto gm_lb21;
                        }
gm_lb23:;
                        {
                            uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t241 = (uint8_t)(t240 == ((uint8_t)12ULL));
                            if (!t241) {
                                goto gm_lb24;
                            }
                            goto gm_lb21;
                        }
gm_lb24:;
                        goto gm_lb20;
                    }
gm_lb21:;
                    {
                        {
                            void * t242 = gm_l0;
                            gm_t88rcucg1b7uk t243 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l82, t242, GM_CONV(gm_csguankko4lmhd, t243));
                            uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l82) + 0));
                            uint8_t t245 = (uint8_t)(t244 == ((uint8_t)9ULL));
                            if (!t245) {
                                goto gm_lb26;
                            }
                            gm_t88rcucg1b7uk t246 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l82) + 8) + 4));
                            gm_l11 = t246;
                            void * t247 = gm_l0;
                            gm_t88rcucg1b7uk t248 = gm_l11;
                            uint64_t t249 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t247, t248);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            __int128 t250 = (__int128)t249;
                            gm_l12 = t250;
                            void * t251 = gm_l0;
                            void * t252 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t253)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t253, gm_srcl8pn2pjtmj6k, 210, 32);
                            gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)t252) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                            uint64_t t256 = gm_f_ptr_of_b77b4c9(t251, t255);
                            __int128 t257 = (__int128)t256;
                            gm_l13 = t257;
                            void * t258 = gm_l0;
                            uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)t258) + 176));
                            uint64_t r260; bool o261 = __builtin_mul_overflow(t259, ((uint64_t)8ULL), &r260);
                            if (o261) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 211, 22);
                            uint64_t t262 = r260;
                            gm_l14 = t262;
                            uint64_t t263 = gm_l14;
                            uint8_t t264 = (t263 >= ((uint64_t)64ULL));
                            if (t264) {
                                uint64_t t265 = (uint64_t)~((uint64_t)0ULL);
                                gm_l83 = t265;
                            } else {
                                uint64_t t266 = gm_l14;
                                unsigned __int128 n267 = (unsigned __int128)t266;
                                if (n267 >= 64) gm_trap_shift((uint64_t)n267, "u64", gm_srcl8pn2pjtmj6k, 212, 60);
                                uint64_t r268 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n267);
                                bool o269 = (uint64_t)(r268 >> (int)n267) != ((uint64_t)1ULL);
                                if (o269) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 212, 60);
                                uint64_t t270 = r268;
                                uint64_t r271; bool o272 = __builtin_sub_overflow(t270, ((uint64_t)1ULL), &r271);
                                if (o272) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 212, 59);
                                uint64_t t273 = r271;
                                gm_l83 = t273;
                            }
                            uint64_t t274 = gm_l83;
                            gm_l15 = t274;
                            gm_tvdg94bk7aa22 t275 = gm_l2;
                            uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&t275) + 0));
                            uint8_t t277 = (uint8_t)((t276 == ((uint8_t)12ULL)));
                            gm_tvdg94bk7aa22 t278;
                            GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t278 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)12ULL));
                            if (t277) {
                                __int128 t279 = gm_l13;
                                void * t280 = gm_l0;
                                void * t281 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t282)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t282, gm_srcl8pn2pjtmj6k, 214, 41);
                                gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)t281) + (uintptr_t)((uint64_t)1ULL) * 16));
                                gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
                                uint64_t t285 = gm_f_ptr_of_b77b4c9(t280, t284);
                                __int128 t286 = (__int128)t285;
                                __int128 r287; bool o288 = __builtin_sub_overflow(t279, t286, &r287);
                                if (o288) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 214, 23);
                                __int128 t289 = r287;
                                int64_t t290 = (int64_t)t289;
                                gm_l16 = t290;
                                __int128 t291 = gm_l12;
                                uint8_t t292 = ((t291 == ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                                if (t292) {
                                    gm_l84 = ((int64_t)0LL);
                                } else {
                                    int64_t t293 = gm_l16;
                                    __int128 t294 = gm_l12;
                                    int64_t t295 = (int64_t)t294;
                                    if (t295 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 215, 54);
                                    if (t293 == ((int64_t)(-9223372036854775807LL - 1)) && t295 == (int64_t)-1) gm_trap("integer overflow in `/`", gm_srcl8pn2pjtmj6k, 215, 54);
                                    int64_t t296 = (int64_t)(t293 / t295);
                                    gm_l84 = t296;
                                }
                                int64_t t297 = gm_l84;
                                gm_l17 = t297;
                                void * t298 = gm_l0;
                                gm_b16_8 t299 = gm_l7;
                                int64_t t300 = gm_l17;
                                unsigned __int128 t301 = (unsigned __int128)t300;
                                gm_f_put_uint_cddin99(t298, t299, t301);
                                gm_tcfa6l7reoosn t302;
                                GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t302 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t302 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t302 + 24, 0);
                                ((uint8_t *)&t302)[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&t302 + 8), GM_ZERO(gm_b0_1));
                                memmove(gm_out, ((uint8_t *)&t302), 32);
                                goto gm_fdone;
                            }
                            void * t303 = gm_l0;
                            gm_t88rcucg1b7uk t304 = gm_l5;
                            gm_tg5lql6itufk7 t305 = gm_f_int_info_ulcevdv(t303, GM_CONV(gm_csguankko4lmhd, t304));
                            gm_l85 = t305;
                            uint8_t t306 = (uint8_t)((((uint8_t *)&gm_l85))[8] != 0);
                            if (t306) {
                                gm_ttovhfee8mf28 t307 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l85));
                                gm_l86 = t307;
                            } else {
                                void * t308 = gm_l0;
                                gm_tl874t2atnlh3 t309 = gm_l1;
                                gm_b16_8 t310;
                                GM_ST(void *, (uint8_t *)&t310, (void *)(gm_s064htvgs8b59));
                                GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(68));
                                gm_t76123gfet73g t311;
                                gm_f_trap_qh84l9d((void *)&t311, (void *)NULL, cx, NULL, t308, t309, t310);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tcfa6l7reoosn t312;
                                GM_ST(uint8_t, (uint8_t *)&t312 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t312 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t312 + 4, 0);
                                ((uint8_t *)&t312)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t312 + 8), t311);
                                memmove(gm_out, ((uint8_t *)&t312), 32);
                                goto gm_fdone;
                            }
                            gm_ttovhfee8mf28 t313 = gm_l86;
                            gm_l18 = t313;
                            void * t314 = gm_l0;
                            gm_t88rcucg1b7uk t315 = gm_l5;
                            void * t316 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t317)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t317, gm_srcl8pn2pjtmj6k, 220, 34);
                            gm_b16_8 t318 = GM_LD(gm_b16_8, (((uint8_t *)t316) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t319 = GM_LD(gm_b16_8, ((uint8_t *)&t318));
                            unsigned __int128 t320 = gm_f_get_int_pg7p4gc(t314, GM_CONV(gm_csguankko4lmhd, t315), t319);
                            gm_l19 = t320;
                            gm_l20 = ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                            uint8_t t321 = GM_LD(uint8_t, ((uint8_t *)&gm_l18));
                            if (t321) {
                                unsigned __int128 t322 = gm_l19;
                                __int128 t323;
                                memset(&t323, 0, sizeof t323); memcpy(&t323, ((uint8_t *)&t322), sizeof t323 < 16 ? sizeof t323 : 16);
                                gm_l20 = t323;
                            } else {
                                unsigned __int128 t324 = gm_l19;
                                unsigned __int128 t325 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                                unsigned __int128 n326 = (unsigned __int128)((uint64_t)1ULL);
                                if (n326 >= 128) gm_trap_shift((uint64_t)n326, "u128", gm_srcl8pn2pjtmj6k, 224, 31);
                                unsigned __int128 t327 = (unsigned __int128)(t325 >> (int)n326);
                                uint8_t t328 = (t324 <= t327);
                                if (t328) {
                                    unsigned __int128 t329 = gm_l19;
                                    __int128 t330;
                                    memset(&t330, 0, sizeof t330); memcpy(&t330, ((uint8_t *)&t329), sizeof t330 < 16 ? sizeof t330 : 16);
                                    gm_l20 = t330;
                                }
                            }
                            __int128 t331 = gm_l20;
                            __int128 t332 = gm_l12;
                            __int128 r333; bool o334 = __builtin_mul_overflow(t331, t332, &r333);
                            __int128 t335 = r333;
                            gm_l21 = t335;
                            gm_tvdg94bk7aa22 t336 = gm_l2;
                            uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&t336) + 0));
                            uint8_t t338 = (uint8_t)((t337 == ((uint8_t)10ULL)));
                            gm_tvdg94bk7aa22 t339;
                            GM_ST(uint8_t, (uint8_t *)&t339 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t339 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t339 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t339 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t339 + 0), ((uint8_t)10ULL));
                            if (t338) {
                                __int128 t340 = gm_l13;
                                __int128 t341 = gm_l21;
                                __int128 r342; bool o343 = __builtin_add_overflow(t340, t341, &r342);
                                __int128 t344 = r342;
                                gm_l87 = t344;
                            } else {
                                __int128 t345 = gm_l13;
                                __int128 t346 = gm_l21;
                                __int128 r347; bool o348 = __builtin_sub_overflow(t345, t346, &r347);
                                __int128 t349 = r347;
                                gm_l87 = t349;
                            }
                            __int128 t350 = gm_l87;
                            gm_l22 = t350;
                            void * t351 = gm_l0;
                            gm_b16_8 t352 = gm_l7;
                            __int128 t353 = gm_l22;
                            uint64_t t354 = (uint64_t)t353;
                            uint64_t t355 = gm_l15;
                            uint64_t t356 = (uint64_t)(t354 & t355);
                            unsigned __int128 t357 = (unsigned __int128)t356;
                            gm_f_put_uint_cddin99(t351, t352, t357);
                            goto gm_lb25;
                        }
gm_lb26:;
                        void * t358 = gm_l0;
                        gm_tl874t2atnlh3 t359 = gm_l1;
                        gm_b16_8 t360;
                        GM_ST(void *, (uint8_t *)&t360, (void *)(gm_sge745dj3cnrb));
                        GM_ST(uintptr_t, (uint8_t *)&t360 + 8, (uintptr_t)(72));
                        gm_t76123gfet73g t361;
                        gm_f_trap_qh84l9d((void *)&t361, (void *)NULL, cx, NULL, t358, t359, t360);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t362;
                        GM_ST(uint8_t, (uint8_t *)&t362 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t362 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t362 + 4, 0);
                        ((uint8_t *)&t362)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t362 + 8), t361);
                        memmove(gm_out, ((uint8_t *)&t362), 32);
                        goto gm_fdone;
                    }
gm_lb25:;
                    goto gm_lb1;
                }
gm_lb20:;
            }
            {
                {
                    uint8_t t363 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t364 = (uint8_t)(t363 == ((uint8_t)14ULL));
                    if (!t364) {
                        goto gm_lb27;
                    }
                    void * t365 = gm_l0;
                    gm_t88rcucg1b7uk t366 = gm_l4;
                    gm_f_union_def_h8mvf4t((void *)&gm_l88, t365, GM_CONV(gm_csguankko4lmhd, t366));
                    uint8_t t367 = (uint8_t)((((uint8_t *)&gm_l88))[24] != 0);
                    if (t367) {
                        memmove(((uint8_t *)&gm_l89), ((uint8_t *)&gm_l88), 24);
                    } else {
                        void * t368 = gm_l0;
                        gm_tl874t2atnlh3 t369 = gm_l1;
                        gm_b16_8 t370;
                        GM_ST(void *, (uint8_t *)&t370, (void *)(gm_sb9352gc5i144));
                        GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(20));
                        gm_t76123gfet73g t371;
                        gm_f_trap_qh84l9d((void *)&t371, (void *)NULL, cx, NULL, t368, t369, t370);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t372;
                        GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
                        ((uint8_t *)&t372)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t372 + 8), t371);
                        memmove(gm_out, ((uint8_t *)&t372), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l23), ((uint8_t *)&gm_l89), 24);
                    void * t373 = gm_l0;
                    gm_t88rcucg1b7uk t374 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l23));
                    uint64_t t375 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t373, t374);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t376 = gm_f_us_cuv01fp(t375);
                    gm_l24 = t376;
                    gm_b16_8 t377 = gm_l7;
                    gm_f_fill_abmf5g5(t377, ((uint8_t)0ULL));
                    void * t378 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t380 = gm_l24;
                    uint64_t t381 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t382 = (uintptr_t)t380;
                    if (t381 > t382 || t382 > (uintptr_t)t379) gm_trap_slice(t381, t382, t379, gm_srcl8pn2pjtmj6k, 235, 13);
                    uint64_t t383 = ((uintptr_t)t380) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t384;
                    GM_ST(void *, (uint8_t *)&t384, (void *)(((uint8_t *)t378 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(t383));
                    void * t385 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t386 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t386)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t386, gm_srcl8pn2pjtmj6k, 235, 33);
                    void * t387 = GM_LD(void *, (((uint8_t *)t385) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t388 = GM_LD(uint64_t, ((((uint8_t *)t385) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    uint64_t t389 = gm_l24;
                    uint64_t t390 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t391 = (uintptr_t)t389;
                    if (t390 > t391 || t391 > (uintptr_t)t388) gm_trap_slice(t390, t391, t388, gm_srcl8pn2pjtmj6k, 235, 33);
                    uint64_t t392 = ((uintptr_t)t389) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t393;
                    GM_ST(void *, (uint8_t *)&t393, (void *)(((uint8_t *)t387 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t393 + 8, (uintptr_t)(t392));
                    gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                    gm_f_copy_from_f7j6p05(t384, t394);
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t396 = (uint8_t)(t395 == ((uint8_t)15ULL));
                    if (!t396) {
                        goto gm_lb28;
                    }
                    void * t397 = gm_l0;
                    gm_t88rcucg1b7uk t398 = gm_l3;
                    gm_t88rcucg1b7uk t399 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t397, GM_CONV(gm_csguankko4lmhd, t398)));
                    gm_l25 = t399;
                    void * t400 = gm_l0;
                    gm_t88rcucg1b7uk t401 = gm_l25;
                    gm_f_union_def_h8mvf4t((void *)&gm_l90, t400, GM_CONV(gm_csguankko4lmhd, t401));
                    uint8_t t402 = (uint8_t)((((uint8_t *)&gm_l90))[24] != 0);
                    if (t402) {
                        memmove(((uint8_t *)&gm_l91), ((uint8_t *)&gm_l90), 24);
                    } else {
                        void * t403 = gm_l0;
                        gm_tl874t2atnlh3 t404 = gm_l1;
                        gm_b16_8 t405;
                        GM_ST(void *, (uint8_t *)&t405, (void *)(gm_stk678hojgu6q));
                        GM_ST(uintptr_t, (uint8_t *)&t405 + 8, (uintptr_t)(23));
                        gm_t76123gfet73g t406;
                        gm_f_trap_qh84l9d((void *)&t406, (void *)NULL, cx, NULL, t403, t404, t405);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t407;
                        GM_ST(uint8_t, (uint8_t *)&t407 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t407 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t407 + 4, 0);
                        ((uint8_t *)&t407)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t407 + 8), t406);
                        memmove(gm_out, ((uint8_t *)&t407), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l91), 24);
                    void * t408 = gm_l0;
                    gm_t88rcucg1b7uk t409 = gm_l4;
                    gm_tg5lql6itufk7 t410 = gm_f_int_info_ulcevdv(t408, GM_CONV(gm_csguankko4lmhd, t409));
                    gm_l92 = t410;
                    uint8_t t411 = (uint8_t)((((uint8_t *)&gm_l92))[8] != 0);
                    if (t411) {
                        gm_ttovhfee8mf28 t412 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l92));
                        gm_l93 = t412;
                    } else {
                        void * t413 = gm_l0;
                        gm_tl874t2atnlh3 t414 = gm_l1;
                        gm_b16_8 t415;
                        GM_ST(void *, (uint8_t *)&t415, (void *)(gm_s3ergh2il1tjf));
                        GM_ST(uintptr_t, (uint8_t *)&t415 + 8, (uintptr_t)(42));
                        gm_t76123gfet73g t416;
                        gm_f_trap_qh84l9d((void *)&t416, (void *)NULL, cx, NULL, t413, t414, t415);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t417;
                        GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                        ((uint8_t *)&t417)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t417 + 8), t416);
                        memmove(gm_out, ((uint8_t *)&t417), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t418 = gm_l93;
                    gm_l27 = t418;
                    uint8_t t419 = GM_LD(uint8_t, ((uint8_t *)&gm_l27));
                    void * t420 = gm_l0;
                    gm_t88rcucg1b7uk t421 = gm_l4;
                    void * t422 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t423 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t423)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t423, gm_srcl8pn2pjtmj6k, 241, 53);
                    gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)t422) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t425 = GM_LD(gm_b16_8, ((uint8_t *)&t424));
                    unsigned __int128 t426 = gm_f_get_int_pg7p4gc(t420, GM_CONV(gm_csguankko4lmhd, t421), t425);
                    gm_f_num_val_i3upeur((void *)&gm_l28, t419, t426);
                    gm_b16_8 t427 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                    gm_l29 = t428;
                    gm_l30 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t429 = gm_l30;
                            uint64_t t430 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                            uint8_t t431 = (t429 < t430);
                            if (!t431) {
                                goto gm_lb29;
                            }
                            {
                                uint64_t t432 = gm_l30;
                                void * t433 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                uint64_t t434 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                uint64_t t435 = gm_l30;
                                gm_tckdobunaulee t436 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t433) + (uintptr_t)t435 * 72));
                                gm_t3l8idb2m6c1r t437;
                                GM_ST(uint64_t, ((uint8_t *)&t437 + 0), t432);
                                GM_ST(gm_tckdobunaulee, ((uint8_t *)&t437 + 8), t436);
                                gm_l94 = t437;
                                {
                                    {
                                        uint64_t t438 = GM_LD(uint64_t, ((uint8_t *)&gm_l94));
                                        gm_l31 = t438;
                                        memmove(((uint8_t *)&F.l32), (((uint8_t *)&gm_l94) + 8), 72);
                                        {
                                            {
                                                {
                                                    {
                                                        uint8_t t439 = (uint8_t)(((((uint8_t *)&F.l32) + 16))[4] != 0);
                                                        if (t439) {
                                                            goto gm_lb37;
                                                        }
                                                        gm_l95 = ((uint8_t)1);
                                                        goto gm_lb36;
                                                    }
gm_lb37:;
                                                    {
                                                        gm_l95 = ((uint8_t)0);
                                                        goto gm_lb36;
                                                    }
gm_lb38:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 243, 20);
                                                }
gm_lb36:;
                                                uint8_t t440 = gm_l95;
                                                if (!t440) {
                                                    goto gm_lb35;
                                                }
                                                uint8_t t441 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 24));
                                                uint8_t t442 = GM_LD(uint8_t, ((uint8_t *)&gm_l28));
                                                uint8_t t443 = ((t441 == t442));
                                                if (!t443) {
                                                    goto gm_lb35;
                                                }
                                                unsigned __int128 t444 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l32) + 24)));
                                                gm_l33 = t444;
                                                uint8_t t445 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 24));
                                                if (t445) {
                                                    unsigned __int128 t446 = gm_l33;
                                                    unsigned __int128 r447; bool o448 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t446, &r447);
                                                    unsigned __int128 t449 = r447;
                                                    gm_l96 = t449;
                                                } else {
                                                    unsigned __int128 t450 = gm_l33;
                                                    gm_l96 = t450;
                                                }
                                                unsigned __int128 t451 = gm_l96;
                                                gm_l34 = t451;
                                                unsigned __int128 t452 = gm_l34;
                                                unsigned __int128 t453 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l28) + 16));
                                                uint8_t t454 = ((t452 == t453));
                                                if (t454) {
                                                    void * t455 = gm_l0;
                                                    gm_t2lqu8jsj511d t456 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t455));
                                                    gm_l35 = t456;
                                                    void * t457 = gm_l0;
                                                    void * t458 = gm_l0;
                                                    gm_t88rcucg1b7uk t459 = gm_l25;
                                                    uint64_t t460 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t458, t459);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b16_8 t461 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t457, t460);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l36 = t461;
                                                    void * t462 = gm_l0;
                                                    gm_t88rcucg1b7uk t463 = gm_l25;
                                                    gm_b16_8 t464 = gm_l36;
                                                    uint64_t t465 = gm_l31;
                                                    uint32_t t466 = (uint32_t)t465;
                                                    gm_b16_8 t467 = gm_f_empty_slice_ijtp9lo();
                                                    gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
                                                    gm_b0_1 t469 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t462, t463, t464, t466, t468);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t470 = gm_l0;
                                                    gm_t88rcucg1b7uk t471 = gm_l3;
                                                    gm_b16_8 t472 = gm_l7;
                                                    gm_b16_8 t473 = gm_l36;
                                                    gm_b16_8 t474 = GM_LD(gm_b16_8, ((uint8_t *)&t473));
                                                    gm_b0_1 t475 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t470, t471, t472, t474);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t476 = gm_l0;
                                                    gm_t2lqu8jsj511d t477 = gm_l35;
                                                    gm_f_release_jn5kqdi(t476, GM_CONV(gm_csc7lc6jlkqkuq, t477));
                                                    gm_tcfa6l7reoosn t478;
                                                    GM_ST(uint8_t, (uint8_t *)&t478 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t478 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t478 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t478 + 8, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t478 + 16, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t478 + 24, 0);
                                                    ((uint8_t *)&t478)[0] = 0;
                                                    GM_ST(gm_b0_1, ((uint8_t *)&t478 + 8), GM_ZERO(gm_b0_1));
                                                    memmove(gm_out, ((uint8_t *)&t478), 32);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb34;
                                            }
gm_lb35:;
                                        }
gm_lb34:;
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 242, 13);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t479 = gm_l30;
                            uint64_t r480; bool o481 = __builtin_add_overflow(t479, ((uint64_t)1ULL), &r480);
                            uint64_t t482 = r480;
                            gm_l30 = t482;
                        }
                    }
gm_lb29:;
                    gm_b16_8 t483 = gm_l7;
                    gm_f_fill_abmf5g5(t483, ((uint8_t)0ULL));
                    goto gm_lb1;
                }
gm_lb28:;
            }
            {
                {
                    {
                        {
                            uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t485 = (uint8_t)(t484 == ((uint8_t)16ULL));
                            if (!t485) {
                                goto gm_lb41;
                            }
                            goto gm_lb40;
                        }
gm_lb41:;
                        {
                            uint8_t t486 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t487 = (uint8_t)(t486 == ((uint8_t)17ULL));
                            if (!t487) {
                                goto gm_lb42;
                            }
                            goto gm_lb40;
                        }
gm_lb42:;
                        {
                            uint8_t t488 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t489 = (uint8_t)(t488 == ((uint8_t)18ULL));
                            if (!t489) {
                                goto gm_lb43;
                            }
                            goto gm_lb40;
                        }
gm_lb43:;
                        goto gm_lb39;
                    }
gm_lb40:;
                    {
                        {
                            void * t490 = gm_l0;
                            gm_t88rcucg1b7uk t491 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l97, t490, GM_CONV(gm_csguankko4lmhd, t491));
                            uint8_t t492 = GM_LD(uint8_t, (((uint8_t *)&gm_l97) + 0));
                            uint8_t t493 = (uint8_t)(t492 == ((uint8_t)3ULL));
                            if (!t493) {
                                goto gm_lb45;
                            }
                            void * t494 = gm_l0;
                            gm_t88rcucg1b7uk t495 = gm_l4;
                            void * t496 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t497 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t497)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t497, gm_srcl8pn2pjtmj6k, 260, 38);
                            gm_b16_8 t498 = GM_LD(gm_b16_8, (((uint8_t *)t496) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t499 = GM_LD(gm_b16_8, ((uint8_t *)&t498));
                            double t500 = gm_f_get_float_grmhlm4(t494, GM_CONV(gm_csguankko4lmhd, t495), t499);
                            gm_l37 = t500;
                            void * t501 = gm_l0;
                            gm_t88rcucg1b7uk t502 = gm_l4;
                            void * t503 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t504 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t504)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t504, gm_srcl8pn2pjtmj6k, 261, 38);
                            gm_b16_8 t505 = GM_LD(gm_b16_8, (((uint8_t *)t503) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t506 = GM_LD(gm_b16_8, ((uint8_t *)&t505));
                            double t507 = gm_f_get_float_grmhlm4(t501, GM_CONV(gm_csguankko4lmhd, t502), t506);
                            gm_l38 = t507;
                            {
                                {
                                    uint8_t t508 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    uint8_t t509 = (uint8_t)(t508 == ((uint8_t)16ULL));
                                    if (!t509) {
                                        goto gm_lb47;
                                    }
                                    double t510 = gm_l37;
                                    double t511 = gm_l38;
                                    double t512 = gm_f_num_fmin_1k5uqp0(t510, t511);
                                    gm_l98 = t512;
                                    goto gm_lb46;
                                }
gm_lb47:;
                                {
                                    uint8_t t513 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    uint8_t t514 = (uint8_t)(t513 == ((uint8_t)17ULL));
                                    if (!t514) {
                                        goto gm_lb48;
                                    }
                                    double t515 = gm_l37;
                                    double t516 = gm_l38;
                                    double t517 = gm_f_num_fmax_0qau2jp(t515, t516);
                                    gm_l98 = t517;
                                    goto gm_lb46;
                                }
gm_lb48:;
                                {
                                    double t518 = gm_l37;
                                    double t519 = gm_l38;
                                    double t520 = gm_f_num_fmax_0qau2jp(t518, t519);
                                    void * t521 = gm_l0;
                                    gm_t88rcucg1b7uk t522 = gm_l4;
                                    void * t523 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t524 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t524)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t524, gm_srcl8pn2pjtmj6k, 265, 67);
                                    gm_b16_8 t525 = GM_LD(gm_b16_8, (((uint8_t *)t523) + (uintptr_t)((uint64_t)2ULL) * 16));
                                    gm_b16_8 t526 = GM_LD(gm_b16_8, ((uint8_t *)&t525));
                                    double t527 = gm_f_get_float_grmhlm4(t521, GM_CONV(gm_csguankko4lmhd, t522), t526);
                                    double t528 = gm_f_num_fmin_1k5uqp0(t520, t527);
                                    gm_l98 = t528;
                                    goto gm_lb46;
                                }
gm_lb49:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 262, 22);
                            }
gm_lb46:;
                            double t529 = gm_l98;
                            gm_l39 = t529;
                            void * t530 = gm_l0;
                            gm_t88rcucg1b7uk t531 = gm_l4;
                            double t532 = gm_l39;
                            gm_b16_8 t533 = gm_l7;
                            gm_f_put_float_fiad72d(t530, GM_CONV(gm_csguankko4lmhd, t531), t532, t533);
                            gm_tcfa6l7reoosn t534;
                            GM_ST(uint8_t, (uint8_t *)&t534 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t534 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t534 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t534 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t534 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t534 + 24, 0);
                            ((uint8_t *)&t534)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t534 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t534), 32);
                            goto gm_fdone;
                        }
gm_lb45:;
                    }
gm_lb44:;
                    void * t535 = gm_l0;
                    gm_t88rcucg1b7uk t536 = gm_l4;
                    gm_tg5lql6itufk7 t537 = gm_f_int_info_ulcevdv(t535, GM_CONV(gm_csguankko4lmhd, t536));
                    gm_l99 = t537;
                    uint8_t t538 = (uint8_t)((((uint8_t *)&gm_l99))[8] != 0);
                    if (t538) {
                        gm_ttovhfee8mf28 t539 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l99));
                        gm_l100 = t539;
                    } else {
                        void * t540 = gm_l0;
                        gm_tl874t2atnlh3 t541 = gm_l1;
                        void * t542 = gm_l0;
                        void * t543 = GM_LD(void *, (((uint8_t *)t542) + 8));
                        gm_t88rcucg1b7uk t544 = gm_l4;
                        gm_b16_8 t545 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t543, t544);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t546;
                        GM_ST(gm_b16_8, ((uint8_t *)&t546 + 0), t545);
                        gm_f_format_kj15nqp((void *)&gm_l101, (void *)NULL, cx, NULL, t546);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t547 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                        if (!t547) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 8, gm_srcl8pn2pjtmj6k, 270, 62);
                        gm_b16_8 t548 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
                        gm_b16_8 t549 = GM_LD(gm_b16_8, ((uint8_t *)&t548));
                        gm_t76123gfet73g t550;
                        gm_f_trap_qh84l9d((void *)&t550, (void *)NULL, cx, NULL, t540, t541, t549);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t551;
                        GM_ST(uint8_t, (uint8_t *)&t551 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t551 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t551 + 4, 0);
                        ((uint8_t *)&t551)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t551 + 8), t550);
                        memmove(gm_out, ((uint8_t *)&t551), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t552 = gm_l100;
                    gm_l40 = t552;
                    void * t553 = gm_l0;
                    gm_t88rcucg1b7uk t554 = gm_l4;
                    void * t555 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t556 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t556)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t556, gm_srcl8pn2pjtmj6k, 271, 32);
                    gm_b16_8 t557 = GM_LD(gm_b16_8, (((uint8_t *)t555) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t558 = GM_LD(gm_b16_8, ((uint8_t *)&t557));
                    unsigned __int128 t559 = gm_f_get_int_pg7p4gc(t553, GM_CONV(gm_csguankko4lmhd, t554), t558);
                    gm_l41 = t559;
                    void * t560 = gm_l0;
                    gm_t88rcucg1b7uk t561 = gm_l4;
                    void * t562 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t563 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t563)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t563, gm_srcl8pn2pjtmj6k, 272, 32);
                    gm_b16_8 t564 = GM_LD(gm_b16_8, (((uint8_t *)t562) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t565 = GM_LD(gm_b16_8, ((uint8_t *)&t564));
                    unsigned __int128 t566 = gm_f_get_int_pg7p4gc(t560, GM_CONV(gm_csguankko4lmhd, t561), t565);
                    gm_l42 = t566;
                    gm_l43 = ((uint64_t)0ULL);
                    {
                        {
                            uint8_t t567 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t568 = (uint8_t)(t567 == ((uint8_t)16ULL));
                            if (!t568) {
                                goto gm_lb51;
                            }
                            uint8_t t569 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                            unsigned __int128 t570 = gm_l41;
                            unsigned __int128 t571 = gm_l42;
                            int32_t t572 = gm_f_int_cmp_kb3sm6u(t569, t570, t571);
                            uint8_t t573 = (t572 <= ((int32_t)0LL));
                            if (t573) {
                                gm_l102 = ((uint64_t)0ULL);
                            } else {
                                gm_l102 = ((uint64_t)1ULL);
                            }
                            uint64_t t574 = gm_l102;
                            gm_l43 = t574;
                            goto gm_lb50;
                        }
gm_lb51:;
                        {
                            uint8_t t575 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t576 = (uint8_t)(t575 == ((uint8_t)17ULL));
                            if (!t576) {
                                goto gm_lb52;
                            }
                            uint8_t t577 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                            unsigned __int128 t578 = gm_l41;
                            unsigned __int128 t579 = gm_l42;
                            int32_t t580 = gm_f_int_cmp_kb3sm6u(t577, t578, t579);
                            uint8_t t581 = (t580 >= ((int32_t)0LL));
                            if (t581) {
                                gm_l103 = ((uint64_t)0ULL);
                            } else {
                                gm_l103 = ((uint64_t)1ULL);
                            }
                            uint64_t t582 = gm_l103;
                            gm_l43 = t582;
                            goto gm_lb50;
                        }
gm_lb52:;
                        {
                            void * t583 = gm_l0;
                            gm_t88rcucg1b7uk t584 = gm_l4;
                            void * t585 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t586 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t586)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t586, gm_srcl8pn2pjtmj6k, 278, 41);
                            gm_b16_8 t587 = GM_LD(gm_b16_8, (((uint8_t *)t585) + (uintptr_t)((uint64_t)2ULL) * 16));
                            gm_b16_8 t588 = GM_LD(gm_b16_8, ((uint8_t *)&t587));
                            unsigned __int128 t589 = gm_f_get_int_pg7p4gc(t583, GM_CONV(gm_csguankko4lmhd, t584), t588);
                            gm_l44 = t589;
                            uint8_t t590 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                            unsigned __int128 t591 = gm_l41;
                            unsigned __int128 t592 = gm_l42;
                            int32_t t593 = gm_f_int_cmp_kb3sm6u(t590, t591, t592);
                            uint8_t t594 = (t593 < ((int32_t)0LL));
                            if (t594) {
                                gm_l43 = ((uint64_t)1ULL);
                            } else {
                                uint8_t t595 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                                unsigned __int128 t596 = gm_l41;
                                unsigned __int128 t597 = gm_l44;
                                int32_t t598 = gm_f_int_cmp_kb3sm6u(t595, t596, t597);
                                uint8_t t599 = (t598 > ((int32_t)0LL));
                                if (t599) {
                                    gm_l43 = ((uint64_t)2ULL);
                                }
                            }
                            goto gm_lb50;
                        }
gm_lb53:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 274, 13);
                    }
gm_lb50:;
                    gm_b16_8 t600 = gm_l7;
                    void * t601 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t602 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t603 = gm_l43;
                    if ((uintptr_t)t603 >= (uintptr_t)(t602)) gm_trap_index((uintptr_t)t603, t602, gm_srcl8pn2pjtmj6k, 286, 27);
                    gm_b16_8 t604 = GM_LD(gm_b16_8, (((uint8_t *)t601) + (uintptr_t)t603 * 16));
                    gm_b16_8 t605 = GM_LD(gm_b16_8, ((uint8_t *)&t604));
                    gm_f_copy_from_f7j6p05(t600, t605);
                    goto gm_lb1;
                }
gm_lb39:;
            }
            {
                {
                    uint8_t t606 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t607 = (uint8_t)(t606 == ((uint8_t)19ULL));
                    if (!t607) {
                        goto gm_lb54;
                    }
                    {
                        {
                            void * t608 = gm_l0;
                            gm_t88rcucg1b7uk t609 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l104, t608, GM_CONV(gm_csguankko4lmhd, t609));
                            uint8_t t610 = GM_LD(uint8_t, (((uint8_t *)&gm_l104) + 0));
                            uint8_t t611 = (uint8_t)(t610 == ((uint8_t)3ULL));
                            if (!t611) {
                                goto gm_lb56;
                            }
                            void * t612 = gm_l0;
                            gm_t88rcucg1b7uk t613 = gm_l4;
                            void * t614 = gm_l0;
                            gm_t88rcucg1b7uk t615 = gm_l4;
                            void * t616 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t617 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t617)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t617, gm_srcl8pn2pjtmj6k, 290, 50);
                            gm_b16_8 t618 = GM_LD(gm_b16_8, (((uint8_t *)t616) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t619 = GM_LD(gm_b16_8, ((uint8_t *)&t618));
                            double t620 = gm_f_get_float_grmhlm4(t614, GM_CONV(gm_csguankko4lmhd, t615), t619);
                            uint64_t t621;
                            memset(&t621, 0, sizeof t621); memcpy(&t621, ((uint8_t *)&t620), sizeof t621 < 8 ? sizeof t621 : 8);
                            uint64_t t622 = (uint64_t)(t621 & ((uint64_t)9223372036854775807ULL));
                            double t623;
                            memset(&t623, 0, sizeof t623); memcpy(&t623, ((uint8_t *)&t622), sizeof t623 < 8 ? sizeof t623 : 8);
                            gm_b16_8 t624 = gm_l7;
                            gm_f_put_float_fiad72d(t612, GM_CONV(gm_csguankko4lmhd, t613), t623, t624);
                            gm_tcfa6l7reoosn t625;
                            GM_ST(uint8_t, (uint8_t *)&t625 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t625 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t625 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t625 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t625 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t625 + 24, 0);
                            ((uint8_t *)&t625)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t625 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t625), 32);
                            goto gm_fdone;
                        }
gm_lb56:;
                    }
gm_lb55:;
                    void * t626 = gm_l0;
                    gm_t88rcucg1b7uk t627 = gm_l4;
                    gm_tg5lql6itufk7 t628 = gm_f_int_info_ulcevdv(t626, GM_CONV(gm_csguankko4lmhd, t627));
                    gm_l105 = t628;
                    uint8_t t629 = (uint8_t)((((uint8_t *)&gm_l105))[8] != 0);
                    if (t629) {
                        gm_ttovhfee8mf28 t630 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l105));
                        gm_l106 = t630;
                    } else {
                        void * t631 = gm_l0;
                        gm_tl874t2atnlh3 t632 = gm_l1;
                        gm_b16_8 t633;
                        GM_ST(void *, (uint8_t *)&t633, (void *)(gm_sbel6qookv0gl));
                        GM_ST(uintptr_t, (uint8_t *)&t633 + 8, (uintptr_t)(53));
                        gm_t76123gfet73g t634;
                        gm_f_trap_qh84l9d((void *)&t634, (void *)NULL, cx, NULL, t631, t632, t633);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t635;
                        GM_ST(uint8_t, (uint8_t *)&t635 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t635 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t635 + 4, 0);
                        ((uint8_t *)&t635)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t635 + 8), t634);
                        memmove(gm_out, ((uint8_t *)&t635), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t636 = gm_l106;
                    gm_l45 = t636;
                    void * t637 = gm_l0;
                    gm_t88rcucg1b7uk t638 = gm_l4;
                    void * t639 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t640 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t640)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t640, gm_srcl8pn2pjtmj6k, 294, 32);
                    gm_b16_8 t641 = GM_LD(gm_b16_8, (((uint8_t *)t639) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t642 = GM_LD(gm_b16_8, ((uint8_t *)&t641));
                    unsigned __int128 t643 = gm_f_get_int_pg7p4gc(t637, GM_CONV(gm_csguankko4lmhd, t638), t642);
                    gm_l46 = t643;
                    uint8_t t644 = GM_LD(uint8_t, ((uint8_t *)&gm_l45));
                    uint8_t t645 = (uint8_t)!t644;
                    if (t645) {
                        gm_l107 = ((uint8_t)1);
                    } else {
                        unsigned __int128 t646 = gm_l46;
                        unsigned __int128 n647 = (unsigned __int128)((uint64_t)127ULL);
                        if (n647 >= 128) gm_trap_shift((uint64_t)n647, "u128", gm_srcl8pn2pjtmj6k, 295, 36);
                        unsigned __int128 t648 = (unsigned __int128)(t646 >> (int)n647);
                        uint8_t t649 = ((t648 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                        gm_l107 = t649;
                    }
                    uint8_t t650 = gm_l107;
                    if (t650) {
                        void * t651 = gm_l0;
                        gm_b16_8 t652 = gm_l7;
                        unsigned __int128 t653 = gm_l46;
                        gm_f_put_uint_cddin99(t651, t652, t653);
                        gm_tcfa6l7reoosn t654;
                        GM_ST(uint8_t, (uint8_t *)&t654 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t654 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t654 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t654 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t654 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t654 + 24, 0);
                        ((uint8_t *)&t654)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t654 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t654), 32);
                        goto gm_fdone;
                    }
                    unsigned __int128 t655 = gm_l46;
                    unsigned __int128 r656; bool o657 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t655, &r656);
                    unsigned __int128 t658 = r656;
                    gm_l47 = t658;
                    unsigned __int128 t659 = gm_l47;
                    uint32_t t660 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 4));
                    uint32_t r661; bool o662 = __builtin_sub_overflow(t660, ((uint32_t)1ULL), &r661);
                    if (o662) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 300, 34);
                    uint32_t t663 = r661;
                    unsigned __int128 n664 = (unsigned __int128)t663;
                    if (n664 >= 128) gm_trap_shift((uint64_t)n664, "u128", gm_srcl8pn2pjtmj6k, 300, 22);
                    unsigned __int128 r665 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n664);
                    bool o666 = (unsigned __int128)(r665 >> (int)n664) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                    if (o666) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 300, 22);
                    unsigned __int128 t667 = r665;
                    uint8_t t668 = ((t659 == t667));
                    if (t668) {
                        void * t669 = gm_l0;
                        gm_tl874t2atnlh3 t670 = gm_l1;
                        gm_b16_8 t671;
                        GM_ST(void *, (uint8_t *)&t671, (void *)(gm_sbl3pbs9195ho));
                        GM_ST(uintptr_t, (uint8_t *)&t671 + 8, (uintptr_t)(3));
                        gm_f_checked_overflow_ma1bm1g((void *)&gm_l108, (void *)NULL, cx, NULL, t669, t670, t671);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t672 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                        if (t672) {
                        } else {
                            gm_t76123gfet73g t673 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l108) + 8));
                            gm_tcfa6l7reoosn t674;
                            GM_ST(uint8_t, (uint8_t *)&t674 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t674 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t674 + 4, 0);
                            ((uint8_t *)&t674)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t674 + 8), t673);
                            memmove(gm_out, ((uint8_t *)&t674), 32);
                            goto gm_fdone;
                        }
                        void * t675 = gm_l0;
                        gm_b16_8 t676 = gm_l7;
                        unsigned __int128 t677 = gm_l46;
                        gm_f_put_uint_cddin99(t675, t676, t677);
                        gm_tcfa6l7reoosn t678;
                        GM_ST(uint8_t, (uint8_t *)&t678 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t678 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t678 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t678 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t678 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t678 + 24, 0);
                        ((uint8_t *)&t678)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t678 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t678), 32);
                        goto gm_fdone;
                    }
                    void * t679 = gm_l0;
                    gm_b16_8 t680 = gm_l7;
                    unsigned __int128 t681 = gm_l47;
                    gm_f_put_uint_cddin99(t679, t680, t681);
                    goto gm_lb1;
                }
gm_lb54:;
            }
            {
                {
                    uint8_t t682 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t683 = (uint8_t)(t682 == ((uint8_t)20ULL));
                    if (!t683) {
                        goto gm_lb57;
                    }
                    void * t684 = gm_l0;
                    gm_t88rcucg1b7uk t685 = gm_l4;
                    gm_tg5lql6itufk7 t686 = gm_f_int_info_ulcevdv(t684, GM_CONV(gm_csguankko4lmhd, t685));
                    gm_l109 = t686;
                    uint8_t t687 = (uint8_t)((((uint8_t *)&gm_l109))[8] != 0);
                    if (t687) {
                        gm_ttovhfee8mf28 t688 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l109));
                        gm_l110 = t688;
                    } else {
                        void * t689 = gm_l0;
                        gm_tl874t2atnlh3 t690 = gm_l1;
                        gm_b16_8 t691;
                        GM_ST(void *, (uint8_t *)&t691, (void *)(gm_s3gnqv0up712m));
                        GM_ST(uintptr_t, (uint8_t *)&t691 + 8, (uintptr_t)(60));
                        gm_t76123gfet73g t692;
                        gm_f_trap_qh84l9d((void *)&t692, (void *)NULL, cx, NULL, t689, t690, t691);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t693;
                        GM_ST(uint8_t, (uint8_t *)&t693 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t693 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t693 + 4, 0);
                        ((uint8_t *)&t693)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t693 + 8), t692);
                        memmove(gm_out, ((uint8_t *)&t693), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t694 = gm_l110;
                    gm_l48 = t694;
                    void * t695 = gm_l0;
                    gm_t88rcucg1b7uk t696 = gm_l4;
                    void * t697 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t698 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t698)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t698, gm_srcl8pn2pjtmj6k, 309, 32);
                    gm_b16_8 t699 = GM_LD(gm_b16_8, (((uint8_t *)t697) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t700 = GM_LD(gm_b16_8, ((uint8_t *)&t699));
                    unsigned __int128 t701 = gm_f_get_int_pg7p4gc(t695, GM_CONV(gm_csguankko4lmhd, t696), t700);
                    gm_l49 = t701;
                    void * t702 = gm_l0;
                    gm_t88rcucg1b7uk t703 = gm_l5;
                    void * t704 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t705 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t705)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t705, gm_srcl8pn2pjtmj6k, 310, 32);
                    gm_b16_8 t706 = GM_LD(gm_b16_8, (((uint8_t *)t704) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t707 = GM_LD(gm_b16_8, ((uint8_t *)&t706));
                    unsigned __int128 t708 = gm_f_get_int_pg7p4gc(t702, GM_CONV(gm_csguankko4lmhd, t703), t707);
                    gm_l50 = t708;
                    gm_l51 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                    uint8_t t709 = GM_LD(uint8_t, ((uint8_t *)&gm_l48));
                    unsigned __int128 t710 = gm_l49;
                    unsigned __int128 t711 = gm_l50;
                    int32_t t712 = gm_f_int_cmp_kb3sm6u(t709, t710, t711);
                    uint8_t t713 = (t712 >= ((int32_t)0LL));
                    if (t713) {
                        unsigned __int128 t714 = gm_l49;
                        unsigned __int128 t715 = gm_l50;
                        unsigned __int128 r716; bool o717 = __builtin_sub_overflow(t714, t715, &r716);
                        unsigned __int128 t718 = r716;
                        gm_l51 = t718;
                    } else {
                        unsigned __int128 t719 = gm_l50;
                        unsigned __int128 t720 = gm_l49;
                        unsigned __int128 r721; bool o722 = __builtin_sub_overflow(t719, t720, &r721);
                        unsigned __int128 t723 = r721;
                        gm_l51 = t723;
                    }
                    {
                        {
                            void * t724 = gm_l0;
                            gm_t88rcucg1b7uk t725 = gm_l3;
                            gm_tg5lql6itufk7 t726 = gm_f_int_info_ulcevdv(t724, GM_CONV(gm_csguankko4lmhd, t725));
                            gm_l111 = t726;
                            uint8_t t727 = (uint8_t)((((uint8_t *)&gm_l111))[8] != 0);
                            if (!t727) {
                                goto gm_lb59;
                            }
                            gm_ttovhfee8mf28 t728 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l111));
                            gm_l52 = t728;
                            uint8_t t729 = GM_LD(uint8_t, ((uint8_t *)&gm_l52));
                            uint32_t t730 = GM_LD(uint32_t, (((uint8_t *)&gm_l52) + 4));
                            unsigned __int128 t731 = gm_l51;
                            unsigned __int128 t732 = gm_f_num_ext_348kmbu(t729, t730, t731);
                            gm_l51 = t732;
                            goto gm_lb58;
                        }
gm_lb59:;
                    }
gm_lb58:;
                    void * t733 = gm_l0;
                    gm_b16_8 t734 = gm_l7;
                    unsigned __int128 t735 = gm_l51;
                    gm_f_put_uint_cddin99(t733, t734, t735);
                    goto gm_lb1;
                }
gm_lb57:;
            }
            {
                {
                    uint8_t t736 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t737 = (uint8_t)(t736 == ((uint8_t)21ULL));
                    if (!t737) {
                        goto gm_lb60;
                    }
                    gm_t9qkh0rsooc8d t738 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
                    gm_l53 = t738;
                    void * t739 = gm_l0;
                    gm_tl874t2atnlh3 t740 = gm_l1;
                    gm_t9qkh0rsooc8d t741 = gm_l53;
                    gm_t88rcucg1b7uk t742 = gm_l4;
                    gm_t88rcucg1b7uk t743 = gm_l3;
                    gm_t88rcucg1b7uk t744 = gm_l5;
                    void * t745 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t746 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t746)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t746, gm_srcl8pn2pjtmj6k, 320, 60);
                    gm_b16_8 t747 = GM_LD(gm_b16_8, (((uint8_t *)t745) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t748 = GM_LD(gm_b16_8, ((uint8_t *)&t747));
                    void * t749 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t750 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t750)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t750, gm_srcl8pn2pjtmj6k, 320, 66);
                    gm_b16_8 t751 = GM_LD(gm_b16_8, (((uint8_t *)t749) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t752 = GM_LD(gm_b16_8, ((uint8_t *)&t751));
                    gm_b16_8 t753 = gm_l7;
                    gm_f_pow_7je2uvi((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t739, t740, t741, t742, t743, t744, t748, t752, t753);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb60:;
                {
                    {
                        {
                            uint8_t t754 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t755 = (uint8_t)(t754 == ((uint8_t)22ULL));
                            if (!t755) {
                                goto gm_lb63;
                            }
                            goto gm_lb62;
                        }
gm_lb63:;
                        {
                            uint8_t t756 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t757 = (uint8_t)(t756 == ((uint8_t)23ULL));
                            if (!t757) {
                                goto gm_lb64;
                            }
                            goto gm_lb62;
                        }
gm_lb64:;
                        {
                            uint8_t t758 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t759 = (uint8_t)(t758 == ((uint8_t)24ULL));
                            if (!t759) {
                                goto gm_lb65;
                            }
                            goto gm_lb62;
                        }
gm_lb65:;
                        {
                            uint8_t t760 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t761 = (uint8_t)(t760 == ((uint8_t)25ULL));
                            if (!t761) {
                                goto gm_lb66;
                            }
                            goto gm_lb62;
                        }
gm_lb66:;
                        {
                            uint8_t t762 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t763 = (uint8_t)(t762 == ((uint8_t)26ULL));
                            if (!t763) {
                                goto gm_lb67;
                            }
                            goto gm_lb62;
                        }
gm_lb67:;
                        goto gm_lb61;
                    }
gm_lb62:;
                    void * t764 = gm_l0;
                    gm_t88rcucg1b7uk t765 = gm_l4;
                    gm_f_kind_htv6093((void *)&gm_l113, t764, GM_CONV(gm_csguankko4lmhd, t765));
                    {
                        {
                            uint8_t t766 = GM_LD(uint8_t, (((uint8_t *)&gm_l113) + 0));
                            uint8_t t767 = (uint8_t)(t766 == ((uint8_t)3ULL));
                            if (!t767) {
                                goto gm_lb69;
                            }
                            gm_l112 = ((uint8_t)1);
                            goto gm_lb68;
                        }
gm_lb69:;
                        {
                            gm_l112 = ((uint8_t)0);
                            goto gm_lb68;
                        }
gm_lb70:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 322, 21);
                    }
gm_lb68:;
                    uint8_t t768 = gm_l112;
                    uint8_t t769 = (uint8_t)!t768;
                    if (t769) {
                        void * t770 = gm_l0;
                        gm_tl874t2atnlh3 t771 = gm_l1;
                        gm_b16_8 t772;
                        GM_ST(void *, (uint8_t *)&t772, (void *)(gm_s9h8lp3gkedth));
                        GM_ST(uintptr_t, (uint8_t *)&t772 + 8, (uintptr_t)(28));
                        gm_t76123gfet73g t773;
                        gm_f_trap_qh84l9d((void *)&t773, (void *)NULL, cx, NULL, t770, t771, t772);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t774;
                        GM_ST(uint8_t, (uint8_t *)&t774 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t774 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t774 + 4, 0);
                        ((uint8_t *)&t774)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t774 + 8), t773);
                        memmove(gm_out, ((uint8_t *)&t774), 32);
                        goto gm_fdone;
                    }
                    void * t775 = gm_l0;
                    gm_t88rcucg1b7uk t776 = gm_l4;
                    void * t777 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t778 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t778)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t778, gm_srcl8pn2pjtmj6k, 323, 34);
                    gm_b16_8 t779 = GM_LD(gm_b16_8, (((uint8_t *)t777) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t780 = GM_LD(gm_b16_8, ((uint8_t *)&t779));
                    double t781 = gm_f_get_float_grmhlm4(t775, GM_CONV(gm_csguankko4lmhd, t776), t780);
                    gm_l54 = t781;
                    double t782 = gm_l54;
                    uint64_t t783;
                    memset(&t783, 0, sizeof t783); memcpy(&t783, ((uint8_t *)&t782), sizeof t783 < 8 ? sizeof t783 : 8);
                    gm_l55 = t783;
                    uint64_t t784 = gm_l55;
                    unsigned __int128 n785 = (unsigned __int128)((uint64_t)52ULL);
                    if (n785 >= 64) gm_trap_shift((uint64_t)n785, "u64", gm_srcl8pn2pjtmj6k, 325, 21);
                    uint64_t t786 = (uint64_t)(t784 >> (int)n785);
                    uint64_t t787 = (uint64_t)(t786 & ((uint64_t)2047ULL));
                    gm_l56 = t787;
                    {
                        {
                            uint8_t t788 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t789 = (uint8_t)(t788 == ((uint8_t)22ULL));
                            if (!t789) {
                                goto gm_lb72;
                            }
                            void * t790 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t791 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t791)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t791, gm_srcl8pn2pjtmj6k, 327, 29);
                            {
                                {
                                    uint64_t t792 = gm_l56;
                                    uint8_t t793 = ((t792 == ((uint64_t)2047ULL)));
                                    if (!t793) {
                                        goto gm_lb74;
                                    }
                                    uint64_t t794 = gm_l55;
                                    uint64_t t795 = (uint64_t)(t794 & ((uint64_t)4503599627370495ULL));
                                    uint8_t t796 = (!(t795 == ((uint64_t)0ULL)));
                                    if (!t796) {
                                        goto gm_lb74;
                                    }
                                    gm_l114 = ((uint8_t)1ULL);
                                    goto gm_lb73;
                                }
gm_lb74:;
                                gm_l114 = ((uint8_t)0ULL);
                            }
gm_lb73:;
                            uint8_t t797 = gm_l114;
                            GM_ST(uint8_t, (((uint8_t *)t790) + (uintptr_t)((uint64_t)0ULL) * 1), t797);
                            goto gm_lb71;
                        }
gm_lb72:;
                        {
                            uint8_t t798 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t799 = (uint8_t)(t798 == ((uint8_t)23ULL));
                            if (!t799) {
                                goto gm_lb75;
                            }
                            void * t800 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t801 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t801)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t801, gm_srcl8pn2pjtmj6k, 328, 29);
                            {
                                {
                                    uint64_t t802 = gm_l56;
                                    uint8_t t803 = ((t802 == ((uint64_t)2047ULL)));
                                    if (!t803) {
                                        goto gm_lb77;
                                    }
                                    uint64_t t804 = gm_l55;
                                    uint64_t t805 = (uint64_t)(t804 & ((uint64_t)4503599627370495ULL));
                                    uint8_t t806 = ((t805 == ((uint64_t)0ULL)));
                                    if (!t806) {
                                        goto gm_lb77;
                                    }
                                    gm_l115 = ((uint8_t)1ULL);
                                    goto gm_lb76;
                                }
gm_lb77:;
                                gm_l115 = ((uint8_t)0ULL);
                            }
gm_lb76:;
                            uint8_t t807 = gm_l115;
                            GM_ST(uint8_t, (((uint8_t *)t800) + (uintptr_t)((uint64_t)0ULL) * 1), t807);
                            goto gm_lb71;
                        }
gm_lb75:;
                        {
                            uint8_t t808 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t809 = (uint8_t)(t808 == ((uint8_t)24ULL));
                            if (!t809) {
                                goto gm_lb78;
                            }
                            void * t810 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t811 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t811)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t811, gm_srcl8pn2pjtmj6k, 329, 32);
                            uint64_t t812 = gm_l56;
                            uint8_t t813 = ((t812 == ((uint64_t)2047ULL)));
                            if (t813) {
                                gm_l116 = ((uint8_t)0ULL);
                            } else {
                                gm_l116 = ((uint8_t)1ULL);
                            }
                            uint8_t t814 = gm_l116;
                            GM_ST(uint8_t, (((uint8_t *)t810) + (uintptr_t)((uint64_t)0ULL) * 1), t814);
                            goto gm_lb71;
                        }
gm_lb78:;
                        {
                            uint8_t t815 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t816 = (uint8_t)(t815 == ((uint8_t)25ULL));
                            if (!t816) {
                                goto gm_lb79;
                            }
                            void * t817 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t818 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t818)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t818, gm_srcl8pn2pjtmj6k, 330, 31);
                            uint64_t t819 = gm_l55;
                            unsigned __int128 n820 = (unsigned __int128)((uint64_t)63ULL);
                            if (n820 >= 64) gm_trap_shift((uint64_t)n820, "u64", gm_srcl8pn2pjtmj6k, 330, 44);
                            uint64_t t821 = (uint64_t)(t819 >> (int)n820);
                            uint8_t t822 = ((t821 == ((uint64_t)1ULL)));
                            if (t822) {
                                gm_l117 = ((uint8_t)1ULL);
                            } else {
                                gm_l117 = ((uint8_t)0ULL);
                            }
                            uint8_t t823 = gm_l117;
                            GM_ST(uint8_t, (((uint8_t *)t817) + (uintptr_t)((uint64_t)0ULL) * 1), t823);
                            goto gm_lb71;
                        }
gm_lb79:;
                        {
                            void * t824 = gm_l0;
                            gm_t88rcucg1b7uk t825 = gm_l4;
                            double t826 = gm_l54;
                            double t827 = (double)(__builtin_sqrt(t826));
                            gm_b16_8 t828 = gm_l7;
                            gm_f_put_float_fiad72d(t824, GM_CONV(gm_csguankko4lmhd, t825), t827, t828);
                            goto gm_lb71;
                        }
gm_lb80:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 326, 13);
                    }
gm_lb71:;
                    goto gm_lb1;
                }
gm_lb61:;
            }
            {
                {
                    uint8_t t829 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t830 = (uint8_t)(t829 == ((uint8_t)27ULL));
                    if (!t830) {
                        goto gm_lb81;
                    }
                    {
                        {
                            void * t831 = gm_l0;
                            gm_t88rcucg1b7uk t832 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l118, t831, GM_CONV(gm_csguankko4lmhd, t832));
                            uint8_t t833 = GM_LD(uint8_t, (((uint8_t *)&gm_l118) + 0));
                            uint8_t t834 = (uint8_t)(t833 == ((uint8_t)3ULL));
                            if (!t834) {
                                goto gm_lb83;
                            }
                            uint16_t t835 = GM_LD(uint16_t, (((uint8_t *)&gm_l118) + 8));
                            gm_l57 = t835;
                            void * t836 = gm_l0;
                            gm_t88rcucg1b7uk t837 = gm_l4;
                            void * t838 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t839 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t839)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t839, gm_srcl8pn2pjtmj6k, 336, 34);
                            gm_b16_8 t840 = GM_LD(gm_b16_8, (((uint8_t *)t838) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t841 = GM_LD(gm_b16_8, ((uint8_t *)&t840));
                            double t842 = gm_f_get_float_grmhlm4(t836, GM_CONV(gm_csguankko4lmhd, t837), t841);
                            gm_l58 = t842;
                            void * t843 = gm_l0;
                            gm_t88rcucg1b7uk t844 = gm_l4;
                            void * t845 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t846 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t846)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t846, gm_srcl8pn2pjtmj6k, 337, 34);
                            gm_b16_8 t847 = GM_LD(gm_b16_8, (((uint8_t *)t845) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t848 = GM_LD(gm_b16_8, ((uint8_t *)&t847));
                            double t849 = gm_f_get_float_grmhlm4(t843, GM_CONV(gm_csguankko4lmhd, t844), t848);
                            gm_l59 = t849;
                            void * t850 = gm_l0;
                            gm_t88rcucg1b7uk t851 = gm_l4;
                            void * t852 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t853 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t853)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t853, gm_srcl8pn2pjtmj6k, 338, 34);
                            gm_b16_8 t854 = GM_LD(gm_b16_8, (((uint8_t *)t852) + (uintptr_t)((uint64_t)2ULL) * 16));
                            gm_b16_8 t855 = GM_LD(gm_b16_8, ((uint8_t *)&t854));
                            double t856 = gm_f_get_float_grmhlm4(t850, GM_CONV(gm_csguankko4lmhd, t851), t855);
                            gm_l60 = t856;
                            uint16_t t857 = gm_l57;
                            uint8_t t858 = ((t857 == ((uint16_t)32ULL)));
                            if (t858) {
                                void * t859 = gm_l0;
                                gm_t88rcucg1b7uk t860 = gm_l4;
                                double t861 = gm_l58;
                                float t862 = (float)((double)t861);
                                double t863 = gm_l59;
                                float t864 = (float)((double)t863);
                                double t865 = gm_l60;
                                float t866 = (float)((double)t865);
                                float t867 = (float)(__builtin_fmaf(t862, t864, t866));
                                double t868 = (double)t867;
                                gm_b16_8 t869 = gm_l7;
                                gm_f_put_float_fiad72d(t859, GM_CONV(gm_csguankko4lmhd, t860), t868, t869);
                            } else {
                                void * t870 = gm_l0;
                                gm_t88rcucg1b7uk t871 = gm_l4;
                                double t872 = gm_l58;
                                double t873 = gm_l59;
                                double t874 = gm_l60;
                                double t875 = (double)(__builtin_fma(t872, t873, t874));
                                gm_b16_8 t876 = gm_l7;
                                gm_f_put_float_fiad72d(t870, GM_CONV(gm_csguankko4lmhd, t871), t875, t876);
                            }
                            goto gm_lb82;
                        }
gm_lb83:;
                        void * t877 = gm_l0;
                        gm_tl874t2atnlh3 t878 = gm_l1;
                        gm_b16_8 t879;
                        GM_ST(void *, (uint8_t *)&t879, (void *)(gm_sh01rb1u2jpfs));
                        GM_ST(uintptr_t, (uint8_t *)&t879 + 8, (uintptr_t)(18));
                        gm_t76123gfet73g t880;
                        gm_f_trap_qh84l9d((void *)&t880, (void *)NULL, cx, NULL, t877, t878, t879);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t881;
                        GM_ST(uint8_t, (uint8_t *)&t881 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t881 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t881 + 4, 0);
                        ((uint8_t *)&t881)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t881 + 8), t880);
                        memmove(gm_out, ((uint8_t *)&t881), 32);
                        goto gm_fdone;
                    }
gm_lb82:;
                    goto gm_lb1;
                }
gm_lb81:;
            }
            {
                {
                    {
                        {
                            uint8_t t882 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t883 = (uint8_t)(t882 == ((uint8_t)32ULL));
                            if (!t883) {
                                goto gm_lb86;
                            }
                            goto gm_lb85;
                        }
gm_lb86:;
                        {
                            uint8_t t884 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t885 = (uint8_t)(t884 == ((uint8_t)33ULL));
                            if (!t885) {
                                goto gm_lb87;
                            }
                            goto gm_lb85;
                        }
gm_lb87:;
                        goto gm_lb84;
                    }
gm_lb85:;
                    void * t886 = gm_l0;
                    gm_t88rcucg1b7uk t887 = gm_l4;
                    gm_tg5lql6itufk7 t888 = gm_f_int_info_ulcevdv(t886, GM_CONV(gm_csguankko4lmhd, t887));
                    gm_l119 = t888;
                    uint8_t t889 = (uint8_t)((((uint8_t *)&gm_l119))[8] != 0);
                    if (t889) {
                        gm_ttovhfee8mf28 t890 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l119));
                        gm_l120 = t890;
                    } else {
                        void * t891 = gm_l0;
                        gm_tl874t2atnlh3 t892 = gm_l1;
                        gm_b16_8 t893;
                        GM_ST(void *, (uint8_t *)&t893, (void *)(gm_s26c80h2pm6a7));
                        GM_ST(uintptr_t, (uint8_t *)&t893 + 8, (uintptr_t)(25));
                        gm_t76123gfet73g t894;
                        gm_f_trap_qh84l9d((void *)&t894, (void *)NULL, cx, NULL, t891, t892, t893);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t895;
                        GM_ST(uint8_t, (uint8_t *)&t895 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t895 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t895 + 4, 0);
                        ((uint8_t *)&t895)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t895 + 8), t894);
                        memmove(gm_out, ((uint8_t *)&t895), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t896 = gm_l120;
                    gm_l61 = t896;
                    void * t897 = gm_l0;
                    gm_t88rcucg1b7uk t898 = gm_l5;
                    gm_tg5lql6itufk7 t899 = gm_f_int_info_ulcevdv(t897, GM_CONV(gm_csguankko4lmhd, t898));
                    gm_l121 = t899;
                    uint8_t t900 = (uint8_t)((((uint8_t *)&gm_l121))[8] != 0);
                    if (t900) {
                        gm_ttovhfee8mf28 t901 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l121));
                        gm_l122 = t901;
                    } else {
                        void * t902 = gm_l0;
                        gm_tl874t2atnlh3 t903 = gm_l1;
                        gm_b16_8 t904;
                        GM_ST(void *, (uint8_t *)&t904, (void *)(gm_sti4tkgslrkgo));
                        GM_ST(uintptr_t, (uint8_t *)&t904 + 8, (uintptr_t)(25));
                        gm_t76123gfet73g t905;
                        gm_f_trap_qh84l9d((void *)&t905, (void *)NULL, cx, NULL, t902, t903, t904);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t906;
                        GM_ST(uint8_t, (uint8_t *)&t906 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t906 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t906 + 4, 0);
                        ((uint8_t *)&t906)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t906 + 8), t905);
                        memmove(gm_out, ((uint8_t *)&t906), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t907 = gm_l122;
                    gm_l62 = t907;
                    uint32_t t908 = GM_LD(uint32_t, (((uint8_t *)&gm_l61) + 4));
                    gm_l63 = t908;
                    void * t909 = gm_l0;
                    void * t910 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t911 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t911)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t911, gm_srcl8pn2pjtmj6k, 349, 29);
                    void * t912 = GM_LD(void *, (((uint8_t *)t910) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t913 = GM_LD(uint64_t, ((((uint8_t *)t910) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    void * t914 = gm_l0;
                    gm_t88rcucg1b7uk t915 = gm_l4;
                    uint64_t t916 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t914, t915);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t917 = gm_f_us_cuv01fp(t916);
                    uint64_t t918 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t919 = (uintptr_t)t917;
                    if (t918 > t919 || t919 > (uintptr_t)t913) gm_trap_slice(t918, t919, t913, gm_srcl8pn2pjtmj6k, 349, 29);
                    uint64_t t920 = ((uintptr_t)t917) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t921;
                    GM_ST(void *, (uint8_t *)&t921, (void *)(((uint8_t *)t912 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t921 + 8, (uintptr_t)(t920));
                    gm_b16_8 t922 = GM_LD(gm_b16_8, ((uint8_t *)&t921));
                    unsigned __int128 t923 = gm_f_get_uint_bq8c4b7(t909, t922);
                    uint32_t t924 = gm_l63;
                    unsigned __int128 t925 = gm_f_num_mask_do3naah(t924);
                    unsigned __int128 t926 = (unsigned __int128)(t923 & t925);
                    gm_l64 = t926;
                    void * t927 = gm_l0;
                    gm_t88rcucg1b7uk t928 = gm_l5;
                    void * t929 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t930 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t930)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t930, gm_srcl8pn2pjtmj6k, 350, 35);
                    gm_b16_8 t931 = GM_LD(gm_b16_8, (((uint8_t *)t929) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t932 = GM_LD(gm_b16_8, ((uint8_t *)&t931));
                    unsigned __int128 t933 = gm_f_get_int_pg7p4gc(t927, GM_CONV(gm_csguankko4lmhd, t928), t932);
                    gm_l65 = t933;
                    gm_l66 = ((uint32_t)0ULL);
                    uint8_t t934 = GM_LD(uint8_t, ((uint8_t *)&gm_l62));
                    if (t934) {
                        unsigned __int128 t935 = gm_l65;
                        unsigned __int128 n936 = (unsigned __int128)((uint64_t)127ULL);
                        if (n936 >= 128) gm_trap_shift((uint64_t)n936, "u128", gm_srcl8pn2pjtmj6k, 352, 39);
                        unsigned __int128 t937 = (unsigned __int128)(t935 >> (int)n936);
                        uint8_t t938 = ((t937 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
                        gm_l123 = t938;
                    } else {
                        gm_l123 = ((uint8_t)0);
                    }
                    uint8_t t939 = gm_l123;
                    uint8_t t940 = (uint8_t)!t939;
                    if (t940) {
                        unsigned __int128 t941 = gm_l65;
                        uint32_t t942 = gm_l63;
                        unsigned __int128 t943 = (unsigned __int128)t942;
                        if (t943 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 352, 65);
                        unsigned __int128 t944 = (unsigned __int128)(t941 % t943);
                        uint32_t t945 = (uint32_t)t944;
                        gm_l66 = t945;
                    }
                    gm_tvdg94bk7aa22 t946 = gm_l2;
                    uint8_t t947 = GM_LD(uint8_t, (((uint8_t *)&t946) + 0));
                    uint8_t t948 = (uint8_t)((t947 == ((uint8_t)33ULL)));
                    gm_tvdg94bk7aa22 t949;
                    GM_ST(uint8_t, (uint8_t *)&t949 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t949 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t949 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t949 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t949 + 0), ((uint8_t)33ULL));
                    if (t948) {
                        uint32_t t950 = gm_l63;
                        uint32_t t951 = gm_l66;
                        uint32_t r952; bool o953 = __builtin_sub_overflow(t950, t951, &r952);
                        if (o953) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 353, 35);
                        uint32_t t954 = r952;
                        uint32_t t955 = gm_l63;
                        if (t955 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 353, 34);
                        uint32_t t956 = (uint32_t)(t954 % t955);
                        gm_l66 = t956;
                    }
                    uint32_t t957 = gm_l66;
                    uint8_t t958 = ((t957 == ((uint32_t)0ULL)));
                    if (t958) {
                        unsigned __int128 t959 = gm_l64;
                        gm_l124 = t959;
                    } else {
                        unsigned __int128 t960 = gm_l64;
                        uint32_t t961 = gm_l66;
                        unsigned __int128 t962 = (unsigned __int128)t961;
                        unsigned __int128 n963 = (unsigned __int128)t962;
                        if (n963 >= 128) gm_trap_shift((uint64_t)n963, "u128", gm_srcl8pn2pjtmj6k, 354, 43);
                        unsigned __int128 r964 = (unsigned __int128)((unsigned __int128)t960 << (int)n963);
                        bool o965 = (unsigned __int128)(r964 >> (int)n963) != t960;
                        unsigned __int128 t966 = r964;
                        unsigned __int128 t967 = gm_l64;
                        uint32_t t968 = gm_l63;
                        uint32_t t969 = gm_l66;
                        uint32_t r970; bool o971 = __builtin_sub_overflow(t968, t969, &r970);
                        if (o971) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 354, 71);
                        uint32_t t972 = r970;
                        unsigned __int128 t973 = (unsigned __int128)t972;
                        unsigned __int128 n974 = (unsigned __int128)t973;
                        if (n974 >= 128) gm_trap_shift((uint64_t)n974, "u128", gm_srcl8pn2pjtmj6k, 354, 61);
                        unsigned __int128 t975 = (unsigned __int128)(t967 >> (int)n974);
                        unsigned __int128 t976 = (unsigned __int128)(t966 | t975);
                        uint32_t t977 = gm_l63;
                        unsigned __int128 t978 = gm_f_num_mask_do3naah(t977);
                        unsigned __int128 t979 = (unsigned __int128)(t976 & t978);
                        gm_l124 = t979;
                    }
                    unsigned __int128 t980 = gm_l124;
                    gm_l67 = t980;
                    void * t981 = gm_l0;
                    gm_b16_8 t982 = gm_l7;
                    uint8_t t983 = GM_LD(uint8_t, ((uint8_t *)&gm_l61));
                    uint32_t t984 = gm_l63;
                    unsigned __int128 t985 = gm_l67;
                    unsigned __int128 t986 = gm_f_num_ext_348kmbu(t983, t984, t985);
                    gm_f_put_uint_cddin99(t981, t982, t986);
                    goto gm_lb1;
                }
gm_lb84:;
            }
            {
                {
                    {
                        {
                            uint8_t t987 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t988 = (uint8_t)(t987 == ((uint8_t)28ULL));
                            if (!t988) {
                                goto gm_lb90;
                            }
                            goto gm_lb89;
                        }
gm_lb90:;
                        {
                            uint8_t t989 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t990 = (uint8_t)(t989 == ((uint8_t)29ULL));
                            if (!t990) {
                                goto gm_lb91;
                            }
                            goto gm_lb89;
                        }
gm_lb91:;
                        {
                            uint8_t t991 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t992 = (uint8_t)(t991 == ((uint8_t)30ULL));
                            if (!t992) {
                                goto gm_lb92;
                            }
                            goto gm_lb89;
                        }
gm_lb92:;
                        {
                            uint8_t t993 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t994 = (uint8_t)(t993 == ((uint8_t)31ULL));
                            if (!t994) {
                                goto gm_lb93;
                            }
                            goto gm_lb89;
                        }
gm_lb93:;
                        {
                            uint8_t t995 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t996 = (uint8_t)(t995 == ((uint8_t)34ULL));
                            if (!t996) {
                                goto gm_lb94;
                            }
                            goto gm_lb89;
                        }
gm_lb94:;
                        goto gm_lb88;
                    }
gm_lb89:;
                    void * t997 = gm_l0;
                    gm_t88rcucg1b7uk t998 = gm_l4;
                    gm_tg5lql6itufk7 t999 = gm_f_int_info_ulcevdv(t997, GM_CONV(gm_csguankko4lmhd, t998));
                    gm_l125 = t999;
                    uint8_t t1000 = (uint8_t)((((uint8_t *)&gm_l125))[8] != 0);
                    if (t1000) {
                        gm_ttovhfee8mf28 t1001 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l125));
                        gm_l126 = t1001;
                    } else {
                        void * t1002 = gm_l0;
                        gm_tl874t2atnlh3 t1003 = gm_l1;
                        gm_b16_8 t1004;
                        GM_ST(void *, (uint8_t *)&t1004, (void *)(gm_spqf88ftq0gqk));
                        GM_ST(uintptr_t, (uint8_t *)&t1004 + 8, (uintptr_t)(28));
                        gm_t76123gfet73g t1005;
                        gm_f_trap_qh84l9d((void *)&t1005, (void *)NULL, cx, NULL, t1002, t1003, t1004);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t1006;
                        GM_ST(uint8_t, (uint8_t *)&t1006 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1006 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1006 + 4, 0);
                        ((uint8_t *)&t1006)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1006 + 8), t1005);
                        memmove(gm_out, ((uint8_t *)&t1006), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t1007 = gm_l126;
                    gm_l68 = t1007;
                    uint32_t t1008 = GM_LD(uint32_t, (((uint8_t *)&gm_l68) + 4));
                    gm_l69 = t1008;
                    void * t1009 = gm_l0;
                    void * t1010 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t1011 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t1011)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t1011, gm_srcl8pn2pjtmj6k, 360, 29);
                    void * t1012 = GM_LD(void *, (((uint8_t *)t1010) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t1013 = GM_LD(uint64_t, ((((uint8_t *)t1010) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    void * t1014 = gm_l0;
                    gm_t88rcucg1b7uk t1015 = gm_l4;
                    uint64_t t1016 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t1014, t1015);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t1017 = gm_f_us_cuv01fp(t1016);
                    uint64_t t1018 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t1019 = (uintptr_t)t1017;
                    if (t1018 > t1019 || t1019 > (uintptr_t)t1013) gm_trap_slice(t1018, t1019, t1013, gm_srcl8pn2pjtmj6k, 360, 29);
                    uint64_t t1020 = ((uintptr_t)t1017) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t1021;
                    GM_ST(void *, (uint8_t *)&t1021, (void *)(((uint8_t *)t1012 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t1021 + 8, (uintptr_t)(t1020));
                    gm_b16_8 t1022 = GM_LD(gm_b16_8, ((uint8_t *)&t1021));
                    unsigned __int128 t1023 = gm_f_get_uint_bq8c4b7(t1009, t1022);
                    uint32_t t1024 = gm_l69;
                    unsigned __int128 t1025 = gm_f_num_mask_do3naah(t1024);
                    unsigned __int128 t1026 = (unsigned __int128)(t1023 & t1025);
                    gm_l70 = t1026;
                    gm_l71 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                    {
                        {
                            uint8_t t1027 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1028 = (uint8_t)(t1027 == ((uint8_t)28ULL));
                            if (!t1028) {
                                goto gm_lb96;
                            }
                            void * t1029 = gm_l0;
                            gm_b16_8 t1030 = gm_l7;
                            unsigned __int128 t1031 = gm_l70;
                            uint32_t t1032 = (uint32_t)gm_clz((unsigned __int128)(unsigned __int128)(t1031), 128);
                            uint32_t t1033 = gm_l69;
                            uint32_t r1034; bool o1035 = __builtin_sub_overflow(((uint32_t)128ULL), t1033, &r1034);
                            if (o1035) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 53);
                            uint32_t t1036 = r1034;
                            uint32_t r1037; bool o1038 = __builtin_sub_overflow(t1032, t1036, &r1037);
                            if (o1038) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 42);
                            uint32_t t1039 = r1037;
                            unsigned __int128 t1040 = (unsigned __int128)t1039;
                            gm_f_put_uint_cddin99(t1029, t1030, t1040);
                            gm_tcfa6l7reoosn t1041;
                            GM_ST(uint8_t, (uint8_t *)&t1041 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1041 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1041 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1041 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1041 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1041 + 24, 0);
                            ((uint8_t *)&t1041)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t1041 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t1041), 32);
                            goto gm_fdone;
                        }
gm_lb96:;
                        {
                            uint8_t t1042 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1043 = (uint8_t)(t1042 == ((uint8_t)29ULL));
                            if (!t1043) {
                                goto gm_lb97;
                            }
                            void * t1044 = gm_l0;
                            gm_b16_8 t1045 = gm_l7;
                            unsigned __int128 t1046 = gm_l70;
                            uint8_t t1047 = ((t1046 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                            if (t1047) {
                                uint32_t t1048 = gm_l69;
                                unsigned __int128 t1049 = (unsigned __int128)t1048;
                                gm_l127 = t1049;
                            } else {
                                unsigned __int128 t1050 = gm_l70;
                                uint32_t t1051 = (uint32_t)gm_ctz((unsigned __int128)(unsigned __int128)(t1050), 128);
                                unsigned __int128 t1052 = (unsigned __int128)t1051;
                                gm_l127 = t1052;
                            }
                            unsigned __int128 t1053 = gm_l127;
                            gm_f_put_uint_cddin99(t1044, t1045, t1053);
                            gm_tcfa6l7reoosn t1054;
                            GM_ST(uint8_t, (uint8_t *)&t1054 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1054 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1054 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1054 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1054 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1054 + 24, 0);
                            ((uint8_t *)&t1054)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t1054 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t1054), 32);
                            goto gm_fdone;
                        }
gm_lb97:;
                        {
                            uint8_t t1055 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1056 = (uint8_t)(t1055 == ((uint8_t)30ULL));
                            if (!t1056) {
                                goto gm_lb98;
                            }
                            void * t1057 = gm_l0;
                            gm_b16_8 t1058 = gm_l7;
                            unsigned __int128 t1059 = gm_l70;
                            uint32_t t1060 = (uint32_t)gm_popcount((unsigned __int128)(unsigned __int128)(t1059));
                            unsigned __int128 t1061 = (unsigned __int128)t1060;
                            gm_f_put_uint_cddin99(t1057, t1058, t1061);
                            gm_tcfa6l7reoosn t1062;
                            GM_ST(uint8_t, (uint8_t *)&t1062 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1062 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1062 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1062 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1062 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1062 + 24, 0);
                            ((uint8_t *)&t1062)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t1062 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t1062), 32);
                            goto gm_fdone;
                        }
gm_lb98:;
                        {
                            uint8_t t1063 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1064 = (uint8_t)(t1063 == ((uint8_t)31ULL));
                            if (!t1064) {
                                goto gm_lb99;
                            }
                            uint32_t t1065 = gm_l69;
                            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 376, 24);
                            uint32_t t1066 = (uint32_t)(t1065 % ((uint32_t)8ULL));
                            uint8_t t1067 = (!(t1066 == ((uint32_t)0ULL)));
                            if (t1067) {
                                void * t1068 = gm_l0;
                                gm_tl874t2atnlh3 t1069 = gm_l1;
                                uint32_t t1070 = gm_l69;
                                gm_t88rcucg1b7uk t1071;
                                GM_ST(uint32_t, ((uint8_t *)&t1071 + 0), t1070);
                                gm_f_format_ven7kt5((void *)&gm_l128, (void *)NULL, cx, NULL, t1071);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t1072 = (uint8_t)!((((uint8_t *)&gm_l128))[0] != 0);
                                if (!t1072) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l128) + 8, gm_srcl8pn2pjtmj6k, 376, 62);
                                gm_b16_8 t1073 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l128) + 8));
                                gm_b16_8 t1074 = GM_LD(gm_b16_8, ((uint8_t *)&t1073));
                                gm_t76123gfet73g t1075;
                                gm_f_trap_qh84l9d((void *)&t1075, (void *)NULL, cx, NULL, t1068, t1069, t1074);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tcfa6l7reoosn t1076;
                                GM_ST(uint8_t, (uint8_t *)&t1076 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t1076 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t1076 + 4, 0);
                                ((uint8_t *)&t1076)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1076 + 8), t1075);
                                memmove(gm_out, ((uint8_t *)&t1076), 32);
                                goto gm_fdone;
                            }
                            gm_l73 = ((uint32_t)0ULL);
                            uint32_t t1077 = gm_l69;
                            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 33);
                            uint32_t t1078 = (uint32_t)(t1077 / ((uint32_t)8ULL));
                            gm_l72 = t1078;
                            {
                                for (;;) {
                                    uint32_t t1079 = gm_l73;
                                    uint32_t t1080 = gm_l72;
                                    uint8_t t1081 = (t1079 < t1080);
                                    if (!t1081) {
                                        goto gm_lb100;
                                    }
                                    {
                                        {
                                            {
                                                uint32_t t1082 = gm_l73;
                                                gm_l74 = t1082;
                                                unsigned __int128 t1083 = gm_l71;
                                                unsigned __int128 t1084 = gm_l70;
                                                uint32_t t1085 = gm_l74;
                                                uint32_t r1086; bool o1087 = __builtin_mul_overflow(t1085, ((uint32_t)8ULL), &r1086);
                                                if (o1087) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 58);
                                                uint32_t t1088 = r1086;
                                                unsigned __int128 t1089 = (unsigned __int128)t1088;
                                                unsigned __int128 n1090 = (unsigned __int128)t1089;
                                                if (n1090 >= 128) gm_trap_shift((uint64_t)n1090, "u128", gm_srcl8pn2pjtmj6k, 377, 48);
                                                unsigned __int128 t1091 = (unsigned __int128)(t1084 >> (int)n1090);
                                                unsigned __int128 t1092 = (unsigned __int128)(t1091 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 255ULL)));
                                                uint32_t t1093 = gm_l69;
                                                if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 83);
                                                uint32_t t1094 = (uint32_t)(t1093 / ((uint32_t)8ULL));
                                                uint32_t r1095; bool o1096 = __builtin_sub_overflow(t1094, ((uint32_t)1ULL), &r1095);
                                                if (o1096) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                                uint32_t t1097 = r1095;
                                                uint32_t t1098 = gm_l74;
                                                uint32_t r1099; bool o1100 = __builtin_sub_overflow(t1097, t1098, &r1099);
                                                if (o1100) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                                uint32_t t1101 = r1099;
                                                uint32_t r1102; bool o1103 = __builtin_mul_overflow(t1101, ((uint32_t)8ULL), &r1102);
                                                if (o1103) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 82);
                                                uint32_t t1104 = r1102;
                                                unsigned __int128 t1105 = (unsigned __int128)t1104;
                                                unsigned __int128 n1106 = (unsigned __int128)t1105;
                                                if (n1106 >= 128) gm_trap_shift((uint64_t)n1106, "u128", gm_srcl8pn2pjtmj6k, 377, 46);
                                                unsigned __int128 r1107 = (unsigned __int128)((unsigned __int128)t1092 << (int)n1106);
                                                bool o1108 = (unsigned __int128)(r1107 >> (int)n1106) != t1092;
                                                if (o1108) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 377, 46);
                                                unsigned __int128 t1109 = r1107;
                                                unsigned __int128 t1110 = (unsigned __int128)(t1083 | t1109);
                                                gm_l71 = t1110;
                                                goto gm_lb103;
                                            }
gm_lb104:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 377, 21);
                                        }
gm_lb103:;
                                    }
gm_lb102:;
                                    uint32_t t1111 = gm_l73;
                                    uint32_t r1112; bool o1113 = __builtin_add_overflow(t1111, ((uint32_t)1ULL), &r1112);
                                    uint32_t t1114 = r1112;
                                    gm_l73 = t1114;
                                }
                            }
gm_lb100:;
                            goto gm_lb95;
                        }
gm_lb99:;
                        {
                            gm_l76 = ((uint32_t)0ULL);
                            uint32_t t1115 = gm_l69;
                            gm_l75 = t1115;
                            {
                                for (;;) {
                                    uint32_t t1116 = gm_l76;
                                    uint32_t t1117 = gm_l75;
                                    uint8_t t1118 = (t1116 < t1117);
                                    if (!t1118) {
                                        goto gm_lb106;
                                    }
                                    {
                                        {
                                            {
                                                uint32_t t1119 = gm_l76;
                                                gm_l77 = t1119;
                                                unsigned __int128 t1120 = gm_l70;
                                                uint32_t t1121 = gm_l77;
                                                unsigned __int128 t1122 = (unsigned __int128)t1121;
                                                unsigned __int128 n1123 = (unsigned __int128)t1122;
                                                if (n1123 >= 128) gm_trap_shift((uint64_t)n1123, "u128", gm_srcl8pn2pjtmj6k, 381, 29);
                                                unsigned __int128 t1124 = (unsigned __int128)(t1120 >> (int)n1123);
                                                unsigned __int128 t1125 = (unsigned __int128)(t1124 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
                                                uint8_t t1126 = ((t1125 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
                                                if (t1126) {
                                                    unsigned __int128 t1127 = gm_l71;
                                                    uint32_t t1128 = gm_l69;
                                                    uint32_t r1129; bool o1130 = __builtin_sub_overflow(t1128, ((uint32_t)1ULL), &r1129);
                                                    if (o1130) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                                    uint32_t t1131 = r1129;
                                                    uint32_t t1132 = gm_l77;
                                                    uint32_t r1133; bool o1134 = __builtin_sub_overflow(t1131, t1132, &r1133);
                                                    if (o1134) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                                    uint32_t t1135 = r1133;
                                                    unsigned __int128 t1136 = (unsigned __int128)t1135;
                                                    unsigned __int128 n1137 = (unsigned __int128)t1136;
                                                    if (n1137 >= 128) gm_trap_shift((uint64_t)n1137, "u128", gm_srcl8pn2pjtmj6k, 381, 59);
                                                    unsigned __int128 r1138 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n1137);
                                                    bool o1139 = (unsigned __int128)(r1138 >> (int)n1137) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                                                    if (o1139) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 381, 59);
                                                    unsigned __int128 t1140 = r1138;
                                                    unsigned __int128 t1141 = (unsigned __int128)(t1127 | t1140);
                                                    gm_l71 = t1141;
                                                }
                                                goto gm_lb109;
                                            }
gm_lb110:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 380, 21);
                                        }
gm_lb109:;
                                    }
gm_lb108:;
                                    uint32_t t1142 = gm_l76;
                                    uint32_t r1143; bool o1144 = __builtin_add_overflow(t1142, ((uint32_t)1ULL), &r1143);
                                    uint32_t t1145 = r1143;
                                    gm_l76 = t1145;
                                }
                            }
gm_lb106:;
                            goto gm_lb95;
                        }
gm_lb105:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 362, 13);
                    }
gm_lb95:;
                    void * t1146 = gm_l0;
                    gm_b16_8 t1147 = gm_l7;
                    uint8_t t1148 = GM_LD(uint8_t, ((uint8_t *)&gm_l68));
                    uint32_t t1149 = gm_l69;
                    unsigned __int128 t1150 = gm_l71;
                    unsigned __int128 t1151 = gm_f_num_ext_348kmbu(t1148, t1149, t1150);
                    gm_f_put_uint_cddin99(t1146, t1147, t1151);
                    goto gm_lb1;
                }
gm_lb88:;
            }
            {
                {
                    uint8_t t1152 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t1153 = (uint8_t)(t1152 == ((uint8_t)62ULL));
                    if (!t1153) {
                        goto gm_lb111;
                    }
                    void * t1154 = gm_l0;
                    gm_t88rcucg1b7uk t1155 = gm_l5;
                    gm_tg5lql6itufk7 t1156 = gm_f_int_info_ulcevdv(t1154, GM_CONV(gm_csguankko4lmhd, t1155));
                    gm_l129 = t1156;
                    uint8_t t1157 = (uint8_t)((((uint8_t *)&gm_l129))[8] != 0);
                    if (t1157) {
                        gm_ttovhfee8mf28 t1158 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l129));
                        gm_l130 = t1158;
                    } else {
                        void * t1159 = gm_l0;
                        gm_tl874t2atnlh3 t1160 = gm_l1;
                        gm_b16_8 t1161;
                        GM_ST(void *, (uint8_t *)&t1161, (void *)(gm_sartclcojin4f));
                        GM_ST(uintptr_t, (uint8_t *)&t1161 + 8, (uintptr_t)(51));
                        gm_t76123gfet73g t1162;
                        gm_f_trap_qh84l9d((void *)&t1162, (void *)NULL, cx, NULL, t1159, t1160, t1161);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t1163;
                        GM_ST(uint8_t, (uint8_t *)&t1163 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1163 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1163 + 4, 0);
                        ((uint8_t *)&t1163)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1163 + 8), t1162);
                        memmove(gm_out, ((uint8_t *)&t1163), 32);
                        goto gm_fdone;
                    }
                    gm_ttovhfee8mf28 t1164 = gm_l130;
                    gm_l78 = t1164;
                    uint8_t t1165 = GM_LD(uint8_t, ((uint8_t *)&gm_l78));
                    void * t1166 = gm_l0;
                    gm_t88rcucg1b7uk t1167 = gm_l5;
                    void * t1168 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t1169 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1169)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1169, gm_srcl8pn2pjtmj6k, 389, 56);
                    gm_b16_8 t1170 = GM_LD(gm_b16_8, (((uint8_t *)t1168) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t1171 = GM_LD(gm_b16_8, ((uint8_t *)&t1170));
                    unsigned __int128 t1172 = gm_f_get_int_pg7p4gc(t1166, GM_CONV(gm_csguankko4lmhd, t1167), t1171);
                    gm_f_num_val_i3upeur((void *)&gm_l79, t1165, t1172);
                    unsigned __int128 t1173 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l79) + 16));
                    uint64_t t1174 = (uint64_t)t1173;
                    uint64_t t1175 = (uint64_t)(((uint64_t)14695981039346656037ULL) ^ t1174);
                    gm_l80 = t1175;
                    void * t1176 = gm_l0;
                    gm_tl874t2atnlh3 t1177 = gm_l1;
                    gm_t88rcucg1b7uk t1178 = gm_l4;
                    void * t1179 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t1180 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t1180)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t1180, gm_srcl8pn2pjtmj6k, 391, 38);
                    gm_b16_8 t1181 = GM_LD(gm_b16_8, (((uint8_t *)t1179) + (uintptr_t)((uint64_t)0ULL) * 16));
                    void * t1182 = (void *)((uint8_t *)&gm_l80);
                    gm_f_hash_val_nr2e99r((void *)&gm_l131, (void *)NULL, cx, NULL, t1176, t1177, t1178, t1181, t1182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1183 = (uint8_t)!((((uint8_t *)&gm_l131))[0] != 0);
                    if (t1183) {
                    } else {
                        gm_t76123gfet73g t1184 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l131) + 8));
                        gm_tcfa6l7reoosn t1185;
                        GM_ST(uint8_t, (uint8_t *)&t1185 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1185 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1185 + 4, 0);
                        ((uint8_t *)&t1185)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1185 + 8), t1184);
                        memmove(gm_out, ((uint8_t *)&t1185), 32);
                        goto gm_fdone;
                    }
                    void * t1186 = gm_l0;
                    gm_b16_8 t1187 = gm_l7;
                    uint64_t t1188 = gm_l80;
                    unsigned __int128 t1189 = (unsigned __int128)t1188;
                    gm_f_put_uint_cddin99(t1186, t1187, t1189);
                    goto gm_lb1;
                }
gm_lb111:;
                {
                    {
                        {
                            uint8_t t1190 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1191 = (uint8_t)(t1190 == ((uint8_t)40ULL));
                            if (!t1191) {
                                goto gm_lb114;
                            }
                            goto gm_lb113;
                        }
gm_lb114:;
                        {
                            uint8_t t1192 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1193 = (uint8_t)(t1192 == ((uint8_t)41ULL));
                            if (!t1193) {
                                goto gm_lb115;
                            }
                            goto gm_lb113;
                        }
gm_lb115:;
                        {
                            uint8_t t1194 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1195 = (uint8_t)(t1194 == ((uint8_t)42ULL));
                            if (!t1195) {
                                goto gm_lb116;
                            }
                            goto gm_lb113;
                        }
gm_lb116:;
                        {
                            uint8_t t1196 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1197 = (uint8_t)(t1196 == ((uint8_t)43ULL));
                            if (!t1197) {
                                goto gm_lb117;
                            }
                            goto gm_lb113;
                        }
gm_lb117:;
                        {
                            uint8_t t1198 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1199 = (uint8_t)(t1198 == ((uint8_t)44ULL));
                            if (!t1199) {
                                goto gm_lb118;
                            }
                            goto gm_lb113;
                        }
gm_lb118:;
                        {
                            uint8_t t1200 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1201 = (uint8_t)(t1200 == ((uint8_t)45ULL));
                            if (!t1201) {
                                goto gm_lb119;
                            }
                            goto gm_lb113;
                        }
gm_lb119:;
                        {
                            uint8_t t1202 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1203 = (uint8_t)(t1202 == ((uint8_t)46ULL));
                            if (!t1203) {
                                goto gm_lb120;
                            }
                            goto gm_lb113;
                        }
gm_lb120:;
                        {
                            uint8_t t1204 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1205 = (uint8_t)(t1204 == ((uint8_t)47ULL));
                            if (!t1205) {
                                goto gm_lb121;
                            }
                            goto gm_lb113;
                        }
gm_lb121:;
                        {
                            uint8_t t1206 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1207 = (uint8_t)(t1206 == ((uint8_t)48ULL));
                            if (!t1207) {
                                goto gm_lb122;
                            }
                            goto gm_lb113;
                        }
gm_lb122:;
                        {
                            uint8_t t1208 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1209 = (uint8_t)(t1208 == ((uint8_t)49ULL));
                            if (!t1209) {
                                goto gm_lb123;
                            }
                            goto gm_lb113;
                        }
gm_lb123:;
                        {
                            uint8_t t1210 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1211 = (uint8_t)(t1210 == ((uint8_t)50ULL));
                            if (!t1211) {
                                goto gm_lb124;
                            }
                            goto gm_lb113;
                        }
gm_lb124:;
                        {
                            uint8_t t1212 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1213 = (uint8_t)(t1212 == ((uint8_t)51ULL));
                            if (!t1213) {
                                goto gm_lb125;
                            }
                            goto gm_lb113;
                        }
gm_lb125:;
                        {
                            uint8_t t1214 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1215 = (uint8_t)(t1214 == ((uint8_t)52ULL));
                            if (!t1215) {
                                goto gm_lb126;
                            }
                            goto gm_lb113;
                        }
gm_lb126:;
                        {
                            uint8_t t1216 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1217 = (uint8_t)(t1216 == ((uint8_t)53ULL));
                            if (!t1217) {
                                goto gm_lb127;
                            }
                            goto gm_lb113;
                        }
gm_lb127:;
                        {
                            uint8_t t1218 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1219 = (uint8_t)(t1218 == ((uint8_t)54ULL));
                            if (!t1219) {
                                goto gm_lb128;
                            }
                            goto gm_lb113;
                        }
gm_lb128:;
                        {
                            uint8_t t1220 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1221 = (uint8_t)(t1220 == ((uint8_t)55ULL));
                            if (!t1221) {
                                goto gm_lb129;
                            }
                            goto gm_lb113;
                        }
gm_lb129:;
                        {
                            uint8_t t1222 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1223 = (uint8_t)(t1222 == ((uint8_t)56ULL));
                            if (!t1223) {
                                goto gm_lb130;
                            }
                            goto gm_lb113;
                        }
gm_lb130:;
                        {
                            uint8_t t1224 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1225 = (uint8_t)(t1224 == ((uint8_t)57ULL));
                            if (!t1225) {
                                goto gm_lb131;
                            }
                            goto gm_lb113;
                        }
gm_lb131:;
                        goto gm_lb112;
                    }
gm_lb113:;
                    void * t1226 = gm_l0;
                    gm_tl874t2atnlh3 t1227 = gm_l1;
                    gm_tvdg94bk7aa22 t1228 = gm_l2;
                    gm_t88rcucg1b7uk t1229 = gm_l3;
                    gm_t88rcucg1b7uk t1230 = gm_l4;
                    gm_t88rcucg1b7uk t1231 = gm_l5;
                    gm_b16_8 t1232 = gm_l6;
                    gm_b16_8 t1233 = gm_l7;
                    gm_f_vector_builtin_3fn933h((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t1226, t1227, t1228, t1229, t1230, t1231, t1232, t1233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb112:;
            }
            {
                {
                    void * t1234 = gm_l0;
                    gm_tl874t2atnlh3 t1235 = gm_l1;
                    gm_tvdg94bk7aa22 t1236 = gm_l2;
                    gm_th9nl3oso747e t1237;
                    GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t1237 + 0), t1236);
                    gm_f_format_985clkf((void *)&gm_l132, (void *)NULL, cx, NULL, t1237);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1238 = (uint8_t)!((((uint8_t *)&gm_l132))[0] != 0);
                    if (!t1238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l132) + 8, gm_srcl8pn2pjtmj6k, 395, 42);
                    gm_b16_8 t1239 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l132) + 8));
                    gm_b16_8 t1240 = GM_LD(gm_b16_8, ((uint8_t *)&t1239));
                    gm_t76123gfet73g t1241;
                    gm_f_unsupported_70vvk1e((void *)&t1241, (void *)NULL, cx, NULL, t1234, t1235, t1240);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1242;
                    GM_ST(uint8_t, (uint8_t *)&t1242 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1242 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1242 + 4, 0);
                    ((uint8_t *)&t1242)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1242 + 8), t1241);
                    memmove(gm_out, ((uint8_t *)&t1242), 32);
                    goto gm_fdone;
                }
gm_lb132:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 191, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tcfa6l7reoosn t1243;
        GM_ST(uint8_t, (uint8_t *)&t1243 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t1243 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t1243 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t1243 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t1243 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t1243 + 24, 0);
        ((uint8_t *)&t1243)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t1243 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t1243), 32);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_array_ptr_to_slice_7dg3m1v F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ptr_ty */
    gm_b16_8 gm_l2; /* b */
    gm_b16_8 gm_l3; /* dst */
    uint64_t gm_l4; /* len */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    uint64_t gm_l6; /* n */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l7;
    gm_t53at80pfdus3 gm_l8;
    gm_l4 = ((uint64_t)0ULL);
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_kind_htv6093((void *)&gm_l7, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)8ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l7) + 8) + 4));
            gm_l5 = t8;
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = gm_l5;
            gm_f_kind_htv6093((void *)&gm_l8, t9, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)11ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            gm_l6 = t13;
            uint64_t t14 = gm_l6;
            gm_l4 = t14;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = gm_l0;
    gm_b16_8 t16 = gm_l3;
    void * t17 = gm_l0;
    gm_b16_8 t18 = gm_l2;
    uint64_t t19 = gm_f_ptr_of_b77b4c9(t17, t18);
    uint64_t t20 = gm_l4;
    gm_f_put_slice_ko14loi(t15, t16, t19, t20);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_coerce_bytes_9kf3u24 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* from */
    gm_t88rcucg1b7uk gm_l4; /* to */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_ttovhfee8mf28 gm_l7; /* info */
    uint32_t gm_l8; /* variant */
    gm_t88rcucg1b7uk gm_l9; /* derr */
    gm_b16_8 gm_l10; /* lifted */
    gm_t88rcucg1b7uk gm_l11; /* src_in */
    gm_t88rcucg1b7uk gm_l12; /* dst_in */
    gm_b16_8 gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    gm_b16_8 gm_l15; /* sl */
    GM_STACK_CHECK(&F, 2053u);
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
    gm_tg5lql6itufk7 gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_t53at80pfdus3 gm_l19;
    uint8_t gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_tgglga8ro3isc gm_l22;
    gm_t53at80pfdus3 gm_l23;
    uint8_t gm_l24;
    gm_t53at80pfdus3 gm_l25;
    uint8_t gm_l26;
    gm_t53at80pfdus3 gm_l27;
    {
        {
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                    if (!t37) {
                        goto gm_lb2;
                    }
                    {
                        {
                            void * t38 = gm_l0;
                            gm_t88rcucg1b7uk t39 = gm_l4;
                            gm_tg5lql6itufk7 t40 = gm_f_int_info_ulcevdv(t38, GM_CONV(gm_csguankko4lmhd, t39));
                            gm_l16 = t40;
                            uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                            if (!t41) {
                                goto gm_lb4;
                            }
                            gm_ttovhfee8mf28 t42 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l16));
                            gm_l7 = t42;
                            void * t43 = gm_l0;
                            gm_b16_8 t44 = gm_l6;
                            uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                            uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                            void * t47 = gm_l0;
                            gm_t88rcucg1b7uk t48 = gm_l3;
                            gm_b16_8 t49 = gm_l5;
                            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                            unsigned __int128 t51 = gm_f_get_int_pg7p4gc(t47, GM_CONV(gm_csguankko4lmhd, t48), t50);
                            unsigned __int128 t52 = gm_f_num_ext_348kmbu(t45, t46, t51);
                            gm_f_put_uint_cddin99(t43, t44, t52);
                            goto gm_lb3;
                        }
gm_lb4:;
                        void * t53 = gm_l0;
                        gm_b16_8 t54 = gm_l6;
                        void * t55 = gm_l0;
                        gm_t88rcucg1b7uk t56 = gm_l3;
                        gm_b16_8 t57 = gm_l5;
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                        unsigned __int128 t59 = gm_f_get_int_pg7p4gc(t55, GM_CONV(gm_csguankko4lmhd, t56), t58);
                        gm_f_put_uint_cddin99(t53, t54, t59);
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)1ULL));
                    if (!t61) {
                        goto gm_lb5;
                    }
                    void * t62 = gm_l0;
                    gm_t88rcucg1b7uk t63 = gm_l3;
                    gm_f_kind_htv6093((void *)&gm_l19, t62, GM_CONV(gm_csguankko4lmhd, t63));
                    {
                        {
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)3ULL));
                            if (!t65) {
                                goto gm_lb7;
                            }
                            gm_l18 = ((uint8_t)1);
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            gm_l18 = ((uint8_t)0);
                            goto gm_lb6;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 21);
                    }
gm_lb6:;
                    uint8_t t66 = gm_l18;
                    uint8_t t67 = (uint8_t)!t66;
                    if (t67) {
                        gm_l17 = ((uint8_t)1);
                    } else {
                        void * t68 = gm_l0;
                        gm_t88rcucg1b7uk t69 = gm_l4;
                        gm_f_kind_htv6093((void *)&gm_l21, t68, GM_CONV(gm_csguankko4lmhd, t69));
                        {
                            {
                                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                uint8_t t71 = (uint8_t)(t70 == ((uint8_t)3ULL));
                                if (!t71) {
                                    goto gm_lb10;
                                }
                                gm_l20 = ((uint8_t)1);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                gm_l20 = ((uint8_t)0);
                                goto gm_lb9;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 56);
                        }
gm_lb9:;
                        uint8_t t72 = gm_l20;
                        uint8_t t73 = (uint8_t)!t72;
                        gm_l17 = t73;
                    }
                    uint8_t t74 = gm_l17;
                    if (t74) {
                        void * t75 = gm_l0;
                        gm_tl874t2atnlh3 t76 = gm_l1;
                        gm_b16_8 t77;
                        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sd4ibvv64si9n));
                        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(44));
                        gm_t76123gfet73g t78;
                        gm_f_trap_qh84l9d((void *)&t78, (void *)NULL, cx, NULL, t75, t76, t77);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t79;
                        GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                        ((uint8_t *)&t79)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t79 + 8), t78);
                        memmove(gm_out, ((uint8_t *)&t79), 32);
                        goto gm_fdone;
                    }
                    void * t80 = gm_l0;
                    gm_t88rcucg1b7uk t81 = gm_l4;
                    void * t82 = gm_l0;
                    gm_t88rcucg1b7uk t83 = gm_l3;
                    gm_b16_8 t84 = gm_l5;
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                    double t86 = gm_f_get_float_grmhlm4(t82, GM_CONV(gm_csguankko4lmhd, t83), t85);
                    gm_b16_8 t87 = gm_l6;
                    gm_f_put_float_fiad72d(t80, GM_CONV(gm_csguankko4lmhd, t81), t86, t87);
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)2ULL));
                    if (!t89) {
                        goto gm_lb12;
                    }
                    void * t90 = gm_l0;
                    gm_t88rcucg1b7uk t91 = gm_l4;
                    gm_b16_8 t92 = gm_l6;
                    gm_b16_8 t93 = gm_l5;
                    gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                    gm_b0_1 t95 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t90, t91, t92, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)3ULL));
                    if (!t97) {
                        goto gm_lb13;
                    }
                    void * t98 = gm_l0;
                    gm_t88rcucg1b7uk t99 = gm_l4;
                    gm_b16_8 t100 = gm_l6;
                    gm_b16_8 t101 = gm_l5;
                    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                    gm_b0_1 t103 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t98, t99, t100, ((uint8_t)0), t102);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t105 = (uint8_t)(t104 == ((uint8_t)4ULL));
                    if (!t105) {
                        goto gm_lb14;
                    }
                    uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                    gm_l8 = t106;
                    void * t107 = gm_l0;
                    gm_t88rcucg1b7uk t108 = gm_l3;
                    gm_b16_8 t109 = gm_l5;
                    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                    uint8_t t111 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t107, t108, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t111) {
                        void * t112 = gm_l0;
                        gm_t88rcucg1b7uk t113 = gm_l4;
                        gm_tgglga8ro3isc t114 = gm_f_res_types_pg7rvck(t112, GM_CONV(gm_csguankko4lmhd, t113));
                        gm_l22 = t114;
                        gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l22));
                        gm_l9 = t115;
                        void * t116 = gm_l0;
                        void * t117 = gm_l0;
                        gm_t88rcucg1b7uk t118 = gm_l9;
                        uint64_t t119 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t117, t118);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t120 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t116, t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t120;
                        void * t121 = gm_l0;
                        gm_t88rcucg1b7uk t122 = gm_l9;
                        gm_b16_8 t123 = gm_l10;
                        uint32_t t124 = gm_l8;
                        void * t125 = gm_l0;
                        gm_t88rcucg1b7uk t126 = gm_l3;
                        gm_b16_8 t127 = gm_l5;
                        gm_b16_8 t128 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t125, t126, t127);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                        gm_b0_1 t130 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t121, t122, t123, t124, t129);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t131 = gm_l0;
                        gm_t88rcucg1b7uk t132 = gm_l4;
                        gm_b16_8 t133 = gm_l6;
                        gm_b16_8 t134 = gm_l10;
                        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                        gm_b0_1 t136 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t131, t132, t133, ((uint8_t)1), t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    } else {
                        void * t137 = gm_l0;
                        gm_t88rcucg1b7uk t138 = gm_l4;
                        gm_b16_8 t139 = gm_l6;
                        void * t140 = gm_l0;
                        gm_t88rcucg1b7uk t141 = gm_l3;
                        gm_b16_8 t142 = gm_l5;
                        gm_b16_8 t143 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t140, t141, t142);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                        gm_b0_1 t145 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t137, t138, t139, ((uint8_t)0), t144);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t147 = (uint8_t)(t146 == ((uint8_t)7ULL));
                    if (!t147) {
                        goto gm_lb15;
                    }
                    void * t148 = gm_l0;
                    gm_t88rcucg1b7uk t149 = gm_l3;
                    gm_b16_8 t150 = gm_l5;
                    gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                    gm_b16_8 t152 = gm_l6;
                    gm_b0_1 t153 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t148, t149, t151, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t155 = (uint8_t)(t154 == ((uint8_t)11ULL));
                    if (!t155) {
                        goto gm_lb16;
                    }
                    void * t156 = gm_l0;
                    gm_t88rcucg1b7uk t157 = gm_l3;
                    gm_b16_8 t158 = gm_l5;
                    gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                    uint8_t t160 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t156, t157, t159);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t161 = (uint8_t)!t160;
                    if (t161) {
                        gm_b16_8 t162 = gm_l6;
                        gm_f_fill_abmf5g5(t162, ((uint8_t)0ULL));
                        gm_tcfa6l7reoosn t163;
                        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t163 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t163 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t163 + 24, 0);
                        ((uint8_t *)&t163)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t163 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t163), 32);
                        goto gm_fdone;
                    }
                    void * t164 = gm_l0;
                    gm_t88rcucg1b7uk t165 = gm_l3;
                    gm_t88rcucg1b7uk t166 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t164, GM_CONV(gm_csguankko4lmhd, t165)));
                    gm_l11 = t166;
                    void * t167 = gm_l0;
                    gm_t88rcucg1b7uk t168 = gm_l4;
                    gm_t88rcucg1b7uk t169 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t167, GM_CONV(gm_csguankko4lmhd, t168)));
                    gm_l12 = t169;
                    void * t170 = gm_l0;
                    gm_t88rcucg1b7uk t171 = gm_l3;
                    gm_b16_8 t172 = gm_l5;
                    gm_b16_8 t173 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t170, t171, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t173;
                    {
                        {
                            void * t174 = gm_l0;
                            gm_t88rcucg1b7uk t175 = gm_l11;
                            gm_f_kind_htv6093((void *)&gm_l23, t174, GM_CONV(gm_csguankko4lmhd, t175));
                            uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t177 = (uint8_t)(t176 == ((uint8_t)8ULL));
                            if (!t177) {
                                goto gm_lb18;
                            }
                            gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l23) + 8) + 4));
                            gm_l14 = t178;
                            void * t179 = gm_l0;
                            gm_t88rcucg1b7uk t180 = gm_l12;
                            gm_f_kind_htv6093((void *)&gm_l25, t179, GM_CONV(gm_csguankko4lmhd, t180));
                            {
                                {
                                    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)10ULL));
                                    if (!t182) {
                                        goto gm_lb20;
                                    }
                                    gm_l24 = ((uint8_t)1);
                                    goto gm_lb19;
                                }
gm_lb20:;
                                {
                                    gm_l24 = ((uint8_t)0);
                                    goto gm_lb19;
                                }
gm_lb21:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 55);
                            }
gm_lb19:;
                            uint8_t t183 = gm_l24;
                            if (!t183) {
                                goto gm_lb18;
                            }
                            void * t184 = gm_l0;
                            gm_t88rcucg1b7uk t185 = gm_l14;
                            gm_f_kind_htv6093((void *)&gm_l27, t184, GM_CONV(gm_csguankko4lmhd, t185));
                            {
                                {
                                    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                                    uint8_t t187 = (uint8_t)(t186 == ((uint8_t)11ULL));
                                    if (!t187) {
                                        goto gm_lb23;
                                    }
                                    gm_l26 = ((uint8_t)1);
                                    goto gm_lb22;
                                }
gm_lb23:;
                                {
                                    gm_l26 = ((uint8_t)0);
                                    goto gm_lb22;
                                }
gm_lb24:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 90);
                            }
gm_lb22:;
                            uint8_t t188 = gm_l26;
                            if (!t188) {
                                goto gm_lb18;
                            }
                            void * t189 = gm_l0;
                            void * t190 = gm_l0;
                            gm_t88rcucg1b7uk t191 = gm_l12;
                            uint64_t t192 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t190, t191);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t193 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t189, t192);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l15 = t193;
                            void * t194 = gm_l0;
                            gm_t88rcucg1b7uk t195 = gm_l11;
                            gm_b16_8 t196 = gm_l13;
                            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                            gm_b16_8 t198 = gm_l15;
                            gm_b0_1 t199 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t194, t195, t197, t198);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t200 = gm_l0;
                            gm_t88rcucg1b7uk t201 = gm_l4;
                            gm_b16_8 t202 = gm_l6;
                            gm_b16_8 t203 = gm_l15;
                            gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                            gm_b0_1 t205 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t200, t201, t202, t204);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                        void * t206 = gm_l0;
                        gm_t88rcucg1b7uk t207 = gm_l4;
                        gm_b16_8 t208 = gm_l6;
                        gm_b16_8 t209 = gm_l13;
                        gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
                        gm_b0_1 t211 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t206, t207, t208, t210);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
gm_lb17:;
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    {
                        {
                            uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t213 = (uint8_t)(t212 == ((uint8_t)6ULL));
                            if (!t213) {
                                goto gm_lb27;
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        {
                            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t215 = (uint8_t)(t214 == ((uint8_t)8ULL));
                            if (!t215) {
                                goto gm_lb28;
                            }
                            goto gm_lb26;
                        }
gm_lb28:;
                        {
                            uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t217 = (uint8_t)(t216 == ((uint8_t)10ULL));
                            if (!t217) {
                                goto gm_lb29;
                            }
                            goto gm_lb26;
                        }
gm_lb29:;
                        goto gm_lb25;
                    }
gm_lb26:;
                    gm_b16_8 t218 = gm_l6;
                    gm_b16_8 t219 = gm_l5;
                    gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                    gm_f_copy_from_f7j6p05(t218, t220);
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    {
                        {
                            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t222 = (uint8_t)(t221 == ((uint8_t)5ULL));
                            if (!t222) {
                                goto gm_lb32;
                            }
                            goto gm_lb31;
                        }
gm_lb32:;
                        {
                            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t224 = (uint8_t)(t223 == ((uint8_t)9ULL));
                            if (!t224) {
                                goto gm_lb33;
                            }
                            goto gm_lb31;
                        }
gm_lb33:;
                        {
                            uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t226 = (uint8_t)(t225 == ((uint8_t)12ULL));
                            if (!t226) {
                                goto gm_lb34;
                            }
                            goto gm_lb31;
                        }
gm_lb34:;
                        goto gm_lb30;
                    }
gm_lb31:;
                    void * t227 = gm_l0;
                    gm_tl874t2atnlh3 t228 = gm_l1;
                    gm_b16_8 t229;
                    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_sbjj41cb5lph3));
                    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(59));
                    gm_t76123gfet73g t230;
                    gm_f_trap_qh84l9d((void *)&t230, (void *)NULL, cx, NULL, t227, t228, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t231;
                    GM_ST(uint8_t, (uint8_t *)&t231 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t231 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t231 + 4, 0);
                    ((uint8_t *)&t231)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t231 + 8), t230);
                    memmove(gm_out, ((uint8_t *)&t231), 32);
                    goto gm_fdone;
                }
gm_lb30:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 9, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tcfa6l7reoosn t232;
        GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t232 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t232 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t232 + 24, 0);
        ((uint8_t *)&t232)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t232 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t232), 32);
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
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_convert_is6an2k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* src_ty */
    gm_t88rcucg1b7uk gm_l4; /* dst_ty */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_t88rcucg1b7uk gm_l7; /* target */
    uint8_t gm_l8; /* is_float */
    gm_t86h6hhm6d76n gm_l9; /* sv */
    double gm_l10; /* fv */
    gm_ttovhfee8mf28 gm_l11; /* info */
    gm_tcmkompbl09il gm_l12; /* u */
    gm_ttovhfee8mf28 gm_l13; /* info */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_ttovhfee8mf28 gm_l15; /* info */
    gm_ttovhfee8mf28 gm_l16; /* tinfo */
    uint8_t gm_l17; /* signed */
    uint32_t gm_l18; /* bits */
    gm_t51b2en4qp02s gm_l19; /* pb */
    uint64_t gm_l20; /* n */
    uint8_t gm_l21; /* finite */
    gm_t86h6hhm6d76n gm_l22; /* n */
    gm_t51b2en4qp02s gm_l23; /* pb */
    uint64_t gm_l24; /* sz */
    uint8_t gm_l25; /* neg */
    gm_t86h6hhm6d76n gm_l26; /* n */
    gm_t86h6hhm6d76n gm_l27; /* n */
    uint16_t gm_l28; /* w */
    double gm_l29; /* x */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_ttovhfee8mf28 gm_l31; /* info */
    GM_STACK_CHECK(&F, 3860u);
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
    gm_t88rcucg1b7uk gm_l32;
    gm_t53at80pfdus3 gm_l33;
    gm_tg5lql6itufk7 gm_l34;
    unsigned __int128 gm_l35;
    gm_t57euh612ligr gm_l36;
    gm_tg5lql6itufk7 gm_l37;
    gm_tkn42u7na3t81 gm_l38;
    gm_tg5lql6itufk7 gm_l39;
    gm_tabb710d9mq2e gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tg5lql6itufk7 gm_l42;
    uint8_t gm_l43;
    gm_tsp9ul70rnkg6 gm_l44;
    gm_t86h6hhm6d76n gm_l45;
    gm_tsp9ul70rnkg6 gm_l46;
    gm_t86h6hhm6d76n gm_l47;
    gm_tsp9ul70rnkg6 gm_l48;
    gm_t53at80pfdus3 gm_l49;
    double gm_l50;
    gm_tkn42u7na3t81 gm_l51;
    gm_tg5lql6itufk7 gm_l52;
    gm_tabb710d9mq2e gm_l53;
    {
        gm_tvdg94bk7aa22 t24 = gm_l2;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        uint8_t t26 = (uint8_t)((t25 == ((uint8_t)5ULL)));
        gm_tvdg94bk7aa22 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)5ULL));
        if (t26) {
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l4;
            gm_t88rcucg1b7uk t30 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t28, GM_CONV(gm_csguankko4lmhd, t29)));
            gm_l32 = t30;
        } else {
            gm_t88rcucg1b7uk t31 = gm_l4;
            gm_l32 = t31;
        }
        gm_t88rcucg1b7uk t32 = gm_l32;
        gm_l7 = t32;
        gm_l8 = ((uint8_t)0);
        gm_t86h6hhm6d76n t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t33 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0));
        GM_ST(unsigned __int128, ((uint8_t *)&t33 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
        gm_l9 = t33;
        gm_l10 = ((double)0e0);
        void * t34 = gm_l0;
        gm_t88rcucg1b7uk t35 = gm_l3;
        gm_f_kind_htv6093((void *)&gm_l33, t34, GM_CONV(gm_csguankko4lmhd, t35));
        {
            {
                {
                    {
                        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                        uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                        if (!t37) {
                            goto gm_lb4;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                        uint8_t t39 = (uint8_t)(t38 == ((uint8_t)1ULL));
                        if (!t39) {
                            goto gm_lb5;
                        }
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                        uint8_t t41 = (uint8_t)(t40 == ((uint8_t)2ULL));
                        if (!t41) {
                            goto gm_lb6;
                        }
                        goto gm_lb3;
                    }
gm_lb6:;
                    goto gm_lb2;
                }
gm_lb3:;
                void * t42 = gm_l0;
                gm_t88rcucg1b7uk t43 = gm_l3;
                gm_tg5lql6itufk7 t44 = gm_f_int_info_ulcevdv(t42, GM_CONV(gm_csguankko4lmhd, t43));
                gm_l34 = t44;
                uint8_t t45 = (uint8_t)((((uint8_t *)&gm_l34))[8] != 0);
                if (!t45) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 75, 21);
                gm_ttovhfee8mf28 t46 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l34));
                gm_l11 = t46;
                uint8_t t47 = GM_LD(uint8_t, ((uint8_t *)&gm_l11));
                void * t48 = gm_l0;
                gm_t88rcucg1b7uk t49 = gm_l3;
                gm_b16_8 t50 = gm_l5;
                unsigned __int128 t51 = gm_f_get_int_pg7p4gc(t48, GM_CONV(gm_csguankko4lmhd, t49), t50);
                gm_f_num_val_i3upeur((void *)&gm_l9, t47, t51);
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                uint8_t t53 = (uint8_t)(t52 == ((uint8_t)3ULL));
                if (!t53) {
                    goto gm_lb7;
                }
                gm_l8 = ((uint8_t)1);
                void * t54 = gm_l0;
                gm_t88rcucg1b7uk t55 = gm_l3;
                gm_b16_8 t56 = gm_l5;
                double t57 = gm_f_get_float_grmhlm4(t54, GM_CONV(gm_csguankko4lmhd, t55), t56);
                gm_l10 = t57;
                goto gm_lb1;
            }
gm_lb7:;
            {
                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                uint8_t t59 = (uint8_t)(t58 == ((uint8_t)4ULL));
                if (!t59) {
                    goto gm_lb8;
                }
                void * t60 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t61, gm_srcl8pn2pjtmj6k, 82, 32);
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t60) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t63 = ((t62 == ((uint8_t)0ULL)));
                if (t63) {
                    gm_l35 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                } else {
                    gm_l35 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                }
                unsigned __int128 t64 = gm_l35;
                GM_ST(unsigned __int128, (((uint8_t *)&gm_l9) + 16), t64);
                goto gm_lb1;
            }
gm_lb8:;
            {
                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                uint8_t t66 = (uint8_t)(t65 == ((uint8_t)17ULL));
                if (!t66) {
                    goto gm_lb9;
                }
                {
                    {
                        void * t67 = gm_l0;
                        gm_t88rcucg1b7uk t68 = gm_l3;
                        gm_f_union_def_h8mvf4t((void *)&gm_l36, t67, GM_CONV(gm_csguankko4lmhd, t68));
                        uint8_t t69 = (uint8_t)((((uint8_t *)&gm_l36))[24] != 0);
                        if (!t69) {
                            goto gm_lb11;
                        }
                        memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l36), 24);
                        void * t70 = gm_l0;
                        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                        gm_tg5lql6itufk7 t72 = gm_f_int_info_ulcevdv(t70, GM_CONV(gm_csguankko4lmhd, t71));
                        gm_l37 = t72;
                        uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l37))[8] != 0);
                        if (!t73) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 85, 25);
                        gm_ttovhfee8mf28 t74 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l37));
                        gm_l13 = t74;
                        uint8_t t75 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
                        void * t76 = gm_l0;
                        gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                        void * t78 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        void * t80 = gm_l0;
                        gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                        uint64_t t82 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t80, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t83 = gm_f_us_cuv01fp(t82);
                        uint64_t t84 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t85 = (uintptr_t)t83;
                        if (t84 > t85 || t85 > (uintptr_t)t79) gm_trap_slice(t84, t85, t79, gm_srcl8pn2pjtmj6k, 86, 60);
                        uint64_t t86 = ((uintptr_t)t83) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t87;
                        GM_ST(void *, (uint8_t *)&t87, (void *)(((uint8_t *)t78 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(t86));
                        unsigned __int128 t88 = gm_f_get_int_pg7p4gc(t76, GM_CONV(gm_csguankko4lmhd, t77), t87);
                        gm_f_num_val_i3upeur((void *)&gm_l9, t75, t88);
                        goto gm_lb10;
                    }
gm_lb11:;
                    {
                        {
                            void * t89 = gm_l0;
                            gm_t88rcucg1b7uk t90 = gm_l3;
                            gm_tkn42u7na3t81 t91 = gm_f_bits_struct_ty_22ue20s(t89, GM_CONV(gm_csguankko4lmhd, t90));
                            gm_l38 = t91;
                            uint8_t t92 = (uint8_t)((((uint8_t *)&gm_l38))[4] != 0);
                            if (!t92) {
                                goto gm_lb13;
                            }
                            gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l38));
                            gm_l14 = t93;
                            void * t94 = gm_l0;
                            gm_t88rcucg1b7uk t95 = gm_l14;
                            gm_tg5lql6itufk7 t96 = gm_f_int_info_ulcevdv(t94, GM_CONV(gm_csguankko4lmhd, t95));
                            gm_l39 = t96;
                            uint8_t t97 = (uint8_t)((((uint8_t *)&gm_l39))[8] != 0);
                            if (!t97) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 88, 25);
                            gm_ttovhfee8mf28 t98 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l39));
                            gm_l15 = t98;
                            void * t99 = gm_l0;
                            void * t100 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            void * t102 = gm_l0;
                            gm_t88rcucg1b7uk t103 = gm_l14;
                            uint64_t t104 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t102, t103);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t105 = gm_f_us_cuv01fp(t104);
                            uint64_t t106 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t107 = (uintptr_t)t105;
                            if (t106 > t107 || t107 > (uintptr_t)t101) gm_trap_slice(t106, t107, t101, gm_srcl8pn2pjtmj6k, 89, 57);
                            uint64_t t108 = ((uintptr_t)t105) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t109;
                            GM_ST(void *, (uint8_t *)&t109, (void *)(((uint8_t *)t100 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(t108));
                            unsigned __int128 t110 = gm_f_get_uint_bq8c4b7(t99, t109);
                            uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 4));
                            unsigned __int128 t112 = gm_f_num_mask_do3naah(t111);
                            unsigned __int128 t113 = (unsigned __int128)(t110 & t112);
                            gm_t86h6hhm6d76n t114;
                            GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t114 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0));
                            GM_ST(unsigned __int128, ((uint8_t *)&t114 + 16), t113);
                            gm_l9 = t114;
                            goto gm_lb12;
                        }
gm_lb13:;
                        void * t115 = gm_l0;
                        gm_tl874t2atnlh3 t116 = gm_l1;
                        void * t117 = gm_l0;
                        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 8));
                        gm_t88rcucg1b7uk t119 = gm_l3;
                        gm_b16_8 t120 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t118, t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t121;
                        GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t120);
                        gm_f_format_pe0ujmq((void *)&gm_l40, (void *)NULL, cx, NULL, t121);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                        if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_srcl8pn2pjtmj6k, 91, 38);
                        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                        gm_t76123gfet73g t125;
                        gm_f_trap_qh84l9d((void *)&t125, (void *)NULL, cx, NULL, t115, t116, t124);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t126;
                        GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                        ((uint8_t *)&t126)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t126 + 8), t125);
                        memmove(gm_out, ((uint8_t *)&t126), 32);
                        goto gm_fdone;
                    }
gm_lb12:;
                }
gm_lb10:;
                goto gm_lb1;
            }
gm_lb9:;
            {
                void * t127 = gm_l0;
                gm_tl874t2atnlh3 t128 = gm_l1;
                void * t129 = gm_l0;
                void * t130 = GM_LD(void *, (((uint8_t *)t129) + 8));
                gm_t88rcucg1b7uk t131 = gm_l3;
                gm_b16_8 t132 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t130, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t133;
                GM_ST(gm_b16_8, ((uint8_t *)&t133 + 0), t132);
                gm_f_format_pe0ujmq((void *)&gm_l41, (void *)NULL, cx, NULL, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcl8pn2pjtmj6k, 94, 35);
                gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                gm_t76123gfet73g t137;
                gm_f_trap_qh84l9d((void *)&t137, (void *)NULL, cx, NULL, t127, t128, t136);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t138;
                GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                ((uint8_t *)&t138)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t138 + 8), t137);
                memmove(gm_out, ((uint8_t *)&t138), 32);
                goto gm_fdone;
            }
gm_lb14:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 73, 5);
        }
gm_lb1:;
    }
    {
        {
            {
                void * t139 = gm_l0;
                gm_t88rcucg1b7uk t140 = gm_l7;
                gm_tg5lql6itufk7 t141 = gm_f_int_info_ulcevdv(t139, GM_CONV(gm_csguankko4lmhd, t140));
                gm_l42 = t141;
                uint8_t t142 = (uint8_t)((((uint8_t *)&gm_l42))[8] != 0);
                if (!t142) {
                    goto gm_lb16;
                }
                gm_ttovhfee8mf28 t143 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l42));
                gm_l16 = t143;
                uint8_t t144 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
                gm_l17 = t144;
                uint32_t t145 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 4));
                gm_l18 = t145;
                uint8_t t146 = gm_l8;
                uint8_t t147 = (uint8_t)!t146;
                if (t147) {
                    gm_tvdg94bk7aa22 t148 = gm_l2;
                    uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t148) + 0));
                    uint8_t t150 = (uint8_t)((t149 == ((uint8_t)5ULL)));
                    gm_tvdg94bk7aa22 t151;
                    GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t151 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)5ULL));
                    if (t150) {
                        uint8_t t152 = gm_l17;
                        uint32_t t153 = gm_l18;
                        uint8_t t154 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t152, t153);
                        if (t154) {
                            void * t155 = gm_l0;
                            gm_t88rcucg1b7uk t156 = gm_l7;
                            uint64_t t157 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t155, t156);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t158 = gm_f_us_cuv01fp(t157);
                            gm_l20 = t158;
                            void * t159 = gm_l0;
                            void * t160 = (void *)((uint8_t *)&gm_l19);
                            uint64_t t161 = gm_l20;
                            uint64_t t162 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t163 = (uintptr_t)t161;
                            if (t162 > t163 || t163 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t162, t163, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 104, 32);
                            uint64_t t164 = ((uintptr_t)t161) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t165;
                            GM_ST(void *, (uint8_t *)&t165, (void *)(((uint8_t *)t160 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(t164));
                            uint8_t t166 = gm_l17;
                            uint32_t t167 = gm_l18;
                            unsigned __int128 t168 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                            unsigned __int128 t169 = gm_f_num_ext_348kmbu(t166, t167, t168);
                            gm_f_put_uint_cddin99(t159, t165, t169);
                            void * t170 = gm_l0;
                            gm_t88rcucg1b7uk t171 = gm_l4;
                            gm_b16_8 t172 = gm_l6;
                            void * t173 = (void *)((uint8_t *)&gm_l19);
                            uint64_t t174 = gm_l20;
                            uint64_t t175 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t176 = (uintptr_t)t174;
                            if (t175 > t176 || t176 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t175, t176, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 105, 46);
                            uint64_t t177 = ((uintptr_t)t174) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t178;
                            GM_ST(void *, (uint8_t *)&t178, (void *)(((uint8_t *)t173 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(t177));
                            gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                            gm_b0_1 t180 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t170, t171, t172, t179);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            gm_b16_8 t181 = gm_l6;
                            gm_f_fill_abmf5g5(t181, ((uint8_t)0ULL));
                        }
                    } else {
                        gm_tvdg94bk7aa22 t182 = gm_l2;
                        uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&t182) + 0));
                        uint8_t t184 = (uint8_t)((t183 == ((uint8_t)6ULL)));
                        gm_tvdg94bk7aa22 t185;
                        GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t185 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t185 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)6ULL));
                        if (t184) {
                            void * t186 = gm_l0;
                            gm_b16_8 t187 = gm_l6;
                            uint8_t t188 = gm_l17;
                            uint32_t t189 = gm_l18;
                            uint8_t t190 = gm_l17;
                            uint32_t t191 = gm_l18;
                            gm_t86h6hhm6d76n t192;
                            gm_f_num_sat_66dqcpl((void *)&t192, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t190, t191);
                            unsigned __int128 t193 = gm_f_num_bits_onl28vp(&t192);
                            unsigned __int128 t194 = gm_f_num_ext_348kmbu(t188, t189, t193);
                            gm_f_put_uint_cddin99(t186, t187, t194);
                        } else {
                            void * t195 = gm_l0;
                            gm_b16_8 t196 = gm_l6;
                            uint8_t t197 = gm_l17;
                            uint32_t t198 = gm_l18;
                            unsigned __int128 t199 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                            unsigned __int128 t200 = gm_f_num_ext_348kmbu(t197, t198, t199);
                            gm_f_put_uint_cddin99(t195, t196, t200);
                        }
                    }
                    gm_tcfa6l7reoosn t201;
                    GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t201 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t201 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t201 + 24, 0);
                    ((uint8_t *)&t201)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t201 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t201), 32);
                    goto gm_fdone;
                }
                double t202 = gm_l10;
                double t203 = gm_l10;
                double t204 = (t202 - t203);
                uint8_t t205 = ((t204 == ((double)0e0)));
                gm_l21 = t205;
                gm_tvdg94bk7aa22 t206 = gm_l2;
                uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&t206) + 0));
                uint8_t t208 = (uint8_t)((t207 == ((uint8_t)5ULL)));
                gm_tvdg94bk7aa22 t209;
                GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t209 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t209 + 0), ((uint8_t)5ULL));
                if (t208) {
                    gm_b16_8 t210 = gm_l6;
                    gm_f_fill_abmf5g5(t210, ((uint8_t)0ULL));
                    uint8_t t211 = gm_l21;
                    uint8_t t212 = (uint8_t)!t211;
                    if (t212) {
                        gm_l43 = ((uint8_t)1);
                    } else {
                        double t213 = gm_l10;
                        uint8_t t214 = gm_f_num_integral_qbts4a6(t213);
                        uint8_t t215 = (uint8_t)!t214;
                        gm_l43 = t215;
                    }
                    uint8_t t216 = gm_l43;
                    if (t216) {
                        gm_tcfa6l7reoosn t217;
                        GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t217 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t217 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t217 + 24, 0);
                        ((uint8_t *)&t217)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t217 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t217), 32);
                        goto gm_fdone;
                    }
                    double t218 = gm_l10;
                    gm_tvdg94bk7aa22 t219;
                    GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t219 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)1ULL));
                    gm_f_num_float_int_ba5kc1t((void *)&gm_l44, t218, t219);
                    uint8_t t220 = (uint8_t)((((uint8_t *)&gm_l44))[32] != 0);
                    if (t220) {
                        memmove(((uint8_t *)&gm_l45), ((uint8_t *)&gm_l44), 32);
                    } else {
                        gm_tcfa6l7reoosn t221;
                        GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t221 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t221 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t221 + 24, 0);
                        ((uint8_t *)&t221)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t221 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t221), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l22), ((uint8_t *)&gm_l45), 32);
                    uint8_t t222 = gm_l17;
                    uint32_t t223 = gm_l18;
                    uint8_t t224 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)), t222, t223);
                    uint8_t t225 = (uint8_t)!t224;
                    if (t225) {
                        gm_tcfa6l7reoosn t226;
                        GM_ST(uint8_t, (uint8_t *)&t226 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t226 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t226 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t226 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t226 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t226 + 24, 0);
                        ((uint8_t *)&t226)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t226 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t226), 32);
                        goto gm_fdone;
                    }
                    void * t227 = gm_l0;
                    gm_t88rcucg1b7uk t228 = gm_l7;
                    uint64_t t229 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t227, t228);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t230 = gm_f_us_cuv01fp(t229);
                    gm_l24 = t230;
                    void * t231 = gm_l0;
                    void * t232 = (void *)((uint8_t *)&gm_l23);
                    uint64_t t233 = gm_l24;
                    uint64_t t234 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t235 = (uintptr_t)t233;
                    if (t234 > t235 || t235 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t234, t235, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 124, 24);
                    uint64_t t236 = ((uintptr_t)t233) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t237;
                    GM_ST(void *, (uint8_t *)&t237, (void *)(((uint8_t *)t232 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(t236));
                    uint8_t t238 = gm_l17;
                    uint32_t t239 = gm_l18;
                    unsigned __int128 t240 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)));
                    unsigned __int128 t241 = gm_f_num_ext_348kmbu(t238, t239, t240);
                    gm_f_put_uint_cddin99(t231, t237, t241);
                    void * t242 = gm_l0;
                    gm_t88rcucg1b7uk t243 = gm_l4;
                    gm_b16_8 t244 = gm_l6;
                    void * t245 = (void *)((uint8_t *)&gm_l23);
                    uint64_t t246 = gm_l24;
                    uint64_t t247 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t248 = (uintptr_t)t246;
                    if (t247 > t248 || t248 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t247, t248, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 125, 38);
                    uint64_t t249 = ((uintptr_t)t246) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t250;
                    GM_ST(void *, (uint8_t *)&t250, (void *)(((uint8_t *)t245 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(t249));
                    gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
                    gm_b0_1 t252 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t242, t243, t244, t251);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t253;
                    GM_ST(uint8_t, (uint8_t *)&t253 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t253 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t253 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t253 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t253 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t253 + 24, 0);
                    ((uint8_t *)&t253)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t253 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t253), 32);
                    goto gm_fdone;
                }
                gm_tvdg94bk7aa22 t254 = gm_l2;
                uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&t254) + 0));
                uint8_t t256 = (uint8_t)((t255 == ((uint8_t)6ULL)));
                gm_tvdg94bk7aa22 t257;
                GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t257 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)6ULL));
                if (t256) {
                    double t258 = gm_l10;
                    uint8_t t259 = gm_f_num_is_nan_b0ma7mo(t258);
                    if (t259) {
                        gm_b16_8 t260 = gm_l6;
                        gm_f_fill_abmf5g5(t260, ((uint8_t)0ULL));
                        gm_tcfa6l7reoosn t261;
                        GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t261 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t261 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t261 + 24, 0);
                        ((uint8_t *)&t261)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t261 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t261), 32);
                        goto gm_fdone;
                    }
                    double t262 = gm_l10;
                    uint64_t t263;
                    memset(&t263, 0, sizeof t263); memcpy(&t263, ((uint8_t *)&t262), sizeof t263 < 8 ? sizeof t263 : 8);
                    unsigned __int128 n264 = (unsigned __int128)((uint64_t)63ULL);
                    if (n264 >= 64) gm_trap_shift((uint64_t)n264, "u64", gm_srcl8pn2pjtmj6k, 133, 21);
                    uint64_t t265 = (uint64_t)(t263 >> (int)n264);
                    uint8_t t266 = ((t265 == ((uint64_t)1ULL)));
                    gm_l25 = t266;
                    uint8_t t267 = gm_l25;
                    unsigned __int128 t268 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                    gm_t86h6hhm6d76n t269;
                    GM_ST(uint8_t, (uint8_t *)&t269 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t269 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t269 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t269 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t269 + 0), t267);
                    GM_ST(unsigned __int128, ((uint8_t *)&t269 + 16), t268);
                    gm_l26 = t269;
                    uint8_t t270 = gm_l21;
                    if (t270) {
                        double t271 = gm_l10;
                        gm_tvdg94bk7aa22 t272;
                        GM_ST(uint8_t, (uint8_t *)&t272 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t272 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t272 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t272 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)1ULL));
                        gm_f_num_float_int_ba5kc1t((void *)&gm_l46, t271, t272);
                        uint8_t t273 = (uint8_t)((((uint8_t *)&gm_l46))[32] != 0);
                        if (t273) {
                            memmove(((uint8_t *)&gm_l47), ((uint8_t *)&gm_l46), 32);
                        } else {
                            memmove(((uint8_t *)&gm_l47), ((uint8_t *)&gm_l26), 32);
                        }
                        memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l47), 32);
                    }
                    void * t274 = gm_l0;
                    gm_b16_8 t275 = gm_l6;
                    uint8_t t276 = gm_l17;
                    uint32_t t277 = gm_l18;
                    uint8_t t278 = gm_l17;
                    uint32_t t279 = gm_l18;
                    gm_t86h6hhm6d76n t280;
                    gm_f_num_sat_66dqcpl((void *)&t280, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l26)), t278, t279);
                    unsigned __int128 t281 = gm_f_num_bits_onl28vp(&t280);
                    unsigned __int128 t282 = gm_f_num_ext_348kmbu(t276, t277, t281);
                    gm_f_put_uint_cddin99(t274, t275, t282);
                    gm_tcfa6l7reoosn t283;
                    GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t283 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t283 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t283 + 24, 0);
                    ((uint8_t *)&t283)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t283 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t283), 32);
                    goto gm_fdone;
                }
                uint8_t t284 = gm_l21;
                if (t284) {
                    {
                        {
                            double t285 = gm_l10;
                            gm_tvdg94bk7aa22 t286 = gm_l2;
                            gm_f_num_float_int_ba5kc1t((void *)&gm_l48, t285, t286);
                            uint8_t t287 = (uint8_t)((((uint8_t *)&gm_l48))[32] != 0);
                            if (!t287) {
                                goto gm_lb18;
                            }
                            memmove(((uint8_t *)&gm_l27), ((uint8_t *)&gm_l48), 32);
                            uint8_t t288 = gm_l17;
                            uint32_t t289 = gm_l18;
                            uint8_t t290 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)), t288, t289);
                            if (t290) {
                                void * t291 = gm_l0;
                                gm_b16_8 t292 = gm_l6;
                                uint8_t t293 = gm_l17;
                                uint32_t t294 = gm_l18;
                                unsigned __int128 t295 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)));
                                unsigned __int128 t296 = gm_f_num_ext_348kmbu(t293, t294, t295);
                                gm_f_put_uint_cddin99(t291, t292, t296);
                                gm_tcfa6l7reoosn t297;
                                GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t297 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t297 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t297 + 24, 0);
                                ((uint8_t *)&t297)[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&t297 + 8), GM_ZERO(gm_b0_1));
                                memmove(gm_out, ((uint8_t *)&t297), 32);
                                goto gm_fdone;
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                    }
gm_lb17:;
                }
                void * t298 = gm_l0;
                uint8_t t299 = gm_f_fast_23em7sv(t298);
                if (t299) {
                    gm_b16_8 t300 = gm_l6;
                    gm_f_fill_abmf5g5(t300, ((uint8_t)0ULL));
                    gm_tcfa6l7reoosn t301;
                    GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t301 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t301 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t301 + 24, 0);
                    ((uint8_t *)&t301)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t301 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t301), 32);
                    goto gm_fdone;
                }
                void * t302 = gm_l0;
                gm_tl874t2atnlh3 t303 = gm_l1;
                gm_b16_8 t304;
                GM_ST(void *, (uint8_t *)&t304, (void *)(gm_s9o2c36nosv54));
                GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(40));
                gm_t76123gfet73g t305;
                gm_f_trap_qh84l9d((void *)&t305, (void *)NULL, cx, NULL, t302, t303, t304);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t306;
                GM_ST(uint8_t, (uint8_t *)&t306 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t306 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t306 + 4, 0);
                ((uint8_t *)&t306)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t306 + 8), t305);
                memmove(gm_out, ((uint8_t *)&t306), 32);
                goto gm_fdone;
            }
gm_lb16:;
        }
gm_lb15:;
    }
    {
        {
            {
                void * t307 = gm_l0;
                gm_t88rcucg1b7uk t308 = gm_l7;
                gm_f_kind_htv6093((void *)&gm_l49, t307, GM_CONV(gm_csguankko4lmhd, t308));
                uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                uint8_t t310 = (uint8_t)(t309 == ((uint8_t)3ULL));
                if (!t310) {
                    goto gm_lb20;
                }
                uint16_t t311 = GM_LD(uint16_t, (((uint8_t *)&gm_l49) + 8));
                gm_l28 = t311;
                uint8_t t312 = gm_l8;
                if (t312) {
                    double t313 = gm_l10;
                    uint16_t t314 = gm_l28;
                    double t315 = gm_f_num_round_2pna7m3(t313, t314);
                    gm_l50 = t315;
                } else {
                    uint16_t t316 = gm_l28;
                    double t317 = gm_f_num_int_to_float_4hrvis6(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t316);
                    gm_l50 = t317;
                }
                double t318 = gm_l50;
                gm_l29 = t318;
                void * t319 = gm_l0;
                gm_t88rcucg1b7uk t320 = gm_l7;
                double t321 = gm_l29;
                gm_b16_8 t322 = gm_l6;
                gm_f_put_float_fiad72d(t319, GM_CONV(gm_csguankko4lmhd, t320), t321, t322);
                gm_tcfa6l7reoosn t323;
                GM_ST(uint8_t, (uint8_t *)&t323 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t323 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t323 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t323 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t323 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t323 + 24, 0);
                ((uint8_t *)&t323)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t323 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t323), 32);
                goto gm_fdone;
            }
gm_lb20:;
        }
gm_lb19:;
        {
            {
                void * t324 = gm_l0;
                gm_t88rcucg1b7uk t325 = gm_l7;
                gm_tkn42u7na3t81 t326 = gm_f_bits_struct_ty_22ue20s(t324, GM_CONV(gm_csguankko4lmhd, t325));
                gm_l51 = t326;
                uint8_t t327 = (uint8_t)((((uint8_t *)&gm_l51))[4] != 0);
                if (!t327) {
                    goto gm_lb22;
                }
                gm_t88rcucg1b7uk t328 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l51));
                gm_l30 = t328;
                uint8_t t329 = gm_l8;
                uint8_t t330 = (uint8_t)!t329;
                if (t330) {
                    void * t331 = gm_l0;
                    gm_t88rcucg1b7uk t332 = gm_l30;
                    gm_tg5lql6itufk7 t333 = gm_f_int_info_ulcevdv(t331, GM_CONV(gm_csguankko4lmhd, t332));
                    gm_l52 = t333;
                    uint8_t t334 = (uint8_t)((((uint8_t *)&gm_l52))[8] != 0);
                    if (!t334) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 160, 21);
                    gm_ttovhfee8mf28 t335 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l52));
                    gm_l31 = t335;
                    void * t336 = gm_l0;
                    gm_b16_8 t337 = gm_l6;
                    uint32_t t338 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 4));
                    unsigned __int128 t339 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                    unsigned __int128 t340 = gm_f_num_ext_348kmbu(((uint8_t)0), t338, t339);
                    gm_f_put_uint_cddin99(t336, t337, t340);
                    gm_tcfa6l7reoosn t341;
                    GM_ST(uint8_t, (uint8_t *)&t341 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t341 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t341 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t341 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t341 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t341 + 24, 0);
                    ((uint8_t *)&t341)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t341 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t341), 32);
                    goto gm_fdone;
                }
                goto gm_lb21;
            }
gm_lb22:;
        }
gm_lb21:;
    }
    {
        void * t342 = gm_l0;
        gm_tl874t2atnlh3 t343 = gm_l1;
        void * t344 = gm_l0;
        void * t345 = GM_LD(void *, (((uint8_t *)t344) + 8));
        gm_t88rcucg1b7uk t346 = gm_l7;
        gm_b16_8 t347 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t345, t346);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t348;
        GM_ST(gm_b16_8, ((uint8_t *)&t348 + 0), t347);
        gm_f_format_fv5tfj6((void *)&gm_l53, (void *)NULL, cx, NULL, t348);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t349 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
        if (!t349) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_srcl8pn2pjtmj6k, 165, 26);
        gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
        gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
        gm_t76123gfet73g t352;
        gm_f_trap_qh84l9d((void *)&t352, (void *)NULL, cx, NULL, t342, t343, t351);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t353;
        GM_ST(uint8_t, (uint8_t *)&t353 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t353 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t353 + 4, 0);
        ((uint8_t *)&t353)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t353 + 8), t352);
        memmove(gm_out, ((uint8_t *)&t353), 32);
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
inline __attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_bytes_2qnf0cp F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* w */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
                gm_l5 = ((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_sub_overflow(t13, t14, &r15);
                if (o16) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 416, 22);
                uint64_t t17 = r15;
                uint64_t t18 = (t17 < ((uint64_t)8ULL) ? t17 : ((uint64_t)8ULL));
                gm_l4 = t18;
                {
                    for (;;) {
                        uint64_t t19 = gm_l5;
                        uint64_t t20 = gm_l4;
                        uint8_t t21 = (t19 < t20);
                        if (!t21) {
                            goto gm_lb4;
                        }
                        {
                            {
                                {
                                    uint64_t t22 = gm_l5;
                                    gm_l6 = t22;
                                    uint64_t t23 = gm_l3;
                                    void * t24 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t26 = gm_l2;
                                    uint64_t t27 = gm_l6;
                                    uint64_t r28; bool o29 = __builtin_add_overflow(t26, t27, &r28);
                                    if (o29) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 416, 61);
                                    uint64_t t30 = r28;
                                    if ((uintptr_t)t30 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t30, t25, gm_srcl8pn2pjtmj6k, 416, 55);
                                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t24) + (uintptr_t)t30 * 1));
                                    uint64_t t32 = (uint64_t)t31;
                                    uint64_t t33 = gm_l6;
                                    uint64_t r34; bool o35 = __builtin_mul_overflow(t33, ((uint64_t)8ULL), &r34);
                                    if (o35) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 416, 76);
                                    uint64_t t36 = r34;
                                    uint64_t t37 = (uint64_t)t36;
                                    unsigned __int128 n38 = (unsigned __int128)t37;
                                    if (n38 >= 64) gm_trap_shift((uint64_t)n38, "u64", gm_srcl8pn2pjtmj6k, 416, 51);
                                    uint64_t r39 = (uint64_t)((uint64_t)t32 << (int)n38);
                                    bool o40 = (uint64_t)(r39 >> (int)n38) != t32;
                                    if (o40) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 416, 51);
                                    uint64_t t41 = r39;
                                    uint64_t t42 = (uint64_t)(t23 | t41);
                                    gm_l3 = t42;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 416, 9);
                            }
gm_lb7:;
                        }
gm_lb6:;
                        uint64_t t43 = gm_l5;
                        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                        uint64_t t46 = r44;
                        gm_l5 = t46;
                    }
                }
gm_lb4:;
                void * t47 = gm_l0;
                uint64_t t48 = gm_l3;
                gm_f_hash_u64_32bripn(t47, t48);
                uint64_t t49 = gm_l2;
                uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)8ULL), &r50);
                if (o51) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 418, 9);
                uint64_t t52 = r50;
                gm_l2 = t52;
            }
gm_lb3:;
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
extern __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2) { gm_f_hash_int_26mf5j0(a0, &a1, a2); }
extern __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_hash_val_nr2e99r F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* h */
    gm_ttovhfee8mf28 gm_l5; /* info */
    double gm_l6; /* x */
    gm_b16_8 gm_l7; /* elems */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* n */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint64_t gm_l13; /* esz */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* inner */
    gm_t88rcucg1b7uk gm_l22; /* err */
    gm_t88rcucg1b7uk gm_l23; /* ok */
    uint8_t gm_l24; /* is_err */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* u */
    uint32_t gm_l27; /* vi */
    gm_t88rcucg1b7uk gm_l28; /* pt */
    uint64_t gm_l29; /* off */
    void * gm_l30; /* a */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* i */
    gm_tfbnciphf3261 gm_l35; /* fl */
    uint64_t gm_l36; /* off */
    GM_STACK_CHECK(&F, 4620u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l37;
    gm_tg5lql6itufk7 gm_l38;
    uint64_t gm_l39;
    gm_tcfa6l7reoosn gm_l40;
    gm_tcfa6l7reoosn gm_l41;
    gm_tmc91gv7n9c0i gm_l42;
    gm_tcfa6l7reoosn gm_l43;
    gm_tcfa6l7reoosn gm_l44;
    uint64_t gm_l45;
    gm_t88rcucg1b7uk gm_l46;
    gm_tcfa6l7reoosn gm_l47;
    gm_tkn42u7na3t81 gm_l48;
    gm_tcfa6l7reoosn gm_l49;
    gm_t57euh612ligr gm_l50;
    gm_t9805qtonbsag gm_l51;
    uint32_t gm_l52;
    gm_tcfa6l7reoosn gm_l53;
    void * gm_l54;
    void * gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_tcfa6l7reoosn gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tabb710d9mq2e gm_l59;
    {
        void * t52 = gm_l0;
        gm_t88rcucg1b7uk t53 = gm_l2;
        gm_f_kind_htv6093((void *)&gm_l37, t52, GM_CONV(gm_csguankko4lmhd, t53));
        {
            {
                {
                    {
                        {
                            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)0ULL));
                            if (!t55) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)1ULL));
                            if (!t57) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)2ULL));
                            if (!t59) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t60 = gm_l0;
                    gm_t88rcucg1b7uk t61 = gm_l2;
                    gm_tg5lql6itufk7 t62 = gm_f_int_info_ulcevdv(t60, GM_CONV(gm_csguankko4lmhd, t61));
                    gm_l38 = t62;
                    uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l38))[8] != 0);
                    if (!t63) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 434, 21);
                    gm_ttovhfee8mf28 t64 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l38));
                    gm_l5 = t64;
                    void * t65 = gm_l4;
                    uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
                    void * t67 = gm_l0;
                    gm_t88rcucg1b7uk t68 = gm_l2;
                    gm_b16_8 t69 = gm_l3;
                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                    unsigned __int128 t71 = gm_f_get_int_pg7p4gc(t67, GM_CONV(gm_csguankko4lmhd, t68), t70);
                    gm_t86h6hhm6d76n t72;
                    gm_f_num_val_i3upeur((void *)&t72, t66, t71);
                    uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                    gm_f_hash_int_26mf5j0(t65, &t72, t73);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)3ULL));
                    if (!t75) {
                        goto gm_lb7;
                    }
                    void * t76 = gm_l0;
                    gm_t88rcucg1b7uk t77 = gm_l2;
                    gm_b16_8 t78 = gm_l3;
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                    double t80 = gm_f_get_float_grmhlm4(t76, GM_CONV(gm_csguankko4lmhd, t77), t79);
                    gm_l6 = t80;
                    double t81 = gm_l6;
                    uint8_t t82 = ((t81 == ((double)0e0)));
                    if (t82) {
                        gm_l6 = ((double)0e0);
                    }
                    void * t83 = gm_l4;
                    double t84 = gm_l6;
                    uint64_t t85;
                    memset(&t85, 0, sizeof t85); memcpy(&t85, ((uint8_t *)&t84), sizeof t85 < 8 ? sizeof t85 : 8);
                    gm_f_hash_u64_32bripn(t83, t85);
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t87 = (uint8_t)(t86 == ((uint8_t)4ULL));
                    if (!t87) {
                        goto gm_lb8;
                    }
                    void * t88 = gm_l4;
                    void * t89 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t90)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t90, gm_srcl8pn2pjtmj6k, 442, 33);
                    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)t89) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t92 = ((t91 == ((uint8_t)0ULL)));
                    if (t92) {
                        gm_l39 = ((uint64_t)0ULL);
                    } else {
                        gm_l39 = ((uint64_t)1ULL);
                    }
                    uint64_t t93 = gm_l39;
                    gm_f_hash_u64_32bripn(t88, t93);
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    {
                        {
                            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)8ULL));
                            if (!t95) {
                                goto gm_lb11;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t97 = (uint8_t)(t96 == ((uint8_t)9ULL));
                            if (!t97) {
                                goto gm_lb12;
                            }
                            goto gm_lb10;
                        }
gm_lb12:;
                        {
                            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t99 = (uint8_t)(t98 == ((uint8_t)7ULL));
                            if (!t99) {
                                goto gm_lb13;
                            }
                            goto gm_lb10;
                        }
gm_lb13:;
                        {
                            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t101 = (uint8_t)(t100 == ((uint8_t)16ULL));
                            if (!t101) {
                                goto gm_lb14;
                            }
                            goto gm_lb10;
                        }
gm_lb14:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    void * t102 = gm_l4;
                    void * t103 = gm_l0;
                    gm_b16_8 t104 = gm_l3;
                    gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                    uint64_t t106 = gm_f_ptr_of_b77b4c9(t103, t105);
                    gm_f_hash_u64_32bripn(t102, t106);
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t108 = (uint8_t)(t107 == ((uint8_t)5ULL));
                    if (!t108) {
                        goto gm_lb15;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t110 = (uint8_t)(t109 == ((uint8_t)12ULL));
                    if (!t110) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                    gm_l7 = t111;
                    gm_l9 = ((uint64_t)0ULL);
                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    gm_l8 = t112;
                    {
                        for (;;) {
                            uint64_t t113 = gm_l9;
                            uint64_t t114 = gm_l8;
                            uint8_t t115 = (t113 < t114);
                            if (!t115) {
                                goto gm_lb17;
                            }
                            {
                                {
                                    {
                                        uint64_t t116 = gm_l9;
                                        gm_l10 = t116;
                                        void * t117 = gm_l0;
                                        gm_tl874t2atnlh3 t118 = gm_l1;
                                        void * t119 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                        uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint64_t t121 = gm_l10;
                                        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t119) + (uintptr_t)t121 * 4));
                                        void * t123 = gm_l0;
                                        gm_t88rcucg1b7uk t124 = gm_l2;
                                        gm_b16_8 t125 = gm_l3;
                                        uint64_t t126 = gm_l10;
                                        uint32_t t127 = (uint32_t)t126;
                                        gm_b16_8 t128 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t123, t124, t125, t127);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t129 = gm_l4;
                                        gm_f_hash_val_nr2e99r((void *)&gm_l40, (void *)NULL, cx, NULL, t117, t118, t122, t128, t129);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                        if (t130) {
                                        } else {
                                            gm_t76123gfet73g t131 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l40) + 8));
                                            gm_tcfa6l7reoosn t132;
                                            GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                                            ((uint8_t *)&t132)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t132 + 8), t131);
                                            memmove(gm_out, ((uint8_t *)&t132), 32);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 446, 13);
                                }
gm_lb20:;
                            }
gm_lb19:;
                            uint64_t t133 = gm_l9;
                            uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                            uint64_t t136 = r134;
                            gm_l9 = t136;
                        }
                    }
gm_lb17:;
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t138 = (uint8_t)(t137 == ((uint8_t)11ULL));
                    if (!t138) {
                        goto gm_lb22;
                    }
                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                    gm_l11 = t139;
                    gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 8));
                    gm_l12 = t140;
                    void * t141 = gm_l0;
                    gm_t88rcucg1b7uk t142 = gm_l12;
                    uint64_t t143 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t141, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t144 = gm_f_us_cuv01fp(t143);
                    gm_l13 = t144;
                    void * t145 = gm_l0;
                    gm_t88rcucg1b7uk t146 = gm_l12;
                    gm_t53at80pfdus3 t147;
                    gm_f_kind_htv6093((void *)&t147, t145, GM_CONV(gm_csguankko4lmhd, t146));
                    gm_tjcj4j2gbh6c8 t148;
                    GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)0));
                    GM_ST(uint16_t, ((uint8_t *)&t148 + 2), ((uint16_t)8ULL));
                    uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t147) + 0));
                    uint8_t t150 = (uint8_t)(t149 == ((uint8_t)0ULL));
                    if (t150) {
                        gm_tjcj4j2gbh6c8 t151 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t147) + 8));
                        t150 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t151), ((uint8_t *)&t148));
                    }
                    uint8_t t152 = (uint8_t)(t150);
                    gm_t53at80pfdus3 t153;
                    memset(&t153, 0, sizeof t153);
                    GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t153 + 8), t148);
                    if (t152) {
                        void * t154 = gm_l4;
                        void * t155 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t157 = gm_l11;
                        uint64_t t158 = gm_f_us_cuv01fp(t157);
                        uint64_t t159 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t160 = (uintptr_t)t158;
                        if (t159 > t160 || t160 > (uintptr_t)t156) gm_trap_slice(t159, t160, t156, gm_srcl8pn2pjtmj6k, 451, 31);
                        uint64_t t161 = ((uintptr_t)t158) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t162;
                        GM_ST(void *, (uint8_t *)&t162, (void *)(((uint8_t *)t155 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(t161));
                        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                        gm_f_hash_bytes_2qnf0cp(t154, t163);
                        gm_tcfa6l7reoosn t164;
                        GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t164 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t164 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t164 + 24, 0);
                        ((uint8_t *)&t164)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t164 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t164), 32);
                        goto gm_fdone;
                    }
                    gm_l15 = ((uint64_t)0ULL);
                    uint64_t t165 = gm_l11;
                    uint64_t t166 = gm_f_us_cuv01fp(t165);
                    gm_l14 = t166;
                    {
                        for (;;) {
                            uint64_t t167 = gm_l15;
                            uint64_t t168 = gm_l14;
                            uint8_t t169 = (t167 < t168);
                            if (!t169) {
                                goto gm_lb23;
                            }
                            {
                                {
                                    {
                                        uint64_t t170 = gm_l15;
                                        gm_l16 = t170;
                                        void * t171 = gm_l0;
                                        gm_tl874t2atnlh3 t172 = gm_l1;
                                        gm_t88rcucg1b7uk t173 = gm_l12;
                                        void * t174 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t176 = gm_l16;
                                        uint64_t t177 = gm_l13;
                                        uint64_t r178; bool o179 = __builtin_mul_overflow(t176, t177, &r178);
                                        if (o179) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 62);
                                        uint64_t t180 = r178;
                                        uint64_t t181 = gm_l16;
                                        uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                                        if (o183) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 454, 72);
                                        uint64_t t184 = r182;
                                        uint64_t t185 = gm_l13;
                                        uint64_t r186; bool o187 = __builtin_mul_overflow(t184, t185, &r186);
                                        if (o187) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 71);
                                        uint64_t t188 = r186;
                                        uint64_t t189 = (uintptr_t)t180;
                                        uint64_t t190 = (uintptr_t)t188;
                                        if (t189 > t190 || t190 > (uintptr_t)t175) gm_trap_slice(t189, t190, t175, gm_srcl8pn2pjtmj6k, 454, 60);
                                        uint64_t t191 = ((uintptr_t)t188) - (uintptr_t)t180;
                                        gm_b16_8 t192;
                                        GM_ST(void *, (uint8_t *)&t192, (void *)(((uint8_t *)t174 + (uintptr_t)t180 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(t191));
                                        void * t193 = gm_l4;
                                        gm_f_hash_val_nr2e99r((void *)&gm_l41, (void *)NULL, cx, NULL, t171, t172, t173, t192, t193);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                        if (t194) {
                                        } else {
                                            gm_t76123gfet73g t195 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l41) + 8));
                                            gm_tcfa6l7reoosn t196;
                                            GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t196 + 4, 0);
                                            ((uint8_t *)&t196)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t196 + 8), t195);
                                            memmove(gm_out, ((uint8_t *)&t196), 32);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb26;
                                    }
gm_lb27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 454, 13);
                                }
gm_lb26:;
                            }
gm_lb25:;
                            uint64_t t197 = gm_l15;
                            uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                            uint64_t t200 = r198;
                            gm_l15 = t200;
                        }
                    }
gm_lb23:;
                    goto gm_lb1;
                }
gm_lb22:;
            }
            {
                {
                    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t202 = (uint8_t)(t201 == ((uint8_t)10ULL));
                    if (!t202) {
                        goto gm_lb28;
                    }
                    void * t203 = gm_l0;
                    gm_tl874t2atnlh3 t204 = gm_l1;
                    gm_t88rcucg1b7uk t205 = gm_l2;
                    gm_b16_8 t206 = gm_l3;
                    gm_f_seq_of_ifahtjs((void *)&gm_l42, (void *)NULL, cx, NULL, t203, t204, t205, t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                    if (t207) {
                    } else {
                        gm_t76123gfet73g t208 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l42) + 8));
                        gm_tcfa6l7reoosn t209;
                        GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                        ((uint8_t *)&t209)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t209 + 8), t208);
                        memmove(gm_out, ((uint8_t *)&t209), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l17), (((uint8_t *)&gm_l42) + 8), 40);
                    void * t210 = gm_l4;
                    uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    gm_f_hash_u64_32bripn(t210, t211);
                    void * t212 = gm_l0;
                    gm_t88rcucg1b7uk t213 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l17));
                    gm_t53at80pfdus3 t214;
                    gm_f_kind_htv6093((void *)&t214, t212, GM_CONV(gm_csguankko4lmhd, t213));
                    gm_tjcj4j2gbh6c8 t215;
                    GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)0));
                    GM_ST(uint16_t, ((uint8_t *)&t215 + 2), ((uint16_t)8ULL));
                    uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&t214) + 0));
                    uint8_t t217 = (uint8_t)(t216 == ((uint8_t)0ULL));
                    if (t217) {
                        gm_tjcj4j2gbh6c8 t218 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t214) + 8));
                        t217 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t218), ((uint8_t *)&t215));
                    }
                    uint8_t t219 = (uint8_t)(t217);
                    gm_t53at80pfdus3 t220;
                    memset(&t220, 0, sizeof t220);
                    GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t220 + 8), t215);
                    if (t219) {
                        void * t221 = gm_l4;
                        void * t222 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
                        uint64_t t223 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                        uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                        uint64_t t225 = gm_f_us_cuv01fp(t224);
                        uint64_t t226 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t227 = (uintptr_t)t225;
                        if (t226 > t227 || t227 > (uintptr_t)t223) gm_trap_slice(t226, t227, t223, gm_srcl8pn2pjtmj6k, 460, 31);
                        uint64_t t228 = ((uintptr_t)t225) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t229;
                        GM_ST(void *, (uint8_t *)&t229, (void *)(((uint8_t *)t222 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(t228));
                        gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                        gm_f_hash_bytes_2qnf0cp(t221, t230);
                        gm_tcfa6l7reoosn t231;
                        GM_ST(uint8_t, (uint8_t *)&t231 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t231 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t231 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t231 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t231 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t231 + 24, 0);
                        ((uint8_t *)&t231)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t231 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t231), 32);
                        goto gm_fdone;
                    }
                    gm_l19 = ((uint64_t)0ULL);
                    uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    uint64_t t233 = gm_f_us_cuv01fp(t232);
                    gm_l18 = t233;
                    {
                        for (;;) {
                            uint64_t t234 = gm_l19;
                            uint64_t t235 = gm_l18;
                            uint8_t t236 = (t234 < t235);
                            if (!t236) {
                                goto gm_lb29;
                            }
                            {
                                {
                                    {
                                        uint64_t t237 = gm_l19;
                                        gm_l20 = t237;
                                        void * t238 = gm_l0;
                                        gm_tl874t2atnlh3 t239 = gm_l1;
                                        gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l17));
                                        void * t241 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
                                        uint64_t t242 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                                        uint64_t t243 = gm_l20;
                                        uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                        uint64_t r245; bool o246 = __builtin_mul_overflow(t243, t244, &r245);
                                        if (o246) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 71);
                                        uint64_t t247 = r245;
                                        uint64_t t248 = gm_l20;
                                        uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                                        if (o250) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 463, 83);
                                        uint64_t t251 = r249;
                                        uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                        uint64_t r253; bool o254 = __builtin_mul_overflow(t251, t252, &r253);
                                        if (o254) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 82);
                                        uint64_t t255 = r253;
                                        uint64_t t256 = (uintptr_t)t247;
                                        uint64_t t257 = (uintptr_t)t255;
                                        if (t256 > t257 || t257 > (uintptr_t)t242) gm_trap_slice(t256, t257, t242, gm_srcl8pn2pjtmj6k, 463, 64);
                                        uint64_t t258 = ((uintptr_t)t255) - (uintptr_t)t247;
                                        gm_b16_8 t259;
                                        GM_ST(void *, (uint8_t *)&t259, (void *)(((uint8_t *)t241 + (uintptr_t)t247 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t259 + 8, (uintptr_t)(t258));
                                        void * t260 = gm_l4;
                                        gm_f_hash_val_nr2e99r((void *)&gm_l43, (void *)NULL, cx, NULL, t238, t239, t240, t259, t260);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                        if (t261) {
                                        } else {
                                            gm_t76123gfet73g t262 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l43) + 8));
                                            gm_tcfa6l7reoosn t263;
                                            GM_ST(uint8_t, (uint8_t *)&t263 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t263 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t263 + 4, 0);
                                            ((uint8_t *)&t263)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t263 + 8), t262);
                                            memmove(gm_out, ((uint8_t *)&t263), 32);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 463, 13);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t264 = gm_l19;
                            uint64_t r265; bool o266 = __builtin_add_overflow(t264, ((uint64_t)1ULL), &r265);
                            uint64_t t267 = r265;
                            gm_l19 = t267;
                        }
                    }
gm_lb29:;
                    goto gm_lb1;
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t269 = (uint8_t)(t268 == ((uint8_t)13ULL));
                    if (!t269) {
                        goto gm_lb34;
                    }
                    gm_t88rcucg1b7uk t270 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l37) + 8));
                    gm_l21 = t270;
                    void * t271 = gm_l0;
                    void * t272 = GM_LD(void *, (((uint8_t *)t271) + 8));
                    gm_t88rcucg1b7uk t273 = gm_l21;
                    uint8_t t274 = gm_f_nullable_pointer_7qfmn6o(t272, GM_CONV(gm_csguankko4lmhd, t273));
                    if (t274) {
                        void * t275 = gm_l4;
                        void * t276 = gm_l0;
                        gm_b16_8 t277 = gm_l3;
                        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
                        uint64_t t279 = gm_f_ptr_of_b77b4c9(t276, t278);
                        gm_f_hash_u64_32bripn(t275, t279);
                    } else {
                        void * t280 = gm_l0;
                        gm_t88rcucg1b7uk t281 = gm_l2;
                        gm_b16_8 t282 = gm_l3;
                        gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                        uint8_t t284 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t280, t281, t283);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t284) {
                            void * t285 = gm_l4;
                            gm_f_hash_u64_32bripn(t285, ((uint64_t)1ULL));
                            void * t286 = gm_l0;
                            gm_tl874t2atnlh3 t287 = gm_l1;
                            gm_t88rcucg1b7uk t288 = gm_l21;
                            void * t289 = gm_l0;
                            gm_t88rcucg1b7uk t290 = gm_l2;
                            gm_b16_8 t291 = gm_l3;
                            gm_b16_8 t292 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t289, t290, t291);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t293 = gm_l4;
                            gm_f_hash_val_nr2e99r((void *)&gm_l44, (void *)NULL, cx, NULL, t286, t287, t288, t292, t293);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t294 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                            if (t294) {
                            } else {
                                gm_t76123gfet73g t295 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l44) + 8));
                                gm_tcfa6l7reoosn t296;
                                GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                                ((uint8_t *)&t296)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t296 + 8), t295);
                                memmove(gm_out, ((uint8_t *)&t296), 32);
                                goto gm_fdone;
                            }
                        } else {
                            void * t297 = gm_l4;
                            gm_f_hash_u64_32bripn(t297, ((uint64_t)0ULL));
                        }
                    }
                    goto gm_lb1;
                }
gm_lb34:;
                {
                    uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t299 = (uint8_t)(t298 == ((uint8_t)14ULL));
                    if (!t299) {
                        goto gm_lb35;
                    }
                    gm_t88rcucg1b7uk t300 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l37) + 8));
                    gm_l22 = t300;
                    gm_t88rcucg1b7uk t301 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 4));
                    gm_l23 = t301;
                    void * t302 = gm_l0;
                    gm_t88rcucg1b7uk t303 = gm_l2;
                    gm_b16_8 t304 = gm_l3;
                    gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                    uint8_t t306 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t302, t303, t305);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t306;
                    void * t307 = gm_l4;
                    uint8_t t308 = gm_l24;
                    if (t308) {
                        gm_l45 = ((uint64_t)1ULL);
                    } else {
                        gm_l45 = ((uint64_t)0ULL);
                    }
                    uint64_t t309 = gm_l45;
                    gm_f_hash_u64_32bripn(t307, t309);
                    void * t310 = gm_l0;
                    gm_tl874t2atnlh3 t311 = gm_l1;
                    uint8_t t312 = gm_l24;
                    if (t312) {
                        gm_t88rcucg1b7uk t313 = gm_l22;
                        gm_l46 = t313;
                    } else {
                        gm_t88rcucg1b7uk t314 = gm_l23;
                        gm_l46 = t314;
                    }
                    gm_t88rcucg1b7uk t315 = gm_l46;
                    void * t316 = gm_l0;
                    gm_t88rcucg1b7uk t317 = gm_l2;
                    gm_b16_8 t318 = gm_l3;
                    gm_b16_8 t319 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t316, t317, t318);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t320 = gm_l4;
                    gm_f_hash_val_nr2e99r((void *)&gm_l47, (void *)NULL, cx, NULL, t310, t311, t315, t319, t320);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t321 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (t321) {
                    } else {
                        gm_t76123gfet73g t322 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l47) + 8));
                        gm_tcfa6l7reoosn t323;
                        GM_ST(uint8_t, (uint8_t *)&t323 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t323 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t323 + 4, 0);
                        ((uint8_t *)&t323)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t323 + 8), t322);
                        memmove(gm_out, ((uint8_t *)&t323), 32);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb35:;
            }
            {
                {
                    uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t325 = (uint8_t)(t324 == ((uint8_t)17ULL));
                    if (!t325) {
                        goto gm_lb36;
                    }
                    {
                        {
                            void * t326 = gm_l0;
                            gm_t88rcucg1b7uk t327 = gm_l2;
                            gm_tkn42u7na3t81 t328 = gm_f_bits_struct_ty_22ue20s(t326, GM_CONV(gm_csguankko4lmhd, t327));
                            gm_l48 = t328;
                            uint8_t t329 = (uint8_t)((((uint8_t *)&gm_l48))[4] != 0);
                            if (!t329) {
                                goto gm_lb38;
                            }
                            gm_t88rcucg1b7uk t330 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l48));
                            gm_l25 = t330;
                            void * t331 = gm_l0;
                            gm_tl874t2atnlh3 t332 = gm_l1;
                            gm_t88rcucg1b7uk t333 = gm_l25;
                            gm_b16_8 t334 = gm_l3;
                            void * t335 = gm_l4;
                            gm_f_hash_val_nr2e99r((void *)&gm_l49, (void *)NULL, cx, NULL, t331, t332, t333, t334, t335);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t336 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (t336) {
                            } else {
                                gm_t76123gfet73g t337 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l49) + 8));
                                gm_tcfa6l7reoosn t338;
                                GM_ST(uint8_t, (uint8_t *)&t338 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t338 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t338 + 4, 0);
                                ((uint8_t *)&t338)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t338 + 8), t337);
                                memmove(gm_out, ((uint8_t *)&t338), 32);
                                goto gm_fdone;
                            }
                            gm_tcfa6l7reoosn t339;
                            GM_ST(uint8_t, (uint8_t *)&t339 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t339 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t339 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t339 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t339 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t339 + 24, 0);
                            ((uint8_t *)&t339)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t339 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t339), 32);
                            goto gm_fdone;
                        }
gm_lb38:;
                    }
gm_lb37:;
                    {
                        {
                            void * t340 = gm_l0;
                            gm_t88rcucg1b7uk t341 = gm_l2;
                            gm_f_union_def_h8mvf4t((void *)&gm_l50, t340, GM_CONV(gm_csguankko4lmhd, t341));
                            uint8_t t342 = (uint8_t)((((uint8_t *)&gm_l50))[24] != 0);
                            if (!t342) {
                                goto gm_lb40;
                            }
                            memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l50), 24);
                            void * t343 = gm_l0;
                            gm_t88rcucg1b7uk t344 = gm_l2;
                            gm_b16_8 t345 = gm_l3;
                            gm_b16_8 t346 = GM_LD(gm_b16_8, ((uint8_t *)&t345));
                            gm_t9805qtonbsag t347 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t343, t344, t346);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l51 = t347;
                            uint8_t t348 = (uint8_t)((((uint8_t *)&gm_l51))[4] != 0);
                            if (t348) {
                                uint32_t t349 = GM_LD(uint32_t, ((uint8_t *)&gm_l51));
                                gm_l52 = t349;
                            } else {
                                void * t350 = gm_l0;
                                gm_tl874t2atnlh3 t351 = gm_l1;
                                gm_t88rcucg1b7uk t352 = gm_l2;
                                gm_b16_8 t353 = gm_l3;
                                gm_b16_8 t354 = GM_LD(gm_b16_8, ((uint8_t *)&t353));
                                gm_t76123gfet73g t355;
                                gm_f_invalid_tag_1gif354((void *)&t355, (void *)NULL, cx, NULL, t350, t351, t352, t354);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tcfa6l7reoosn t356;
                                GM_ST(uint8_t, (uint8_t *)&t356 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t356 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
                                ((uint8_t *)&t356)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t356 + 8), t355);
                                memmove(gm_out, ((uint8_t *)&t356), 32);
                                goto gm_fdone;
                            }
                            uint32_t t357 = gm_l52;
                            gm_l27 = t357;
                            void * t358 = gm_l4;
                            uint32_t t359 = gm_l27;
                            uint64_t t360 = (uint64_t)t359;
                            gm_f_hash_u64_32bripn(t358, t360);
                            {
                                {
                                    void * t361 = GM_LD(void *, (((uint8_t *)&gm_l26) + 8));
                                    uint64_t t362 = GM_LD(uint64_t, ((((uint8_t *)&gm_l26) + 8) + 8));
                                    uint32_t t363 = gm_l27;
                                    uint64_t t364 = (uint64_t)t363;
                                    uint64_t t365 = gm_f_us_cuv01fp(t364);
                                    if ((uintptr_t)t365 >= (uintptr_t)(t362)) gm_trap_index((uintptr_t)t365, t362, gm_srcl8pn2pjtmj6k, 488, 26);
                                    uint8_t t366 = (uint8_t)((((((uint8_t *)t361) + (uintptr_t)t365 * 72) + 16))[4] != 0);
                                    if (!t366) {
                                        goto gm_lb42;
                                    }
                                    gm_t88rcucg1b7uk t367 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t361) + (uintptr_t)t365 * 72) + 16));
                                    gm_l28 = t367;
                                    void * t368 = gm_l0;
                                    gm_t88rcucg1b7uk t369 = gm_l2;
                                    uint64_t t370 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t368, t369);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t371 = gm_f_us_cuv01fp(t370);
                                    gm_l29 = t371;
                                    void * t372 = gm_l0;
                                    gm_tl874t2atnlh3 t373 = gm_l1;
                                    gm_t88rcucg1b7uk t374 = gm_l28;
                                    void * t375 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t376 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t377 = gm_l29;
                                    uint64_t t378 = gm_l29;
                                    void * t379 = gm_l0;
                                    gm_t88rcucg1b7uk t380 = gm_l28;
                                    uint64_t t381 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t379, t380);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t382 = gm_f_us_cuv01fp(t381);
                                    uint64_t r383; bool o384 = __builtin_add_overflow(t378, t382, &r383);
                                    if (o384) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 490, 53);
                                    uint64_t t385 = r383;
                                    uint64_t t386 = (uintptr_t)t377;
                                    uint64_t t387 = (uintptr_t)t385;
                                    if (t386 > t387 || t387 > (uintptr_t)t376) gm_trap_slice(t386, t387, t376, gm_srcl8pn2pjtmj6k, 490, 46);
                                    uint64_t t388 = ((uintptr_t)t385) - (uintptr_t)t377;
                                    gm_b16_8 t389;
                                    GM_ST(void *, (uint8_t *)&t389, (void *)(((uint8_t *)t375 + (uintptr_t)t377 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t389 + 8, (uintptr_t)(t388));
                                    void * t390 = gm_l4;
                                    gm_f_hash_val_nr2e99r((void *)&gm_l53, (void *)NULL, cx, NULL, t372, t373, t374, t389, t390);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t391 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                    if (t391) {
                                    } else {
                                        gm_t76123gfet73g t392 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l53) + 8));
                                        gm_tcfa6l7reoosn t393;
                                        GM_ST(uint8_t, (uint8_t *)&t393 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t393 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t393 + 4, 0);
                                        ((uint8_t *)&t393)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t393 + 8), t392);
                                        memmove(gm_out, ((uint8_t *)&t393), 32);
                                        goto gm_fdone;
                                    }
                                    goto gm_lb41;
                                }
gm_lb42:;
                            }
gm_lb41:;
                            gm_tcfa6l7reoosn t394;
                            GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t394 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t394 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t394 + 24, 0);
                            ((uint8_t *)&t394)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t394 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t394), 32);
                            goto gm_fdone;
                        }
gm_lb40:;
                    }
gm_lb39:;
                    void * t395 = gm_l0;
                    gm_t88rcucg1b7uk t396 = gm_l2;
                    void * t397 = gm_f_adt_of_8k5vqud(t395, GM_CONV(gm_csguankko4lmhd, t396));
                    gm_l54 = t397;
                    uint8_t t398 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l54)) != 0);
                    if (t398) {
                        void * t399 = GM_LD(void *, ((uint8_t *)&gm_l54));
                        gm_l55 = t399;
                    } else {
                        void * t400 = gm_l0;
                        gm_tl874t2atnlh3 t401 = gm_l1;
                        void * t402 = gm_l0;
                        void * t403 = GM_LD(void *, (((uint8_t *)t402) + 8));
                        gm_t88rcucg1b7uk t404 = gm_l2;
                        gm_b16_8 t405 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t403, t404);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t406;
                        GM_ST(gm_b16_8, ((uint8_t *)&t406 + 0), t405);
                        gm_f_format_h8e7ps1((void *)&gm_l56, (void *)NULL, cx, NULL, t406);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t407 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                        if (!t407) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_srcl8pn2pjtmj6k, 494, 56);
                        gm_b16_8 t408 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                        gm_b16_8 t409 = GM_LD(gm_b16_8, ((uint8_t *)&t408));
                        gm_t76123gfet73g t410;
                        gm_f_trap_qh84l9d((void *)&t410, (void *)NULL, cx, NULL, t400, t401, t409);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t411;
                        GM_ST(uint8_t, (uint8_t *)&t411 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t411 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t411 + 4, 0);
                        ((uint8_t *)&t411)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t411 + 8), t410);
                        memmove(gm_out, ((uint8_t *)&t411), 32);
                        goto gm_fdone;
                    }
                    void * t412 = gm_l55;
                    gm_l30 = t412;
                    {
                        {
                            void * t413 = gm_l30;
                            uint8_t t414 = GM_LD(uint8_t, ((((uint8_t *)t413) + 24) + 0));
                            uint8_t t415 = (uint8_t)(t414 == ((uint8_t)0ULL));
                            if (!t415) {
                                goto gm_lb44;
                            }
                            memmove(((uint8_t *)&F.l31), ((((uint8_t *)t413) + 24) + 8), 48);
                            gm_l33 = ((uint64_t)0ULL);
                            uint64_t t416 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                            gm_l32 = t416;
                            {
                                for (;;) {
                                    uint64_t t417 = gm_l33;
                                    uint64_t t418 = gm_l32;
                                    uint8_t t419 = (t417 < t418);
                                    if (!t419) {
                                        goto gm_lb45;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t420 = gm_l33;
                                                gm_l34 = t420;
                                                void * t421 = gm_l0;
                                                gm_t88rcucg1b7uk t422 = gm_l2;
                                                uint64_t t423 = gm_l34;
                                                uint32_t t424 = (uint32_t)t423;
                                                gm_tfbnciphf3261 t425 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t421, t422, t424);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l35 = t425;
                                                uint64_t t426 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                uint64_t t427 = gm_f_us_cuv01fp(t426);
                                                gm_l36 = t427;
                                                void * t428 = gm_l0;
                                                gm_tl874t2atnlh3 t429 = gm_l1;
                                                gm_t88rcucg1b7uk t430 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l35));
                                                void * t431 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                uint64_t t432 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                uint64_t t433 = gm_l36;
                                                uint64_t t434 = gm_l36;
                                                void * t435 = gm_l0;
                                                gm_t88rcucg1b7uk t436 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l35));
                                                uint64_t t437 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t435, t436);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint64_t t438 = gm_f_us_cuv01fp(t437);
                                                uint64_t r439; bool o440 = __builtin_add_overflow(t434, t438, &r439);
                                                if (o440) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 499, 52);
                                                uint64_t t441 = r439;
                                                uint64_t t442 = (uintptr_t)t433;
                                                uint64_t t443 = (uintptr_t)t441;
                                                if (t442 > t443 || t443 > (uintptr_t)t432) gm_trap_slice(t442, t443, t432, gm_srcl8pn2pjtmj6k, 499, 45);
                                                uint64_t t444 = ((uintptr_t)t441) - (uintptr_t)t433;
                                                gm_b16_8 t445;
                                                GM_ST(void *, (uint8_t *)&t445, (void *)(((uint8_t *)t431 + (uintptr_t)t433 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t445 + 8, (uintptr_t)(t444));
                                                void * t446 = gm_l4;
                                                gm_f_hash_val_nr2e99r((void *)&gm_l57, (void *)NULL, cx, NULL, t428, t429, t430, t445, t446);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t447 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                                if (t447) {
                                                } else {
                                                    gm_t76123gfet73g t448 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l57) + 8));
                                                    gm_tcfa6l7reoosn t449;
                                                    GM_ST(uint8_t, (uint8_t *)&t449 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t449 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t449 + 4, 0);
                                                    ((uint8_t *)&t449)[0] = 1;
                                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t449 + 8), t448);
                                                    memmove(gm_out, ((uint8_t *)&t449), 32);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb48;
                                            }
gm_lb49:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 496, 13);
                                        }
gm_lb48:;
                                    }
gm_lb47:;
                                    uint64_t t450 = gm_l33;
                                    uint64_t r451; bool o452 = __builtin_add_overflow(t450, ((uint64_t)1ULL), &r451);
                                    uint64_t t453 = r451;
                                    gm_l33 = t453;
                                }
                            }
gm_lb45:;
                            goto gm_lb43;
                        }
gm_lb44:;
                        void * t454 = gm_l0;
                        gm_tl874t2atnlh3 t455 = gm_l1;
                        void * t456 = gm_l0;
                        void * t457 = GM_LD(void *, (((uint8_t *)t456) + 8));
                        gm_t88rcucg1b7uk t458 = gm_l2;
                        gm_b16_8 t459 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t457, t458);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t460;
                        GM_ST(gm_b16_8, ((uint8_t *)&t460 + 0), t459);
                        gm_f_format_8k5eh95((void *)&gm_l58, (void *)NULL, cx, NULL, t460);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t461 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                        if (!t461) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srcl8pn2pjtmj6k, 495, 62);
                        gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                        gm_b16_8 t463 = GM_LD(gm_b16_8, ((uint8_t *)&t462));
                        gm_t76123gfet73g t464;
                        gm_f_trap_qh84l9d((void *)&t464, (void *)NULL, cx, NULL, t454, t455, t463);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t465;
                        GM_ST(uint8_t, (uint8_t *)&t465 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t465 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t465 + 4, 0);
                        ((uint8_t *)&t465)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t465 + 8), t464);
                        memmove(gm_out, ((uint8_t *)&t465), 32);
                        goto gm_fdone;
                    }
gm_lb43:;
                    goto gm_lb1;
                }
gm_lb36:;
            }
            {
                {
                    void * t466 = gm_l0;
                    gm_tl874t2atnlh3 t467 = gm_l1;
                    void * t468 = gm_l0;
                    void * t469 = GM_LD(void *, (((uint8_t *)t468) + 8));
                    gm_t88rcucg1b7uk t470 = gm_l2;
                    gm_b16_8 t471 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t469, t470);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t472;
                    GM_ST(gm_b16_8, ((uint8_t *)&t472 + 0), t471);
                    gm_f_format_8k5eh95((void *)&gm_l59, (void *)NULL, cx, NULL, t472);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t473 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                    if (!t473) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 8, gm_srcl8pn2pjtmj6k, 502, 35);
                    gm_b16_8 t474 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
                    gm_b16_8 t475 = GM_LD(gm_b16_8, ((uint8_t *)&t474));
                    gm_t76123gfet73g t476;
                    gm_f_trap_qh84l9d((void *)&t476, (void *)NULL, cx, NULL, t466, t467, t475);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t477;
                    GM_ST(uint8_t, (uint8_t *)&t477 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t477 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t477 + 4, 0);
                    ((uint8_t *)&t477)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t477 + 8), t476);
                    memmove(gm_out, ((uint8_t *)&t477), 32);
                    goto gm_fdone;
                }
gm_lb50:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 432, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tcfa6l7reoosn t478;
        GM_ST(uint8_t, (uint8_t *)&t478 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t478 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t478 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t478 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t478 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t478 + 24, 0);
        ((uint8_t *)&t478)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t478 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t478), 32);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_int_text_vrjnuu2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* v */
    gm_ttovhfee8mf28 gm_l4; /* info */
    GM_STACK_CHECK(&F, 432u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tg5lql6itufk7 gm_l5;
    gm_ttovhfee8mf28 gm_l6;
    gm_b16_8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
    gm_l3 = t5;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_tg5lql6itufk7 t8 = gm_f_int_info_ulcevdv(t6, GM_CONV(gm_csguankko4lmhd, t7));
    gm_l5 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t9) {
        gm_ttovhfee8mf28 t10 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l5));
        gm_l6 = t10;
    } else {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
        F.ret = t11;
        goto gm_fret;
    }
    gm_ttovhfee8mf28 t12 = gm_l6;
    gm_l4 = t12;
    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&gm_l4));
    if (t13) {
        unsigned __int128 t14 = gm_l3;
        __int128 t15;
        memset(&t15, 0, sizeof t15); memcpy(&t15, ((uint8_t *)&t14), sizeof t15 < 16 ? sizeof t15 : 16);
        gm_tpl3q6e8s9qo1 t16;
        GM_ST(__int128, ((uint8_t *)&t16 + 0), t15);
        gm_f_format_9qvncot((void *)&gm_l8, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcl8pn2pjtmj6k, 185, 29);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_l7 = t19;
    } else {
        unsigned __int128 t20 = gm_l3;
        gm_tsn2omssg6cpf t21;
        GM_ST(unsigned __int128, ((uint8_t *)&t21 + 0), t20);
        gm_f_format_ra7pont((void *)&gm_l9, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcl8pn2pjtmj6k, 185, 70);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_l7 = t24;
    }
    gm_b16_8 t25 = gm_l7;
    F.ret = t25;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_align_lavuopj F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* p */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l5;
    uint64_t gm_l6;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_f_kind_htv6093((void *)&gm_l5, t9, GM_CONV(gm_csguankko4lmhd, t10));
    {
        {
            {
                {
                    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t12 = (uint8_t)(t11 == ((uint8_t)8ULL));
                    if (!t12) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                    gm_l2 = t13;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)9ULL));
                    if (!t15) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                    gm_l2 = t16;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t17 = gm_l0;
            gm_t88rcucg1b7uk t18 = gm_l2;
            uint64_t t19 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = ((t19 == ((uint64_t)0ULL)));
            if (t20) {
                gm_l6 = ((uint64_t)1ULL);
            } else {
                void * t21 = gm_l0;
                gm_t88rcucg1b7uk t22 = gm_l2;
                uint64_t t23 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t23;
            }
            uint64_t t24 = gm_l6;
            F.ret = t24;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)13ULL));
            if (!t26) {
                goto gm_lb6;
            }
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t27;
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l4;
            uint64_t t30 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t30;
            goto gm_fret;
        }
gm_lb6:;
        {
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 174, 5);
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
