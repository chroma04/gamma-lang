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
typedef struct gm_csqbqksqco2bm6 { uint32_t f0; void * f1; } gm_csqbqksqco2bm6;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csqbqksqco2bm6) == 16 && _Alignof(gm_csqbqksqco2bm6) == 8 && offsetof(gm_csqbqksqco2bm6, f0) == 0 && offsetof(gm_csqbqksqco2bm6, f1) == 8, "layout of `FieldPat`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tdu0qimd4ae4c;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tdu0qimd4ae4c) == 32 && _Alignof(gm_tdu0qimd4ae4c) == 8 && offsetof(gm_tdu0qimd4ae4c, f0) == 0 && offsetof(gm_tdu0qimd4ae4c, f1) == 16 && offsetof(gm_tdu0qimd4ae4c, f2) == 20, "layout of `PatBind`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
typedef struct { gm_t428ue2u3hf5g f0; uint8_t f1; uint8_t pe[7]; } gm_trgmlgt2gmmjs;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_trgmlgt2gmmjs) == 40 && _Alignof(gm_trgmlgt2gmmjs) == 8 && offsetof(gm_trgmlgt2gmmjs, f0) == 0 && offsetof(gm_trgmlgt2gmmjs, f1) == 32, "layout of `?ConstKey`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bind_pattern_3potkvo {
    gm_frame h;
    void * ret;
};
struct gm_fr_bind_unchecked_5nngm43 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bound_n051ued {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_c_union_match_2tgsktr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_cmp_i1knrgu {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    int32_t ret;
};
struct gm_fr_const_pattern_value_kf1he7o {
    gm_frame h;
    gm_tbci5ospk5nnn l5; /* i */
    gm_t6evimt84lndq l6; /* c */
    gm_tbci5ospk5nnn l10; /* n */
    void * ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_declare_binds_rk1ncsj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ilis91k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uda0n0p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_18lv8vg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2n44ecs {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k1v0hr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_67q8e18 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6nnjhja {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_81ifu7p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_99i3s4a {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b3e91bl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_chnm9ud {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cqh7lia {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dd92ata {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eb4sgm9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i4872o7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ktbu9b9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l002mid {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l7hthr8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lcm4k53 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m9kjvp2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_npjtqgc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qm09p7m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tb06a6e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tn4egi5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v40sfri {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vcvtacd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_integer_pattern_ty_ovv2ftb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_c_union_p1r4eve {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_i33ccm0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_literal_pattern_qio1dmp {
    gm_frame h;
    gm_tbci5ospk5nnn l8; /* v */
    void * ret;
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
struct gm_fr_names_constant_m4v5hk8 {
    gm_frame h;
    gm_t930q5l38hdap l2; /* b */
    uint8_t ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_path_pattern_l6sl3lm {
    gm_frame h;
    void * ret;
};
struct gm_fr_pattern_5gn6kid {
    gm_frame h;
    void * ret;
};
struct gm_fr_pattern_here_fg1q9vt {
    gm_frame h;
    gm_tbci5ospk5nnn l15; /* l */
    gm_tbci5ospk5nnn l16; /* h */
    void * ret;
};
struct gm_fr_pattern_int_pp4f5ak {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* v */
    gm_tkv1vap90vht2 l7; /* r */
    gm_t5vbkdq81qfs7 l8; /* x */
    gm_t0ialelq3ql1k l9; /* u */
    gm_tbci5ospk5nnn l10; /* i */
    gm_tbci5ospk5nnn l13; /* i */
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_payload_pattern_28pvmeh {
    gm_frame h;
    gm_t4gut2c9fj1kt l8; /* d */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_31airv7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l74t9rq {
    gm_frame h;
    gm_tnrgict8sthcv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_qg9npma {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_br5lm9l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_mktnnke {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t5pgl2t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_int_ub6npub {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
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
struct gm_fr_struct_pattern_4ln2pok {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* names */
    gm_trqfvmjtt4sij l13; /* ftys */
    gm_t0mglksrkfa5e l16; /* f */
    gm_trqfvmjtt4sij l17; /* args */
    gm_tnrgict8sthcv l20; /* a */
    gm_tnrgict8sthcv l24; /* a */
    void * ret;
};
struct gm_fr_sub_patterns_jr84m36 {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_variant_pattern_8aa0fg2 {
    gm_frame h;
    gm_t4gut2c9fj1kt l9; /* d */
    gm_tckdobunaulee l15; /* v */
    void * ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_bind_pattern_3potkvo(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_unchecked_5nngm43(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_bound_n051ued(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_c_union_match_2tgsktr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_const_pattern_value_kf1he7o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uda0n0p(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_evaluate_ukvgal2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_18lv8vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2bh0ost(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_2n44ecs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_67q8e18(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6nnjhja(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_81ifu7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_99i3s4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_b3e91bl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_chnm9ud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cqh7lia(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_dd92ata(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_eb4sgm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_i4872o7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l7hthr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lcm4k53(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_m9kjvp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_npjtqgc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qm09p7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tb06a6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_tn4egi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_v40sfri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vcvtacd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_o9r57ha(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_integer_pattern_ty_ovv2ftb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_c_union_p1r4eve(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_i33ccm0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_literal_pattern_qio1dmp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_names_constant_m4v5hk8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_path_pattern_l6sl3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_t9qkh0rsooc8d a4, void * a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) void * gm_f_pattern_here_fg1q9vt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) void gm_f_pattern_int_pp4f5ak(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_payload_pattern_28pvmeh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, const void * a2, const void * a3, gm_t9qkh0rsooc8d a4, void * a5, gm_tl874t2atnlh3 a6);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_31airv7(void * a0, gm_csqbqksqco2bm6 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qg9npma(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_br5lm9l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_mktnnke(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_integer_66qj2qi(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_struct_pattern_4ln2pok(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t9qkh0rsooc8d a5, void * a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_patterns_jr84m36(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_t9qkh0rsooc8d a4, void * a5, gm_tl874t2atnlh3 a6, uint8_t a7);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_variant_pattern_8aa0fg2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t9qkh0rsooc8d a5, void * a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_ksksp29ghso[40] __attribute__((aligned(8)));
extern const uint8_t gm_s026u6q948evn[5];
extern const uint8_t gm_s07uh61s060o9[25];
extern const uint8_t gm_s0f4hcujnvqta[95];
extern const uint8_t gm_s1ffqahkqb2ed[141];
extern const uint8_t gm_s297r0ooe1bmq[29];
extern const uint8_t gm_s2aov9df7ef50[53];
extern const uint8_t gm_s2hgdf6b9d53s[5];
extern const uint8_t gm_s3fj8brek97co[86];
extern const uint8_t gm_s3s5u5e6aukj7[48];
extern const uint8_t gm_s4mghosb8ogmj[56];
extern const uint8_t gm_s67lg9pjq86ie[55];
extern const uint8_t gm_s6c1femb11dge[76];
extern const uint8_t gm_s6cmbp3c9g09j[61];
extern const uint8_t gm_s89j7s4avcmfs[28];
extern const uint8_t gm_s8fq66mhsgcib[104];
extern const uint8_t gm_s9d2uvo0cndd7[4];
extern const uint8_t gm_s9omga3p3drmu[51];
extern const uint8_t gm_s9rhhj2j5hs2v[36];
extern const uint8_t gm_sa9ukgikn2pki[26];
extern const uint8_t gm_saeed1dnph0jk[52];
extern const uint8_t gm_saqh6vgbmf8m2[153];
extern const uint8_t gm_savtbnmf9bjnr[17];
extern const uint8_t gm_sb03flnhs22vb[3];
extern const uint8_t gm_sbapuchngtvak[28];
extern const uint8_t gm_sd7ar5jm4rru0[48];
extern const uint8_t gm_sd7guj32ltqh8[47];
extern const uint8_t gm_sem5pak34704h[35];
extern const uint8_t gm_setnqjs5cth99[45];
extern const uint8_t gm_sgcuckp03jlhp[23];
extern const uint8_t gm_sglgjrjci6t1d[25];
extern const uint8_t gm_sha819aic5lkk[22];
extern const uint8_t gm_shq89iblo3j6k[51];
extern const uint8_t gm_siq422vplpm2h[54];
extern const uint8_t gm_sj4epgmk695op[75];
extern const uint8_t gm_sks5ueb3h9e10[24];
extern const uint8_t gm_smbkn5defd9ht[48];
extern const uint8_t gm_snti96ut68ga4[120];
extern const uint8_t gm_so5215183on4e[58];
extern const uint8_t gm_sp7me12frc01v[22];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsk3h2du2ldnc[];
extern const uint8_t gm_srqehfttj6ngk[4];
extern const uint8_t gm_srv8c2gciqkuj[48];
extern const uint8_t gm_ssdho4pv9rajn[55];
extern const uint8_t gm_stfs4nd3l87tt[48];
extern const gm_tmpl gm_tpl0gp8c85b2lf5;
extern const gm_tmpl gm_tpl13591hp4g6qh;
extern const gm_tmpl gm_tpl17l9mok2be4a;
extern const gm_tmpl gm_tpl1ige45obm306;
extern const gm_tmpl gm_tpl5g9nntg83p8b;
extern const gm_tmpl gm_tpl6104ee3qfshr;
extern const gm_tmpl gm_tpl6186pbdul6ge;
extern const gm_tmpl gm_tpl6u21hsinpli4;
extern const gm_tmpl gm_tpl7cjap0b34jjm;
extern const gm_tmpl gm_tpl86i0s811rrlp;
extern const gm_tmpl gm_tpl9l4fqsojrjsa;
extern const gm_tmpl gm_tplbaonrrpbr1uh;
extern const gm_tmpl gm_tplbj3gvifc61li;
extern const gm_tmpl gm_tpldqr3c69jgnqi;
extern const gm_tmpl gm_tplf0dvnr2mq4ft;
extern const gm_tmpl gm_tplg575kk57c4i8;
extern const gm_tmpl gm_tplhdrbi5vdvbe6;
extern const gm_tmpl gm_tplhpkbpbdl6lee;
extern const gm_tmpl gm_tpllsaus03ne167;
extern const gm_tmpl gm_tpln4t9g9do4qg7;
extern const gm_tmpl gm_tplnoi44u4met07;
extern const gm_tmpl gm_tplq5eond7ctc71;
extern const gm_tmpl gm_tplu0n2ra1s30o8;
extern const gm_tmpl gm_tplvle6f1rmlmjh;
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
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_c_union_match_2tgsktr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_c_union_match_2tgsktr F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_tb06a6e((void *)&gm_l2, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcsk3h2du2ldnc, 478, 63);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_i1knrgu F;
    int32_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = (!(t2 == t3));
    if (t4) {
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a0));
        if (t5) {
            F.ret = ((int32_t)-1LL);
            goto gm_fret;
        }
        F.ret = ((int32_t)1LL);
        goto gm_fret;
    }
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    int32_t t10 = gm_f_mag_cmp_s39veq6(t7, t9);
    gm_l2 = t10;
    uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t11) {
        int32_t t12 = gm_l2;
        if (t12 == ((int32_t)-2147483648LL)) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 360, 28);
        int32_t t13 = (int32_t)((uint32_t)0 - (uint32_t)t12);
        F.ret = t13;
        goto gm_fret;
    }
    int32_t t14 = gm_l2;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t3, t5);
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srclv3pj5es0f8o, 330, 5);
    uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)a0));
    gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_tbci5ospk5nnn t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t10 + 8), t9);
    memmove(gm_out, ((uint8_t *)&t10), 48);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ilis91k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uda0n0p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uda0n0p F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_count_omtt6qd F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 48));
    F.ret = t3;
    goto gm_fret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_2onqfca(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 4);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
inline __attribute__((aligned(4))) void gm_f_format_18lv8vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_18lv8vg F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snti96ut68ga4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(119));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpln4t9g9do4qg7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_2n44ecs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2n44ecs F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_stfs4nd3l87tt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5g9nntg83p8b), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_5k1v0hr F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_savtbnmf9bjnr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvle6f1rmlmjh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_67q8e18(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_67q8e18 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4mghosb8ogmj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(55));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu0n2ra1s30o8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6nnjhja(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6nnjhja F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0f4hcujnvqta));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(94));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhdrbi5vdvbe6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_81ifu7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_81ifu7p F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd7ar5jm4rru0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplf0dvnr2mq4ft), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_99i3s4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_99i3s4a F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sj4epgmk695op));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(74));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnoi44u4met07), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_b3e91bl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b3e91bl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9rhhj2j5hs2v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhpkbpbdl6lee), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_chnm9ud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_chnm9ud F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srv8c2gciqkuj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbj3gvifc61li), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cqh7lia(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cqh7lia F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3s5u5e6aukj7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl13591hp4g6qh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dd92ata(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dd92ata F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa9ukgikn2pki));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbaonrrpbr1uh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_eb4sgm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_eb4sgm9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9omga3p3drmu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0gp8c85b2lf5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_i4872o7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_i4872o7 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saeed1dnph0jk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllsaus03ne167), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ktbu9b9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbapuchngtvak));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq5eond7ctc71), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l002mid F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sglgjrjci6t1d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg575kk57c4i8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l7hthr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l7hthr8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3fj8brek97co));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(85));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6u21hsinpli4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lcm4k53(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lcm4k53 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siq422vplpm2h));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6104ee3qfshr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_m9kjvp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_m9kjvp2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smbkn5defd9ht));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7cjap0b34jjm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_npjtqgc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_npjtqgc F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2aov9df7ef50));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9l4fqsojrjsa), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qm09p7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qm09p7m F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sks5ueb3h9e10));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1ige45obm306), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tb06a6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tb06a6e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8fq66mhsgcib));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(103));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl86i0s811rrlp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tn4egi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tn4egi5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssdho4pv9rajn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldqr3c69jgnqi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v40sfri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v40sfri F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sha819aic5lkk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl17l9mok2be4a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vcvtacd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_vcvtacd F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shq89iblo3j6k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_2bh0ost((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6186pbdul6ge), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) void gm_f_list_i33ccm0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_i33ccm0 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_uda0n0p();
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
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t gm_l2;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)&F.l0) + 8) + 8));
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        gm_l2 = t6;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l2;
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t7);
    memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_31airv7(void * a0, gm_csqbqksqco2bm6 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_31airv7 F;
    void * gm_l0; /* l */
    gm_ttggq7ng4kp3p gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_ttggq7ng4kp3p, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_uda0n0p();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_br5lm9l(t8, ((uint64_t)1ULL), t10);
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
    gm_ttggq7ng4kp3p t18 = gm_l1;
    GM_ST(gm_ttggq7ng4kp3p, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l74t9rq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_r9g5q6n();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_t5pgl2t(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 120), ((uint8_t *)&F.l1), 120);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qg9npma(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_qg9npma F;
    void * gm_l0; /* l */
    gm_tdu0qimd4ae4c gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_ilis91k();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_mktnnke(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_br5lm9l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_br5lm9l F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_mktnnke(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_mktnnke F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t5pgl2t F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_int_ub6npub F;
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l1;
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    uint8_t t3 = (t2 <= ((uint64_t)512ULL));
    if (t3) {
        gm_b16_8 t4 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        F.ret = t5;
        goto gm_fret;
    }
    uint64_t t6 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    gm_t4ce0jd3tdro4 t7;
    GM_ST(uint64_t, ((uint8_t *)&t7 + 0), t6);
    gm_f_format_5k1v0hr((void *)&gm_l1, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src7h8dni243k2v, 68, 12);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 8), t2);
    memmove(gm_out, ((uint8_t *)&t3), 48);
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
inline __attribute__((aligned(4))) void * gm_f_bind_pattern_3potkvo(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_bind_pattern_3potkvo F;
    void * gm_l0; /* cx */
    gm_t08olq97rscn3 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_t9qkh0rsooc8d gm_l3; /* ctx */
    void * gm_l4; /* binds */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint32_t gm_l6; /* id */
    GM_STACK_CHECK(&F, 822u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_t9qkh0rsooc8d t2 = gm_l3;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    uint8_t t4 = (uint8_t)((t3 == ((uint8_t)2ULL)));
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
    if (t4) {
        void * t6 = gm_l0;
        gm_tl874t2atnlh3 t7 = gm_l5;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s6c1femb11dge));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(75));
        gm_b0_1 t9 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t10 = gm_l2;
        gm_tl874t2atnlh3 t11 = gm_l5;
        gm_tb38v5s9j1r32 t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
        void * t13 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t10, &t12, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t13;
        goto gm_fret;
    }
    void * t14 = gm_l0;
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    uint8_t t16 = gm_f_names_constant_m4v5hk8((void *)NULL, cx, NULL, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t16) {
        void * t17 = gm_l0;
        gm_tl874t2atnlh3 t18 = gm_l5;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_t6j6smj7qoctu t21;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 16), t20);
        gm_f_format_18lv8vg((void *)&gm_l7, (void *)NULL, cx, NULL, &t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcsk3h2du2ldnc, 89, 24);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_b0_1 t25 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t17, t18, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t26 = gm_l0;
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_t88rcucg1b7uk t28 = gm_l2;
    gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
    uint32_t t30 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t26, t27, t28, ((uint8_t)0), t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t30;
    void * t31 = gm_l4;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    uint8_t t33 = gm_f_bound_n051ued(t31, t32);
    if (t33) {
        void * t34 = gm_l0;
        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_tnpifv20irnph t37;
        GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
        gm_f_format_b3e91bl((void *)&gm_l8, (void *)NULL, cx, NULL, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcsk3h2du2ldnc, 94, 29);
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
        gm_b0_1 t41 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t34, t35, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t42 = gm_l4;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        uint32_t t44 = gm_l6;
        gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_tdu0qimd4ae4c t46;
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t43);
        GM_ST(uint32_t, ((uint8_t *)&t46 + 16), t44);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t46 + 20), t45);
        gm_tftb1odc739t5 t47 = gm_f_push_qg9npma(t42, &t46);
        gm_l9 = t47;
        uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcsk3h2du2ldnc, 96, 9);
    }
    gm_t88rcucg1b7uk t49 = gm_l2;
    uint32_t t50 = gm_l6;
    gm_tl874t2atnlh3 t51 = gm_l5;
    gm_tb38v5s9j1r32 t52;
    memset(&t52, 0, sizeof t52);
    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
    GM_ST(uint32_t, ((uint8_t *)&t52 + 8), t50);
    void * t53 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t49, &t52, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t53;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_unchecked_5nngm43(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_bind_unchecked_5nngm43 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* ctx */
    void * gm_l3; /* binds */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_t08olq97rscn3 gm_l5; /* name */
    uint32_t gm_l6; /* id */
    void * gm_l7; /* inner */
    gm_tagc77imt2chm gm_l8; /* pp */
    gm_b16_8 gm_l9; /* args */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l13; /* args */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    GM_STACK_CHECK(&F, 668u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l17;
    void * t25 = gm_l1;
    {
        {
            {
                {
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                    if (!t27) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t25) + 8) + 8), 32);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)2ULL));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t25) + 8) + 8), 32);
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            {
                {
                    gm_t9qkh0rsooc8d t30 = gm_l2;
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
                    uint8_t t32 = (uint8_t)(!(t31 == ((uint8_t)2ULL)));
                    if (!t32) {
                        goto gm_lb7;
                    }
                    void * t33 = gm_l3;
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_t9qkh0rsooc8d t35;
                    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)2ULL));
                    uint8_t t36 = gm_f_bound_n051ued(t33, t34);
                    uint8_t t37 = (uint8_t)!t36;
                    if (!t37) {
                        goto gm_lb7;
                    }
                    void * t38 = gm_l0;
                    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    void * t40 = gm_l0;
                    void * t41 = GM_LD(void *, ((uint8_t *)t40));
                    void * t42 = (void *)(((uint8_t *)t41) + 8);
                    gm_t88rcucg1b7uk t43 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                    uint32_t t45 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t38, t39, t43, ((uint8_t)0), t44);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l6 = t45;
                    void * t46 = gm_l3;
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    uint32_t t48 = gm_l6;
                    gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                    gm_tdu0qimd4ae4c t50;
                    GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t47);
                    GM_ST(uint32_t, ((uint8_t *)&t50 + 16), t48);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t50 + 20), t49);
                    gm_tftb1odc739t5 t51 = gm_f_push_qg9npma(t46, &t50);
                    gm_l17 = t51;
                    uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcsk3h2du2ldnc, 29, 13);
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)6ULL));
            if (!t54) {
                goto gm_lb8;
            }
            void * t55 = GM_LD(void *, ((((uint8_t *)t25) + 8) + 8));
            gm_l7 = t55;
            void * t56 = gm_l0;
            void * t57 = gm_l7;
            gm_t9qkh0rsooc8d t58 = gm_l2;
            void * t59 = gm_l3;
            gm_b0_1 t60 = gm_f_bind_unchecked_5nngm43((void *)NULL, cx, NULL, t56, t57, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)7ULL));
            if (!t62) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t25) + 8) + 8), 32);
            {
                {
                    uint8_t t63 = (uint8_t)(((((uint8_t *)&gm_l8) + 8))[16] != 0);
                    if (!t63) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                    gm_l9 = t64;
                    gm_l11 = ((uint64_t)0ULL);
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    gm_l10 = t65;
                    {
                        for (;;) {
                            uint64_t t66 = gm_l11;
                            uint64_t t67 = gm_l10;
                            uint8_t t68 = (t66 < t67);
                            if (!t68) {
                                goto gm_lb12;
                            }
                            {
                                {
                                    {
                                        uint64_t t69 = gm_l11;
                                        gm_l12 = t69;
                                        void * t70 = gm_l0;
                                        void * t71 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint64_t t73 = gm_l12;
                                        void * t74 = (void *)((((uint8_t *)t71) + (uintptr_t)t73 * 120) + 40);
                                        gm_t9qkh0rsooc8d t75 = gm_l2;
                                        void * t76 = gm_l3;
                                        gm_b0_1 t77 = gm_f_bind_unchecked_5nngm43((void *)NULL, cx, NULL, t70, t74, t75, t76);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 33, 13);
                                }
gm_lb15:;
                            }
gm_lb14:;
                            uint64_t t78 = gm_l11;
                            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                            uint64_t t81 = r79;
                            gm_l11 = t81;
                        }
                    }
gm_lb12:;
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)8ULL));
            if (!t83) {
                goto gm_lb17;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((((uint8_t *)t25) + 8) + 8));
            gm_l13 = t84;
            gm_l15 = ((uint64_t)0ULL);
            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
            gm_l14 = t85;
            {
                for (;;) {
                    uint64_t t86 = gm_l15;
                    uint64_t t87 = gm_l14;
                    uint8_t t88 = (t86 < t87);
                    if (!t88) {
                        goto gm_lb18;
                    }
                    {
                        {
                            {
                                uint64_t t89 = gm_l15;
                                gm_l16 = t89;
                                void * t90 = gm_l0;
                                void * t91 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                uint64_t t93 = gm_l16;
                                void * t94 = (void *)((((uint8_t *)t91) + (uintptr_t)t93 * 120) + 40);
                                gm_t9qkh0rsooc8d t95 = gm_l2;
                                void * t96 = gm_l3;
                                gm_b0_1 t97 = gm_f_bind_unchecked_5nngm43((void *)NULL, cx, NULL, t90, t94, t95, t96);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb21;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 35, 25);
                        }
gm_lb21:;
                    }
gm_lb20:;
                    uint64_t t98 = gm_l15;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l15 = t101;
                }
            }
