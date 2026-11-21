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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint32_t f2; } gm_tba3spc7ecv13;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
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
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tba3spc7ecv13) == 12 && _Alignof(gm_tba3spc7ecv13) == 4 && offsetof(gm_tba3spc7ecv13, f0) == 0 && offsetof(gm_tba3spc7ecv13, f1) == 4 && offsetof(gm_tba3spc7ecv13, f2) == 8, "layout of `Slot(u32, u32)`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { gm_tba3spc7ecv13 f0; uint8_t f1; uint8_t pe[3]; } gm_tf38rotgtqq79;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
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
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tf38rotgtqq79) == 16 && _Alignof(gm_tf38rotgtqq79) == 4 && offsetof(gm_tf38rotgtqq79, f0) == 0 && offsetof(gm_tf38rotgtqq79, f1) == 12, "layout of `?(signed: bool, min: u32, max: u32)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tbci5ospk5nnn a[2]; } gm_thu7n15ilds8r;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { gm_tngtuiuskn938 a[2]; } gm_tdskijji6mt1d;
_Static_assert(sizeof(gm_thu7n15ilds8r) == 96 && _Alignof(gm_thu7n15ilds8r) == 8 && offsetof(gm_thu7n15ilds8r, a) == 0, "layout of `[2]Int`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_tdskijji6mt1d) == 192 && _Alignof(gm_tdskijji6mt1d) == 8 && offsetof(gm_tdskijji6mt1d, a) == 0, "layout of `[2]Rat`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_obligation_ss66lhk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* o */
    void * l2; /* tys */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_t9bmh80edou9c l14; /* <temporary array> */
    gm_tdmi4fk9nhqa7 l15; /* <temporary array> */
    gm_trqfvmjtt4sij l19; /* parts */
    gm_t91vm1g0k2rpm l26; /* n */
    gm_b0_1 ret;
};
struct gm_fr_clear_a02qt6l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_p214f4b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_conversion_ok_qftumna {
    gm_frame h;
    gm_tckdobunaulee l8; /* v */
    uint8_t ret;
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
struct gm_fr_empty_slice_g3tddrb_2 {
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
struct gm_fr_empty_slice_r81t436 {
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
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fin_cond_32pmhkp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fin_expr_rk98p2r {
    gm_frame h;
    void * l0; /* cx */
    gm_t710lsrq1m8gk l8; /* x */
    gm_tbci5ospk5nnn l11; /* v */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_trqfvmjtt4sij l19; /* fields */
    gm_tdskijji6mt1d l20; /* <tmp> */
    gm_tngtuiuskn938 l23; /* r */
    gm_b0_1 ret;
};
struct gm_fr_fin_float_9k2be5v {
    gm_frame h;
    gm_tngtuiuskn938 l2; /* r */
    gm_b0_1 ret;
};
struct gm_fr_fin_pat_92m9fnd {
    gm_frame h;
    void * l0; /* cx */
    gm_tbci5ospk5nnn l2; /* lo */
    gm_tbci5ospk5nnn l3; /* hi */
    gm_thu7n15ilds8r l4; /* <tmp> */
    gm_tbci5ospk5nnn l7; /* v */
    gm_b0_1 ret;
};
struct gm_fr_finalize_k1g9q48 {
    gm_frame h;
    gm_tpm220j5qhjke l14; /* o */
    gm_b0_1 ret;
};
struct gm_fr_fn_2v5b782 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_513k31a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bfunt71 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ud6f4hb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_095q43u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2d2knap {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k1v0hr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_61q4vm1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_83ij19e {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c5b6tc3 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_du48r1i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ebi1ob2 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i1v5tqv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i8teo07 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_isu6baa {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ja2c5em {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lhid49k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ns6dben {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o9tvoie {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qm09p7m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sl0efd0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sq1gio6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_int_range_jdeefnh {
    gm_frame h;
    gm_tf38rotgtqq79 ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
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
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_literal_core_ii46ie6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_lossless_a54o69s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_numeric_uv79l0u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
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
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_settle_h9ikvs5 {
    gm_frame h;
    gm_t88rcucg1b7uk l6; /* src */
    uint8_t l7; /* mentioned */
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_show_rat_1s624db {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_float_o1iop8t {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_t2b6tm8c0vgo1 ret;
};
struct gm_fr_walk_v858co7 {
    gm_frame h;
    void * l0; /* s */
    gm_closure l2; /* f */
    gm_t53at80pfdus3 l3; /* k */
    gm_b0_1 ret;
};
struct gm_fr_write_v26t591 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_fallbacks_gcbql0t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deferred_match_742e2u5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_obligation_ss66lhk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_clear_a02qt6l(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_p214f4b(void * a0);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_conversion_ok_qftumna(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_cond_32pmhkp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_expr_rk98p2r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_float_9k2be5v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_pat_92m9fnd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finalize_k1g9q48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_fixed_by_afiovc1(void * a0, void * a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2v5b782(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_513k31a(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ud6f4hb(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_095q43u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_2d2knap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_61q4vm1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_83ij19e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c5b6tc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_du48r1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ebi1ob2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_i1v5tqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_i8teo07(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_isu6baa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ja2c5em(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lhid49k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_o9tvoie(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qm09p7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sl0efd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sq1gio6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_from_f64_1parp9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) uint8_t gm_f_has_ord_1io9knm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf38rotgtqq79 gm_f_int_range_jdeefnh(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_literal_core_ii46ie6(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_numeric_uv79l0u(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_vars_vpp9ov6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_km0lis6h9ua[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s03s7smc9pnp4[8];
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s5n7j87m9k09e[36];
extern const uint8_t gm_s75gqsar0656b[50];
extern const uint8_t gm_s794tb9qjlq1h[42];
extern const uint8_t gm_s7f68g16j7a3o[6];
extern const uint8_t gm_s7s25m3nlrif2[6];
extern const uint8_t gm_s95cbsclgo7gk[36];
extern const uint8_t gm_s9jrvf2ogt807[17];
extern const uint8_t gm_s9nm0mpn8r8ov[40];
extern const uint8_t gm_s9tjtf1373ti5[5];
extern const uint8_t gm_savtbnmf9bjnr[17];
extern const uint8_t gm_sbanjlvk3n14i[4];
extern const uint8_t gm_sf44a6nc03ff5[10];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgbib360sn2mu[131];
extern const uint8_t gm_sgpahsg79gte4[26];
extern const uint8_t gm_sgujtgfflv5dk[8];
extern const uint8_t gm_sh0adpq1lnkpj[40];
extern const uint8_t gm_sh57gpir0vvh2[24];
extern const uint8_t gm_siflgunfq3r7b[11];
extern const uint8_t gm_sjb60mo5hejk2[6];
extern const uint8_t gm_sjcocjnsa7u7g[85];
extern const uint8_t gm_sks5ueb3h9e10[24];
extern const uint8_t gm_sl85di9lce6ej[128];
extern const uint8_t gm_sllaecjankikg[63];
extern const uint8_t gm_snoj0t5lf2lht[4];
extern const uint8_t gm_snqm9b8h9b70a[120];
extern const uint8_t gm_sq89uknu8mulu[8];
extern const uint8_t gm_sqlp5qu42o606[11];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr8du5b2u9c7v[33];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6b5gn9iggr1p[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcseljft758gtd[];
extern const uint8_t gm_sruk98mc5gf6c[44];
extern const uint8_t gm_ssrd6bs7gcs4g[29];
extern const uint8_t gm_sub9rknupld7d[52];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svknltl22rlbs[9];
extern const gm_tmpl gm_tpl1dirc8ldrsao;
extern const gm_tmpl gm_tpl1ige45obm306;
extern const gm_tmpl gm_tpl1rfj3tqlepqo;
extern const gm_tmpl gm_tpl4r3nb3bvll23;
extern const gm_tmpl gm_tpl9v8qcr7jilen;
extern const gm_tmpl gm_tplf579fe8ol0qp;
extern const gm_tmpl gm_tplh05i67kfcnal;
extern const gm_tmpl gm_tpllgrg03l2v74l;
extern const gm_tmpl gm_tpllnpq25q3a5a4;
extern const gm_tmpl gm_tplmghq7vvcvnvi;
extern const gm_tmpl gm_tplnkb386hrc6va;
extern const gm_tmpl gm_tplr0qnbmgpa2m5;
extern const gm_tmpl gm_tpls6vb484t8cfc;
extern const gm_tmpl gm_tplsdamq31ts61q;
extern const gm_tmpl gm_tpltt9gd20e1v7c;
extern const gm_tmpl gm_tplu2tisf3oaee8;
extern const gm_tmpl gm_tpluue9jh13g9df;
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq9m6mvtmgde(a, b);
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
inline __attribute__((aligned(4))) void gm_f_clear_a02qt6l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_a02qt6l F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
    if (t6 > t7 || t7 > (uintptr_t)t5) gm_trap_slice(t6, t7, t5, gm_srcrgc9bkquf6nr, 140, 15);
    uint64_t t8 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_p214f4b(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_p214f4b F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
    if (t6 > t7 || t7 > (uintptr_t)t5) gm_trap_slice(t6, t7, t5, gm_srcrgc9bkquf6nr, 140, 15);
    uint64_t t8 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 20)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_complex_of_kh32tib F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0rphlcbno91v));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(7));
    gm_tkn42u7na3t81 t5 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_t2b6tm8c0vgo1 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t5);
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2v5b782(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_2v5b782 F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 13u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_settle_h9ikvs5 *)((&F.h)->up))->l6));
    uint8_t t4 = (gm_eq9m6mvtmgde(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_settle_h9ikvs5 *)((&F.h)->up))->l7), ((uint8_t)1));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_513k31a(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_513k31a F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fin_pat_92m9fnd *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_b0_1 t4 = gm_f_fin_pat_92m9fnd((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_bfunt71 F;
    gm_t88rcucg1b7uk gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t3 = gm_l0;
    gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l2));
    gm_f_walk_v858co7(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ud6f4hb(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_ud6f4hb F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fin_expr_rk98p2r *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_b0_1 t4 = gm_f_fin_expr_rk98p2r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_format_095q43u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_095q43u F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sruk98mc5gf6c));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmghq7vvcvnvi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_2d2knap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_2d2knap F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjcocjnsa7u7g));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4r3nb3bvll23), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_61q4vm1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_61q4vm1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqlp5qu42o606));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllnpq25q3a5a4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_83ij19e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_83ij19e F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssrd6bs7gcs4g));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpls6vb484t8cfc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c5b6tc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c5b6tc3 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh0adpq1lnkpj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(39));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1dirc8ldrsao), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_du48r1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_du48r1i F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sub9rknupld7d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr0qnbmgpa2m5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ebi1ob2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ebi1ob2 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl85di9lce6ej));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(127));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplf579fe8ol0qp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_i1v5tqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_i1v5tqv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s75gqsar0656b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu2tisf3oaee8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_i8teo07(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_i8teo07 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgbib360sn2mu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(130));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnkb386hrc6va), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_isu6baa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_isu6baa F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5n7j87m9k09e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpluue9jh13g9df), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ja2c5em(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ja2c5em F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9jrvf2ogt807));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpltt9gd20e1v7c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lhid49k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lhid49k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95cbsclgo7gk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplh05i67kfcnal), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ns6dben F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgpahsg79gte4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9v8qcr7jilen), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_o9tvoie(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_o9tvoie F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s794tb9qjlq1h));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllgrg03l2v74l), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sl0efd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sl0efd0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sllaecjankikg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1rfj3tqlepqo), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sq1gio6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sq1gio6 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9nm0mpn8r8ov));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsdamq31ts61q), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_ab2ohk2 F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint8_t t3 = ((t2 == ((uint64_t)1ULL)));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
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
            gm_f_kind_jcnt4hm((void *)&gm_l3, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)12ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t8;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            F.ret = t10;
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_g3tddrb_2();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_r81t436();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_v0p8ic9(t8, ((uint64_t)1ULL), t10);
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
    gm_tl874t2atnlh3 t18 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_g3tddrb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_61fb705_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 88), ((uint8_t *)&F.l1), 88);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 88)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_rat_1s624db F;
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t4 = gm_f_write_v26t591((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_walk_v858co7 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    gm_closure t2 = F.l2;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    gm_b0_1 t7;
    if (GM_PTR_TAGGED(t6)) {
        ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t6))(GM_CONV(gm_csguankko4lmhd, t3));
    } else {
        t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = F.l0;
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_t53at80pfdus3 t11;
    gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
    F.l3 = t11;
    void * t12 = (void *)((uint8_t *)&F.l3);
    gm_closure t13 = ((gm_closure){(void *)gm_f_fn_bfunt71, (void *)&F.h, cx});
    gm_f_for_each_component_o5cuh7c(t12, t13);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1) {
    struct gm_fr_write_v26t591 F;
    void * gm_l1; /* out */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l1;
    gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
    if (t4) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t5 = gm_l1;
    gm_tftb1odc739t5 t6 = gm_f_push_t83rg2m(t5, ((uint8_t)47ULL));
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src6b5gn9iggr1p, 130, 5);
    void * t8 = gm_l1;
    gm_b0_1 t9 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 48)), t8);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_obligation_ss66lhk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_check_obligation_ss66lhk F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l7; /* from0 */
    gm_t88rcucg1b7uk gm_l8; /* to */
    gm_tl874t2atnlh3 gm_l9; /* span */
    gm_t88rcucg1b7uk gm_l10; /* from */
    gm_closure gm_l11; /* numeric */
    uint8_t gm_l12; /* ff */
    uint8_t gm_l13; /* tf */
    gm_tpmdr2hmdhsqr gm_l16; /* <temporary array> */
    gm_b16_8 gm_l17; /* methods */
    gm_b16_8 gm_l18; /* hint */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* m */
    gm_t88rcucg1b7uk gm_l23; /* ty */
    gm_tl874t2atnlh3 gm_l24; /* span */
    gm_t88rcucg1b7uk gm_l25; /* t */
    gm_t88rcucg1b7uk gm_l27; /* t */
    uint8_t gm_l28; /* is_float */
    uint8_t gm_l29; /* ok */
    uint32_t gm_l30; /* p */
    gm_b16_8 gm_l31; /* what */
    GM_STACK_CHECK(&F, 3613u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l32;
    uint8_t gm_l33;
    uint8_t gm_l34;
    gm_t53at80pfdus3 gm_l35;
    uint8_t gm_l36;
    gm_t53at80pfdus3 gm_l37;
    gm_b16_8 gm_l38;
    uint8_t gm_l39;
    gm_b16_8 gm_l40;
    gm_b16_8 gm_l41;
    gm_tabb710d9mq2e gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tabb710d9mq2e gm_l48;
    uint8_t gm_l49;
    gm_t53at80pfdus3 gm_l50;
    uint8_t gm_l51;
    gm_t53at80pfdus3 gm_l52;
    gm_t53at80pfdus3 gm_l53;
    uint8_t gm_l54;
    uint8_t gm_l55;
    uint8_t gm_l56;
    uint8_t gm_l57;
    uint8_t gm_l58;
    uint8_t gm_l59;
    uint8_t gm_l60;
    gm_b16_8 gm_l61;
    gm_b16_8 gm_l62;
    gm_tabb710d9mq2e gm_l63;
    {
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, ((uint8_t *)t35));
        void * t37 = (void *)(((uint8_t *)t36) + 8);
        F.l2 = t37;
        {
            {
                {
                    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)0ULL));
                    if (!t39) {
                        goto gm_lb2;
                    }
                    gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
                    gm_l3 = t40;
                    gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)a1) + 8) + 4));
                    gm_l4 = t41;
                    void * t42 = gm_l0;
                    void * t43 = (void *)(((uint8_t *)t42) + 96);
                    void * t44 = gm_l0;
                    void * t45 = GM_LD(void *, ((uint8_t *)t44));
                    gm_t88rcucg1b7uk t46 = gm_l3;
                    gm_t88rcucg1b7uk t47 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t43, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l5 = t47;
                    {
                        {
                            void * t48 = F.l2;
                            gm_t88rcucg1b7uk t49 = gm_l5;
                            uint8_t t50 = gm_f_is_unit_n83ttnb(t48, GM_CONV(gm_csguankko4lmhd, t49));
                            uint8_t t51 = (uint8_t)!t50;
                            if (!t51) {
                                goto gm_lb4;
                            }
                            void * t52 = F.l2;
                            gm_t88rcucg1b7uk t53 = gm_l5;
                            gm_t53at80pfdus3 t54;
                            gm_f_kind_jcnt4hm((void *)&t54, t52, GM_CONV(gm_csguankko4lmhd, t53));
                            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
                            uint8_t t56 = (uint8_t)(!(t55 == ((uint8_t)5ULL)));
                            if (!t56) {
                                goto gm_lb4;
                            }
                            void * t57 = F.l2;
                            gm_t88rcucg1b7uk t58 = gm_l5;
                            gm_t53at80pfdus3 t59;
                            memset(&t59, 0, sizeof t59);
                            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)5ULL));
                            uint8_t t60 = gm_f_is_error_8k27gd1(t57, GM_CONV(gm_csguankko4lmhd, t58));
                            uint8_t t61 = (uint8_t)!t60;
                            if (!t61) {
                                goto gm_lb4;
                            }
                            gm_tl874t2atnlh3 t62 = gm_l4;
                            void * t63 = gm_l0;
                            gm_t88rcucg1b7uk t64 = gm_l5;
                            gm_b16_8 t65 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t63, t64);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t66;
                            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                            gm_f_format_2d2knap((void *)&gm_l32, (void *)NULL, cx, NULL, t66);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                            if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcseljft758gtd, 59, 39);
                            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                            gm_t4gut2c9fj1kt t70;
                            gm_f_error_s6hn871((void *)&t70, (void *)NULL, cx, NULL, t62, t69);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l6 = t70;
                            void * t71 = (void *)((uint8_t *)&F.l6);
                            gm_b16_8 t72;
                            GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sr8du5b2u9c7v));
                            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(32));
                            uint32_t t73 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                            uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                            uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                            gm_tl874t2atnlh3 t76;
                            GM_ST(uint32_t, ((uint8_t *)&t76 + 0), t73);
                            GM_ST(uint32_t, ((uint8_t *)&t76 + 4), t74);
                            GM_ST(uint32_t, ((uint8_t *)&t76 + 8), t75);
                            gm_b16_8 t77;
                            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s9tjtf1373ti5));
                            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(4));
                            gm_b0_1 t78 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t71, t72, t76, t77);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t79 = gm_l0;
                            void * t80 = GM_LD(void *, ((uint8_t *)t79));
                            gm_b0_1 t81 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t80, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
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
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)1ULL));
                    if (!t83) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
                    gm_l7 = t84;
                    gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a1) + 8) + 4));
                    gm_l8 = t85;
                    gm_tl874t2atnlh3 t86 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)a1) + 8) + 8));
                    gm_l9 = t86;
                    void * t87 = gm_l0;
                    void * t88 = (void *)(((uint8_t *)t87) + 96);
                    void * t89 = gm_l0;
                    void * t90 = GM_LD(void *, ((uint8_t *)t89));
                    gm_t88rcucg1b7uk t91 = gm_l7;
                    gm_t88rcucg1b7uk t92 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t88, t90, t91);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t92;
                    void * t93 = F.l2;
                    gm_t88rcucg1b7uk t94 = gm_l10;
                    uint8_t t95 = gm_f_is_error_8k27gd1(t93, GM_CONV(gm_csguankko4lmhd, t94));
                    if (t95) {
                        gm_l33 = ((uint8_t)1);
                    } else {
                        void * t96 = gm_l0;
                        void * t97 = GM_LD(void *, ((uint8_t *)t96));
                        gm_t88rcucg1b7uk t98 = gm_l10;
                        gm_t88rcucg1b7uk t99 = gm_l8;
                        uint8_t t100 = gm_f_conversion_ok_qftumna(t97, GM_CONV(gm_csguankko4lmhd, t98), GM_CONV(gm_csguankko4lmhd, t99));
                        gm_l33 = t100;
                    }
                    uint8_t t101 = gm_l33;
                    if (t101) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    gm_closure t102 = ((gm_closure){(void *)gm_f_numeric_uv79l0u, (void *)&F.h, cx});
                    gm_l11 = t102;
                    void * t103 = F.l2;
                    gm_t88rcucg1b7uk t104 = gm_l10;
                    gm_f_kind_jcnt4hm((void *)&gm_l35, t103, GM_CONV(gm_csguankko4lmhd, t104));
                    {
                        {
                            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)3ULL));
                            if (!t106) {
                                goto gm_lb7;
                            }
                            gm_l34 = ((uint8_t)1);
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            gm_l34 = ((uint8_t)0);
                            goto gm_lb6;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 68, 19);
                    }