gm_lb18:;
            goto gm_lb1;
        }
gm_lb17:;
        {
            goto gm_lb1;
        }
gm_lb23:;
        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 26, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_bound_n051ued(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bound_n051ued F;
    void * gm_l0; /* binds */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l4; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
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
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 41, 5);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_c_union_match_2tgsktr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_const_pattern_value_kf1he7o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_const_pattern_value_kf1he7o F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* tys */
    void * gm_l4; /* v */
    gm_t428ue2u3hf5g gm_l7; /* key */
    gm_b16_8 gm_l8; /* b */
    gm_b16_8 gm_l9; /* b */
    GM_STACK_CHECK(&F, 1927u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tjoi8o798ne9k gm_l15;
    gm_t6evimt84lndq gm_l16;
    gm_trgmlgt2gmmjs gm_l17;
    gm_t428ue2u3hf5g gm_l18;
    uint8_t gm_l19;
    gm_thrdpodrs8pgt gm_l20;
    gm_tbci5ospk5nnn gm_l21;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_l3 = t10;
    void * t11 = gm_l0;
    void * t12 = gm_l1;
    gm_t88rcucg1b7uk t13 = gm_l2;
    gm_tkn42u7na3t81 t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[4] = 1;
    void * t15 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t11, t12, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t15;
    void * t16 = gm_l3;
    void * t17 = gm_l4;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
    uint8_t t19 = gm_f_is_error_8k27gd1(t16, GM_CONV(gm_csguankko4lmhd, t18));
    if (t19) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    {
        {
            void * t20 = gm_l4;
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
            if (!t22) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l5), ((((uint8_t *)t20) + 8) + 8), 48);
            void * t23 = gm_l3;
            gm_t88rcucg1b7uk t24 = gm_l2;
            uint8_t t25 = gm_f_is_integer_8d4f9ek(t23, GM_CONV(gm_csguankko4lmhd, t24));
            if (!t25) {
                goto gm_lb2;
            }
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, ((uint8_t *)t26));
            gm_t88rcucg1b7uk t28 = gm_l2;
            uint8_t t29 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t27, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = (uint8_t)!t29;
            if (t30) {
                void * t31 = gm_l0;
                void * t32 = gm_l1;
                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 72));
                gm_b16_8 t34 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t35 = gm_l0;
                gm_t88rcucg1b7uk t36 = gm_l2;
                gm_b16_8 t37 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t38;
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t34);
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 16), t37);
                gm_f_format_qm09p7m((void *)&gm_l11, (void *)NULL, cx, NULL, &t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcsk3h2du2ldnc, 259, 30);
                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                gm_b0_1 t42 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t31, t33, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_t88rcucg1b7uk t43 = gm_l2;
            gm_tbci5ospk5nnn t44 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l5));
            void * t45 = gm_l1;
            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 72));
            gm_t2b6tm8c0vgo1 t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t47 + 8), t44);
            void * t48 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t43, &t47, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t48;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t49 = gm_l0;
    void * t50 = gm_l4;
    gm_t88rcucg1b7uk t51 = gm_l2;
    void * t52 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t49, t50, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t52;
    void * t53 = gm_l3;
    gm_t88rcucg1b7uk t54 = gm_l2;
    uint8_t t55 = gm_f_is_integer_8d4f9ek(t53, GM_CONV(gm_csguankko4lmhd, t54));
    if (t55) {
        gm_l13 = ((uint8_t)1);
    } else {
        void * t56 = gm_l3;
        gm_t88rcucg1b7uk t57 = gm_l2;
        gm_t53at80pfdus3 t58;
        gm_f_kind_jcnt4hm((void *)&t58, t56, GM_CONV(gm_csguankko4lmhd, t57));
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        uint8_t t60 = (uint8_t)((t59 == ((uint8_t)4ULL)));
        gm_t53at80pfdus3 t61;
        memset(&t61, 0, sizeof t61);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)4ULL));
        gm_l13 = t60;
    }
    uint8_t t62 = gm_l13;
    if (t62) {
        gm_l12 = ((uint8_t)1);
    } else {
        gm_t88rcucg1b7uk t63 = gm_l2;
        void * t64 = gm_l3;
        gm_t88rcucg1b7uk t65 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t66 = (gm_eq9m6mvtmgde(((uint8_t *)&t63), ((uint8_t *)&t65)));
        gm_l12 = t66;
    }
    uint8_t t67 = gm_l12;
    uint8_t t68 = (uint8_t)!t67;
    if (t68) {
        void * t69 = gm_l0;
        void * t70 = gm_l1;
        gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
        void * t72 = gm_l0;
        gm_t88rcucg1b7uk t73 = gm_l2;
        gm_b16_8 t74 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t75;
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t74);
        gm_f_format_l7hthr8((void *)&gm_l14, (void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcsk3h2du2ldnc, 266, 26);
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b0_1 t79 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t69, t71, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t80 = gm_l0;
    void * t81 = GM_LD(void *, ((uint8_t *)t80));
    void * t82 = gm_l4;
    void * t83 = gm_l0;
    void * t84 = GM_LD(void *, ((uint8_t *)t83));
    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t84) + 2432));
    gm_f_evaluate_ukvgal2((void *)&gm_l15, (void *)NULL, cx, NULL, t81, t82, t85);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t86 = (uint8_t)((((uint8_t *)&gm_l15))[48] != 0);
    if (t86) {
        memmove(((uint8_t *)&gm_l16), ((uint8_t *)&gm_l15), 48);
    } else {
        void * t87 = gm_l0;
        void * t88 = gm_l1;
        gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t88) + 72));
        gm_b16_8 t90;
        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_s07uh61s060o9));
        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(24));
        gm_b0_1 t91 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t87, t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((void *)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l16), 48);
    void * t92 = gm_l0;
    void * t93 = GM_LD(void *, ((uint8_t *)t92));
    gm_t88rcucg1b7uk t94 = gm_l2;
    void * t95 = gm_l1;
    gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t95) + 72));
    gm_t6evimt84lndq t97 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l6));
    gm_f_value_key_vki574i((void *)&gm_l17, (void *)NULL, cx, NULL, t93, &t97, t94, t96);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t98 = (uint8_t)((((uint8_t *)&gm_l17))[32] != 0);
    if (t98) {
        memmove(((uint8_t *)&gm_l18), ((uint8_t *)&gm_l17), 32);
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l18), 32);
    {
        {
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)3ULL));
            if (!t100) {
                goto gm_lb4;
            }
            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_l8 = t101;
            gm_t88rcucg1b7uk t102 = gm_l2;
            gm_b16_8 t103 = gm_l8;
            void * t104 = gm_l1;
            gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t104) + 72));
            gm_t2b6tm8c0vgo1 t106;
            memset(&t106, 0, sizeof t106);
            GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t106 + 8), t103);
            void * t107 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t102, &t106, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t107;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)0ULL));
            if (!t109) {
                goto gm_lb5;
            }
            gm_b16_8 t110 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 8));
            gm_l9 = t110;
            void * t111 = gm_l3;
            gm_t88rcucg1b7uk t112 = gm_l2;
            gm_t53at80pfdus3 t113;
            gm_f_kind_jcnt4hm((void *)&t113, t111, GM_CONV(gm_csguankko4lmhd, t112));
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&t113) + 0));
            uint8_t t115 = (uint8_t)((t114 == ((uint8_t)4ULL)));
            gm_t53at80pfdus3 t116;
            memset(&t116, 0, sizeof t116);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)4ULL));
            if (t115) {
                gm_t88rcucg1b7uk t117 = gm_l2;
                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t119 = (t118 > ((uint64_t)0ULL));
                if (t119) {
                    void * t120 = GM_LD(void *, ((uint8_t *)&gm_l9));
                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)t120) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t123 = ((t122 == ((uint8_t)1ULL)));
                    gm_l19 = t123;
                } else {
                    gm_l19 = ((uint8_t)0);
                }
                uint8_t t124 = gm_l19;
                void * t125 = gm_l1;
                gm_tl874t2atnlh3 t126 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t125) + 72));
                gm_t2b6tm8c0vgo1 t127;
                memset(&t127, 0, sizeof t127);
                GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)4ULL));
                GM_ST(uint8_t, ((uint8_t *)&t127 + 8), t124);
                void * t128 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t117, &t127, t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t128;
                goto gm_fret;
            }
            void * t129 = gm_l0;
            void * t130 = GM_LD(void *, ((uint8_t *)t129));
            gm_t428ue2u3hf5g t131 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l7));
            gm_f_key_int_pqscuvn((void *)&gm_l20, (void *)NULL, cx, NULL, t130, &t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l20))[48] != 0);
            if (t132) {
                memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l20), 48);
            } else {
                F.ret = ((void *)0);
                goto gm_fret;
            }
            memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l21), 48);
            gm_t88rcucg1b7uk t133 = gm_l2;
            gm_tbci5ospk5nnn t134 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l10));
            void * t135 = gm_l1;
            gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t135) + 72));
            gm_t2b6tm8c0vgo1 t137;
            GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t137 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t137 + 8), t134);
            void * t138 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t133, &t137, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t138;
            goto gm_fret;
        }
gm_lb5:;
        {
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 274, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_declare_binds_rk1ncsj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* binds */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l4; /* b */
    GM_STACK_CHECK(&F, 401u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l1;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
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
                        void * t15 = gm_l0;
                        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 16));
                        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 20));
                        gm_t930q5l38hdap t19;
                        memset(&t19, 0, sizeof t19);
                        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t19 + 8), t17);
                        gm_b0_1 t20 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t15, t16, &t19, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 468, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l3;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l3 = t24;
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
inline __attribute__((aligned(4))) uint8_t gm_f_integer_pattern_ty_ovv2ftb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_integer_pattern_ty_ovv2ftb F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (t6) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 96);
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_taari1s0m51a7 t13 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t8, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t15;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[1] = 1;
    uint8_t t16 = (gm_eq5307j5k9ms(((uint8_t *)&t13), ((uint8_t *)&t15)));
    if (t16) {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 96);
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        gm_t88rcucg1b7uk t22 = gm_l1;
        uint8_t t23 = gm_f_require_integer_66qj2qi((void *)NULL, cx, NULL, t18, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t23;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t24 = gm_l2;
    F.ret = t24;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_c_union_p1r4eve(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_c_union_p1r4eve F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    uint8_t gm_l4;
    {
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((uint8_t *)t7));
            void * t9 = (void *)(((uint8_t *)t8) + 8);
            gm_t88rcucg1b7uk t10 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t9, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)17ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t13;
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, ((uint8_t *)t14));
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 1088));
            uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 1088) + 8));
            uint32_t t18 = gm_l2;
            uint64_t t19 = (uint64_t)t18;
            if ((uintptr_t)t19 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t19, t17, gm_srcsk3h2du2ldnc, 474, 12);
            {
                {
                    uint8_t t20 = GM_LD(uint8_t, (((((uint8_t *)t16) + (uintptr_t)t19 * 184) + 24) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)2ULL));
                    if (!t21) {
                        goto gm_lb4;
                    }
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 474, 12);
            }
gm_lb3:;
            uint8_t t22 = gm_l4;
            F.ret = t22;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_literal_pattern_qio1dmp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_literal_pattern_qio1dmp F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tibupt4dmcqai gm_l5; /* s */
    uint8_t gm_l6; /* b */
    void * gm_l7; /* operand */
    GM_STACK_CHECK(&F, 2164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    uint8_t gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_thrdpodrs8pgt gm_l13;
    gm_tbci5ospk5nnn gm_l14;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_l4 = t13;
    void * t14 = gm_l1;
    {
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t14) + 8) + 8), 24);
            gm_t88rcucg1b7uk t17 = gm_l2;
            void * t18 = gm_l4;
            gm_t88rcucg1b7uk t19 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = (!gm_eq9m6mvtmgde(((uint8_t *)&t17), ((uint8_t *)&t19)));
            if (t20) {
                void * t21 = gm_l0;
                gm_tl874t2atnlh3 t22 = gm_l3;
                void * t23 = gm_l0;
                gm_t88rcucg1b7uk t24 = gm_l2;
                gm_b16_8 t25 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t26;
                GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
                gm_f_format_i4872o7((void *)&gm_l9, (void *)NULL, cx, NULL, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcsk3h2du2ldnc, 179, 32);
                gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                gm_b0_1 t30 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t21, t22, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t88rcucg1b7uk t31 = gm_l2;
            gm_t88rcucg1b7uk t32 = gm_l2;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_tl874t2atnlh3 t35 = gm_l3;
            gm_t2b6tm8c0vgo1 t36;
            memset(&t36, 0, sizeof t36);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t34);
            void * t37 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t32, &t36, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t38 = gm_l3;
            gm_tb38v5s9j1r32 t39;
            memset(&t39, 0, sizeof t39);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)2ULL));
            GM_ST(void *, ((uint8_t *)&t39 + 8), t37);
            void * t40 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t31, &t39, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t40;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)3ULL));
            if (!t42) {
                goto gm_lb3;
            }
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 8));
            gm_l6 = t43;
            void * t44 = gm_l4;
            gm_t88rcucg1b7uk t45 = gm_l2;
            gm_t53at80pfdus3 t46;
            gm_f_kind_jcnt4hm((void *)&t46, t44, GM_CONV(gm_csguankko4lmhd, t45));
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t46) + 0));
            uint8_t t48 = (uint8_t)(!(t47 == ((uint8_t)4ULL)));
            gm_t53at80pfdus3 t49;
            memset(&t49, 0, sizeof t49);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)4ULL));
            if (t48) {
                void * t50 = gm_l0;
                gm_tl874t2atnlh3 t51 = gm_l3;
                void * t52 = gm_l0;
                gm_t88rcucg1b7uk t53 = gm_l2;
                gm_b16_8 t54 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t55;
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                gm_f_format_eb4sgm9((void *)&gm_l10, (void *)NULL, cx, NULL, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcsk3h2du2ldnc, 185, 32);
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                gm_b0_1 t59 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t50, t51, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t88rcucg1b7uk t60 = gm_l2;
            gm_t88rcucg1b7uk t61 = gm_l2;
            uint8_t t62 = gm_l6;
            gm_tl874t2atnlh3 t63 = gm_l3;
            gm_t2b6tm8c0vgo1 t64;
            memset(&t64, 0, sizeof t64);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)4ULL));
            GM_ST(uint8_t, ((uint8_t *)&t64 + 8), t62);
            void * t65 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t61, &t64, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t66 = gm_l3;
            gm_tb38v5s9j1r32 t67;
            memset(&t67, 0, sizeof t67);
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
            GM_ST(void *, ((uint8_t *)&t67 + 8), t65);
            void * t68 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t60, &t67, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t68;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t70 = (uint8_t)(t69 == ((uint8_t)1ULL));
            if (!t70) {
                goto gm_lb4;
            }
            void * t71 = gm_l0;
            gm_tl874t2atnlh3 t72 = gm_l3;
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s6cmbp3c9g09j));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(60));
            gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t75 = gm_l2;
            gm_tl874t2atnlh3 t76 = gm_l3;
            gm_tb38v5s9j1r32 t77;
            memset(&t77, 0, sizeof t77);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
            void * t78 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t75, &t77, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t78;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)12ULL));
            if (!t80) {
                goto gm_lb5;
            }
            void * t81 = GM_LD(void *, (((((uint8_t *)t14) + 8) + 8) + 8));
            gm_l7 = t81;
            void * t82 = gm_l7;
            {
                {
                    uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t82) + 8) + 0));
                    uint8_t t84 = (uint8_t)(t83 == ((uint8_t)1ULL));
                    if (!t84) {
                        goto gm_lb7;
                    }
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 193, 34);
            }
gm_lb6:;
            uint8_t t85 = gm_l11;
            if (t85) {
                void * t86 = gm_l0;
                gm_tl874t2atnlh3 t87 = gm_l3;
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s6cmbp3c9g09j));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(60));
                gm_b0_1 t89 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t90 = gm_l2;
                gm_tl874t2atnlh3 t91 = gm_l3;
                gm_tb38v5s9j1r32 t92;
                memset(&t92, 0, sizeof t92);
                GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
                void * t93 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t90, &t92, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t93;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 176, 5);
    }
gm_lb1:;
    void * t94 = gm_l0;
    gm_t88rcucg1b7uk t95 = gm_l2;
    uint8_t t96 = gm_f_integer_pattern_ty_ovv2ftb((void *)NULL, cx, NULL, t94, t95);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t97 = (uint8_t)!t96;
    if (t97) {
        void * t98 = gm_l0;
        gm_tl874t2atnlh3 t99 = gm_l3;
        void * t100 = gm_l0;
        gm_t88rcucg1b7uk t101 = gm_l2;
        gm_b16_8 t102 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t100, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t103;
        GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
        gm_f_format_lcm4k53((void *)&gm_l12, (void *)NULL, cx, NULL, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsk3h2du2ldnc, 200, 24);
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
        gm_b0_1 t107 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t98, t99, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t108 = gm_l2;
        gm_tl874t2atnlh3 t109 = gm_l3;
        gm_tb38v5s9j1r32 t110;
        memset(&t110, 0, sizeof t110);
        GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)0ULL));
        void * t111 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t108, &t110, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t111;
        goto gm_fret;
    }
    void * t112 = gm_l0;
    void * t113 = gm_l1;
    gm_t88rcucg1b7uk t114 = gm_l2;
    gm_f_pattern_int_pp4f5ak((void *)&gm_l13, (void *)NULL, cx, NULL, t112, t113, t114);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t115 = (uint8_t)((((uint8_t *)&gm_l13))[48] != 0);
    if (t115) {
        memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l13), 48);
    } else {
        gm_t88rcucg1b7uk t116 = gm_l2;
        gm_tl874t2atnlh3 t117 = gm_l3;
        gm_tb38v5s9j1r32 t118;
        memset(&t118, 0, sizeof t118);
        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
        void * t119 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t116, &t118, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t119;
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l8), ((uint8_t *)&gm_l14), 48);
    gm_t88rcucg1b7uk t120 = gm_l2;
    gm_t88rcucg1b7uk t121 = gm_l2;
    gm_tbci5ospk5nnn t122 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l8));
    gm_tl874t2atnlh3 t123 = gm_l3;
    gm_t2b6tm8c0vgo1 t124;
    GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t124 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t124 + 8), t122);
    void * t125 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t121, &t124, t123);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t126 = gm_l3;
    gm_tb38v5s9j1r32 t127;
    memset(&t127, 0, sizeof t127);
    GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)2ULL));
    GM_ST(void *, ((uint8_t *)&t127 + 8), t125);
    void * t128 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t120, &t127, t126);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t128;
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_constant_m4v5hk8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_names_constant_m4v5hk8 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l3; /* d */
    gm_t88rcucg1b7uk gm_l4; /* t */
    void * gm_l5; /* tys */
    GM_STACK_CHECK(&F, 877u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqvu78rpi6q1h gm_l6;
    uint8_t gm_l7;
    gm_tf5srh4mp4b3i gm_l8;
    uint64_t gm_l9;
    gm_t6qooom8qa2do gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_tdlpv0q69lov1 gm_l15;
    {
        {
            void * t14 = gm_l0;
            gm_b16_8 t15 = gm_l1;
            gm_f_lookup_local_6f38t7p((void *)&gm_l6, t14, t15);
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l6))[208] != 0);
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l6), 208);
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 162, 44);
            }
gm_lb3:;
            uint8_t t19 = gm_l7;
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = gm_l0;
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 8));
    gm_b16_8 t24 = gm_l1;
    gm_tf5srh4mp4b3i t25 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t21, t23, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t25;
    uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
    if (t26) {
        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
        gm_l9 = t27;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t28 = gm_l9;
    gm_l3 = t28;
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    uint64_t t31 = gm_l3;
    gm_f_resolve_decl_pus042v((void *)&gm_l10, (void *)NULL, cx, NULL, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)8ULL));
            if (!t33) {
                goto gm_lb7;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb7:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)7ULL));
            if (!t35) {
                goto gm_lb8;
            }
            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 8));
            gm_l4 = t36;
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, ((uint8_t *)t37));
            void * t39 = (void *)(((uint8_t *)t38) + 8);
            gm_l5 = t39;
            void * t40 = gm_l5;
            gm_t88rcucg1b7uk t41 = gm_l4;
            uint8_t t42 = gm_f_is_integer_8d4f9ek(t40, GM_CONV(gm_csguankko4lmhd, t41));
            if (t42) {
                gm_l13 = ((uint8_t)1);
            } else {
                void * t43 = gm_l5;
                gm_t88rcucg1b7uk t44 = gm_l4;
                gm_t53at80pfdus3 t45;
                gm_f_kind_jcnt4hm((void *)&t45, t43, GM_CONV(gm_csguankko4lmhd, t44));
                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
                uint8_t t47 = (uint8_t)((t46 == ((uint8_t)4ULL)));
                gm_t53at80pfdus3 t48;
                memset(&t48, 0, sizeof t48);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)4ULL));
                gm_l13 = t47;
            }
            uint8_t t49 = gm_l13;
            if (t49) {
                gm_l12 = ((uint8_t)1);
            } else {
                gm_t88rcucg1b7uk t50 = gm_l4;
                void * t51 = gm_l5;
                gm_t88rcucg1b7uk t52 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t53 = (gm_eq9m6mvtmgde(((uint8_t *)&t50), ((uint8_t *)&t52)));
                gm_l12 = t53;
            }
            uint8_t t54 = gm_l12;
            if (t54) {
                gm_l11 = ((uint8_t)1);
            } else {
                void * t55 = gm_l0;
                void * t56 = GM_LD(void *, ((uint8_t *)t55));
                gm_t88rcucg1b7uk t57 = gm_l4;
                gm_f_union_variants_a7khaqp((void *)&gm_l15, t56, GM_CONV(gm_csguankko4lmhd, t57));
                {
                    {
                        uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l15))[24] != 0);
                        if (t58) {
                            goto gm_lb10;
                        }
                        gm_l14 = ((uint8_t)0);
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        gm_l14 = ((uint8_t)1);
                        goto gm_lb9;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 168, 89);
                }
gm_lb9:;
                uint8_t t59 = gm_l14;
                gm_l11 = t59;
            }
            uint8_t t60 = gm_l11;
            F.ret = t60;
            goto gm_fret;
        }
gm_lb8:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 164, 5);
    }
gm_lb6:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_path_pattern_l6sl3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_t9qkh0rsooc8d a4, void * a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_path_pattern_l6sl3lm F;
    void * gm_l0; /* cx */
    void * gm_l1; /* path */
    gm_th2lqbo77pvsc gm_l2; /* args */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t9qkh0rsooc8d gm_l4; /* ctx */
    void * gm_l5; /* binds */
    gm_tl874t2atnlh3 gm_l6; /* span */
    void * gm_l7; /* wild */
    gm_t08olq97rscn3 gm_l8; /* n */
    void * gm_l9; /* base */
    gm_t08olq97rscn3 gm_l10; /* name */
    gm_t88rcucg1b7uk gm_l11; /* t */
    void * gm_l12; /* v */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_t88rcucg1b7uk gm_l14; /* t */
    GM_STACK_CHECK(&F, 1376u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_tkn42u7na3t81 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_tf5srh4mp4b3i gm_l19;
    void * gm_l20;
    void * gm_l21;
    uint8_t gm_l22;
    gm_tkn42u7na3t81 gm_l23;
    uint8_t gm_l24;
    gm_th2lqbo77pvsc gm_l25;
    gm_tkn42u7na3t81 gm_l26;
    gm_t88rcucg1b7uk t26 = gm_l3;
    gm_tl874t2atnlh3 t27 = gm_l6;
    gm_tb38v5s9j1r32 t28;
    memset(&t28, 0, sizeof t28);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
    void * t29 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t26, &t28, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t29;
    void * t30 = gm_l1;
    {
        {
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)8ULL));
            if (!t32) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t30) + 8) + 8), 32);
            void * t33 = gm_l0;
            gm_t88rcucg1b7uk t34 = gm_l3;
            gm_t88rcucg1b7uk t35 = gm_l3;
            gm_t9qkh0rsooc8d t36 = gm_l4;
            void * t37 = gm_l5;
            gm_tl874t2atnlh3 t38 = gm_l6;
            void * t39 = gm_f_variant_pattern_8aa0fg2((void *)NULL, cx, NULL, t33, t34, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)), &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)), t35, t36, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t39;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)22ULL));
            if (!t41) {
                goto gm_lb3;
            }
            void * t42 = GM_LD(void *, ((((uint8_t *)t30) + 8) + 8));
            gm_l9 = t42;
            memmove(((uint8_t *)&gm_l10), (((((uint8_t *)t30) + 8) + 8) + 8), 32);
            {
                {
                    void * t43 = gm_l0;
                    void * t44 = gm_l9;
                    gm_tkn42u7na3t81 t45 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t43, t44);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t45;
                    uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l15))[4] != 0);
                    if (!t46) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l15));
                    gm_l11 = t47;
                    gm_t88rcucg1b7uk t48 = gm_l11;
                    gm_t88rcucg1b7uk t49 = gm_l3;
                    uint8_t t50 = (!gm_eq9m6mvtmgde(((uint8_t *)&t48), ((uint8_t *)&t49)));
                    if (t50) {
                        void * t51 = gm_l0;
                        gm_tl874t2atnlh3 t52 = gm_l6;
                        void * t53 = gm_l0;
                        gm_t88rcucg1b7uk t54 = gm_l11;
                        gm_b16_8 t55 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t53, t54);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t56 = gm_l0;
                        gm_t88rcucg1b7uk t57 = gm_l3;
                        gm_b16_8 t58 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t56, t57);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t59;
                        GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t55);
                        GM_ST(gm_b16_8, ((uint8_t *)&t59 + 16), t58);
                        gm_f_format_81ifu7p((void *)&gm_l16, (void *)NULL, cx, NULL, &t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                        if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcsk3h2du2ldnc, 370, 36);
                        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                        gm_b0_1 t63 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t51, t52, t62);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t64 = gm_l7;
                        F.ret = t64;
                        goto gm_fret;
                    }
                    void * t65 = gm_l0;
                    gm_t88rcucg1b7uk t66 = gm_l11;
                    gm_t88rcucg1b7uk t67 = gm_l3;
                    gm_t9qkh0rsooc8d t68 = gm_l4;
                    void * t69 = gm_l5;
                    gm_tl874t2atnlh3 t70 = gm_l6;
                    void * t71 = gm_f_variant_pattern_8aa0fg2((void *)NULL, cx, NULL, t65, t66, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l10)), &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)), t67, t68, t69, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t71;
                    goto gm_fret;
                }