gm_lb6:;
                    uint8_t t107 = gm_l34;
                    gm_l12 = t107;
                    void * t108 = F.l2;
                    gm_t88rcucg1b7uk t109 = gm_l8;
                    gm_f_kind_jcnt4hm((void *)&gm_l37, t108, GM_CONV(gm_csguankko4lmhd, t109));
                    {
                        {
                            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)3ULL));
                            if (!t111) {
                                goto gm_lb10;
                            }
                            gm_l36 = ((uint8_t)1);
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            gm_l36 = ((uint8_t)0);
                            goto gm_lb9;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 69, 19);
                    }
gm_lb9:;
                    uint8_t t112 = gm_l36;
                    gm_l13 = t112;
                    gm_closure t113 = gm_l11;
                    gm_t88rcucg1b7uk t114 = gm_l10;
                    gm_closure t115 = t113;
                    gm_node *ccx116 = t115.cx;
                    void * t117 = t115.code;
                    uint8_t t118;
                    if (GM_PTR_TAGGED(t117)) {
                        t118 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t117))(GM_CONV(gm_csguankko4lmhd, t114));
                    } else {
                        t118 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t117)(t115.env, ccx116, NULL, t114);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t119 = (uint8_t)!t118;
                    if (t119) {
                        gm_l39 = ((uint8_t)1);
                    } else {
                        gm_closure t120 = gm_l11;
                        gm_t88rcucg1b7uk t121 = gm_l8;
                        gm_closure t122 = t120;
                        gm_node *ccx123 = t122.cx;
                        void * t124 = t122.code;
                        uint8_t t125;
                        if (GM_PTR_TAGGED(t124)) {
                            t125 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t124))(GM_CONV(gm_csguankko4lmhd, t121));
                        } else {
                            t125 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t124)(t122.env, ccx123, NULL, t121);
                        }
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t126 = (uint8_t)!t125;
                        gm_l39 = t126;
                    }
                    uint8_t t127 = gm_l39;
                    if (t127) {
                        gm_b16_8 t128 = gm_f_empty_slice_5nn7ph0();
                        gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                        gm_l38 = t129;
                    } else {
                        {
                            {
                                uint8_t t130 = gm_l12;
                                uint8_t t131 = (uint8_t)!t130;
                                if (!t131) {
                                    goto gm_lb13;
                                }
                                uint8_t t132 = gm_l13;
                                uint8_t t133 = (uint8_t)!t132;
                                if (!t133) {
                                    goto gm_lb13;
                                }
                                gm_b16_8 t134;
                                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s7f68g16j7a3o));
                                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(5));
                                gm_b16_8 t135;
                                GM_ST(void *, (uint8_t *)&t135, (void *)(gm_s02419ud122hi));
                                GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(7));
                                gm_b16_8 t136;
                                GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sbanjlvk3n14i));
                                GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(3));
                                gm_t9bmh80edou9c t137;
                                GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t134);
                                GM_ST(gm_b16_8, ((uint8_t *)&t137 + 16), t135);
                                GM_ST(gm_b16_8, ((uint8_t *)&t137 + 32), t136);
                                F.l14 = t137;
                                void * t138 = (void *)((uint8_t *)&F.l14);
                                uint64_t t139 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t140 = (uintptr_t)((uint64_t)3ULL);
                                if (t139 > t140 || t140 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t139, t140, ((uint64_t)3ULL), gm_srcseljft758gtd, 72, 41);
                                uint64_t t141 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t142;
                                GM_ST(void *, (uint8_t *)&t142, (void *)(((uint8_t *)t138 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(t141));
                                gm_l40 = t142;
                                goto gm_lb12;
                            }
gm_lb13:;
                            {
                                {
                                    uint8_t t143 = gm_l12;
                                    if (!t143) {
                                        goto gm_lb15;
                                    }
                                    uint8_t t144 = gm_l13;
                                    uint8_t t145 = (uint8_t)!t144;
                                    if (!t145) {
                                        goto gm_lb15;
                                    }
                                    gm_b16_8 t146;
                                    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s7f68g16j7a3o));
                                    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(5));
                                    gm_b16_8 t147;
                                    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sjb60mo5hejk2));
                                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(5));
                                    gm_b16_8 t148;
                                    GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s02419ud122hi));
                                    GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(7));
                                    gm_b16_8 t149;
                                    GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sbanjlvk3n14i));
                                    GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(3));
                                    gm_tdmi4fk9nhqa7 t150;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t146);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 16), t147);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 32), t148);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 48), t149);
                                    F.l15 = t150;
                                    void * t151 = (void *)((uint8_t *)&F.l15);
                                    uint64_t t152 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t153 = (uintptr_t)((uint64_t)4ULL);
                                    if (t152 > t153 || t153 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t152, t153, ((uint64_t)4ULL), gm_srcseljft758gtd, 74, 37);
                                    uint64_t t154 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t155;
                                    GM_ST(void *, (uint8_t *)&t155, (void *)(((uint8_t *)t151 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(t154));
                                    gm_l41 = t155;
                                    goto gm_lb14;
                                }
gm_lb15:;
                                gm_b16_8 t156;
                                GM_ST(void *, (uint8_t *)&t156, (void *)(gm_sfrjcsmisriv1));
                                GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(2));
                                gm_tpmdr2hmdhsqr t157;
                                GM_ST(gm_b16_8, ((uint8_t *)&t157 + 0), t156);
                                gm_l16 = t157;
                                void * t158 = (void *)((uint8_t *)&gm_l16);
                                uint64_t t159 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t160 = (uintptr_t)((uint64_t)1ULL);
                                if (t159 > t160 || t160 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t159, t160, ((uint64_t)1ULL), gm_srcseljft758gtd, 76, 20);
                                uint64_t t161 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t162;
                                GM_ST(void *, (uint8_t *)&t162, (void *)(((uint8_t *)t158 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(t161));
                                gm_l41 = t162;
                            }
gm_lb14:;
                            gm_b16_8 t163 = gm_l41;
                            gm_l40 = t163;
                        }