gm_lb5:;
            }
gm_lb4:;
            {
                {
                    {
                        {
                            uint8_t t72 = (uint8_t)((((uint8_t *)a2))[16] != 0);
                            if (t72) {
                                goto gm_lb9;
                            }
                            gm_l17 = ((uint8_t)1);
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            gm_l17 = ((uint8_t)0);
                            goto gm_lb8;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 376, 16);
                    }
gm_lb8:;
                    uint8_t t73 = gm_l17;
                    if (!t73) {
                        goto gm_lb7;
                    }
                    void * t74 = gm_l0;
                    void * t75 = gm_l9;
                    gm_tf5srh4mp4b3i t76 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t74, t75);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t76;
                    {
                        {
                            uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
                            if (t77) {
                                goto gm_lb12;
                            }
                            gm_l18 = ((uint8_t)0);
                            goto gm_lb11;
                        }
gm_lb12:;
                        {
                            gm_l18 = ((uint8_t)1);
                            goto gm_lb11;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 376, 33);
                    }
gm_lb11:;
                    uint8_t t78 = gm_l18;
                    if (!t78) {
                        goto gm_lb7;
                    }
                    void * t79 = gm_l0;
                    void * t80 = gm_l1;
                    gm_t88rcucg1b7uk t81 = gm_l3;
                    void * t82 = gm_f_const_pattern_value_kf1he7o((void *)NULL, cx, NULL, t79, t80, t81);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t82;
                    uint8_t t83 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l20)) != 0);
                    if (t83) {
                        void * t84 = GM_LD(void *, ((uint8_t *)&gm_l20));
                        gm_l21 = t84;
                    } else {
                        void * t85 = gm_l7;
                        F.ret = t85;
                        goto gm_fret;
                    }
                    void * t86 = gm_l21;
                    gm_l12 = t86;
                    gm_t88rcucg1b7uk t87 = gm_l3;
                    void * t88 = gm_l12;
                    gm_tl874t2atnlh3 t89 = gm_l6;
                    gm_tb38v5s9j1r32 t90;
                    memset(&t90, 0, sizeof t90);
                    GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)2ULL));
                    GM_ST(void *, ((uint8_t *)&t90 + 8), t88);
                    void * t91 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t87, &t90, t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t91;
                    goto gm_fret;
                }
gm_lb7:;
            }
gm_lb6:;
            {
                {
                    uint8_t t92 = (uint8_t)((((uint8_t *)a2))[16] != 0);
                    if (t92) {
                        goto gm_lb15;
                    }
                    gm_l22 = ((uint8_t)0);
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    gm_l22 = ((uint8_t)1);
                    goto gm_lb14;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 381, 16);
            }
gm_lb14:;
            uint8_t t93 = gm_l22;
            if (t93) {
                {
                    {
                        void * t94 = gm_l0;
                        void * t95 = gm_l1;
                        gm_tkn42u7na3t81 t96 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t94, t95);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l23 = t96;
                        uint8_t t97 = (uint8_t)((((uint8_t *)&gm_l23))[4] != 0);
                        if (!t97) {
                            goto gm_lb18;
                        }
                        gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l23));
                        gm_l13 = t98;
                        void * t99 = gm_l0;
                        void * t100 = GM_LD(void *, ((uint8_t *)t99));
                        gm_t88rcucg1b7uk t101 = gm_l13;
                        gm_f_struct_fields_chqu3it((void *)&gm_l25, t100, GM_CONV(gm_csguankko4lmhd, t101));
                        {
                            {
                                uint8_t t102 = (uint8_t)((((uint8_t *)&gm_l25))[16] != 0);
                                if (t102) {
                                    goto gm_lb20;
                                }
                                gm_l24 = ((uint8_t)0);
                                goto gm_lb19;
                            }
gm_lb20:;
                            {
                                gm_l24 = ((uint8_t)1);
                                goto gm_lb19;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 382, 51);
                        }
gm_lb19:;
                        uint8_t t103 = gm_l24;
                        if (!t103) {
                            goto gm_lb18;
                        }
                        void * t104 = gm_l0;
                        void * t105 = gm_l1;
                        gm_t88rcucg1b7uk t106 = gm_l13;
                        gm_tkn42u7na3t81 t107;
                        GM_ST(uint8_t, (uint8_t *)&t107 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t107 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t107), t106);
                        ((uint8_t *)&t107)[4] = 1;
                        gm_t88rcucg1b7uk t108 = gm_l3;
                        gm_t9qkh0rsooc8d t109 = gm_l4;
                        void * t110 = gm_l5;
                        gm_tl874t2atnlh3 t111 = gm_l6;
                        void * t112 = gm_f_struct_pattern_4ln2pok((void *)NULL, cx, NULL, t104, t105, t107, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)), t108, t109, t110, t111);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t112;
                        goto gm_fret;
                    }
gm_lb18:;
                }
gm_lb17:;
            }
            void * t113 = gm_l0;
            void * t114 = gm_l1;
            gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t114) + 72));
            gm_b16_8 t116;
            GM_ST(void *, (uint8_t *)&t116, (void *)(gm_so5215183on4e));
            GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(57));
            gm_b0_1 t117 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t113, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t118 = gm_l7;
            F.ret = t118;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
            uint8_t t120 = (uint8_t)(t119 == ((uint8_t)6ULL));
            if (!t120) {
                goto gm_lb22;
            }
            void * t121 = gm_l0;
            void * t122 = gm_l1;
            void * t123 = gm_l0;
            void * t124 = gm_l1;
            gm_tkn42u7na3t81 t125 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t126 = gm_l3;
            gm_t9qkh0rsooc8d t127 = gm_l4;
            void * t128 = gm_l5;
            gm_tl874t2atnlh3 t129 = gm_l6;
            void * t130 = gm_f_struct_pattern_4ln2pok((void *)NULL, cx, NULL, t121, t122, t125, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)), t126, t127, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t130;
            goto gm_fret;
        }
gm_lb22:;
        {
            uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
            uint8_t t132 = (uint8_t)(t131 == ((uint8_t)19ULL));
            if (!t132) {
                goto gm_lb23;
            }
            void * t133 = gm_l0;
            void * t134 = gm_l1;
            gm_t88rcucg1b7uk t135 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t135;
            void * t136 = gm_l0;
            void * t137 = gm_l1;
            void * t138 = gm_l0;
            void * t139 = GM_LD(void *, ((uint8_t *)t138));
            void * t140 = (void *)(((uint8_t *)t139) + 8);
            gm_t88rcucg1b7uk t141 = gm_l14;
            uint8_t t142 = gm_f_is_error_8k27gd1(t140, GM_CONV(gm_csguankko4lmhd, t141));
            if (t142) {
                gm_tkn42u7na3t81 t143;
                memset(&t143, 0, sizeof t143);
                gm_l26 = t143;
            } else {
                gm_t88rcucg1b7uk t144 = gm_l14;
                gm_tkn42u7na3t81 t145;
                GM_ST(uint8_t, (uint8_t *)&t145 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t145 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t145), t144);
                ((uint8_t *)&t145)[4] = 1;
                gm_l26 = t145;
            }
            gm_tkn42u7na3t81 t146 = gm_l26;
            gm_t88rcucg1b7uk t147 = gm_l3;
            gm_t9qkh0rsooc8d t148 = gm_l4;
            void * t149 = gm_l5;
            gm_tl874t2atnlh3 t150 = gm_l6;
            void * t151 = gm_f_struct_pattern_4ln2pok((void *)NULL, cx, NULL, t136, t137, t146, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)), t147, t148, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t151;
            goto gm_fret;
        }
gm_lb23:;
        {
            void * t152 = gm_l0;
            void * t153 = gm_l1;
            gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t153) + 72));
            gm_b16_8 t155;
            GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sd7guj32ltqh8));
            GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(46));
            gm_b0_1 t156 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t152, t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t157 = gm_l7;
            F.ret = t157;
            goto gm_fret;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 365, 5);
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
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4) {
    struct gm_fr_pattern_5gn6kid F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty0 */
    gm_t9qkh0rsooc8d gm_l3; /* ctx */
    void * gm_l4; /* binds */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint8_t gm_l8; /* bare */
    gm_t88rcucg1b7uk gm_l9; /* inner */
    void * gm_l10; /* ip */
    gm_t88rcucg1b7uk gm_l11; /* ok */
    void * gm_l12; /* ip */
    GM_STACK_CHECK(&F, 1587u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l13;
    gm_t53at80pfdus3 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    uint8_t gm_l16;
    gm_t53at80pfdus3 gm_l17;
    void * gm_l18;
    uint8_t gm_l19;
    void * gm_l20;
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    void * t24 = (void *)(((uint8_t *)t23) + 8);
    gm_l5 = t24;
    void * t25 = gm_l0;
    void * t26 = (void *)(((uint8_t *)t25) + 96);
    void * t27 = gm_l0;
    void * t28 = GM_LD(void *, ((uint8_t *)t27));
    gm_t88rcucg1b7uk t29 = gm_l2;
    gm_t88rcucg1b7uk t30 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t26, t28, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t30;
    void * t31 = gm_l5;
    gm_t88rcucg1b7uk t32 = gm_l6;
    uint8_t t33 = gm_f_is_error_8k27gd1(t31, GM_CONV(gm_csguankko4lmhd, t32));
    if (t33) {
        void * t34 = gm_l0;
        void * t35 = gm_l1;
        gm_t9qkh0rsooc8d t36 = gm_l3;
        void * t37 = gm_l4;
        gm_b0_1 t38 = gm_f_bind_unchecked_5nngm43((void *)NULL, cx, NULL, t34, t35, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t39 = gm_l6;
        void * t40 = gm_l1;
        gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 48));
        gm_tb38v5s9j1r32 t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
        void * t43 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t39, &t42, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t43;
        goto gm_fret;
    }
    {
        {
            void * t44 = gm_l1;
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t44) + 8) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
            if (!t46) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t44) + 8) + 8), 32);
            void * t47 = gm_l5;
            gm_t88rcucg1b7uk t48 = gm_l6;
            gm_f_kind_jcnt4hm((void *)&gm_l14, t47, GM_CONV(gm_csguankko4lmhd, t48));
            {
                {
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)13ULL));
                    if (!t50) {
                        goto gm_lb4;
                    }
                    gm_l13 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)14ULL));
                    if (!t52) {
                        goto gm_lb5;
                    }
                    gm_l13 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_l13 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 58, 12);
            }
gm_lb3:;
            uint8_t t53 = gm_l13;
            if (t53) {
                void * t54 = gm_l0;
                void * t55 = gm_l1;
                gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 48));
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                void * t58 = gm_l0;
                gm_t88rcucg1b7uk t59 = gm_l6;
                gm_b16_8 t60 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t58, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                gm_tfeab279c16rr t62;
                GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t57);
                GM_ST(gm_b16_8, ((uint8_t *)&t62 + 16), t60);
                GM_ST(gm_b16_8, ((uint8_t *)&t62 + 32), t61);
                gm_f_format_99i3s4a((void *)&gm_l15, (void *)NULL, cx, NULL, &t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcsk3h2du2ldnc, 59, 30);
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                gm_b0_1 t66 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t54, t56, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t67 = gm_l0;
            gm_t88rcucg1b7uk t68 = gm_l6;
            gm_t9qkh0rsooc8d t69 = gm_l3;
            void * t70 = gm_l4;
            void * t71 = gm_l1;
            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 48));
            void * t73 = gm_f_bind_pattern_3potkvo((void *)NULL, cx, NULL, t67, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t68, t69, t70, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t73;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t74 = gm_l3;
    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t74) + 0));
    uint8_t t76 = (uint8_t)(!(t75 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t77;
    GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)1ULL));
    if (t76) {
        void * t78 = gm_l1;
        {
            {
                uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t78) + 8) + 0));
                uint8_t t80 = (uint8_t)(t79 == ((uint8_t)1ULL));
                if (!t80) {
                    goto gm_lb8;
                }
                gm_l16 = ((uint8_t)1);
                goto gm_lb7;
            }
gm_lb8:;
            {
                gm_l16 = ((uint8_t)0);
                goto gm_lb7;
            }
gm_lb9:;
            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 66, 17);
        }
gm_lb7:;
        uint8_t t81 = gm_l16;
        gm_l8 = t81;
        void * t82 = gm_l5;
        gm_t88rcucg1b7uk t83 = gm_l6;
        gm_f_kind_jcnt4hm((void *)&gm_l17, t82, GM_CONV(gm_csguankko4lmhd, t83));
        {
            {
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
                uint8_t t85 = (uint8_t)(t84 == ((uint8_t)13ULL));
                if (!t85) {
                    goto gm_lb11;
                }
                gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 8));
                gm_l9 = t86;
                {
                    {
                        void * t87 = gm_l1;
                        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t87) + 8) + 0));
                        uint8_t t89 = (uint8_t)(!(t88 == ((uint8_t)4ULL)));
                        if (!t89) {
                            goto gm_lb13;
                        }
                        void * t90 = gm_l1;
                        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t90) + 8) + 0));
                        uint8_t t92 = (uint8_t)(!(t91 == ((uint8_t)0ULL)));
                        if (!t92) {
                            goto gm_lb13;
                        }
                        uint8_t t93 = gm_l8;
                        gm_to361lgknsu69 t94 = GM_LD(gm_to361lgknsu69, (((uint8_t *)t87) + 8));
                        gm_to361lgknsu69 t95;
                        memset(&t95, 0, sizeof t95);
                        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)4ULL));
                        gm_to361lgknsu69 t96 = GM_LD(gm_to361lgknsu69, (((uint8_t *)t90) + 8));
                        gm_to361lgknsu69 t97;
                        memset(&t97, 0, sizeof t97);
                        GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)0ULL));
                        if (t93) {
                            void * t98 = gm_l0;
                            void * t99 = gm_l1;
                            gm_t88rcucg1b7uk t100 = gm_l9;
                            gm_t9qkh0rsooc8d t101 = gm_l3;
                            void * t102 = gm_l4;
                            void * t103 = gm_f_pattern_here_fg1q9vt((void *)NULL, cx, NULL, t98, t99, t100, t101, t102);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l18 = t103;
                        } else {
                            void * t104 = gm_l0;
                            void * t105 = gm_l1;
                            gm_t88rcucg1b7uk t106 = gm_l9;
                            gm_t9qkh0rsooc8d t107 = gm_l3;
                            void * t108 = gm_l4;
                            void * t109 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t104, t105, t106, t107, t108);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l18 = t109;
                        }
                        void * t110 = gm_l18;
                        gm_l10 = t110;
                        gm_t88rcucg1b7uk t111 = gm_l6;
                        void * t112 = gm_l10;
                        void * t113 = gm_l1;
                        gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t113) + 48));
                        gm_tb38v5s9j1r32 t115;
                        memset(&t115, 0, sizeof t115);
                        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)5ULL));
                        GM_ST(void *, ((uint8_t *)&t115 + 8), t112);
                        void * t116 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t111, &t115, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t116;
                        goto gm_fret;
                    }