gm_lb12:;
                        gm_b16_8 t164 = gm_l40;
                        gm_l38 = t164;
                    }
                    gm_b16_8 t165 = gm_l38;
                    gm_l17 = t165;
                    gm_b16_8 t166;
                    GM_ST(void *, (uint8_t *)&t166, (void *)(gm_svknltl22rlbs));
                    GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(8));
                    gm_l18 = t166;
                    void * t167 = gm_l0;
                    void * t168 = GM_LD(void *, ((uint8_t *)t167));
                    gm_t88rcucg1b7uk t169 = gm_l8;
                    gm_tkn42u7na3t81 t170 = gm_f_bits_struct_ty_l1kjtap(t168, GM_CONV(gm_csguankko4lmhd, t169));
                    gm_t88rcucg1b7uk t171 = gm_l10;
                    gm_tkn42u7na3t81 t172;
                    GM_ST(uint8_t, (uint8_t *)&t172 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t172 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t172), t171);
                    ((uint8_t *)&t172)[4] = 1;
                    uint8_t t173 = (gm_eqs65r2l0loh(((uint8_t *)&t170), ((uint8_t *)&t172)));
                    if (t173) {
                        void * t174 = gm_l0;
                        gm_t88rcucg1b7uk t175 = gm_l10;
                        gm_b16_8 t176 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t174, t175);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t177 = gm_l0;
                        gm_t88rcucg1b7uk t178 = gm_l8;
                        gm_b16_8 t179 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t177, t178);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t180 = gm_l0;
                        gm_t88rcucg1b7uk t181 = gm_l8;
                        gm_b16_8 t182 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t180, t181);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tfeab279c16rr t183;
                        GM_ST(gm_b16_8, ((uint8_t *)&t183 + 0), t176);
                        GM_ST(gm_b16_8, ((uint8_t *)&t183 + 16), t179);
                        GM_ST(gm_b16_8, ((uint8_t *)&t183 + 32), t182);
                        gm_f_format_ebi1ob2((void *)&gm_l42, (void *)NULL, cx, NULL, &t183);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t184 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                        if (!t184) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_srcseljft758gtd, 81, 24);
                        gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                        gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                        gm_l18 = t186;
                    } else {
                        uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                        uint8_t t188 = (t187 > ((uint64_t)0ULL));
                        if (t188) {
                            gm_trqfvmjtt4sij t189;
                            gm_f_list_epvducg((void *)&t189, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l19 = t189;
                            gm_b16_8 t190 = gm_l17;
                            gm_l20 = t190;
                            gm_l21 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t192 = gm_l21;
                                    uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                    uint8_t t194 = (t192 < t193);
                                    if (!t194) {
                                        goto gm_lb16;
                                    }
                                    {
                                        void * t195 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                        uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                        uint64_t t197 = gm_l21;
                                        {
                                            {
                                                gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)t195) + (uintptr_t)t197 * 16));
                                                gm_l22 = t198;
                                                void * t199 = (void *)((uint8_t *)&F.l19);
                                                gm_b16_8 t200 = gm_l22;
                                                void * t201 = gm_l0;
                                                gm_t88rcucg1b7uk t202 = gm_l8;
                                                gm_b16_8 t203 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t201, t202);
                                                if (gm_unw.kind) {
                                                    goto gm_ll191;
                                                }
                                                gm_t6j6smj7qoctu t204;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t204 + 0), t200);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t204 + 16), t203);
                                                gm_f_format_61q4vm1((void *)&gm_l43, (void *)NULL, cx, NULL, &t204);
                                                if (gm_unw.kind) {
                                                    goto gm_ll191;
                                                }
                                                uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                                if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_srcseljft758gtd, 84, 47);
                                                gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                                                gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
                                                gm_tftb1odc739t5 t208 = gm_f_push_3ef746b(t199, t207);
                                                gm_l44 = t208;
                                                uint8_t t209 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                                if (!t209) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srcseljft758gtd, 84, 36);
                                                goto gm_lb19;
                                            }
gm_lb20:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 84, 17);
                                        }
gm_lb19:;
                                    }
gm_lb18:;
                                    uint64_t t210 = gm_l21;
                                    uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                                    uint64_t t213 = r211;
                                    gm_l21 = t213;
                                }
                            }
                            goto gm_ls214;
gm_ll191:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb16; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb18; }
                            }
                            goto gm_lad;
gm_ls214:;
gm_lb16:;
                            gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                            gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                            gm_b16_8 t217;
                            GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(2));
                            gm_f_join_sfknljb((void *)&gm_l45, (void *)NULL, cx, NULL, t216, t217);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t218 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                            if (!t218) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_srcseljft758gtd, 85, 54);
                            gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                            gm_tnpifv20irnph t220;
                            GM_ST(gm_b16_8, ((uint8_t *)&t220 + 0), t219);
                            gm_f_format_ja2c5em((void *)&gm_l46, (void *)NULL, cx, NULL, t220);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t221 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                            if (!t221) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srcseljft758gtd, 85, 24);
                            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                            gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                            gm_l18 = t223;
                        }
                    }
                    void * t224 = gm_l0;
                    gm_tl874t2atnlh3 t225 = gm_l9;
                    void * t226 = gm_l0;
                    gm_t88rcucg1b7uk t227 = gm_l8;
                    gm_b16_8 t228 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t226, t227);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t229 = gm_l0;
                    gm_t88rcucg1b7uk t230 = gm_l10;
                    gm_b16_8 t231 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t229, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t232 = gm_l18;
                    gm_tfeab279c16rr t233;
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t228);
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 16), t231);
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 32), t232);
                    gm_f_format_c5b6tc3((void *)&gm_l47, (void *)NULL, cx, NULL, &t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srcseljft758gtd, 87, 28);
                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                    gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                    gm_b0_1 t237 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t224, t225, t236);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t239 = (uint8_t)(t238 == ((uint8_t)2ULL));
                    if (!t239) {
                        goto gm_lb21;
                    }
                    gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
                    gm_l23 = t240;
                    gm_tl874t2atnlh3 t241 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)a1) + 8) + 4));
                    gm_l24 = t241;
                    void * t242 = gm_l0;
                    void * t243 = (void *)(((uint8_t *)t242) + 96);
                    void * t244 = gm_l0;
                    void * t245 = GM_LD(void *, ((uint8_t *)t244));
                    gm_t88rcucg1b7uk t246 = gm_l23;
                    gm_t88rcucg1b7uk t247 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t243, t245, t246);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t247;
                    {
                        {
                            void * t248 = F.l2;
                            gm_t88rcucg1b7uk t249 = gm_l25;
                            uint8_t t250 = gm_f_is_error_8k27gd1(t248, GM_CONV(gm_csguankko4lmhd, t249));
                            uint8_t t251 = (uint8_t)!t250;
                            if (!t251) {
                                goto gm_lb23;
                            }
                            void * t252 = gm_l0;
                            void * t253 = GM_LD(void *, ((uint8_t *)t252));
                            gm_t88rcucg1b7uk t254 = gm_l25;
                            uint8_t t255 = gm_f_has_ord_1io9knm(t253, GM_CONV(gm_csguankko4lmhd, t254));
                            uint8_t t256 = (uint8_t)!t255;
                            if (!t256) {
                                goto gm_lb23;
                            }
                            void * t257 = gm_l0;
                            gm_tl874t2atnlh3 t258 = gm_l24;
                            void * t259 = gm_l0;
                            gm_t88rcucg1b7uk t260 = gm_l25;
                            gm_b16_8 t261 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t259, t260);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t262;
                            GM_ST(gm_b16_8, ((uint8_t *)&t262 + 0), t261);
                            gm_f_format_095q43u((void *)&gm_l48, (void *)NULL, cx, NULL, t262);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t263 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t263) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srcseljft758gtd, 92, 32);
                            gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                            gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                            gm_b0_1 t266 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t257, t258, t265);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                    }
gm_lb22:;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t268 = (uint8_t)(t267 == ((uint8_t)3ULL));
                    if (!t268) {
                        goto gm_lb24;
                    }
                    memmove(((uint8_t *)&F.l26), (((uint8_t *)a1) + 8), 40);
                    void * t269 = gm_l0;
                    void * t270 = (void *)(((uint8_t *)t269) + 96);
                    void * t271 = gm_l0;
                    void * t272 = GM_LD(void *, ((uint8_t *)t271));
                    gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l26));
                    gm_t88rcucg1b7uk t274 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t270, t272, t273);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t274;
                    void * t275 = F.l2;
                    gm_t88rcucg1b7uk t276 = gm_l27;
                    gm_f_kind_jcnt4hm((void *)&gm_l50, t275, GM_CONV(gm_csguankko4lmhd, t276));
                    {
                        {
                            uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t278 = (uint8_t)(t277 == ((uint8_t)11ULL));
                            if (!t278) {
                                goto gm_lb26;
                            }
                            gm_l49 = ((uint8_t)1);
                            goto gm_lb25;
                        }
gm_lb26:;
                        {
                            uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t280 = (uint8_t)(t279 == ((uint8_t)20ULL));
                            if (!t280) {
                                goto gm_lb27;
                            }
                            gm_l49 = ((uint8_t)1);
                            goto gm_lb25;
                        }
gm_lb27:;
                        {
                            gm_l49 = ((uint8_t)0);
                            goto gm_lb25;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 97, 16);
                    }
gm_lb25:;
                    uint8_t t281 = gm_l49;
                    if (t281) {
                        void * t282 = gm_l0;
                        gm_tl874t2atnlh3 t283 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 4));
                        gm_b16_8 t284;
                        GM_ST(void *, (uint8_t *)&t284, (void *)(gm_snqm9b8h9b70a));
                        GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(119));
                        gm_b0_1 t285 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t282, t283, t284);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t286 = F.l2;
                    gm_t88rcucg1b7uk t287 = gm_l27;
                    uint8_t t288 = gm_f_is_error_8k27gd1(t286, GM_CONV(gm_csguankko4lmhd, t287));
                    if (t288) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t289 = F.l2;
                    gm_t88rcucg1b7uk t290 = gm_l27;
                    gm_f_kind_jcnt4hm((void *)&gm_l52, t289, GM_CONV(gm_csguankko4lmhd, t290));
                    {
                        {
                            uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&gm_l52) + 0));
                            uint8_t t292 = (uint8_t)(t291 == ((uint8_t)3ULL));
                            if (!t292) {
                                goto gm_lb30;
                            }
                            gm_l51 = ((uint8_t)1);
                            goto gm_lb29;
                        }
gm_lb30:;
                        {
                            gm_l51 = ((uint8_t)0);
                            goto gm_lb29;
                        }
gm_lb31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 102, 25);
                    }
gm_lb29:;
                    uint8_t t293 = gm_l51;
                    gm_l28 = t293;
                    gm_l29 = ((uint8_t)0);
                    {
                        {
                            void * t294 = F.l2;
                            gm_t88rcucg1b7uk t295 = gm_l27;
                            gm_f_kind_jcnt4hm((void *)&gm_l53, t294, GM_CONV(gm_csguankko4lmhd, t295));
                            uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l53) + 0));
                            uint8_t t297 = (uint8_t)(t296 == ((uint8_t)19ULL));
                            if (!t297) {
                                goto gm_lb33;
                            }
                            uint32_t t298 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 8));
                            gm_l30 = t298;
                            uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 32));
                            if (t299) {
                                void * t300 = gm_l0;
                                void * t301 = GM_LD(void *, ((uint8_t *)t300));
                                uint32_t t302 = gm_l30;
                                gm_t9qkh0rsooc8d t303;
                                GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)3ULL));
                                uint8_t t304 = gm_f_param_has_2r09oc7(t301, t302, t303);
                                gm_l55 = t304;
                            } else {
                                void * t305 = gm_l0;
                                void * t306 = GM_LD(void *, ((uint8_t *)t305));
                                uint32_t t307 = gm_l30;
                                gm_t9qkh0rsooc8d t308;
                                GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)2ULL));
                                uint8_t t309 = gm_f_param_has_2r09oc7(t306, t307, t308);
                                gm_l55 = t309;
                            }
                            uint8_t t310 = gm_l55;
                            if (t310) {
                                uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
                                uint8_t t312 = (uint8_t)!t311;
                                if (t312) {
                                    gm_l56 = ((uint8_t)1);
                                } else {
                                    void * t313 = gm_l0;
                                    void * t314 = GM_LD(void *, ((uint8_t *)t313));
                                    uint32_t t315 = gm_l30;
                                    gm_t9qkh0rsooc8d t316;
                                    GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)4ULL));
                                    uint8_t t317 = gm_f_param_has_2r09oc7(t314, t315, t316);
                                    gm_l56 = t317;
                                }
                                uint8_t t318 = gm_l56;
                                gm_l54 = t318;
                            } else {
                                gm_l54 = ((uint8_t)0);
                            }
                            uint8_t t319 = gm_l54;
                            gm_l29 = t319;
                            goto gm_lb32;
                        }
gm_lb33:;
                        uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 32));
                        if (t320) {
                            void * t321 = F.l2;
                            gm_t88rcucg1b7uk t322 = gm_l27;
                            uint8_t t323 = gm_f_is_integer_8d4f9ek(t321, GM_CONV(gm_csguankko4lmhd, t322));
                            if (t323) {
                                uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
                                uint8_t t325 = (uint8_t)!t324;
                                if (t325) {
                                    gm_l58 = ((uint8_t)1);
                                } else {
                                    void * t326 = F.l2;
                                    gm_t88rcucg1b7uk t327 = gm_l27;
                                    uint8_t t328 = gm_f_is_signed_integer_s66djm7(t326, GM_CONV(gm_csguankko4lmhd, t327));
                                    gm_l58 = t328;
                                }
                                uint8_t t329 = gm_l58;
                                gm_l57 = t329;
                            } else {
                                gm_l57 = ((uint8_t)0);
                            }
                            uint8_t t330 = gm_l57;
                            gm_l29 = t330;
                        } else {
                            uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
                            if (t331) {
                                void * t332 = F.l2;
                                gm_t88rcucg1b7uk t333 = gm_l27;
                                uint8_t t334 = gm_f_is_signed_integer_s66djm7(t332, GM_CONV(gm_csguankko4lmhd, t333));
                                if (t334) {
                                    gm_l59 = ((uint8_t)1);
                                } else {
                                    uint8_t t335 = gm_l28;
                                    gm_l59 = t335;
                                }
                                uint8_t t336 = gm_l59;
                                gm_l29 = t336;
                            } else {
                                void * t337 = F.l2;
                                gm_t88rcucg1b7uk t338 = gm_l27;
                                uint8_t t339 = gm_f_is_integer_8d4f9ek(t337, GM_CONV(gm_csguankko4lmhd, t338));
                                if (t339) {
                                    gm_l60 = ((uint8_t)1);
                                } else {
                                    uint8_t t340 = gm_l28;
                                    gm_l60 = t340;
                                }
                                uint8_t t341 = gm_l60;
                                gm_l29 = t341;
                            }
                        }
                    }
gm_lb32:;
                    uint8_t t342 = gm_l29;
                    uint8_t t343 = (uint8_t)!t342;
                    if (t343) {
                        uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 32));
                        if (t344) {
                            gm_b16_8 t345;
                            GM_ST(void *, (uint8_t *)&t345, (void *)(gm_sgujtgfflv5dk));
                            GM_ST(uintptr_t, (uint8_t *)&t345 + 8, (uintptr_t)(7));
                            gm_l61 = t345;
                        } else {
                            uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
                            if (t346) {
                                gm_b16_8 t347;
                                GM_ST(void *, (uint8_t *)&t347, (void *)(gm_sh57gpir0vvh2));
                                GM_ST(uintptr_t, (uint8_t *)&t347 + 8, (uintptr_t)(23));
                                gm_l62 = t347;
                            } else {
                                gm_b16_8 t348;
                                GM_ST(void *, (uint8_t *)&t348, (void *)(gm_s03s7smc9pnp4));
                                GM_ST(uintptr_t, (uint8_t *)&t348 + 8, (uintptr_t)(7));
                                gm_l62 = t348;
                            }
                            gm_b16_8 t349 = gm_l62;
                            gm_l61 = t349;
                        }
                        gm_b16_8 t350 = gm_l61;
                        gm_l31 = t350;
                        void * t351 = gm_l0;
                        gm_tl874t2atnlh3 t352 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 4));
                        gm_b16_8 t353 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 16));
                        gm_b16_8 t354 = gm_l31;
                        void * t355 = gm_l0;
                        gm_t88rcucg1b7uk t356 = gm_l27;
                        gm_b16_8 t357 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t355, t356);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tfeab279c16rr t358;
                        GM_ST(gm_b16_8, ((uint8_t *)&t358 + 0), t353);
                        GM_ST(gm_b16_8, ((uint8_t *)&t358 + 16), t354);
                        GM_ST(gm_b16_8, ((uint8_t *)&t358 + 32), t357);
                        gm_f_format_isu6baa((void *)&gm_l63, (void *)NULL, cx, NULL, &t358);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t359 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                        if (!t359) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_srcseljft758gtd, 115, 34);
                        gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                        gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
                        gm_b0_1 t362 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t351, t352, t361);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb24:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 55, 5);
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
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_conversion_ok_qftumna(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conversion_ok_qftumna F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* from */
    gm_t88rcucg1b7uk gm_l2; /* to */
    gm_t88rcucg1b7uk gm_l3; /* b */
    gm_t88rcucg1b7uk gm_l4; /* b */
    gm_tep2drsl9408b gm_l5; /* u */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_tkn42u7na3t81 gm_l9;
    gm_tkn42u7na3t81 gm_l10;
    gm_tdlpv0q69lov1 gm_l11;
    uint8_t gm_l12;
    void * t16 = gm_l0;
    gm_t88rcucg1b7uk t17 = gm_l1;
    gm_t88rcucg1b7uk t18 = gm_l2;
    uint8_t t19 = gm_f_lossless_a54o69s(t16, GM_CONV(gm_csguankko4lmhd, t17), GM_CONV(gm_csguankko4lmhd, t18));
    if (t19) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    {
        {
            void * t20 = gm_l0;
            gm_t88rcucg1b7uk t21 = gm_l1;
            gm_tkn42u7na3t81 t22 = gm_f_bits_struct_ty_l1kjtap(t20, GM_CONV(gm_csguankko4lmhd, t21));
            gm_l9 = t22;
            uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
            if (!t23) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l9));
            gm_l3 = t24;
            gm_t88rcucg1b7uk t25 = gm_l3;
            gm_t88rcucg1b7uk t26 = gm_l2;
            uint8_t t27 = (gm_eq9m6mvtmgde(((uint8_t *)&t25), ((uint8_t *)&t26)));
            if (!t27) {
                goto gm_lb2;
            }
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l1;
            uint8_t t30 = gm_f_is_plain_n60iihm(t28, GM_CONV(gm_csguankko4lmhd, t29));
            if (!t30) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t31 = gm_l0;
            gm_t88rcucg1b7uk t32 = gm_l2;
            gm_tkn42u7na3t81 t33 = gm_f_bits_struct_ty_l1kjtap(t31, GM_CONV(gm_csguankko4lmhd, t32));
            gm_l10 = t33;
            uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
            if (!t34) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l10));
            gm_l4 = t35;
            gm_t88rcucg1b7uk t36 = gm_l4;
            gm_t88rcucg1b7uk t37 = gm_l1;
            uint8_t t38 = (gm_eq9m6mvtmgde(((uint8_t *)&t36), ((uint8_t *)&t37)));
            if (!t38) {
                goto gm_lb4;
            }
            void * t39 = gm_l0;
            gm_t88rcucg1b7uk t40 = gm_l2;
            uint8_t t41 = gm_f_is_plain_n60iihm(t39, GM_CONV(gm_csguankko4lmhd, t40));
            if (!t41) {
                goto gm_lb4;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            void * t42 = gm_l0;
            gm_t88rcucg1b7uk t43 = gm_l1;
            gm_f_union_variants_a7khaqp((void *)&gm_l11, t42, GM_CONV(gm_csguankko4lmhd, t43));
            uint8_t t44 = (uint8_t)((((uint8_t *)&gm_l11))[24] != 0);
            if (!t44) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l11), 24);
            void * t45 = gm_l0;
            void * t46 = (void *)(((uint8_t *)t45) + 8);
            gm_t88rcucg1b7uk t47 = gm_l2;
            uint8_t t48 = gm_f_is_integer_8d4f9ek(t46, GM_CONV(gm_csguankko4lmhd, t47));
            if (!t48) {
                goto gm_lb6;
            }
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l6 = t49;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t50 = gm_l7;
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t52 = (t50 < t51);
                    if (!t52) {
                        goto gm_lb7;
                    }
                    {
                        void * t53 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t55 = gm_l7;
                        {
                            {
                                memmove(((uint8_t *)&F.l8), (((uint8_t *)t53) + (uintptr_t)t55 * 72), 72);
                                {
                                    {
                                        uint8_t t56 = (uint8_t)(((((uint8_t *)&F.l8) + 16))[4] != 0);
                                        if (t56) {
                                            goto gm_lb13;
                                        }
                                        gm_l12 = ((uint8_t)0);
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                    {
                                        gm_l12 = ((uint8_t)1);
                                        goto gm_lb12;
                                    }
gm_lb14:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 294, 16);
                                }
gm_lb12:;
                                uint8_t t57 = gm_l12;
                                if (t57) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 293, 9);
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint64_t t58 = gm_l7;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l7 = t61;
                }
            }
gm_lb7:;
            void * t62 = gm_l0;
            gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 16));
            gm_t88rcucg1b7uk t64 = gm_l2;
            uint8_t t65 = gm_f_lossless_a54o69s(t62, GM_CONV(gm_csguankko4lmhd, t63), GM_CONV(gm_csguankko4lmhd, t64));
            F.ret = t65;
            goto gm_fret;
        }
gm_lb6:;
    }
gm_lb5:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fin_cond_32pmhkp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fin_cond_32pmhkp F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* cond */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* p */
    GM_STACK_CHECK(&F, 179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t9 = gm_l1;
    gm_l2 = t9;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t15 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t13) + (uintptr_t)t15 * 24), 24);
                        {
                            {
                                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
                                if (!t17) {
                                    goto gm_lb7;
                                }
                                void * t18 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t18;
                                void * t19 = gm_l0;
                                void * t20 = gm_l5;
                                gm_b0_1 t21 = gm_f_fin_pat_92m9fnd((void *)NULL, cx, NULL, t19, t20);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 199, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l3;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l3 = t25;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_expr_rk98p2r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fin_expr_rk98p2r F;
    void * gm_l1; /* e */
    uint64_t gm_l2; /* errors */
    gm_b16_8 gm_l3; /* arms */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l6; /* a */
    gm_trl2khmuphncd gm_l7; /* x */
    gm_toceffb697p0u gm_l9; /* fixed_by */
    void * gm_l10; /* tys */
    gm_tl874t2atnlh3 gm_l13; /* span */
    gm_b16_8 gm_l14; /* t */
    void * gm_l15; /* r */
    void * gm_l16; /* c */
    gm_t88rcucg1b7uk gm_l17; /* elem */
    gm_tl874t2atnlh3 gm_l18; /* span */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    void * gm_l24; /* x */
    gm_tmn7n0n9raou5 gm_l25; /* kind */
    void * gm_l26; /* inner */
    gm_t88rcucg1b7uk gm_l27; /* target */
    gm_b16_8 gm_l28; /* steps */
    GM_STACK_CHECK(&F, 3721u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_toceffb697p0u gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    gm_t53at80pfdus3 gm_l32;
    uint8_t gm_l33;
    gm_t53at80pfdus3 gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tabb710d9mq2e gm_l36;
    uint8_t gm_l37;
    gm_t53at80pfdus3 gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tkn42u7na3t81 gm_l40;
    gm_t88rcucg1b7uk gm_l41;
    gm_tabb710d9mq2e gm_l42;
    uint8_t gm_l43;
    gm_t53at80pfdus3 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_th2lqbo77pvsc gm_l46;
    gm_tabb710d9mq2e gm_l47;
    {
        void * t46 = F.l0;
        void * t47 = GM_LD(void *, ((uint8_t *)t46));
        void * t48 = GM_LD(void *, ((uint8_t *)t47));
        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 8));
        uint64_t t50 = gm_f_error_count_omtt6qd(t49);
        gm_l2 = t50;
        void * t51 = gm_l1;
        gm_closure t52 = ((gm_closure){(void *)gm_f_fn_ud6f4hb, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t53 = gm_l1;
        {
            {
                uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
                uint8_t t55 = (uint8_t)(t54 == ((uint8_t)44ULL));
                if (!t55) {
                    goto gm_lb2;
                }
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((((uint8_t *)t53) + 8) + 8) + 8));
                gm_l3 = t56;
                gm_b16_8 t57 = gm_l3;
                gm_l4 = t57;
                gm_l5 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t59 = gm_l5;
                        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint8_t t61 = (t59 < t60);
                        if (!t61) {
                            goto gm_lb3;
                        }
                        {
                            void * t62 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint64_t t64 = gm_l5;
                            {
                                {
                                    memmove(((uint8_t *)&gm_l6), (((uint8_t *)t62) + (uintptr_t)t64 * 24), 24);
                                    void * t65 = F.l0;
                                    void * t66 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    gm_b0_1 t67 = gm_f_fin_pat_92m9fnd((void *)NULL, cx, NULL, t65, t66);
                                    if (gm_unw.kind) {
                                        goto gm_ll58;
                                    }
                                    goto gm_lb6;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 127, 13);
                            }
gm_lb6:;
                        }
gm_lb5:;
                        uint64_t t68 = gm_l5;
                        uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                        uint64_t t71 = r69;
                        gm_l5 = t71;
                    }
                }
                goto gm_ls72;
gm_ll58:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb3; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb5; }
                }
                goto gm_lad;
gm_ls72:;
gm_lb3:;
                {
                    {
                        void * t73 = F.l0;
                        uint64_t t74 = GM_LD(uint64_t, ((((uint8_t *)t73) + 480) + 8));
                        uint8_t t75 = (t74 > ((uint64_t)0ULL));
                        if (!t75) {
                            goto gm_lb9;
                        }
                        void * t76 = F.l0;
                        void * t77 = GM_LD(void *, ((uint8_t *)t76));
                        void * t78 = GM_LD(void *, ((uint8_t *)t77));
                        void * t79 = GM_LD(void *, (((uint8_t *)t78) + 8));
                        uint64_t t80 = gm_f_error_count_omtt6qd(t79);
                        uint64_t t81 = gm_l2;
                        uint8_t t82 = ((t80 == t81));
                        if (!t82) {
                            goto gm_lb9;
                        }
                        void * t83 = F.l0;
                        void * t84 = gm_l1;
                        gm_b0_1 t85 = gm_f_check_deferred_match_742e2u5((void *)NULL, cx, NULL, t83, t84);
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
gm_lb2:;
            {
                uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
                uint8_t t87 = (uint8_t)(t86 == ((uint8_t)47ULL));
                if (!t87) {
                    goto gm_lb10;
                }
                memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t53) + 8) + 8), 32);
                void * t88 = F.l0;
                gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                gm_b0_1 t91 = gm_f_fin_cond_32pmhkp((void *)NULL, cx, NULL, t88, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb10:;
            {
                uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
                uint8_t t93 = (uint8_t)(t92 == ((uint8_t)48ULL));
                if (!t93) {
                    goto gm_lb11;
                }
                memmove(((uint8_t *)&F.l8), ((((uint8_t *)t53) + 8) + 8), 40);
                void * t94 = F.l0;
                gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                gm_b0_1 t97 = gm_f_fin_cond_32pmhkp((void *)NULL, cx, NULL, t94, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb11:;
            {
                goto gm_lb1;
            }
gm_lb12:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 125, 5);
        }
gm_lb1:;
    }
    {
        void * t98 = gm_l1;
        {
            {
                uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t98) + 8) + 0));
                uint8_t t100 = (uint8_t)(t99 == ((uint8_t)0ULL));
                if (!t100) {
                    goto gm_lb14;
                }
                gm_l30 = ((uint8_t)1);
                goto gm_lb13;
            }
gm_lb14:;
            {
                gm_l30 = ((uint8_t)0);
                goto gm_lb13;
            }
gm_lb15:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 135, 33);
        }