gm_lb13:;
                }
gm_lb12:;
                goto gm_lb10;
            }
gm_lb11:;
            {
                uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
                uint8_t t118 = (uint8_t)(t117 == ((uint8_t)14ULL));
                if (!t118) {
                    goto gm_lb14;
                }
                gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l17) + 8) + 4));
                gm_l11 = t119;
                {
                    {
                        void * t120 = gm_l1;
                        {
                            {
                                uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t120) + 8) + 0));
                                uint8_t t122 = (uint8_t)(t121 == ((uint8_t)6ULL));
                                if (!t122) {
                                    goto gm_lb18;
                                }
                                gm_l19 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l19 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 72, 34);
                        }
gm_lb17:;
                        uint8_t t123 = gm_l19;
                        if (!t123) {
                            goto gm_lb16;
                        }
                        void * t124 = gm_l1;
                        uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t124) + 8) + 0));
                        uint8_t t126 = (uint8_t)(!(t125 == ((uint8_t)0ULL)));
                        if (!t126) {
                            goto gm_lb16;
                        }
                        uint8_t t127 = gm_l8;
                        gm_to361lgknsu69 t128 = GM_LD(gm_to361lgknsu69, (((uint8_t *)t124) + 8));
                        gm_to361lgknsu69 t129;
                        memset(&t129, 0, sizeof t129);
                        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)0ULL));
                        if (t127) {
                            void * t130 = gm_l0;
                            void * t131 = gm_l1;
                            gm_t88rcucg1b7uk t132 = gm_l11;
                            gm_t9qkh0rsooc8d t133 = gm_l3;
                            void * t134 = gm_l4;
                            void * t135 = gm_f_pattern_here_fg1q9vt((void *)NULL, cx, NULL, t130, t131, t132, t133, t134);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l20 = t135;
                        } else {
                            void * t136 = gm_l0;
                            void * t137 = gm_l1;
                            gm_t88rcucg1b7uk t138 = gm_l11;
                            gm_t9qkh0rsooc8d t139 = gm_l3;
                            void * t140 = gm_l4;
                            void * t141 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t136, t137, t138, t139, t140);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l20 = t141;
                        }
                        void * t142 = gm_l20;
                        gm_l12 = t142;
                        gm_t88rcucg1b7uk t143 = gm_l6;
                        void * t144 = gm_l12;
                        void * t145 = gm_l1;
                        gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t145) + 48));
                        gm_tb38v5s9j1r32 t147;
                        memset(&t147, 0, sizeof t147);
                        GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)6ULL));
                        GM_ST(void *, ((uint8_t *)&t147 + 8), t144);
                        void * t148 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t143, &t147, t146);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t148;
                        goto gm_fret;
                    }
gm_lb16:;
                }
gm_lb15:;
                goto gm_lb10;
            }
gm_lb14:;
            {
                goto gm_lb10;
            }
gm_lb20:;
            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 67, 9);
        }
gm_lb10:;
    }
    void * t149 = gm_l0;
    void * t150 = gm_l1;
    gm_t88rcucg1b7uk t151 = gm_l6;
    gm_t9qkh0rsooc8d t152 = gm_l3;
    void * t153 = gm_l4;
    void * t154 = gm_f_pattern_here_fg1q9vt((void *)NULL, cx, NULL, t149, t150, t151, t152, t153);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t154;
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
__attribute__((aligned(4))) void * gm_f_pattern_here_fg1q9vt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4) {
    struct gm_fr_pattern_here_fg1q9vt F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty0 */
    gm_t9qkh0rsooc8d gm_l3; /* ctx */
    void * gm_l4; /* binds */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* wild */
    gm_t08olq97rscn3 gm_l9; /* name */
    gm_t08olq97rscn3 gm_l10; /* name */
    void * gm_l11; /* e */
    void * gm_l12; /* lo */
    void * gm_l13; /* hi */
    uint8_t gm_l14; /* inclusive */
    void * gm_l17; /* inner */
    gm_t88rcucg1b7uk gm_l18; /* err */
    void * gm_l19; /* ip */
    gm_b16_8 gm_l20; /* args */
    gm_b16_8 gm_l21; /* elems */
    gm_b16_8 gm_l22; /* names */
    gm_b16_8 gm_l23; /* fields */
    gm_tagc77imt2chm gm_l24; /* pp */
    uint64_t gm_l25; /* errors */
    uint64_t gm_l26; /* nbinds */
    void * gm_l27; /* r */
    gm_b16_8 gm_l28; /* args */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    GM_STACK_CHECK(&F, 3272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l32;
    gm_t53at80pfdus3 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_thrdpodrs8pgt gm_l36;
    gm_tbci5ospk5nnn gm_l37;
    gm_thrdpodrs8pgt gm_l38;
    gm_tbci5ospk5nnn gm_l39;
    uint8_t gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    gm_t53at80pfdus3 gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_t53at80pfdus3 gm_l45;
    gm_tabb710d9mq2e gm_l46;
    {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, ((uint8_t *)t30));
        void * t32 = (void *)(((uint8_t *)t31) + 8);
        gm_l5 = t32;
        void * t33 = gm_l0;
        void * t34 = (void *)(((uint8_t *)t33) + 96);
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, ((uint8_t *)t35));
        gm_t88rcucg1b7uk t37 = gm_l2;
        gm_t88rcucg1b7uk t38 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t34, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t38;
        void * t39 = gm_l1;
        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 48));
        gm_l7 = t40;
        gm_t88rcucg1b7uk t41 = gm_l6;
        gm_tl874t2atnlh3 t42 = gm_l7;
        gm_tb38v5s9j1r32 t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
        void * t44 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t41, &t43, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t44;
        void * t45 = gm_l1;
        {
            {
                {
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)0ULL));
                    if (!t47) {
                        goto gm_lb2;
                    }
                    void * t48 = gm_l8;
                    F.ret = t48;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    {
                        {
                            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
                            if (!t50) {
                                goto gm_lb5;
                            }
                            memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t45) + 8) + 8), 32);
                            goto gm_lb4;
                        }
gm_lb5:;
                        {
                            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)2ULL));
                            if (!t52) {
                                goto gm_lb6;
                            }
                            memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t45) + 8) + 8), 32);
                            goto gm_lb4;
                        }
gm_lb6:;
                        goto gm_lb3;
                    }
gm_lb4:;
                    void * t53 = gm_l0;
                    gm_t88rcucg1b7uk t54 = gm_l6;
                    gm_t9qkh0rsooc8d t55 = gm_l3;
                    void * t56 = gm_l4;
                    gm_tl874t2atnlh3 t57 = gm_l7;
                    void * t58 = gm_f_bind_pattern_3potkvo((void *)NULL, cx, NULL, t53, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l9)), t54, t55, t56, t57);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t58;
                    goto gm_fret;
                }
gm_lb3:;
                {
                    uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)4ULL));
                    if (!t60) {
                        goto gm_lb7;
                    }
                    void * t61 = gm_l5;
                    gm_t88rcucg1b7uk t62 = gm_l6;
                    gm_f_kind_jcnt4hm((void *)&gm_l33, t61, GM_CONV(gm_csguankko4lmhd, t62));
                    {
                        {
                            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)13ULL));
                            if (!t64) {
                                goto gm_lb9;
                            }
                            gm_l32 = ((uint8_t)0);
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            gm_l32 = ((uint8_t)1);
                            goto gm_lb8;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 111, 16);
                    }
gm_lb8:;
                    uint8_t t65 = gm_l32;
                    if (t65) {
                        void * t66 = gm_l0;
                        gm_tl874t2atnlh3 t67 = gm_l7;
                        void * t68 = gm_l0;
                        gm_t88rcucg1b7uk t69 = gm_l6;
                        gm_b16_8 t70 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t68, t69);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t71;
                        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
                        gm_f_format_m9kjvp2((void *)&gm_l34, (void *)NULL, cx, NULL, t71);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcsk3h2du2ldnc, 112, 32);
                        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                        gm_b0_1 t75 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t66, t67, t74);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_t88rcucg1b7uk t76 = gm_l6;
                    gm_tl874t2atnlh3 t77 = gm_l7;
                    gm_tb38v5s9j1r32 t78;
                    memset(&t78, 0, sizeof t78);
                    GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)4ULL));
                    void * t79 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t76, &t78, t77);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t79;
                    goto gm_fret;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)3ULL));
                    if (!t81) {
                        goto gm_lb11;
                    }
                    void * t82 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                    gm_l11 = t82;
                    void * t83 = gm_l0;
                    void * t84 = gm_l11;
                    gm_t88rcucg1b7uk t85 = gm_l6;
                    gm_tl874t2atnlh3 t86 = gm_l7;
                    void * t87 = gm_f_literal_pattern_qio1dmp((void *)NULL, cx, NULL, t83, t84, t85, t86);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t87;
                    goto gm_fret;
                }
gm_lb11:;
                {
                    uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)5ULL));
                    if (!t89) {
                        goto gm_lb12;
                    }
                    void * t90 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                    gm_l12 = t90;
                    void * t91 = GM_LD(void *, (((((uint8_t *)t45) + 8) + 8) + 8));
                    gm_l13 = t91;
                    uint8_t t92 = GM_LD(uint8_t, (((((uint8_t *)t45) + 8) + 8) + 16));
                    gm_l14 = t92;
                    void * t93 = gm_l0;
                    gm_t88rcucg1b7uk t94 = gm_l6;
                    uint8_t t95 = gm_f_integer_pattern_ty_ovv2ftb((void *)NULL, cx, NULL, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t96 = (uint8_t)!t95;
                    if (t96) {
                        void * t97 = gm_l0;
                        gm_tl874t2atnlh3 t98 = gm_l7;
                        void * t99 = gm_l0;
                        gm_t88rcucg1b7uk t100 = gm_l6;
                        gm_b16_8 t101 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t99, t100);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t102;
                        GM_ST(gm_b16_8, ((uint8_t *)&t102 + 0), t101);
                        gm_f_format_cqh7lia((void *)&gm_l35, (void *)NULL, cx, NULL, t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                        if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcsk3h2du2ldnc, 119, 32);
                        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                        gm_b0_1 t106 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t97, t98, t105);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t107 = gm_l8;
                        F.ret = t107;
                        goto gm_fret;
                    }
                    void * t108 = gm_l0;
                    void * t109 = gm_l12;
                    gm_t88rcucg1b7uk t110 = gm_l6;
                    gm_f_pattern_int_pp4f5ak((void *)&gm_l36, (void *)NULL, cx, NULL, t108, t109, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t111 = (uint8_t)((((uint8_t *)&gm_l36))[48] != 0);
                    if (t111) {
                        memmove(((uint8_t *)&gm_l37), ((uint8_t *)&gm_l36), 48);
                    } else {
                        void * t112 = gm_l8;
                        F.ret = t112;
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l15), ((uint8_t *)&gm_l37), 48);
                    void * t113 = gm_l0;
                    void * t114 = gm_l13;
                    gm_t88rcucg1b7uk t115 = gm_l6;
                    gm_f_pattern_int_pp4f5ak((void *)&gm_l38, (void *)NULL, cx, NULL, t113, t114, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l38))[48] != 0);
                    if (t116) {
                        memmove(((uint8_t *)&gm_l39), ((uint8_t *)&gm_l38), 48);
                    } else {
                        void * t117 = gm_l8;
                        F.ret = t117;
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l16), ((uint8_t *)&gm_l39), 48);
                    uint8_t t118 = gm_l14;
                    if (t118) {
                        int32_t t119 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l15)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                        uint8_t t120 = (t119 > ((int32_t)0LL));
                        gm_l41 = t120;
                    } else {
                        gm_l41 = ((uint8_t)0);
                    }
                    uint8_t t121 = gm_l41;
                    if (t121) {
                        gm_l40 = ((uint8_t)1);
                    } else {
                        uint8_t t122 = gm_l14;
                        uint8_t t123 = (uint8_t)!t122;
                        if (t123) {
                            int32_t t124 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l15)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                            uint8_t t125 = (t124 >= ((int32_t)0LL));
                            gm_l42 = t125;
                        } else {
                            gm_l42 = ((uint8_t)0);
                        }
                        uint8_t t126 = gm_l42;
                        gm_l40 = t126;
                    }
                    uint8_t t127 = gm_l40;
                    if (t127) {
                        void * t128 = gm_l0;
                        gm_tl874t2atnlh3 t129 = gm_l7;
                        gm_b16_8 t130;
                        GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sem5pak34704h));
                        GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(34));
                        gm_b0_1 t131 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t128, t129, t130);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_t88rcucg1b7uk t132 = gm_l6;
                    uint8_t t133 = gm_l14;
                    gm_tbci5ospk5nnn t134 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l15));
                    gm_tbci5ospk5nnn t135 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l16));
                    gm_t59juu943t8sl t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 97, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 98, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 100, 0);
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t136 + 0), t134);
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t136 + 48), t135);
                    GM_ST(uint8_t, ((uint8_t *)&t136 + 96), t133);
                    gm_tl874t2atnlh3 t137 = gm_l7;
                    gm_tb38v5s9j1r32 t138;
                    GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_t59juu943t8sl, ((uint8_t *)&t138 + 8), t136);
                    void * t139 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t132, &t138, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t139;
                    goto gm_fret;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t141 = (uint8_t)(t140 == ((uint8_t)6ULL));
                    if (!t141) {
                        goto gm_lb13;
                    }
                    void * t142 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                    gm_l17 = t142;
                    {
                        {
                            void * t143 = gm_l5;
                            gm_t88rcucg1b7uk t144 = gm_l6;
                            gm_f_kind_jcnt4hm((void *)&gm_l43, t143, GM_CONV(gm_csguankko4lmhd, t144));
                            uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                            uint8_t t146 = (uint8_t)(t145 == ((uint8_t)14ULL));
                            if (!t146) {
                                goto gm_lb15;
                            }
                            gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l43) + 8));
                            gm_l18 = t147;
                            void * t148 = gm_l0;
                            void * t149 = gm_l17;
                            gm_t88rcucg1b7uk t150 = gm_l18;
                            gm_t9qkh0rsooc8d t151 = gm_l3;
                            void * t152 = gm_l4;
                            void * t153 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t148, t149, t150, t151, t152);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l19 = t153;
                            gm_t88rcucg1b7uk t154 = gm_l6;
                            void * t155 = gm_l19;
                            gm_tl874t2atnlh3 t156 = gm_l7;
                            gm_tb38v5s9j1r32 t157;
                            memset(&t157, 0, sizeof t157);
                            GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)7ULL));
                            GM_ST(void *, ((uint8_t *)&t157 + 8), t155);
                            void * t158 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t154, &t157, t156);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t158;
                            goto gm_fret;
                        }
gm_lb15:;
                        void * t159 = gm_l0;
                        gm_tl874t2atnlh3 t160 = gm_l7;
                        void * t161 = gm_l0;
                        gm_t88rcucg1b7uk t162 = gm_l6;
                        gm_b16_8 t163 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t161, t162);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t164;
                        GM_ST(gm_b16_8, ((uint8_t *)&t164 + 0), t163);
                        gm_f_format_npjtqgc((void *)&gm_l44, (void *)NULL, cx, NULL, t164);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                        if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_srcsk3h2du2ldnc, 131, 32);
                        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                        gm_b0_1 t168 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t159, t160, t167);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t169 = gm_l8;
                        F.ret = t169;
                        goto gm_fret;
                    }