gm_lb13:;
        uint8_t t101 = gm_l30;
        if (t101) {
            void * t102 = F.l0;
            void * t103 = (void *)(((uint8_t *)t102) + 96);
            void * t104 = F.l0;
            void * t105 = GM_LD(void *, ((uint8_t *)t104));
            void * t106 = (void *)(((uint8_t *)t105) + 8);
            void * t107 = gm_l1;
            gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t107));
            gm_toceffb697p0u t109 = gm_f_fixed_by_afiovc1(t103, t106, GM_CONV(gm_csguankko4lmhd, t108));
            gm_l29 = t109;
        } else {
            gm_toceffb697p0u t110;
            memset(&t110, 0, sizeof t110);
            gm_l29 = t110;
        }
        gm_toceffb697p0u t111 = gm_l29;
        gm_l9 = t111;
        void * t112 = gm_l1;
        void * t113 = F.l0;
        void * t114 = (void *)(((uint8_t *)t113) + 96);
        void * t115 = F.l0;
        void * t116 = GM_LD(void *, ((uint8_t *)t115));
        void * t117 = gm_l1;
        gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t117));
        gm_t88rcucg1b7uk t119 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t114, t116, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t112), t119);
        void * t120 = F.l0;
        void * t121 = GM_LD(void *, ((uint8_t *)t120));
        void * t122 = (void *)(((uint8_t *)t121) + 8);
        gm_l10 = t122;
        void * t123 = gm_l1;
        {
            {
                {
                    uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t123) + 8) + 0));
                    uint8_t t125 = (uint8_t)(t124 == ((uint8_t)0ULL));
                    if (!t125) {
                        goto gm_lb17;
                    }
                    memmove(((uint8_t *)&F.l11), ((((uint8_t *)t123) + 8) + 8), 48);
                    void * t126 = gm_l10;
                    void * t127 = gm_l1;
                    gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t127));
                    gm_f_kind_jcnt4hm((void *)&gm_l32, t126, GM_CONV(gm_csguankko4lmhd, t128));
                    {
                        {
                            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                            uint8_t t130 = (uint8_t)(t129 == ((uint8_t)19ULL));
                            if (!t130) {
                                goto gm_lb19;
                            }
                            gm_l31 = ((uint8_t)1);
                            goto gm_lb18;
                        }
gm_lb19:;
                        {
                            gm_l31 = ((uint8_t)0);
                            goto gm_lb18;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 140, 16);
                    }
gm_lb18:;
                    uint8_t t131 = gm_l31;
                    if (t131) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t132 = gm_l10;
                    void * t133 = gm_l1;
                    gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t133));
                    gm_f_kind_jcnt4hm((void *)&gm_l34, t132, GM_CONV(gm_csguankko4lmhd, t134));
                    {
                        {
                            uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l34) + 0));
                            uint8_t t136 = (uint8_t)(t135 == ((uint8_t)3ULL));
                            if (!t136) {
                                goto gm_lb22;
                            }
                            gm_l33 = ((uint8_t)1);
                            goto gm_lb21;
                        }
gm_lb22:;
                        {
                            gm_l33 = ((uint8_t)0);
                            goto gm_lb21;
                        }
gm_lb23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 141, 16);
                    }
gm_lb21:;
                    uint8_t t137 = gm_l33;
                    if (t137) {
                        void * t138 = F.l0;
                        void * t139 = gm_l1;
                        gm_tngtuiuskn938 t140;
                        gm_f_from_int_ofjuoqh((void *)&t140, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b0_1 t141 = gm_f_fin_float_9k2be5v((void *)NULL, cx, NULL, t138, t139, &t140, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    } else {
                        {
                            {
                                void * t142 = gm_l10;
                                void * t143 = gm_l1;
                                gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t143));
                                uint8_t t145 = gm_f_is_error_8k27gd1(t142, GM_CONV(gm_csguankko4lmhd, t144));
                                uint8_t t146 = (uint8_t)!t145;
                                if (!t146) {
                                    goto gm_lb25;
                                }
                                void * t147 = F.l0;
                                void * t148 = GM_LD(void *, ((uint8_t *)t147));
                                void * t149 = gm_l1;
                                gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t149));
                                uint8_t t151 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t148, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), t150);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t152 = (uint8_t)!t151;
                                if (!t152) {
                                    goto gm_lb25;
                                }
                                void * t153 = gm_l1;
                                gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t153) + 72));
                                gm_b16_8 t155 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t156 = F.l0;
                                void * t157 = gm_l1;
                                gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t157));
                                gm_b16_8 t159 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t156, t158);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t160;
                                GM_ST(gm_b16_8, ((uint8_t *)&t160 + 0), t155);
                                GM_ST(gm_b16_8, ((uint8_t *)&t160 + 16), t159);
                                gm_f_format_lhid49k((void *)&gm_l35, (void *)NULL, cx, NULL, &t160);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t161 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                if (!t161) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcseljft758gtd, 144, 41);
                                gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                                gm_t4gut2c9fj1kt t164;
                                gm_f_error_s6hn871((void *)&t164, (void *)NULL, cx, NULL, t154, t163);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l12 = t164;
                                {
                                    {
                                        uint8_t t165 = (uint8_t)((((uint8_t *)&gm_l9))[12] != 0);
                                        if (!t165) {
                                            goto gm_lb27;
                                        }
                                        gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l9));
                                        gm_l13 = t166;
                                        void * t167 = F.l0;
                                        void * t168 = gm_l1;
                                        gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t168));
                                        gm_b16_8 t170 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t167, t169);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l14 = t170;
                                        void * t171 = (void *)((uint8_t *)&F.l12);
                                        gm_tl874t2atnlh3 t172 = gm_l13;
                                        gm_b16_8 t173 = gm_l14;
                                        gm_b16_8 t174 = gm_l14;
                                        gm_b16_8 t175 = gm_l14;
                                        gm_tfeab279c16rr t176;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t173);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t176 + 16), t174);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t176 + 32), t175);
                                        gm_f_format_i8teo07((void *)&gm_l36, (void *)NULL, cx, NULL, &t176);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                        if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srcseljft758gtd, 148, 39);
                                        gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                                        gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                                        gm_f_add_label_50fj7vv(t171, GM_CONV(gm_cseahvg8v7p0vs, t172), t179);
                                        goto gm_lb26;
                                    }
gm_lb27:;
                                }
gm_lb26:;
                                void * t180 = F.l0;
                                void * t181 = GM_LD(void *, ((uint8_t *)t180));
                                gm_b0_1 t182 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t181, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb24;
                            }
gm_lb25:;
                        }
gm_lb24:;
                    }
                    goto gm_lb16;
                }
gm_lb17:;
            }
            {
                {
                    uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t123) + 8) + 0));
                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)2ULL));
                    if (!t184) {
                        goto gm_lb28;
                    }
                    void * t185 = GM_LD(void *, ((((uint8_t *)t123) + 8) + 8));
                    gm_l15 = t185;
                    void * t186 = gm_l10;
                    void * t187 = gm_l1;
                    gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t187));
                    gm_f_kind_jcnt4hm((void *)&gm_l38, t186, GM_CONV(gm_csguankko4lmhd, t188));
                    {
                        {
                            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 0));
                            uint8_t t190 = (uint8_t)(t189 == ((uint8_t)19ULL));
                            if (!t190) {
                                goto gm_lb30;
                            }
                            gm_l37 = ((uint8_t)1);
                            goto gm_lb29;
                        }
gm_lb30:;
                        {
                            gm_l37 = ((uint8_t)0);
                            goto gm_lb29;
                        }
gm_lb31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 154, 16);
                    }
gm_lb29:;
                    uint8_t t191 = gm_l37;
                    if (t191) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t192 = gm_l10;
                    void * t193 = gm_l1;
                    gm_t88rcucg1b7uk t194 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t193));
                    uint8_t t195 = gm_f_is_integer_8d4f9ek(t192, GM_CONV(gm_csguankko4lmhd, t194));
                    if (t195) {
                        void * t196 = F.l0;
                        void * t197 = gm_l1;
                        gm_tl874t2atnlh3 t198 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t197) + 72));
                        void * t199 = F.l0;
                        void * t200 = gm_l1;
                        gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t200));
                        gm_b16_8 t202 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t199, t201);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t203;
                        GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t202);
                        gm_f_format_i1v5tqv((void *)&gm_l39, (void *)NULL, cx, NULL, t203);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                        if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_srcseljft758gtd, 156, 34);
                        gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                        gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                        gm_b0_1 t207 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t196, t198, t206);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t208 = gm_l1;
                        gm_tbci5ospk5nnn t209;
                        gm_f_zero_s25l990((void *)&t209, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t2b6tm8c0vgo1 t210;
                        GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t210 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t210 + 8), t209);
                        GM_ST(gm_t2b6tm8c0vgo1, (((uint8_t *)t208) + 8), t210);
                    } else {
                        void * t211 = F.l0;
                        void * t212 = gm_l1;
                        void * t213 = gm_l15;
                        gm_b0_1 t214 = gm_f_fin_float_9k2be5v((void *)NULL, cx, NULL, t211, t212, &(*(gm_tngtuiuskn938 *)((uint8_t *)t213)), ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb16;
                }
gm_lb28:;
                {
                    uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t123) + 8) + 0));
                    uint8_t t216 = (uint8_t)(t215 == ((uint8_t)3ULL));
                    if (!t216) {
                        goto gm_lb32;
                    }
                    void * t217 = GM_LD(void *, ((((uint8_t *)t123) + 8) + 8));
                    gm_l16 = t217;
                    void * t218 = F.l0;
                    void * t219 = GM_LD(void *, ((uint8_t *)t218));
                    void * t220 = gm_l1;
                    gm_t88rcucg1b7uk t221 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t220));
                    gm_tkn42u7na3t81 t222 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t219, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l40 = t222;
                    uint8_t t223 = (uint8_t)((((uint8_t *)&gm_l40))[4] != 0);
                    if (t223) {
                        gm_t88rcucg1b7uk t224 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l40));
                        gm_l41 = t224;
                    } else {
                        void * t225 = F.l0;
                        void * t226 = gm_l1;
                        gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t226) + 72));
                        void * t228 = F.l0;
                        void * t229 = gm_l1;
                        gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t229));
                        gm_b16_8 t231 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t228, t230);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t232;
                        GM_ST(gm_b16_8, ((uint8_t *)&t232 + 0), t231);
                        gm_f_format_sq1gio6((void *)&gm_l42, (void *)NULL, cx, NULL, t232);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t233 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                        if (!t233) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_srcseljft758gtd, 165, 34);
                        gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                        gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                        gm_b0_1 t236 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t225, t227, t235);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t237 = gm_l1;
                        void * t238 = F.l0;
                        void * t239 = GM_LD(void *, ((uint8_t *)t238));
                        void * t240 = gm_l1;
                        gm_tl874t2atnlh3 t241 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t240) + 72));
                        void * t242 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t239, t241);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        memmove(((uint8_t *)t237), ((uint8_t *)t242), 88);
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t243 = gm_l41;
                    gm_l17 = t243;
                    void * t244 = gm_l1;
                    gm_tl874t2atnlh3 t245 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t244) + 72));
                    gm_l18 = t245;
                    gm_trqfvmjtt4sij t246;
                    gm_f_list_vp1djv1_2((void *)&t246, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l19 = t246;
                    void * t247 = gm_l16;
                    void * t248 = gm_l16;
                    gm_tngtuiuskn938 t249 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)t247));
                    gm_tngtuiuskn938 t250 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t248) + 96));
                    gm_tdskijji6mt1d t251;
                    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t251 + 0), t249);
                    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t251 + 96), t250);
                    F.l20 = t251;
                    void * t252 = (void *)((uint8_t *)&F.l20);
                    uint64_t t253 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t254 = (uintptr_t)((uint64_t)2ULL);
                    if (t253 > t254 || t254 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t253, t254, ((uint64_t)2ULL), gm_srcseljft758gtd, 171, 13);
                    uint64_t t255 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t256;
                    GM_ST(void *, (uint8_t *)&t256, (void *)(((uint8_t *)t252 + (uintptr_t)((uint64_t)0ULL) * 96)));
                    GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(t255));
                    gm_l21 = t256;
                    gm_l22 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t258 = gm_l22;
                            uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                            uint8_t t260 = (t258 < t259);
                            if (!t260) {
                                goto gm_lb33;
                            }
                            {
                                void * t261 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                uint64_t t263 = gm_l22;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l23), (((uint8_t *)t261) + (uintptr_t)t263 * 96), 96);
                                        gm_t88rcucg1b7uk t264 = gm_l17;
                                        gm_t2b6tm8c0vgo1 t265;
                                        gm_f_untyped_float_m6lbr0g((void *)&t265, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l23)));
                                        if (gm_unw.kind) {
                                            goto gm_ll257;
                                        }
                                        gm_tl874t2atnlh3 t266 = gm_l18;
                                        void * t267 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t264, &t265, t266);
                                        if (gm_unw.kind) {
                                            goto gm_ll257;
                                        }
                                        gm_l24 = t267;
                                        void * t268 = gm_l10;
                                        gm_t88rcucg1b7uk t269 = gm_l17;
                                        gm_f_kind_jcnt4hm((void *)&gm_l44, t268, GM_CONV(gm_csguankko4lmhd, t269));
                                        {
                                            {
                                                uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                                                uint8_t t271 = (uint8_t)(t270 == ((uint8_t)19ULL));
                                                if (!t271) {
                                                    goto gm_lb39;
                                                }
                                                gm_l43 = ((uint8_t)0);
                                                goto gm_lb38;
                                            }
gm_lb39:;
                                            {
                                                gm_l43 = ((uint8_t)1);
                                                goto gm_lb38;
                                            }
gm_lb40:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 173, 20);
                                        }
gm_lb38:;
                                        uint8_t t272 = gm_l43;
                                        if (t272) {
                                            void * t273 = F.l0;
                                            void * t274 = gm_l24;
                                            gm_b0_1 t275 = gm_f_fin_float_9k2be5v((void *)NULL, cx, NULL, t273, t274, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l23)), ((uint8_t)0));
                                            if (gm_unw.kind) {
                                                goto gm_ll257;
                                            }
                                        }
                                        void * t276 = (void *)((uint8_t *)&F.l19);
                                        void * t277 = gm_l24;
                                        gm_tftb1odc739t5 t278 = gm_f_push_d943vgp_2(t276, &(*(gm_tcgrjq9toscke *)((uint8_t *)t277)));
                                        gm_l45 = t278;
                                        uint8_t t279 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                        if (!t279) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcseljft758gtd, 174, 17);
                                        goto gm_lb36;
                                    }
gm_lb37:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 171, 13);
                                }
gm_lb36:;
                            }
gm_lb35:;
                            uint64_t t280 = gm_l22;
                            uint64_t r281; bool o282 = __builtin_add_overflow(t280, ((uint64_t)1ULL), &r281);
                            uint64_t t283 = r281;
                            gm_l22 = t283;
                        }
                    }
                    goto gm_ls284;
gm_ll257:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb33; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb35; }
                    }
                    goto gm_lad;
gm_ls284:;
gm_lb33:;
                    void * t285 = gm_l1;
                    gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                    gm_t2b6tm8c0vgo1 t287;
                    memset(&t287, 0, sizeof t287);
                    GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t287 + 8), t286);
                    GM_ST(gm_t2b6tm8c0vgo1, (((uint8_t *)t285) + 8), t287);
                    goto gm_lb16;
                }
gm_lb32:;
            }
            {
                {
                    uint8_t t288 = GM_LD(uint8_t, ((((uint8_t *)t123) + 8) + 0));
                    uint8_t t289 = (uint8_t)(t288 == ((uint8_t)32ULL));
                    if (!t289) {
                        goto gm_lb41;
                    }
                    gm_tmn7n0n9raou5 t290 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t123) + 8) + 8));
                    gm_l25 = t290;
                    void * t291 = GM_LD(void *, (((((uint8_t *)t123) + 8) + 8) + 8));
                    gm_l26 = t291;
                    gm_tmn7n0n9raou5 t292 = gm_l25;
                    uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&t292) + 0));
                    uint8_t t294 = (uint8_t)((t293 == ((uint8_t)12ULL)));
                    gm_tmn7n0n9raou5 t295;
                    GM_ST(uint8_t, (uint8_t *)&t295 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t295 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t295 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t295 + 0), ((uint8_t)12ULL));
                    if (t294) {
                        void * t296 = gm_l1;
                        gm_t88rcucg1b7uk t297 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t296));
                        gm_l27 = t297;
                        void * t298 = gm_l26;
                        gm_t88rcucg1b7uk t299 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t298));
                        gm_t88rcucg1b7uk t300 = gm_l27;
                        uint8_t t301 = (gm_eq9m6mvtmgde(((uint8_t *)&t299), ((uint8_t *)&t300)));
                        if (t301) {
                            void * t302 = gm_l1;
                            void * t303 = gm_l26;
                            memmove(((uint8_t *)t302), ((uint8_t *)t303), 88);
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        {
                            {
                                void * t304 = F.l0;
                                void * t305 = gm_l26;
                                gm_t88rcucg1b7uk t306 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t305));
                                gm_t88rcucg1b7uk t307 = gm_l27;
                                gm_f_steps_for_768gb3b((void *)&gm_l46, (void *)NULL, cx, NULL, t304, t306, t307, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t308 = (uint8_t)((((uint8_t *)&gm_l46))[16] != 0);
                                if (!t308) {
                                    goto gm_lb43;
                                }
                                gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l46));
                                gm_l28 = t309;
                                void * t310 = gm_l1;
                                void * t311 = gm_l26;
                                gm_b16_8 t312 = gm_l28;
                                gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&t312));
                                void * t314 = gm_f_apply_steps_fd8cr97((void *)NULL, cx, NULL, t311, t313);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                memmove(((uint8_t *)t310), ((uint8_t *)t314), 88);
                                F.ret = GM_ZERO(gm_b0_1);
                                goto gm_fret;
                            }
gm_lb43:;
                        }
gm_lb42:;
                        void * t315 = gm_l26;
                        gm_tl874t2atnlh3 t316 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t315) + 72));
                        void * t317 = F.l0;
                        gm_b16_8 t318 = GM_LD(gm_b16_8, (((uint8_t *)t317) + 360));
                        uint8_t t319 = 0;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t318) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t318))) + i * 12, ((uint8_t *)&t316))) { t319 = 1; break; }
                        uint8_t t320 = (uint8_t)(!t319);
                        if (t320) {
                            void * t321 = F.l0;
                            void * t322 = gm_l26;
                            gm_tl874t2atnlh3 t323 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t322) + 72));
                            void * t324 = F.l0;
                            gm_t88rcucg1b7uk t325 = gm_l27;
                            gm_b16_8 t326 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t324, t325);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t327 = F.l0;
                            void * t328 = gm_l26;
                            gm_t88rcucg1b7uk t329 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t328));
                            gm_b16_8 t330 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t327, t329);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t331;
                            GM_ST(gm_b16_8, ((uint8_t *)&t331 + 0), t326);
                            GM_ST(gm_b16_8, ((uint8_t *)&t331 + 16), t330);
                            gm_f_format_ns6dben((void *)&gm_l47, (void *)NULL, cx, NULL, &t331);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t332 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                            if (!t332) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srcseljft758gtd, 189, 38);
                            gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
                            gm_b0_1 t335 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t321, t323, t334);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t336 = gm_l1;
                        void * t337 = F.l0;
                        void * t338 = GM_LD(void *, ((uint8_t *)t337));
                        void * t339 = gm_l26;
                        gm_tl874t2atnlh3 t340 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t339) + 72));
                        void * t341 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t338, t340);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        memmove(((uint8_t *)t336), ((uint8_t *)t341), 88);
                    }
                    goto gm_lb16;
                }
gm_lb41:;
            }
            {
                {
                    goto gm_lb16;
                }
gm_lb44:;
                gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 138, 5);
            }
        }
gm_lb16:;
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
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fin_float_9k2be5v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3) {
    struct gm_fr_fin_float_9k2be5v F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l3; /* from_int */
    uint16_t gm_l4; /* bits */
    double gm_l5; /* x */
    GM_STACK_CHECK(&F, 795u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, ((uint8_t *)t6));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            void * t9 = gm_l1;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
            gm_f_kind_jcnt4hm((void *)&gm_l6, t8, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)3ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint16_t t13 = GM_LD(uint16_t, (((uint8_t *)&gm_l6) + 8));
            gm_l4 = t13;
            uint16_t t14 = gm_l4;
            double t15 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)), t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t15;
            double t16 = gm_l5;
            double t17 = gm_l5;
            double t18 = (t16 - t17);
            uint8_t t19 = (!(t18 == ((double)0e0)));
            if (t19) {
                void * t20 = gm_l0;
                void * t21 = gm_l1;
                gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
                void * t23 = gm_l0;
                void * t24 = gm_l1;
                gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t24));
                gm_b16_8 t26 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t23, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t27;
                GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
                gm_f_format_sl0efd0((void *)&gm_l7, (void *)NULL, cx, NULL, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcseljft758gtd, 226, 26);
                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                gm_b0_1 t31 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t20, t22, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t32 = gm_l1;
                gm_t2b6tm8c0vgo1 t33;
                memset(&t33, 0, sizeof t33);
                GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t33 + 8), ((double)0e0));
                GM_ST(gm_t2b6tm8c0vgo1, (((uint8_t *)t32) + 8), t33);
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    uint8_t t34 = gm_l3;
                    if (!t34) {
                        goto gm_lb4;
                    }
                    double t35 = gm_l5;
                    gm_tngtuiuskn938 t36;
                    gm_f_from_f64_1parp9j((void *)&t36, (void *)NULL, cx, NULL, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t37 = gm_f_eq_ceufbo4(&t36, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
                    uint8_t t38 = (uint8_t)!t37;
                    if (!t38) {
                        goto gm_lb4;
                    }
                    void * t39 = gm_l0;
                    void * t40 = gm_l1;
                    gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                    gm_b16_8 t42 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t43 = gm_l0;
                    void * t44 = gm_l1;
                    gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t44));
                    gm_b16_8 t46 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t43, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t47;
                    GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t42);
                    GM_ST(gm_b16_8, ((uint8_t *)&t47 + 16), t46);
                    gm_f_format_du48r1i((void *)&gm_l8, (void *)NULL, cx, NULL, &t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcseljft758gtd, 231, 26);
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                    gm_b0_1 t51 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t39, t41, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            void * t52 = gm_l1;
            double t53 = gm_l5;
            gm_t2b6tm8c0vgo1 t54;
            memset(&t54, 0, sizeof t54);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
            GM_ST(double, ((uint8_t *)&t54 + 8), t53);
            GM_ST(gm_t2b6tm8c0vgo1, (((uint8_t *)t52) + 8), t54);
            goto gm_lb1;
        }
gm_lb2:;
        void * t55 = gm_l1;
        gm_t2b6tm8c0vgo1 t56;
        memset(&t56, 0, sizeof t56);
        GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t56 + 8), ((double)0e0));
        GM_ST(gm_t2b6tm8c0vgo1, (((uint8_t *)t55) + 8), t56);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_pat_92m9fnd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fin_pat_92m9fnd F;
    void * gm_l1; /* p */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 663u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l8;
    void * t12 = gm_l1;
    void * t13 = F.l0;
    void * t14 = (void *)(((uint8_t *)t13) + 96);
    void * t15 = F.l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = gm_l1;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
    gm_t88rcucg1b7uk t19 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t14, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t12), t19);
    void * t20 = gm_l1;
    {
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)3ULL));
            if (!t22) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t20) + 8) + 8), 48);
            memmove(((uint8_t *)&F.l3), (((((uint8_t *)t20) + 8) + 8) + 48), 48);
            gm_tbci5ospk5nnn t23 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
            gm_tbci5ospk5nnn t24 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3));
            gm_thu7n15ilds8r t25;
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t25 + 0), t23);
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t25 + 48), t24);
            F.l4 = t25;
            void * t26 = (void *)((uint8_t *)&F.l4);
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = (uintptr_t)((uint64_t)2ULL);
            if (t27 > t28 || t28 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t27, t28, ((uint64_t)2ULL), gm_srcseljft758gtd, 209, 30);
            uint64_t t29 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t26 + (uintptr_t)((uint64_t)0ULL) * 48)));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29));
            gm_l5 = t30;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t32 = gm_l6;
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t34 = (t32 < t33);
                    if (!t34) {
                        goto gm_lb3;
                    }
                    {
                        void * t35 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t37 = gm_l6;
                        {
                            {
                                memmove(((uint8_t *)&F.l7), (((uint8_t *)t35) + (uintptr_t)t37 * 48), 48);
                                {
                                    {
                                        void * t38 = F.l0;
                                        void * t39 = GM_LD(void *, ((uint8_t *)t38));
                                        void * t40 = (void *)(((uint8_t *)t39) + 8);
                                        void * t41 = gm_l1;
                                        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t41));
                                        uint8_t t43 = gm_f_is_integer_8d4f9ek(t40, GM_CONV(gm_csguankko4lmhd, t42));
                                        if (!t43) {
                                            goto gm_lb9;
                                        }
                                        void * t44 = F.l0;
                                        void * t45 = GM_LD(void *, ((uint8_t *)t44));
                                        void * t46 = gm_l1;
                                        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t46));
                                        uint8_t t48 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t45, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), t47);
                                        if (gm_unw.kind) {
                                            goto gm_ll31;
                                        }
                                        uint8_t t49 = (uint8_t)!t48;
                                        if (!t49) {
                                            goto gm_lb9;
                                        }
                                        void * t50 = F.l0;
                                        void * t51 = gm_l1;
                                        gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 120));
                                        gm_b16_8 t53 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                                        if (gm_unw.kind) {
                                            goto gm_ll31;
                                        }
                                        void * t54 = F.l0;
                                        void * t55 = gm_l1;
                                        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t55));
                                        gm_b16_8 t57 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t54, t56);
                                        if (gm_unw.kind) {
                                            goto gm_ll31;
                                        }
                                        gm_t6j6smj7qoctu t58;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t53);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 16), t57);
                                        gm_f_format_qm09p7m((void *)&gm_l8, (void *)NULL, cx, NULL, &t58);
                                        if (gm_unw.kind) {
                                            goto gm_ll31;
                                        }
                                        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcseljft758gtd, 211, 34);
                                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                                        gm_b0_1 t62 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t50, t52, t61);
                                        if (gm_unw.kind) {
                                            goto gm_ll31;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 209, 30);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t63 = gm_l6;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                    uint64_t t66 = r64;
                    gm_l6 = t66;
                }
            }
            goto gm_ls67;
gm_ll31:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb3; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb5; }
            }
            goto gm_lad;