gm_lb14:;
                }
gm_lb13:;
                {
                    uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t171 = (uint8_t)(t170 == ((uint8_t)8ULL));
                    if (!t171) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t172 = GM_LD(gm_b16_8, ((((uint8_t *)t45) + 8) + 8));
                    gm_l20 = t172;
                    {
                        {
                            void * t173 = gm_l5;
                            gm_t88rcucg1b7uk t174 = gm_l6;
                            gm_f_kind_jcnt4hm((void *)&gm_l45, t173, GM_CONV(gm_csguankko4lmhd, t174));
                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                            uint8_t t176 = (uint8_t)(t175 == ((uint8_t)12ULL));
                            if (!t176) {
                                goto gm_lb18;
                            }
                            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                            gm_l21 = t177;
                            gm_b16_8 t178 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l45) + 8) + 16));
                            gm_l22 = t178;
                            void * t179 = gm_l0;
                            gm_b16_8 t180 = gm_l20;
                            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                            gm_b16_8 t182 = gm_l21;
                            gm_b16_8 t183 = gm_l22;
                            gm_t9qkh0rsooc8d t184 = gm_l3;
                            void * t185 = gm_l4;
                            gm_tl874t2atnlh3 t186 = gm_l7;
                            gm_b16_8 t187 = gm_f_sub_patterns_jr84m36((void *)NULL, cx, NULL, t179, t181, t182, t183, t184, t185, t186, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l23 = t187;
                            gm_t88rcucg1b7uk t188 = gm_l6;
                            gm_b16_8 t189 = gm_l23;
                            gm_tl874t2atnlh3 t190 = gm_l7;
                            gm_tb38v5s9j1r32 t191;
                            memset(&t191, 0, sizeof t191);
                            GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)9ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t191 + 8), t189);
                            void * t192 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t188, &t191, t190);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t192;
                            goto gm_fret;
                        }
gm_lb18:;
                        void * t193 = gm_l0;
                        gm_tl874t2atnlh3 t194 = gm_l7;
                        void * t195 = gm_l0;
                        gm_t88rcucg1b7uk t196 = gm_l6;
                        gm_b16_8 t197 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t195, t196);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t198;
                        GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t197);
                        gm_f_format_chnm9ud((void *)&gm_l46, (void *)NULL, cx, NULL, t198);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                        if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srcsk3h2du2ldnc, 139, 32);
                        gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                        gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                        gm_b0_1 t202 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t193, t194, t201);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t203 = gm_l8;
                        F.ret = t203;
                        goto gm_fret;
                    }
gm_lb17:;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t205 = (uint8_t)(t204 == ((uint8_t)7ULL));
                    if (!t205) {
                        goto gm_lb19;
                    }
                    memmove(((uint8_t *)&gm_l24), ((((uint8_t *)t45) + 8) + 8), 32);
                    void * t206 = gm_l0;
                    void * t207 = GM_LD(void *, ((uint8_t *)t206));
                    void * t208 = GM_LD(void *, ((uint8_t *)t207));
                    void * t209 = GM_LD(void *, (((uint8_t *)t208) + 8));
                    uint64_t t210 = gm_f_error_count_omtt6qd(t209);
                    gm_l25 = t210;
                    void * t211 = gm_l4;
                    uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)t211) + 8));
                    gm_l26 = t212;
                    void * t213 = gm_l0;
                    void * t214 = GM_LD(void *, ((uint8_t *)&gm_l24));
                    gm_t88rcucg1b7uk t215 = gm_l6;
                    gm_t9qkh0rsooc8d t216 = gm_l3;
                    void * t217 = gm_l4;
                    gm_tl874t2atnlh3 t218 = gm_l7;
                    void * t219 = gm_f_path_pattern_l6sl3lm((void *)NULL, cx, NULL, t213, t214, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&gm_l24) + 8)), t215, t216, t217, t218);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t219;
                    {
                        {
                            void * t220 = gm_l27;
                            uint8_t t221 = GM_LD(uint8_t, ((((uint8_t *)t220) + 8) + 0));
                            uint8_t t222 = (uint8_t)((t221 == ((uint8_t)0ULL)));
                            if (!t222) {
                                goto gm_lb21;
                            }
                            void * t223 = gm_l0;
                            void * t224 = GM_LD(void *, ((uint8_t *)t223));
                            void * t225 = GM_LD(void *, ((uint8_t *)t224));
                            void * t226 = GM_LD(void *, (((uint8_t *)t225) + 8));
                            gm_tb38v5s9j1r32 t227 = GM_LD(gm_tb38v5s9j1r32, (((uint8_t *)t220) + 8));
                            gm_tb38v5s9j1r32 t228;
                            memset(&t228, 0, sizeof t228);
                            GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)0ULL));
                            uint64_t t229 = gm_f_error_count_omtt6qd(t226);
                            uint64_t t230 = gm_l25;
                            uint8_t t231 = (t229 > t230);
                            if (!t231) {
                                goto gm_lb21;
                            }
                            void * t232 = gm_l4;
                            uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)t232) + 8));
                            uint64_t t234 = gm_l26;
                            uint8_t t235 = ((t233 == t234));
                            if (!t235) {
                                goto gm_lb21;
                            }
                            {
                                {
                                    uint8_t t236 = (uint8_t)(((((uint8_t *)&gm_l24) + 8))[16] != 0);
                                    if (!t236) {
                                        goto gm_lb23;
                                    }
                                    gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                                    gm_l28 = t237;
                                    gm_l30 = ((uint64_t)0ULL);
                                    uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                    gm_l29 = t238;
                                    {
                                        for (;;) {
                                            uint64_t t239 = gm_l30;
                                            uint64_t t240 = gm_l29;
                                            uint8_t t241 = (t239 < t240);
                                            if (!t241) {
                                                goto gm_lb24;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t242 = gm_l30;
                                                        gm_l31 = t242;
                                                        void * t243 = gm_l0;
                                                        void * t244 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                                        uint64_t t245 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                        uint64_t t246 = gm_l31;
                                                        void * t247 = (void *)((((uint8_t *)t244) + (uintptr_t)t246 * 120) + 40);
                                                        gm_t9qkh0rsooc8d t248 = gm_l3;
                                                        void * t249 = gm_l4;
                                                        gm_b0_1 t250 = gm_f_bind_unchecked_5nngm43((void *)NULL, cx, NULL, t243, t247, t248, t249);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb27;
                                                    }
gm_lb28:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 152, 21);
                                                }
gm_lb27:;
                                            }
gm_lb26:;
                                            uint64_t t251 = gm_l30;
                                            uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                                            uint64_t t254 = r252;
                                            gm_l30 = t254;
                                        }
                                    }
gm_lb24:;
                                    goto gm_lb22;
                                }
gm_lb23:;
                            }
gm_lb22:;
                            goto gm_lb20;
                        }
gm_lb21:;
                    }
gm_lb20:;
                    void * t255 = gm_l27;
                    F.ret = t255;
                    goto gm_fret;
                }
gm_lb19:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 107, 5);
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
__attribute__((aligned(4))) void gm_f_pattern_int_pp4f5ak(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_pattern_int_pp4f5ak F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l4; /* l */
    void * gm_l5; /* operand */
    void * gm_l6; /* l */
    void * gm_l11; /* base */
    void * gm_l12; /* c */
    GM_STACK_CHECK(&F, 1076u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l14;
    gm_tf5srh4mp4b3i gm_l15;
    void * gm_l16;
    void * gm_l17;
    uint8_t gm_l18;
    gm_t53at80pfdus3 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_f_zero_s25l990((void *)&F.l3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t22 = gm_l1;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
            if (!t24) {
                goto gm_lb2;
            }
            void * t25 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l4 = t25;
            void * t26 = gm_l4;
            memmove(((uint8_t *)&F.l3), ((uint8_t *)t26), 48);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)12ULL));
            if (!t28) {
                goto gm_lb3;
            }
            uint8_t t29 = GM_LD(uint8_t, (((((uint8_t *)t22) + 8) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)0ULL));
            if (!t30) {
                goto gm_lb3;
            }
            void * t31 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l5 = t31;
            {
                {
                    void * t32 = gm_l5;
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                    if (!t34) {
                        goto gm_lb5;
                    }
                    void * t35 = GM_LD(void *, ((((uint8_t *)t32) + 8) + 8));
                    gm_l6 = t35;
                    void * t36 = gm_l6;
                    gm_tbci5ospk5nnn t37 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)t36));
                    gm_f_neg_agd5c5k((void *)&F.l3, (void *)NULL, cx, NULL, &t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                void * t38 = gm_l0;
                void * t39 = gm_l5;
                gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s89j7s4avcmfs));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(27));
                gm_b0_1 t42 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t38, t40, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_thrdpodrs8pgt t43;
                memset(&t43, 0, sizeof t43);
                memmove(gm_out, ((uint8_t *)&t43), 56);
                goto gm_fdone;
            }
gm_lb4:;
            goto gm_lb1;
        }
gm_lb3:;
        {
            void * t44 = gm_l0;
            void * t45 = gm_l1;
            gm_f_try_untyped_8j8up35((void *)&F.l7, (void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t46 = (uint8_t)((((uint8_t *)&F.l7))[208] != 0);
                    if (!t46) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&F.l8), ((uint8_t *)&F.l7), 208);
                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)0ULL));
                    if (!t48) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&F.l9), (((uint8_t *)&F.l8) + 8), 200);
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)0ULL));
                    if (!t50) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&F.l10), (((uint8_t *)&F.l9) + 8), 48);
                    memmove(((uint8_t *)&F.l3), ((uint8_t *)&F.l10), 48);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    {
                        void * t51 = gm_l1;
                        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                        uint8_t t53 = (uint8_t)(t52 == ((uint8_t)22ULL));
                        if (!t53) {
                            goto gm_lb10;
                        }
                        void * t54 = GM_LD(void *, ((((uint8_t *)t51) + 8) + 8));
                        gm_l11 = t54;
                        void * t55 = gm_l0;
                        void * t56 = gm_l11;
                        gm_tf5srh4mp4b3i t57 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t55, t56);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l15 = t57;
                        {
                            {
                                uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                if (t58) {
                                    goto gm_lb12;
                                }
                                gm_l14 = ((uint8_t)0);
                                goto gm_lb11;
                            }
gm_lb12:;
                            {
                                gm_l14 = ((uint8_t)1);
                                goto gm_lb11;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 224, 53);
                        }
gm_lb11:;
                        uint8_t t59 = gm_l14;
                        if (!t59) {
                            goto gm_lb10;
                        }
                        void * t60 = gm_l0;
                        void * t61 = gm_l1;
                        gm_t88rcucg1b7uk t62 = gm_l2;
                        void * t63 = gm_f_const_pattern_value_kf1he7o((void *)NULL, cx, NULL, t60, t61, t62);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l16 = t63;
                        uint8_t t64 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l16)) != 0);
                        if (t64) {
                            void * t65 = GM_LD(void *, ((uint8_t *)&gm_l16));
                            gm_l17 = t65;
                        } else {
                            gm_thrdpodrs8pgt t66;
                            memset(&t66, 0, sizeof t66);
                            memmove(gm_out, ((uint8_t *)&t66), 56);
                            goto gm_fdone;
                        }
                        void * t67 = gm_l17;
                        gm_l12 = t67;
                        {
                            {
                                void * t68 = gm_l12;
                                uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t68) + 8) + 0));
                                uint8_t t70 = (uint8_t)(t69 == ((uint8_t)0ULL));
                                if (!t70) {
                                    goto gm_lb15;
                                }
                                memmove(((uint8_t *)&F.l13), ((((uint8_t *)t68) + 8) + 8), 48);
                                memmove(((uint8_t *)&F.l3), ((uint8_t *)&F.l13), 48);
                                goto gm_lb14;
                            }
gm_lb15:;
                            gm_thrdpodrs8pgt t71;
                            memset(&t71, 0, sizeof t71);
                            memmove(gm_out, ((uint8_t *)&t71), 56);
                            goto gm_fdone;
                        }
gm_lb14:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    void * t72 = gm_l0;
                    void * t73 = gm_l1;
                    gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 72));
                    gm_b16_8 t75;
                    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_setnqjs5cth99));
                    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(44));
                    gm_b0_1 t76 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t72, t74, t75);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_thrdpodrs8pgt t77;
                    memset(&t77, 0, sizeof t77);
                    memmove(gm_out, ((uint8_t *)&t77), 56);
                    goto gm_fdone;
                }
gm_lb9:;
            }
gm_lb7:;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 211, 5);
    }
gm_lb1:;
    {
        {
            void * t78 = gm_l0;
            void * t79 = GM_LD(void *, ((uint8_t *)t78));
            void * t80 = (void *)(((uint8_t *)t79) + 8);
            gm_t88rcucg1b7uk t81 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l19, t80, GM_CONV(gm_csguankko4lmhd, t81));
            {
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)23ULL));
                    if (!t83) {
                        goto gm_lb19;
                    }
                    gm_l18 = ((uint8_t)0);
                    goto gm_lb18;
                }
gm_lb19:;
                {
                    gm_l18 = ((uint8_t)1);
                    goto gm_lb18;
                }
gm_lb20:;
                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 236, 8);
            }
gm_lb18:;
            uint8_t t84 = gm_l18;
            if (!t84) {
                goto gm_lb17;
            }
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, ((uint8_t *)t85));
            gm_t88rcucg1b7uk t87 = gm_l2;
            uint8_t t88 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t86, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)), t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = (uint8_t)!t88;
            if (!t89) {
                goto gm_lb17;
            }
            void * t90 = gm_l0;
            void * t91 = gm_l1;
            gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t91) + 72));
            gm_b16_8 t93 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = gm_l2;
            gm_b16_8 t96 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t97;
            GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t93);
            GM_ST(gm_b16_8, ((uint8_t *)&t97 + 16), t96);
            gm_f_format_qm09p7m((void *)&gm_l20, (void *)NULL, cx, NULL, &t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcsk3h2du2ldnc, 237, 26);
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
            gm_b0_1 t101 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t90, t92, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thrdpodrs8pgt t102;
            memset(&t102, 0, sizeof t102);
            memmove(gm_out, ((uint8_t *)&t102), 56);
            goto gm_fdone;
        }
gm_lb17:;
    }