gm_ls67:;
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            void * t68 = gm_l1;
            gm_closure t69 = ((gm_closure){(void *)gm_f_fn_513k31a, (void *)&F.h, cx});
            gm_f_for_each_subpat_arlrtrm(t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 207, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_finalize_k1g9q48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_finalize_k1g9q48 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_thsh8t5u55gps gm_l5; /* p */
    gm_t88rcucg1b7uk gm_l6; /* src */
    gm_t88rcucg1b7uk gm_l7; /* core */
    gm_b16_8 gm_l8; /* kind */
    gm_b16_8 gm_l9; /* hint */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* what */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 1459u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l15;
    gm_t53at80pfdus3 gm_l16;
    uint8_t gm_l17;
    gm_t9qkh0rsooc8d gm_l18;
    uint8_t gm_l19;
    gm_t9qkh0rsooc8d gm_l20;
    gm_b16_8 gm_l21;
    gm_taari1s0m51a7 gm_l22;
    uint8_t gm_l23;
    gm_t53at80pfdus3 gm_l24;
    gm_b16_8 gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_b16_8 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tftb1odc739t5 gm_l29;
    void * t30 = gm_l0;
    void * t31 = GM_LD(void *, ((uint8_t *)t30));
    void * t32 = (void *)(((uint8_t *)t31) + 8);
    gm_l2 = t32;
    void * t33 = gm_l0;
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 280));
    gm_l3 = t34;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t35 = gm_l4;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t37 = (t35 < t36);
            if (!t37) {
                goto gm_lb1;
            }
            {
                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t40 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t38) + (uintptr_t)t40 * 20), 20);
                        void * t41 = gm_l0;
                        void * t42 = (void *)(((uint8_t *)t41) + 96);
                        void * t43 = gm_l0;
                        void * t44 = GM_LD(void *, ((uint8_t *)t43));
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
                        gm_t88rcucg1b7uk t46 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t42, t44, t45);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l6 = t46;
                        void * t47 = gm_l2;
                        gm_t88rcucg1b7uk t48 = gm_l6;
                        gm_f_kind_jcnt4hm((void *)&gm_l16, t47, GM_CONV(gm_csguankko4lmhd, t48));
                        {
                            {
                                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                                uint8_t t50 = (uint8_t)(t49 == ((uint8_t)23ULL));
                                if (!t50) {
                                    goto gm_lb7;
                                }
                                gm_l15 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l15 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 11, 12);
                        }
gm_lb6:;
                        uint8_t t51 = gm_l15;
                        if (t51) {
                            goto gm_lb3;
                        }
                        void * t52 = gm_l2;
                        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                        gm_t88rcucg1b7uk t54 = GM_CONV(gm_t88rcucg1b7uk, gm_f_literal_core_ii46ie6(t52, GM_CONV(gm_csguankko4lmhd, t53)));
                        gm_l7 = t54;
                        {
                            {
                                void * t55 = gm_l0;
                                void * t56 = (void *)(((uint8_t *)t55) + 96);
                                void * t57 = gm_l2;
                                gm_t88rcucg1b7uk t58 = gm_l6;
                                gm_t88rcucg1b7uk t59 = gm_l7;
                                gm_t9qkh0rsooc8d t60 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t56, t57, t58, t59);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l18 = t60;
                                {
                                    {
                                        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                        if (t61) {
                                            goto gm_lb12;
                                        }
                                        gm_l17 = ((uint8_t)1);
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    {
                                        gm_l17 = ((uint8_t)0);
                                        goto gm_lb11;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 13, 12);
                                }
gm_lb11:;
                                uint8_t t62 = gm_l17;
                                if (!t62) {
                                    goto gm_lb10;
                                }
                                void * t63 = gm_l0;
                                void * t64 = (void *)(((uint8_t *)t63) + 96);
                                void * t65 = gm_l2;
                                gm_t88rcucg1b7uk t66 = gm_l6;
                                gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                                gm_t9qkh0rsooc8d t68 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t64, t65, t66, t67);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l20 = t68;
                                {
                                    {
                                        uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                        if (t69) {
                                            goto gm_lb15;
                                        }
                                        gm_l19 = ((uint8_t)1);
                                        goto gm_lb14;
                                    }
gm_lb15:;
                                    {
                                        gm_l19 = ((uint8_t)0);
                                        goto gm_lb14;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 13, 53);
                                }
gm_lb14:;
                                uint8_t t70 = gm_l19;
                                if (!t70) {
                                    goto gm_lb10;
                                }
                                void * t71 = gm_l0;
                                void * t72 = (void *)(((uint8_t *)t71) + 96);
                                void * t73 = gm_l2;
                                gm_t88rcucg1b7uk t74 = gm_l6;
                                gm_taari1s0m51a7 t75 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t72, t73, t74);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l22 = t75;
                                {
                                    {
                                        uint8_t t76 = (uint8_t)((((uint8_t *)&gm_l22))[1] != 0);
                                        if (!t76) {
                                            goto gm_lb18;
                                        }
                                        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                                        uint8_t t78 = (uint8_t)(t77 == ((uint8_t)1ULL));
                                        if (!t78) {
                                            goto gm_lb18;
                                        }
                                        gm_b16_8 t79;
                                        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sq89uknu8mulu));
                                        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(7));
                                        gm_l21 = t79;
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                    {
                                        uint8_t t80 = (uint8_t)((((uint8_t *)&gm_l22))[1] != 0);
                                        if (!t80) {
                                            goto gm_lb19;
                                        }
                                        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                                        uint8_t t82 = (uint8_t)(t81 == ((uint8_t)2ULL));
                                        if (!t82) {
                                            goto gm_lb19;
                                        }
                                        gm_b16_8 t83;
                                        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sf44a6nc03ff5));
                                        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(9));
                                        gm_l21 = t83;
                                        goto gm_lb17;
                                    }
gm_lb19:;
                                    {
                                        gm_b16_8 t84;
                                        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_siflgunfq3r7b));
                                        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(10));
                                        gm_l21 = t84;
                                        goto gm_lb17;
                                    }
gm_lb20:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 14, 21);
                                }
gm_lb17:;
                                gm_b16_8 t85 = gm_l21;
                                gm_l8 = t85;
                                gm_b16_8 t86;
                                GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(0));
                                gm_l9 = t86;
                                void * t87 = gm_l2;
                                gm_t88rcucg1b7uk t88 = gm_l7;
                                gm_f_kind_jcnt4hm((void *)&gm_l24, t87, GM_CONV(gm_csguankko4lmhd, t88));
                                {
                                    {
                                        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                        uint8_t t90 = (uint8_t)(t89 == ((uint8_t)19ULL));
                                        if (!t90) {
                                            goto gm_lb22;
                                        }
                                        gm_l23 = ((uint8_t)1);
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                    {
                                        gm_l23 = ((uint8_t)0);
                                        goto gm_lb21;
                                    }
gm_lb23:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 20, 16);
                                }
gm_lb21:;
                                uint8_t t91 = gm_l23;
                                if (t91) {
                                    gm_b16_8 t92 = gm_l8;
                                    gm_b16_8 t93;
                                    GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sq89uknu8mulu));
                                    GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(7));
                                    uint8_t t94 = (gm_eq0iqiubfm7e(((uint8_t *)&t92), ((uint8_t *)&t93)));
                                    if (t94) {
                                        gm_b16_8 t95;
                                        GM_ST(void *, (uint8_t *)&t95, (void *)(gm_s7s25m3nlrif2));
                                        GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(5));
                                        gm_l25 = t95;
                                    } else {
                                        gm_b16_8 t96;
                                        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_snoj0t5lf2lht));
                                        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(3));
                                        gm_l25 = t96;
                                    }
                                    gm_b16_8 t97 = gm_l25;
                                    gm_l10 = t97;
                                    void * t98 = gm_l0;
                                    gm_t88rcucg1b7uk t99 = gm_l7;
                                    gm_b16_8 t100 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t98, t99);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t101 = gm_l10;
                                    void * t102 = gm_l0;
                                    gm_t88rcucg1b7uk t103 = gm_l7;
                                    gm_b16_8 t104 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t102, t103);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tfeab279c16rr t105;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t100);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t105 + 16), t101);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t105 + 32), t104);
                                    gm_f_format_o9tvoie((void *)&gm_l26, (void *)NULL, cx, NULL, &t105);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                    if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcseljft758gtd, 22, 24);
                                    gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                    gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                                    gm_l9 = t108;
                                }
                                gm_b16_8 t109 = gm_l8;
                                gm_b16_8 t110;
                                GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sf44a6nc03ff5));
                                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(9));
                                uint8_t t111 = (gm_eq0iqiubfm7e(((uint8_t *)&t109), ((uint8_t *)&t110)));
                                if (t111) {
                                    gm_b16_8 t112;
                                    GM_ST(void *, (uint8_t *)&t112, (void *)(gm_svklio0qebpog));
                                    GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(8));
                                    gm_l27 = t112;
                                } else {
                                    gm_b16_8 t113;
                                    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_s4lo69slu4igt));
                                    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(7));
                                    gm_l27 = t113;
                                }
                                gm_b16_8 t114 = gm_l27;
                                gm_l11 = t114;
                                void * t115 = gm_l0;
                                gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 8));
                                void * t117 = gm_l0;
                                gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                                gm_b16_8 t119 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t117, t118);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t120 = gm_l8;
                                gm_b16_8 t121 = gm_l11;
                                gm_b16_8 t122 = gm_l9;
                                gm_t0uuhaljsf410 t123;
                                GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t119);
                                GM_ST(gm_b16_8, ((uint8_t *)&t123 + 16), t120);
                                GM_ST(gm_b16_8, ((uint8_t *)&t123 + 32), t121);
                                GM_ST(gm_b16_8, ((uint8_t *)&t123 + 48), t122);
                                gm_f_format_83ij19e((void *)&gm_l28, (void *)NULL, cx, NULL, &t123);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcseljft758gtd, 25, 30);
                                gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                                gm_b0_1 t127 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t115, t116, t126);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t128 = gm_l0;
                                void * t129 = (void *)(((uint8_t *)t128) + 360);
                                gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 8));
                                gm_tftb1odc739t5 t131 = gm_f_push_0mk7udh(t129, GM_CONV(gm_cseahvg8v7p0vs, t130));
                                gm_l29 = t131;
                                uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcseljft758gtd, 26, 13);
                                goto gm_lb9;
                            }
gm_lb10:;
                        }
gm_lb9:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 9, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t133 = gm_l4;
            uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
            uint64_t t136 = r134;
            gm_l4 = t136;
        }
    }
gm_lb1:;
    void * t137 = gm_l0;
    void * t138 = (void *)(((uint8_t *)t137) + 280);
    gm_f_clear_p214f4b(t138);
    void * t139 = gm_l0;
    void * t140 = (void *)(((uint8_t *)t139) + 96);
    void * t141 = gm_l2;
    gm_b0_1 t142 = gm_f_apply_fallbacks_gcbql0t((void *)NULL, cx, NULL, t140, t141);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t143 = gm_l0;
    gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)t143) + 320));
    gm_l12 = t144;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t145 = gm_l13;
            uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t147 = (t145 < t146);
            if (!t147) {
                goto gm_lb24;
            }
            {
                void * t148 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t150 = gm_l13;
                {
                    {
                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t148) + (uintptr_t)t150 * 48), 48);
                        void * t151 = gm_l0;
                        gm_b0_1 t152 = gm_f_check_obligation_ss66lhk((void *)NULL, cx, NULL, t151, &(*(gm_tpm220j5qhjke *)((uint8_t *)&F.l14)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb27;
                    }
gm_lb28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 31, 5);
                }
gm_lb27:;
            }
gm_lb26:;
            uint64_t t153 = gm_l13;
            uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
            uint64_t t156 = r154;
            gm_l13 = t156;
        }
    }
gm_lb24:;
    void * t157 = gm_l0;
    void * t158 = (void *)(((uint8_t *)t157) + 320);
    gm_f_clear_a02qt6l(t158);
    void * t159 = gm_l0;
    void * t160 = gm_l1;
    gm_b0_1 t161 = gm_f_fin_expr_rk98p2r((void *)NULL, cx, NULL, t159, t160);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2v5b782(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_513k31a(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ud6f4hb(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tf38rotgtqq79 gm_f_int_range_jdeefnh(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_range_jdeefnh F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* s */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l4;
    void * t7 = gm_l0;
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 8));
            gm_l2 = t11;
            uint16_t t12 = GM_LD(uint16_t, ((((uint8_t *)&gm_l4) + 8) + 2));
            gm_l3 = t12;
            uint8_t t13 = gm_l2;
            uint16_t t14 = gm_l3;
            uint32_t t15 = (uint32_t)t14;
            uint16_t t16 = gm_l3;
            uint32_t t17 = (uint32_t)t16;
            gm_tba3spc7ecv13 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), t13);
            GM_ST(uint32_t, ((uint8_t *)&t18 + 4), t15);
            GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
            gm_tf38rotgtqq79 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 14, 0);
            GM_ST(gm_tba3spc7ecv13, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[12] = 1;
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_tba3spc7ecv13 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t22 + 4), ((uint32_t)32ULL));
            GM_ST(uint32_t, ((uint8_t *)&t22 + 8), ((uint32_t)64ULL));
            gm_tf38rotgtqq79 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 14, 0);
            GM_ST(gm_tba3spc7ecv13, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[12] = 1;
            F.ret = t23;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)2ULL));
            if (!t25) {
                goto gm_lb4;
            }
            gm_tba3spc7ecv13 t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)1));
            GM_ST(uint32_t, ((uint8_t *)&t26 + 4), ((uint32_t)32ULL));
            GM_ST(uint32_t, ((uint8_t *)&t26 + 8), ((uint32_t)64ULL));
            gm_tf38rotgtqq79 t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 14, 0);
            GM_ST(gm_tba3spc7ecv13, ((uint8_t *)&t27), t26);
            ((uint8_t *)&t27)[12] = 1;
            F.ret = t27;
            goto gm_fret;
        }