gm_lb16:;
    gm_tbci5ospk5nnn t103 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3));
    gm_thrdpodrs8pgt t104;
    GM_ST(uint8_t, (uint8_t *)&t104 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t104 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t104 + 52, 0);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t104), t103);
    ((uint8_t *)&t104)[48] = 1;
    memmove(gm_out, ((uint8_t *)&t104), 56);
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
__attribute__((aligned(4))) void * gm_f_payload_pattern_28pvmeh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, const void * a2, const void * a3, gm_t9qkh0rsooc8d a4, void * a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_payload_pattern_28pvmeh F;
    void * gm_l0; /* cx */
    gm_tkn42u7na3t81 gm_l1; /* payload_ty */
    gm_t08olq97rscn3 gm_l2; /* vname */
    gm_th2lqbo77pvsc gm_l3; /* args0 */
    gm_t9qkh0rsooc8d gm_l4; /* ctx */
    void * gm_l5; /* binds */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_t88rcucg1b7uk gm_l7; /* pt */
    gm_b16_8 gm_l9; /* args */
    uint8_t gm_l10; /* one */
    gm_b16_8 gm_l11; /* elems */
    gm_b16_8 gm_l12; /* names */
    gm_b16_8 gm_l13; /* fs */
    GM_STACK_CHECK(&F, 1769u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_t88rcucg1b7uk gm_l14;
    uint8_t gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_b16_8 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
    if (t14) {
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l1));
        gm_l14 = t15;
    } else {
        {
            {
                uint8_t t16 = (uint8_t)((((uint8_t *)a3))[16] != 0);
                if (t16) {
                    goto gm_lb2;
                }
                gm_l15 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l15 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 439, 12);
        }
gm_lb1:;
        uint8_t t17 = gm_l15;
        if (t17) {
            void * t18 = gm_l0;
            gm_tl874t2atnlh3 t19 = gm_l6;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_tnpifv20irnph t21;
            GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
            gm_f_format_ktbu9b9((void *)&gm_l16, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcsk3h2du2ldnc, 439, 47);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            gm_b0_1 t25 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t18, t19, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        F.ret = ((void *)0);
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t26 = gm_l14;
    gm_l7 = t26;
    uint8_t t27 = (uint8_t)((((uint8_t *)a3))[16] != 0);
    if (t27) {
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)a3));
        gm_l17 = t28;
    } else {
        gm_t9qkh0rsooc8d t29 = gm_l4;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        uint8_t t31 = (uint8_t)(!(t30 == ((uint8_t)2ULL)));
        gm_t9qkh0rsooc8d t32;
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)2ULL));
        if (t31) {
            gm_tl874t2atnlh3 t33 = gm_l6;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_t6j6smj7qoctu t36;
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t34);
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 16), t35);
            gm_f_format_67q8e18((void *)&gm_l18, (void *)NULL, cx, NULL, &t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcsk3h2du2ldnc, 444, 35);
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_t4gut2c9fj1kt t40;
            gm_f_error_s6hn871((void *)&t40, (void *)NULL, cx, NULL, t33, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l8 = t40;
            void * t41 = (void *)((uint8_t *)&F.l8);
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s297r0ooe1bmq));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(28));
            uint32_t t43 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
            uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            gm_tl874t2atnlh3 t46;
            GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t43);
            GM_ST(uint32_t, ((uint8_t *)&t46 + 4), t44);
            GM_ST(uint32_t, ((uint8_t *)&t46 + 8), t45);
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s9d2uvo0cndd7));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(3));
            gm_b0_1 t48 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t41, t42, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t49 = gm_l0;
            void * t50 = GM_LD(void *, ((uint8_t *)t49));
            gm_b0_1 t51 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t50, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        F.ret = ((void *)0);
        goto gm_fret;
    }
    gm_b16_8 t52 = gm_l17;
    gm_l9 = t52;
    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    uint8_t t54 = ((t53 == ((uint64_t)1ULL)));
    if (t54) {
        void * t55 = GM_LD(void *, ((uint8_t *)&gm_l9));
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t56, gm_srcsk3h2du2ldnc, 450, 30);
        {
            {
                uint8_t t57 = (uint8_t)(((((uint8_t *)t55) + (uintptr_t)((uint64_t)0ULL) * 120))[32] != 0);
                if (t57) {
                    goto gm_lb5;
                }
                gm_l20 = ((uint8_t)1);
                goto gm_lb4;
            }
gm_lb5:;
            {
                gm_l20 = ((uint8_t)0);
                goto gm_lb4;
            }
gm_lb6:;
            gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 450, 30);
        }
gm_lb4:;
        uint8_t t58 = gm_l20;
        gm_l19 = t58;
    } else {
        gm_l19 = ((uint8_t)0);
    }
    uint8_t t59 = gm_l19;
    gm_l10 = t59;
    {
        {
            uint8_t t60 = gm_l10;
            if (!t60) {
                goto gm_lb8;
            }
            void * t61 = GM_LD(void *, ((uint8_t *)&gm_l9));
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t62, gm_srcsk3h2du2ldnc, 452, 16);
            uint8_t t63 = GM_LD(uint8_t, ((((((uint8_t *)t61) + (uintptr_t)((uint64_t)0ULL) * 120) + 40) + 8) + 0));
            uint8_t t64 = (uint8_t)((t63 == ((uint8_t)0ULL)));
            if (!t64) {
                goto gm_lb8;
            }
            gm_t88rcucg1b7uk t65 = gm_l7;
            gm_tl874t2atnlh3 t66 = gm_l6;
            gm_to361lgknsu69 t67 = GM_LD(gm_to361lgknsu69, (((((uint8_t *)t61) + (uintptr_t)((uint64_t)0ULL) * 120) + 40) + 8));
            gm_to361lgknsu69 t68;
            memset(&t68, 0, sizeof t68);
            GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
            gm_tb38v5s9j1r32 t69;
            memset(&t69, 0, sizeof t69);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
            void * t70 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t65, &t69, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t70;
            goto gm_fret;
        }
gm_lb8:;
    }
gm_lb7:;
    {
        {
            void * t71 = gm_l0;
            void * t72 = GM_LD(void *, ((uint8_t *)t71));
            void * t73 = (void *)(((uint8_t *)t72) + 8);
            gm_t88rcucg1b7uk t74 = gm_l7;
            gm_f_kind_jcnt4hm((void *)&gm_l21, t73, GM_CONV(gm_csguankko4lmhd, t74));
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)12ULL));
            if (!t76) {
                goto gm_lb10;
            }
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_l11 = t77;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l21) + 8) + 16));
            gm_l12 = t78;
            {
                {
                    uint8_t t79 = gm_l10;
                    if (!t79) {
                        goto gm_lb12;
                    }
                    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t81 = (!(t80 == ((uint64_t)1ULL)));
                    if (!t81) {
                        goto gm_lb12;
                    }
                    void * t82 = gm_l0;
                    void * t83 = GM_LD(void *, ((uint8_t *)&gm_l9));
                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t84, gm_srcsk3h2du2ldnc, 456, 60);
                    void * t85 = (void *)((((uint8_t *)t83) + (uintptr_t)((uint64_t)0ULL) * 120) + 40);
                    gm_t88rcucg1b7uk t86 = gm_l7;
                    gm_t9qkh0rsooc8d t87 = gm_l4;
                    void * t88 = gm_l5;
                    void * t89 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t82, t85, t86, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t89;
                    goto gm_fret;
                }
gm_lb12:;
            }
gm_lb11:;
            void * t90 = gm_l0;
            gm_b16_8 t91 = gm_l9;
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
            gm_b16_8 t93 = gm_l11;
            gm_b16_8 t94 = gm_l12;
            gm_t9qkh0rsooc8d t95 = gm_l4;
            void * t96 = gm_l5;
            gm_tl874t2atnlh3 t97 = gm_l6;
            gm_b16_8 t98 = gm_f_sub_patterns_jr84m36((void *)NULL, cx, NULL, t90, t92, t93, t94, t95, t96, t97, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t98;
            gm_t88rcucg1b7uk t99 = gm_l7;
            gm_b16_8 t100 = gm_l13;
            gm_tl874t2atnlh3 t101 = gm_l6;
            gm_tb38v5s9j1r32 t102;
            memset(&t102, 0, sizeof t102);
            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)9ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t102 + 8), t100);
            void * t103 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t99, &t102, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t103;
            goto gm_fret;
        }
gm_lb10:;
    }
gm_lb9:;
    uint8_t t104 = gm_l10;
    uint8_t t105 = (uint8_t)!t104;
    if (t105) {
        void * t106 = gm_l0;
        gm_tl874t2atnlh3 t107 = gm_l6;
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_tnpifv20irnph t109;
        GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
        gm_f_format_tn4egi5((void *)&gm_l22, (void *)NULL, cx, NULL, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcsk3h2du2ldnc, 461, 33);
        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
        gm_b0_1 t113 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t106, t107, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    uint8_t t115 = ((t114 == ((uint64_t)0ULL)));
    if (t115) {
        gm_t88rcucg1b7uk t116 = gm_l7;
        gm_tl874t2atnlh3 t117 = gm_l6;
        gm_tb38v5s9j1r32 t118;
        memset(&t118, 0, sizeof t118);
        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
        void * t119 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t116, &t118, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t119;
        goto gm_fret;
    }
    void * t120 = gm_l0;
    void * t121 = GM_LD(void *, ((uint8_t *)&gm_l9));
    uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t122)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t122, gm_srcsk3h2du2ldnc, 463, 24);
    void * t123 = (void *)((((uint8_t *)t121) + (uintptr_t)((uint64_t)0ULL) * 120) + 40);
    gm_t88rcucg1b7uk t124 = gm_l7;
    gm_t9qkh0rsooc8d t125 = gm_l4;
    void * t126 = gm_l5;
    void * t127 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t120, t123, t124, t125, t126);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t127;
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
__attribute__((aligned(4))) void * gm_f_struct_pattern_4ln2pok(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t9qkh0rsooc8d a5, void * a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_struct_pattern_4ln2pok F;
    void * gm_l0; /* cx */
    void * gm_l1; /* path */
    gm_tkn42u7na3t81 gm_l2; /* t0 */
    gm_th2lqbo77pvsc gm_l3; /* args0 */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_t9qkh0rsooc8d gm_l5; /* ctx */
    void * gm_l6; /* binds */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* wild */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* fields */
    gm_t6j6smj7qoctu gm_l11; /* h */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_t08olq97rscn3 gm_l21; /* n */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_b16_8 gm_l25; /* fs */
    GM_STACK_CHECK(&F, 2193u);
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
    gm_t88rcucg1b7uk gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_th2lqbo77pvsc gm_l28;
    gm_b16_8 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_ttu09lij36f8c gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_b16_8 gm_l35;
    uint8_t gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    uint8_t gm_l39;
    {
        gm_t88rcucg1b7uk t27 = gm_l4;
        gm_tl874t2atnlh3 t28 = gm_l7;
        gm_tb38v5s9j1r32 t29;
        memset(&t29, 0, sizeof t29);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
        void * t30 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t27, &t29, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t30;
        uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
        if (t31) {
            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
            gm_l26 = t32;
        } else {
            void * t33 = gm_l0;
            void * t34 = gm_l1;
            gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sgcuckp03jlhp));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(22));
            gm_b0_1 t37 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t33, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t38 = gm_l8;
            F.ret = t38;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t39 = gm_l26;
        gm_l9 = t39;
        gm_t88rcucg1b7uk t40 = gm_l9;
        gm_t88rcucg1b7uk t41 = gm_l4;
        uint8_t t42 = (!gm_eq9m6mvtmgde(((uint8_t *)&t40), ((uint8_t *)&t41)));
        if (t42) {
            void * t43 = gm_l0;
            gm_tl874t2atnlh3 t44 = gm_l7;
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l9;
            gm_b16_8 t47 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t48 = gm_l0;
            gm_t88rcucg1b7uk t49 = gm_l4;
            gm_b16_8 t50 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t51;
            GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t47);
            GM_ST(gm_b16_8, ((uint8_t *)&t51 + 16), t50);
            gm_f_format_81ifu7p((void *)&gm_l27, (void *)NULL, cx, NULL, &t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
            if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srcsk3h2du2ldnc, 326, 24);
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t43, t44, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t56 = gm_l8;
            F.ret = t56;
            goto gm_fret;
        }
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, ((uint8_t *)t57));
        gm_t88rcucg1b7uk t59 = gm_l9;
        gm_f_struct_fields_chqu3it((void *)&gm_l28, t58, GM_CONV(gm_csguankko4lmhd, t59));
        uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l28))[16] != 0);
        if (t60) {
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l28));
            gm_l29 = t61;
        } else {
            void * t62 = gm_l0;
            void * t63 = gm_l1;
            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
            void * t65 = gm_l0;
            gm_t88rcucg1b7uk t66 = gm_l9;
            gm_b16_8 t67 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t68;
            GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
            gm_f_format_dd92ata((void *)&gm_l30, (void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcsk3h2du2ldnc, 330, 29);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_b0_1 t72 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t64, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t73 = gm_l8;
            F.ret = t73;
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t74 = gm_l29;
        gm_l10 = t74;
        {
            {
                void * t75 = gm_l0;
                gm_t88rcucg1b7uk t76 = gm_l9;
                gm_f_hidden_field_u7uao2s((void *)&gm_l31, t75, GM_CONV(gm_csguankko4lmhd, t76));
                uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l31))[32] != 0);
                if (!t77) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l31), 32);
                void * t78 = gm_l0;
                gm_tl874t2atnlh3 t79 = gm_l7;
                void * t80 = gm_l0;
                gm_t88rcucg1b7uk t81 = gm_l9;
                gm_b16_8 t82 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 16));
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                gm_tfeab279c16rr t85;
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t82);
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 16), t83);
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 32), t84);
                gm_f_format_6nnjhja((void *)&gm_l32, (void *)NULL, cx, NULL, &t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcsk3h2du2ldnc, 334, 24);
                gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                gm_b0_1 t89 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t78, t79, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t90 = gm_l8;
                F.ret = t90;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_trqfvmjtt4sij t91;
        gm_f_list_epvducg((void *)&t91, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t91;
        gm_trqfvmjtt4sij t92;
        gm_f_list_nof7vfj((void *)&t92, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t92;
        gm_b16_8 t93 = gm_l10;
        gm_l14 = t93;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t94 = gm_l15;
                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t96 = (t94 < t95);
                if (!t96) {
                    goto gm_lb3;
                }
                {
                    void * t97 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t99 = gm_l15;
                    {
                        {
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)t97) + (uintptr_t)t99 * 40), 40);
                            void * t100 = (void *)((uint8_t *)&F.l12);
                            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                            gm_tftb1odc739t5 t102 = gm_f_push_3ef746b(t100, t101);
                            gm_l33 = t102;
                            uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                            if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcsk3h2du2ldnc, 340, 9);
                            void * t104 = (void *)((uint8_t *)&F.l13);
                            gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                            gm_tftb1odc739t5 t106 = gm_f_push_k3l9nic(t104, GM_CONV(gm_csguankko4lmhd, t105));
                            gm_l34 = t106;
                            uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                            if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcsk3h2du2ldnc, 341, 9);
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 339, 5);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t108 = gm_l15;
                uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                uint64_t t111 = r109;
                gm_l15 = t111;
            }
        }
gm_lb3:;
    }
    {
        gm_trqfvmjtt4sij t112;
        gm_f_list_ttksl9g((void *)&t112, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t112;
        uint8_t t113 = (uint8_t)((((uint8_t *)a3))[16] != 0);
        if (t113) {
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)a3));
            gm_l35 = t114;
        } else {
            gm_b16_8 t115 = gm_f_empty_slice_r9g5q6n();
            gm_l35 = t115;
        }
        gm_b16_8 t116 = gm_l35;
        gm_l18 = t116;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t117 = gm_l19;
                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t119 = (t117 < t118);
                if (!t119) {
                    goto gm_lb8;
                }
                {
                    void * t120 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t122 = gm_l19;
                    {
                        {
                            memmove(((uint8_t *)&F.l20), (((uint8_t *)t120) + (uintptr_t)t122 * 120), 120);
                            {
                                {
                                    {
                                        {
                                            uint8_t t123 = (uint8_t)((((uint8_t *)&F.l20))[32] != 0);
                                            if (t123) {
                                                goto gm_lb16;
                                            }
                                            gm_l36 = ((uint8_t)1);
                                            goto gm_lb15;
                                        }
gm_lb16:;
                                        {
                                            gm_l36 = ((uint8_t)0);
                                            goto gm_lb15;
                                        }
gm_lb17:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 346, 12);
                                    }
gm_lb15:;
                                    uint8_t t124 = gm_l36;
                                    if (!t124) {
                                        goto gm_lb14;
                                    }
                                    uint8_t t125 = GM_LD(uint8_t, (((((uint8_t *)&F.l20) + 40) + 8) + 0));
                                    uint8_t t126 = (uint8_t)(t125 == ((uint8_t)1ULL));
                                    if (!t126) {
                                        goto gm_lb14;
                                    }
                                    memmove(((uint8_t *)&gm_l21), (((((uint8_t *)&F.l20) + 40) + 8) + 8), 32);
                                    void * t127 = (void *)((uint8_t *)&F.l17);
                                    gm_t08olq97rscn3 t128 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l21));
                                    gm_t1s75i08qtptd t129;
                                    GM_ST(uint8_t, (uint8_t *)&t129 + 33, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t129 + 34, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t129 + 36, 0);
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t129), t128);
                                    ((uint8_t *)&t129)[32] = 1;
                                    gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l20) + 104));
                                    gm_t0d02142k4tu8 t131 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&F.l20) + 40));
                                    gm_tnrgict8sthcv t132;
                                    GM_ST(uint32_t, (uint8_t *)&t132 + 116, 0);
                                    GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t132 + 0), t129);
                                    GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t132 + 40), t131);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t132 + 104), t130);
                                    gm_tftb1odc739t5 t133 = gm_f_push_l74t9rq(t127, &t132);
                                    gm_l37 = t133;
                                    uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                    if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcsk3h2du2ldnc, 347, 13);
                                    goto gm_lb13;
                                }