gm_lb4:;
        {
            gm_tf38rotgtqq79 t28;
            memset(&t28, 0, sizeof t28);
            F.ret = t28;
            goto gm_fret;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 278, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_literal_core_ii46ie6(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_literal_core_ii46ie6 F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* target */
    gm_t88rcucg1b7uk gm_l2; /* core */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l5;
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_l2 = t9;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t10 = gm_l0;
                gm_t88rcucg1b7uk t11 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l5, t10, GM_CONV(gm_csguankko4lmhd, t11));
                {
                    {
                        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t13 = (uint8_t)(t12 == ((uint8_t)13ULL));
                        if (!t13) {
                            goto gm_lb5;
                        }
                        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                        gm_l3 = t14;
                        gm_t88rcucg1b7uk t15 = gm_l3;
                        gm_l2 = t15;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t17 = (uint8_t)(t16 == ((uint8_t)14ULL));
                        if (!t17) {
                            goto gm_lb6;
                        }
                        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                        gm_l4 = t18;
                        gm_t88rcucg1b7uk t19 = gm_l4;
                        gm_l2 = t19;
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        gm_t88rcucg1b7uk t20 = gm_l2;
                        F.ret = t20;
                        goto gm_fret;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 246, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lossless_a54o69s F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* from */
    gm_t88rcucg1b7uk gm_l2; /* to */
    void * gm_l3; /* tys */
    uint16_t gm_l4; /* b */
    gm_tba3spc7ecv13 gm_l5; /* r */
    uint32_t gm_l6; /* needed */
    uint16_t gm_l7; /* a */
    uint16_t gm_l8; /* b */
    gm_tba3spc7ecv13 gm_l9; /* f */
    gm_tba3spc7ecv13 gm_l10; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 811u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_t53at80pfdus3 gm_l11;
    gm_tf38rotgtqq79 gm_l12;
    gm_tba3spc7ecv13 gm_l13;
    uint32_t gm_l14;
    uint32_t gm_l15;
    uint32_t gm_l16;
    gm_t53at80pfdus3 gm_l17;
    gm_t53at80pfdus3 gm_l18;
    gm_tf38rotgtqq79 gm_l19;
    gm_tba3spc7ecv13 gm_l20;
    gm_tf38rotgtqq79 gm_l21;
    gm_tba3spc7ecv13 gm_l22;
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    gm_l3 = t11;
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_t88rcucg1b7uk t13 = gm_l2;
    uint8_t t14 = (gm_eq9m6mvtmgde(((uint8_t *)&t12), ((uint8_t *)&t13)));
    if (t14) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t15 = gm_l3;
    gm_t88rcucg1b7uk t16 = gm_l1;
    gm_t53at80pfdus3 t17;
    gm_f_kind_jcnt4hm((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
    uint8_t t19 = (uint8_t)((t18 == ((uint8_t)4ULL)));
    gm_t53at80pfdus3 t20;
    memset(&t20, 0, sizeof t20);
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)4ULL));
    if (t19) {
        void * t21 = gm_l3;
        gm_t88rcucg1b7uk t22 = gm_l2;
        uint8_t t23 = gm_f_is_integer_8d4f9ek(t21, GM_CONV(gm_csguankko4lmhd, t22));
        F.ret = t23;
        goto gm_fret;
    }
    {
        {
            void * t24 = gm_l3;
            gm_t88rcucg1b7uk t25 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l11, t24, GM_CONV(gm_csguankko4lmhd, t25));
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)3ULL));
            if (!t27) {
                goto gm_lb2;
            }
            uint16_t t28 = GM_LD(uint16_t, (((uint8_t *)&gm_l11) + 8));
            gm_l4 = t28;
            void * t29 = gm_l3;
            gm_t88rcucg1b7uk t30 = gm_l1;
            uint8_t t31 = gm_f_is_integer_8d4f9ek(t29, GM_CONV(gm_csguankko4lmhd, t30));
            if (!t31) {
                goto gm_lb2;
            }
            void * t32 = gm_l3;
            gm_t88rcucg1b7uk t33 = gm_l1;
            gm_tf38rotgtqq79 t34 = gm_f_int_range_jdeefnh(t32, GM_CONV(gm_csguankko4lmhd, t33));
            gm_l12 = t34;
            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l12))[12] != 0);
            if (t35) {
                gm_tba3spc7ecv13 t36 = GM_LD(gm_tba3spc7ecv13, ((uint8_t *)&gm_l12));
                gm_l13 = t36;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_tba3spc7ecv13 t37 = gm_l13;
            gm_l5 = t37;
            uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
            if (t38) {
                uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                uint32_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint32_t)1ULL), &r40);
                if (o41) gm_trap("integer overflow in `-`", gm_srcseljft758gtd, 262, 33);
                uint32_t t42 = r40;
                gm_l14 = t42;
            } else {
                uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                gm_l14 = t43;
            }
            uint32_t t44 = gm_l14;
            gm_l6 = t44;
            uint32_t t45 = gm_l6;
            uint16_t t46 = gm_l4;
            uint8_t t47 = ((t46 == ((uint16_t)16ULL)));
            if (t47) {
                gm_l15 = ((uint32_t)11ULL);
            } else {
                uint16_t t48 = gm_l4;
                uint8_t t49 = ((t48 == ((uint16_t)32ULL)));
                if (t49) {
                    gm_l16 = ((uint32_t)24ULL);
                } else {
                    gm_l16 = ((uint32_t)53ULL);
                }
                uint32_t t50 = gm_l16;
                gm_l15 = t50;
            }
            uint32_t t51 = gm_l15;
            uint8_t t52 = (t45 <= t51);
            F.ret = t52;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t53 = gm_l3;
            gm_t88rcucg1b7uk t54 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l17, t53, GM_CONV(gm_csguankko4lmhd, t54));
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)3ULL));
            if (!t56) {
                goto gm_lb4;
            }
            uint16_t t57 = GM_LD(uint16_t, (((uint8_t *)&gm_l17) + 8));
            gm_l7 = t57;
            void * t58 = gm_l3;
            gm_t88rcucg1b7uk t59 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l18, t58, GM_CONV(gm_csguankko4lmhd, t59));
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t61 = (uint8_t)(t60 == ((uint8_t)3ULL));
            if (!t61) {
                goto gm_lb4;
            }
            uint16_t t62 = GM_LD(uint16_t, (((uint8_t *)&gm_l18) + 8));
            gm_l8 = t62;
            uint16_t t63 = gm_l8;
            uint16_t t64 = gm_l7;
            uint8_t t65 = (t63 > t64);
            F.ret = t65;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            void * t66 = gm_l3;
            gm_t88rcucg1b7uk t67 = gm_l1;
            uint8_t t68 = gm_f_is_integer_8d4f9ek(t66, GM_CONV(gm_csguankko4lmhd, t67));
            if (!t68) {
                goto gm_lb6;
            }
            void * t69 = gm_l3;
            gm_t88rcucg1b7uk t70 = gm_l2;
            uint8_t t71 = gm_f_is_integer_8d4f9ek(t69, GM_CONV(gm_csguankko4lmhd, t70));
            if (!t71) {
                goto gm_lb6;
            }
            void * t72 = gm_l3;
            gm_t88rcucg1b7uk t73 = gm_l1;
            gm_tf38rotgtqq79 t74 = gm_f_int_range_jdeefnh(t72, GM_CONV(gm_csguankko4lmhd, t73));
            gm_l19 = t74;
            uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l19))[12] != 0);
            if (t75) {
                gm_tba3spc7ecv13 t76 = GM_LD(gm_tba3spc7ecv13, ((uint8_t *)&gm_l19));
                gm_l20 = t76;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_tba3spc7ecv13 t77 = gm_l20;
            gm_l9 = t77;
            void * t78 = gm_l3;
            gm_t88rcucg1b7uk t79 = gm_l2;
            gm_tf38rotgtqq79 t80 = gm_f_int_range_jdeefnh(t78, GM_CONV(gm_csguankko4lmhd, t79));
            gm_l21 = t80;
            uint8_t t81 = (uint8_t)((((uint8_t *)&gm_l21))[12] != 0);
            if (t81) {
                gm_tba3spc7ecv13 t82 = GM_LD(gm_tba3spc7ecv13, ((uint8_t *)&gm_l21));
                gm_l22 = t82;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_tba3spc7ecv13 t83 = gm_l22;
            gm_l10 = t83;
            uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
            uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
            uint8_t t86 = ((t84 == t85));
            if (t86) {
                uint32_t t87 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t89 = (t87 >= t88);
                F.ret = t89;
                goto gm_fret;
            }
            {
                {
                    uint8_t t90 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
                    uint8_t t91 = (uint8_t)!t90;
                    if (!t91) {
                        goto gm_lb8;
                    }
                    uint8_t t92 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                    if (!t92) {
                        goto gm_lb8;
                    }
                    uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                    uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t95 = (t93 > t94);
                    F.ret = t95;
                    goto gm_fret;
                }
gm_lb8:;
            }
gm_lb7:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb6:;
    }
gm_lb5:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_numeric_uv79l0u(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_numeric_uv79l0u F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_obligation_ss66lhk *)((&F.h)->up))->l2));
    gm_t88rcucg1b7uk t6 = gm_l0;
    uint8_t t7 = gm_f_is_integer_8d4f9ek(t5, GM_CONV(gm_csguankko4lmhd, t6));
    if (t7) {
        gm_l1 = ((uint8_t)1);
    } else {
        void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_obligation_ss66lhk *)((&F.h)->up))->l2));
        gm_t88rcucg1b7uk t9 = gm_l0;
        gm_f_kind_jcnt4hm((void *)&gm_l3, t8, GM_CONV(gm_csguankko4lmhd, t9));
        {
            {
                uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t11 = (uint8_t)(t10 == ((uint8_t)3ULL));
                if (!t11) {
                    goto gm_lb2;
                }
                gm_l2 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l2 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 67, 66);
        }
gm_lb1:;
        uint8_t t12 = gm_l2;
        gm_l1 = t12;
    }
    uint8_t t13 = gm_l1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_settle_h9ikvs5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_thsh8t5u55gps gm_l5; /* p */
    gm_t88rcucg1b7uk gm_l8; /* core */
    GM_STACK_CHECK(&F, 556u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l9;
    gm_t53at80pfdus3 gm_l10;
    uint8_t gm_l11;
    gm_t9qkh0rsooc8d gm_l12;
    void * t13 = gm_l0;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 280));
    gm_l3 = t14;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l4;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t21 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t19) + (uintptr_t)t21 * 20), 20);
                        void * t22 = gm_l0;
                        void * t23 = (void *)(((uint8_t *)t22) + 96);
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, ((uint8_t *)t24));
                        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
                        gm_t88rcucg1b7uk t27 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t23, t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        F.l6 = t27;
                        void * t28 = gm_l0;
                        void * t29 = GM_LD(void *, ((uint8_t *)t28));
                        void * t30 = (void *)(((uint8_t *)t29) + 8);
                        gm_t88rcucg1b7uk t31 = F.l6;
                        gm_f_kind_jcnt4hm((void *)&gm_l10, t30, GM_CONV(gm_csguankko4lmhd, t31));
                        {
                            {
                                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                uint8_t t33 = (uint8_t)(t32 == ((uint8_t)23ULL));
                                if (!t33) {
                                    goto gm_lb7;
                                }
                                gm_l9 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l9 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 40, 12);
                        }
gm_lb6:;
                        uint8_t t34 = gm_l9;
                        if (t34) {
                            goto gm_lb3;
                        }
                        F.l7 = ((uint8_t)0);
                        void * t35 = gm_l0;
                        void * t36 = GM_LD(void *, ((uint8_t *)t35));
                        void * t37 = gm_l0;
                        void * t38 = (void *)(((uint8_t *)t37) + 96);
                        void * t39 = gm_l0;
                        void * t40 = GM_LD(void *, ((uint8_t *)t39));
                        gm_t88rcucg1b7uk t41 = gm_l1;
                        gm_t88rcucg1b7uk t42 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t38, t40, t41);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_closure t43 = ((gm_closure){(void *)gm_f_fn_2v5b782, (void *)&F.h, cx});
                        gm_f_walk_v858co7(t36, GM_CONV(gm_csguankko4lmhd, t42), t43);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        uint8_t t44 = F.l7;
                        if (t44) {
                            void * t45 = gm_l0;
                            void * t46 = GM_LD(void *, ((uint8_t *)t45));
                            void * t47 = (void *)(((uint8_t *)t46) + 8);
                            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                            gm_t88rcucg1b7uk t49 = GM_CONV(gm_t88rcucg1b7uk, gm_f_literal_core_ii46ie6(t47, GM_CONV(gm_csguankko4lmhd, t48)));
                            gm_l8 = t49;
                            void * t50 = gm_l0;
                            void * t51 = (void *)(((uint8_t *)t50) + 96);
                            void * t52 = gm_l0;
                            void * t53 = GM_LD(void *, ((uint8_t *)t52));
                            void * t54 = (void *)(((uint8_t *)t53) + 8);
                            gm_t88rcucg1b7uk t55 = F.l6;
                            gm_t88rcucg1b7uk t56 = gm_l8;
                            gm_t9qkh0rsooc8d t57 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t51, t54, t55, t56);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            gm_l12 = t57;
                            {
                                {
                                    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                    if (t58) {
                                        goto gm_lb10;
                                    }
                                    gm_l11 = ((uint8_t)1);
                                    goto gm_lb9;
                                }
gm_lb10:;
                                {
                                    gm_l11 = ((uint8_t)0);
                                    goto gm_lb9;
                                }
gm_lb11:;
                                gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 47, 16);
                            }
gm_lb9:;
                            uint8_t t59 = gm_l11;
                            if (t59) {
                                void * t60 = gm_l0;
                                void * t61 = (void *)(((uint8_t *)t60) + 96);
                                void * t62 = gm_l0;
                                void * t63 = GM_LD(void *, ((uint8_t *)t62));
                                void * t64 = (void *)(((uint8_t *)t63) + 8);
                                gm_t88rcucg1b7uk t65 = F.l6;
                                gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                                gm_t9qkh0rsooc8d t67 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t61, t64, t65, t66);
                                if (gm_unw.kind) {
                                    goto gm_ll15;
                                }
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 38, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t68 = gm_l4;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l4 = t71;
        }
    }
    goto gm_ls72;
gm_ll15:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls72:;
gm_lb1:;
    void * t73 = gm_l0;
    void * t74 = (void *)(((uint8_t *)t73) + 96);
    void * t75 = gm_l0;
    void * t76 = GM_LD(void *, ((uint8_t *)t75));
    gm_t88rcucg1b7uk t77 = gm_l1;
    gm_tl874t2atnlh3 t78 = gm_l2;
    gm_t88rcucg1b7uk t79 = gm_f_settle_vars_vpp9ov6((void *)NULL, cx, NULL, t74, t76, t77, t78);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0);