gm_lb14:;
                                void * t135 = (void *)((uint8_t *)&F.l17);
                                gm_tftb1odc739t5 t136 = gm_f_push_l74t9rq(t135, &(*(gm_tnrgict8sthcv *)((uint8_t *)&F.l20)));
                                gm_l38 = t136;
                                uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcsk3h2du2ldnc, 349, 13);
                            }
gm_lb13:;
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 345, 5);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t138 = gm_l19;
                uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                uint64_t t141 = r139;
                gm_l19 = t141;
            }
        }
gm_lb8:;
    }
    {
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
        gm_l22 = t142;
        gm_l23 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t143 = gm_l23;
                uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                uint8_t t145 = (t143 < t144);
                if (!t145) {
                    goto gm_lb18;
                }
                {
                    void * t146 = GM_LD(void *, ((uint8_t *)&gm_l22));
                    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint64_t t148 = gm_l23;
                    {
                        {
                            memmove(((uint8_t *)&F.l24), (((uint8_t *)t146) + (uintptr_t)t148 * 120), 120);
                            {
                                {
                                    uint8_t t149 = (uint8_t)((((uint8_t *)&F.l24))[32] != 0);
                                    if (t149) {
                                        goto gm_lb24;
                                    }
                                    gm_l39 = ((uint8_t)1);
                                    goto gm_lb23;
                                }
gm_lb24:;
                                {
                                    gm_l39 = ((uint8_t)0);
                                    goto gm_lb23;
                                }
gm_lb25:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 353, 12);
                            }
gm_lb23:;
                            uint8_t t150 = gm_l39;
                            if (t150) {
                                void * t151 = gm_l0;
                                gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l24) + 104));
                                gm_b16_8 t153;
                                GM_ST(void *, (uint8_t *)&t153, (void *)(gm_s67lg9pjq86ie));
                                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(54));
                                gm_b0_1 t154 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t151, t152, t153);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb18;
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 352, 5);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t155 = gm_l23;
                uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                uint64_t t158 = r156;
                gm_l23 = t158;
            }
        }
gm_lb18:;
        void * t159 = gm_l0;
        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
        gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
        gm_t9qkh0rsooc8d t166 = gm_l5;
        void * t167 = gm_l6;
        gm_tl874t2atnlh3 t168 = gm_l7;
        gm_b16_8 t169 = gm_f_sub_patterns_jr84m36((void *)NULL, cx, NULL, t159, t161, t163, t165, t166, t167, t168, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t169;
        gm_t88rcucg1b7uk t170 = gm_l4;
        gm_b16_8 t171 = gm_l25;
        gm_tl874t2atnlh3 t172 = gm_l7;
        gm_tb38v5s9j1r32 t173;
        memset(&t173, 0, sizeof t173);
        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)9ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t173 + 8), t171);
        void * t174 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t170, &t173, t172);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t174;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_patterns_jr84m36(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_t9qkh0rsooc8d a4, void * a5, gm_tl874t2atnlh3 a6, uint8_t a7) {
    struct gm_fr_sub_patterns_jr84m36 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l2; /* elems */
    gm_b16_8 gm_l3; /* names */
    gm_t9qkh0rsooc8d gm_l4; /* ctx */
    void * gm_l5; /* binds */
    gm_tl874t2atnlh3 gm_l6; /* span */
    uint8_t gm_l7; /* partial */
    uint64_t gm_l9; /* next */
    uint8_t gm_l10; /* all_positional */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* a */
    uint64_t gm_l15; /* idx */
    gm_t08olq97rscn3 gm_l16; /* n */
    uint64_t gm_l17; /* found */
    void * gm_l18; /* p */
    GM_STACK_CHECK(&F, 891u);
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
    gm_tf5srh4mp4b3i gm_l19;
    uint64_t gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_trqfvmjtt4sij t11;
    gm_f_list_i33ccm0((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l8 = t11;
    gm_l9 = ((uint64_t)0ULL);
    gm_l10 = ((uint8_t)1);
    gm_l12 = ((uint64_t)0ULL);
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l11 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l12;
            uint64_t t14 = gm_l11;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t16 = gm_l12;
                        gm_l13 = t16;
                        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t19 = gm_l13;
                        void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 120);
                        gm_l14 = t20;
                        gm_l15 = ((uint64_t)0ULL);
                        {
                            {
                                void * t21 = gm_l14;
                                uint8_t t22 = (uint8_t)((((uint8_t *)t21))[32] != 0);
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&gm_l16), ((uint8_t *)t21), 32);
                                gm_l10 = ((uint8_t)0);
                                gm_b16_8 t23 = gm_l3;
                                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                gm_tf5srh4mp4b3i t25 = gm_f_index_of_o9r57ha(t23, t24);
                                gm_l19 = t25;
                                uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
                                if (t26) {
                                    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                                    gm_l20 = t27;
                                } else {
                                    void * t28 = gm_l0;
                                    gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l16) + 16));
                                    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                    gm_tnpifv20irnph t31;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
                                    gm_f_format_v40sfri((void *)&gm_l21, (void *)NULL, cx, NULL, t31);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srcsk3h2du2ldnc, 296, 34);
                                    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                                    gm_b0_1 t35 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t28, t29, t34);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb3;
                                }
                                uint64_t t36 = gm_l20;
                                gm_l17 = t36;
                                uint64_t t37 = gm_l17;
                                gm_l15 = t37;
                                goto gm_lb6;
                            }
gm_lb7:;
                            uint64_t t38 = gm_l9;
                            gm_l15 = t38;
                            uint64_t t39 = gm_l9;
                            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                            if (o41) gm_trap("integer overflow in `+`", gm_srcsk3h2du2ldnc, 302, 13);
                            uint64_t t42 = r40;
                            gm_l9 = t42;
                            uint64_t t43 = gm_l15;
                            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint8_t t45 = (t43 >= t44);
                            if (t45) {
                                void * t46 = gm_l0;
                                void * t47 = gm_l14;
                                gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 104));
                                gm_b16_8 t49;
                                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sp7me12frc01v));
                                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(21));
                                gm_b0_1 t50 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t46, t48, t49);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb3;
                            }
                        }
gm_lb6:;
                        void * t51 = gm_l0;
                        void * t52 = gm_l14;
                        void * t53 = (void *)(((uint8_t *)t52) + 40);
                        void * t54 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t56 = gm_l15;
                        if ((uintptr_t)t56 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t56, t55, gm_srcsk3h2du2ldnc, 308, 33);
                        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + (uintptr_t)t56 * 4));
                        gm_t9qkh0rsooc8d t58 = gm_l4;
                        void * t59 = gm_l5;
                        void * t60 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t51, t53, t57, t58, t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l18 = t60;
                        void * t61 = (void *)((uint8_t *)&F.l8);
                        uint64_t t62 = gm_l15;
                        uint32_t t63 = (uint32_t)t62;
                        void * t64 = gm_l18;
                        gm_ttggq7ng4kp3p t65;
                        GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t65 + 0), t63);
                        GM_ST(void *, ((uint8_t *)&t65 + 8), t64);
                        gm_tftb1odc739t5 t66 = gm_f_push_31airv7(t61, GM_CONV(gm_csqbqksqco2bm6, t65));
                        gm_l22 = t66;
                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcsk3h2du2ldnc, 309, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 290, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t68 = gm_l12;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l12 = t71;
        }
    }
gm_lb1:;
    {
        {
            uint8_t t72 = gm_l7;
            uint8_t t73 = (uint8_t)!t72;
            if (!t73) {
                goto gm_lb9;
            }
            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t76 = (t74 < t75);
            if (!t76) {
                goto gm_lb9;
            }
            uint8_t t77 = gm_l10;
            if (!t77) {
                goto gm_lb9;
            }
            void * t78 = gm_l0;
            gm_tl874t2atnlh3 t79 = gm_l6;
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
            uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            gm_t2lqu8jsj511d t82;
            GM_ST(uint64_t, ((uint8_t *)&t82 + 0), t80);
            GM_ST(uint64_t, ((uint8_t *)&t82 + 8), t81);
            gm_f_format_vcvtacd((void *)&gm_l23, (void *)NULL, cx, NULL, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcsk3h2du2ldnc, 312, 24);
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
            gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
            gm_b0_1 t86 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t78, t79, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
    F.ret = t87;
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
__attribute__((aligned(4))) void * gm_f_variant_pattern_8aa0fg2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t9qkh0rsooc8d a5, void * a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_variant_pattern_8aa0fg2 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* u */
    gm_t08olq97rscn3 gm_l2; /* vname */
    gm_th2lqbo77pvsc gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_t9qkh0rsooc8d gm_l5; /* ctx */
    void * gm_l6; /* binds */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* wild */
    gm_tep2drsl9408b gm_l10; /* uv */
    gm_tf5srh4mp4b3i gm_l11; /* idx */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint64_t gm_l16; /* i */
    void * gm_l17; /* payload */
    GM_STACK_CHECK(&F, 1897u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_tdlpv0q69lov1 gm_l18;
    gm_tep2drsl9408b gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_t3l8idb2m6c1r gm_l21;
    uint64_t gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_t88rcucg1b7uk t7 = gm_l4;
    gm_tl874t2atnlh3 t8 = gm_l7;
    gm_tb38v5s9j1r32 t9;
    memset(&t9, 0, sizeof t9);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
    void * t10 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t7, &t9, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    gm_t88rcucg1b7uk t13 = gm_l1;
    gm_f_union_variants_a7khaqp((void *)&gm_l18, t12, GM_CONV(gm_csguankko4lmhd, t13));
    uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l18))[24] != 0);
    if (t14) {
        memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 24);
    } else {
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l1;
        uint8_t t17 = gm_f_is_c_union_p1r4eve(t15, GM_CONV(gm_csguankko4lmhd, t16));
        if (t17) {
            void * t18 = gm_l0;
            gm_tl874t2atnlh3 t19 = gm_l7;
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, ((uint8_t *)t20));
            gm_t88rcucg1b7uk t22 = gm_l1;
            gm_b16_8 t23 = gm_f_c_union_match_2tgsktr((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t18, t19, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t25 = gm_l8;
            F.ret = t25;
            goto gm_fret;
        }
        gm_tl874t2atnlh3 t26 = gm_l7;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        void * t28 = gm_l0;
        gm_t88rcucg1b7uk t29 = gm_l1;
        gm_b16_8 t30 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t27);
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 16), t30);
        gm_f_format_2n44ecs((void *)&gm_l20, (void *)NULL, cx, NULL, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
        if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcsk3h2du2ldnc, 410, 31);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_t4gut2c9fj1kt t35;
        gm_f_error_s6hn871((void *)&t35, (void *)NULL, cx, NULL, t26, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t35;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sb03flnhs22vb));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(2));
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_srqehfttj6ngk));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(3));
        gm_t6j6smj7qoctu t39;
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t37);
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 16), t38);
        uint8_t t40 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t39) + 0, ((uint8_t *)&t36))) t40 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t39) + 16, ((uint8_t *)&t36))) t40 = 1;
        uint8_t t41 = (uint8_t)(t40);
        if (t41) {
            void * t42 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_saqh6vgbmf8m2));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(152));
            gm_f_add_note_46v502s(t42, t43);
        } else {
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s2hgdf6b9d53s));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(4));
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s026u6q948evn));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
            gm_t6j6smj7qoctu t47;
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t45);
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 16), t46);
            uint8_t t48 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t47) + 0, ((uint8_t *)&t44))) t48 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t47) + 16, ((uint8_t *)&t44))) t48 = 1;
            uint8_t t49 = (uint8_t)(t48);
            if (t49) {
                void * t50 = (void *)((uint8_t *)&F.l9);
                gm_b16_8 t51;
                GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s1ffqahkqb2ed));
                GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(140));
                gm_f_add_note_46v502s(t50, t51);
            }
        }
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, ((uint8_t *)t52));
        gm_b0_1 t54 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t53, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t55 = gm_l8;
        F.ret = t55;
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l19), 24);
    gm_tf5srh4mp4b3i t56;
    memset(&t56, 0, sizeof t56);
    gm_l11 = t56;
    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
    gm_l12 = t58;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t59 = gm_l13;
            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t61 = (t59 < t60);
            if (!t61) {
                goto gm_lb1;
            }
            {
                uint64_t t62 = gm_l13;
                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t65 = gm_l13;
                gm_tckdobunaulee t66 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t63) + (uintptr_t)t65 * 72));
                gm_t3l8idb2m6c1r t67;
                GM_ST(uint64_t, ((uint8_t *)&t67 + 0), t62);
                GM_ST(gm_tckdobunaulee, ((uint8_t *)&t67 + 8), t66);
                gm_l21 = t67;
                {
                    {
                        uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                        gm_l14 = t68;
                        memmove(((uint8_t *)&F.l15), (((uint8_t *)&gm_l21) + 8), 72);
                        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        uint8_t t71 = (gm_eq0iqiubfm7e(((uint8_t *)&t69), ((uint8_t *)&t70)));
                        if (t71) {
                            uint64_t t72 = gm_l14;
                            gm_tf5srh4mp4b3i t73;
                            GM_ST(uint8_t, (uint8_t *)&t73 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t73 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t73 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t73), t72);
                            ((uint8_t *)&t73)[8] = 1;
                            gm_l11 = t73;
                            goto gm_lb1;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsk3h2du2ldnc, 421, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t74 = gm_l13;
            uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
            uint64_t t77 = r75;
            gm_l13 = t77;
        }
    }
gm_lb1:;
    uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
    if (t78) {
        uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
        gm_l22 = t79;
    } else {
        void * t80 = gm_l0;
        gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        void * t82 = gm_l0;
        gm_t88rcucg1b7uk t83 = gm_l1;
        gm_b16_8 t84 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_t6j6smj7qoctu t86;
        GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t84);
        GM_ST(gm_b16_8, ((uint8_t *)&t86 + 16), t85);
        gm_f_format_l002mid((void *)&gm_l23, (void *)NULL, cx, NULL, &t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcsk3h2du2ldnc, 428, 30);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
        gm_b0_1 t90 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t80, t81, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t91 = gm_l8;
        F.ret = t91;
        goto gm_fret;
    }
    uint64_t t92 = gm_l22;
    gm_l16 = t92;
    void * t93 = gm_l0;
    void * t94 = GM_LD(void *, ((uint8_t *)&gm_l10));
    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
    uint64_t t96 = gm_l16;
    if ((uintptr_t)t96 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t96, t95, gm_srcsk3h2du2ldnc, 431, 35);
    gm_tkn42u7na3t81 t97 = GM_LD(gm_tkn42u7na3t81, ((((uint8_t *)t94) + (uintptr_t)t96 * 72) + 16));
    gm_t9qkh0rsooc8d t98 = gm_l5;
    void * t99 = gm_l6;
    gm_tl874t2atnlh3 t100 = gm_l7;
    void * t101 = gm_f_payload_pattern_28pvmeh((void *)NULL, cx, NULL, t93, t97, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), &(*(gm_th2lqbo77pvsc *)((uint8_t *)a3)), t98, t99, t100);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l17 = t101;
    gm_t88rcucg1b7uk t102 = gm_l4;
    uint64_t t103 = gm_l16;
    uint32_t t104 = (uint32_t)t103;
    void * t105 = gm_l17;
    gm_ttggq7ng4kp3p t106;
    GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t106 + 0), t104);
    GM_ST(void *, ((uint8_t *)&t106 + 8), t105);
    gm_tl874t2atnlh3 t107 = gm_l7;
    gm_tb38v5s9j1r32 t108;
    memset(&t108, 0, sizeof t108);
    GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)8ULL));
    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t108 + 8), t106);
    void * t109 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t102, &t108, t107);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t109;
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
