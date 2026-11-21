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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { void * f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; void * f2; } gm_tf361jk9mn3il;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_b16_8 f0; gm_t5hdnnftkup6m f1; uint32_t f2; uint32_t f3; } gm_td1bmbv6cf6nk;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_t5hdnnftkup6m f1; gm_b16_8 f2; uint8_t f3; uint8_t pe[7]; } gm_tdi3m8srnr9m6;
_Static_assert(sizeof(gm_tf361jk9mn3il) == 24 && _Alignof(gm_tf361jk9mn3il) == 8 && offsetof(gm_tf361jk9mn3il, f0) == 0 && offsetof(gm_tf361jk9mn3il, f1) == 8 && offsetof(gm_tf361jk9mn3il, f2) == 16, "layout of `Ctx`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
typedef struct { gm_t5hdnnftkup6m f0; gm_t5hdnnftkup6m f1; } gm_tmo31gpro8pgo;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t8vuock3jnv59 v1; } f1; } gm_t9l835tjp727s;
_Static_assert(sizeof(gm_td1bmbv6cf6nk) == 48 && _Alignof(gm_td1bmbv6cf6nk) == 8 && offsetof(gm_td1bmbv6cf6nk, f0) == 0 && offsetof(gm_td1bmbv6cf6nk, f1) == 16 && offsetof(gm_td1bmbv6cf6nk, f2) == 40 && offsetof(gm_td1bmbv6cf6nk, f3) == 44, "layout of `LocalName`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tdi3m8srnr9m6) == 64 && _Alignof(gm_tdi3m8srnr9m6) == 8 && offsetof(gm_tdi3m8srnr9m6, f0) == 0 && offsetof(gm_tdi3m8srnr9m6, f1) == 16 && offsetof(gm_tdi3m8srnr9m6, f2) == 40 && offsetof(gm_tdi3m8srnr9m6, f3) == 56, "layout of `Target`");
typedef struct { gm_tl5cuhjfnc4cr f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tfgiovingnnea;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tdi3m8srnr9m6 v1; } f1; } gm_tmkrnkep1qbgl;
_Static_assert(sizeof(gm_tmo31gpro8pgo) == 48 && _Alignof(gm_tmo31gpro8pgo) == 8 && offsetof(gm_tmo31gpro8pgo, f0) == 0 && offsetof(gm_tmo31gpro8pgo, f1) == 24, "layout of `RefPair`");
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_t9l835tjp727s) == 32 && _Alignof(gm_t9l835tjp727s) == 8 && offsetof(gm_t9l835tjp727s, f0) == 0 && offsetof(gm_t9l835tjp727s, f1) == 8, "layout of `string!Value`");
_Static_assert(sizeof(gm_tfgiovingnnea) == 88 && _Alignof(gm_tfgiovingnnea) == 8 && offsetof(gm_tfgiovingnnea, f0) == 0 && offsetof(gm_tfgiovingnnea, f1) == 64 && offsetof(gm_tfgiovingnnea, f2) == 76 && offsetof(gm_tfgiovingnnea, f3) == 80, "layout of `LocalScope`");
_Static_assert(sizeof(gm_tmkrnkep1qbgl) == 72 && _Alignof(gm_tmkrnkep1qbgl) == 8 && offsetof(gm_tmkrnkep1qbgl, f0) == 0 && offsetof(gm_tmkrnkep1qbgl, f1) == 8, "layout of `string!Target`");
struct gm_fr_array_k32ana3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_check_clashes_cqluvhl {
    gm_frame h;
    gm_td1bmbv6cf6nk l14; /* me */
    gm_td1bmbv6cf6nk l17; /* l */
    gm_td1bmbv6cf6nk l26; /* l */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_editable_56hen53 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* roots */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_new_name_31k8n9d {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declaration_at_lquhblp {
    gm_frame h;
    gm_tmkrnkep1qbgl ret;
};
struct gm_fr_empty_slice_1du9kj1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_esq6vjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kb2sr0i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oab689p_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r0cer6a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_qmfcuqk_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_m559mi8 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* modules */
    gm_trqfvmjtt4sij l3; /* roots */
    gm_trqfvmjtt4sij l11; /* uses */
    gm_tmo31gpro8pgo l17; /* r */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_fn_o57h82n {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_2g3rqdg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_45lt1qt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7jgfnob {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ce9c14n {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cebtc0e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_egbi75q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_guqevu2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h78t8d0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ics51qh {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o1952f2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s8ajp67 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t3f3kj5 {
    gm_frame h;
    gm_tjftu5su6fh8e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tjftu5su6fh8e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t6rdn7r {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_char_boundary_h62fhaa {
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
struct gm_fr_is_local_jek4n0p {
    gm_frame h;
    gm_tfgiovingnnea l4; /* s */
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
struct gm_fr_is_word_byte_7lklilo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_line_index_2necgtf {
    gm_frame h;
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_line_of_n80f3n1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_list_0202sdh_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0hnol3g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6u1pehv {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ctsmjqd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_t5p1bue_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_u1fokev {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_equal_h2ruinn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_loc_key_n6bogvi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_location_in_v59u1gt {
    gm_frame h;
    gm_tin97tt4ud0s1 l5; /* o */
    gm_t3ph3kbiqt1ah ret;
};
struct gm_fr_narrow_5epj0dh {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_obj_aoa7fl7 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_prepare_rename_i0odtgj {
    gm_frame h;
    gm_tdi3m8srnr9m6 l3; /* t */
    gm_tin97tt4ud0s1 l15; /* o */
    gm_t9l835tjp727s ret;
};
struct gm_fr_push_0pgacsj_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_1drnucl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_1prlsnl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_5gfgdpl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_drp4m1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_sjirrjh {
    gm_frame h;
    gm_tl7hpf5nlilqq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_references_all_9v25v88 {
    gm_frame h;
    gm_tdi3m8srnr9m6 l5; /* t */
    gm_td7pkamr64l0c l6; /* found */
    gm_trqfvmjtt4sij l7; /* locs */
    gm_trqfvmjtt4sij l11; /* out */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_regrow_0vnv2gf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4gke1e7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_a7ph4i8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j0st377 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_khtui0p_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rename_4pp7bs8 {
    gm_frame h;
    gm_tdi3m8srnr9m6 l5; /* t */
    gm_tin97tt4ud0s1 l7; /* o */
    gm_td7pkamr64l0c l8; /* found */
    gm_trqfvmjtt4sij l10; /* by_file */
    gm_trqfvmjtt4sij l11; /* texts */
    gm_trqfvmjtt4sij l12; /* locs */
    gm_tin97tt4ud0s1 l30; /* changes */
    gm_trqfvmjtt4sij l40; /* edits */
    gm_tin97tt4ud0s1 l44; /* e */
    gm_tin97tt4ud0s1 l45; /* o */
    gm_t9l835tjp727s ret;
};
struct gm_fr_sort_by_m9qke1u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_by_ucn5r3c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_text_7ru912k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_text_ls49dp6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_text_of_qs06tq9 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_truncate_pphi06n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_tgh982e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_uri_of_t6r79gp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_value_pkuqal3 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_check_clashes_cqluvhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_check_editable_56hen53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_check_new_name_31k8n9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_declaration_at_lquhblp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint64_t gm_f_dedup_5og6p3h(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_dedup_8ks2cpi(gm_b16_8 a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_definition_at_9jj9uuu(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1du9kj1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_esq6vjf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kb2sr0i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oab689p_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r0cer6a(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_qmfcuqk_3(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_facts_s8nso10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_m559mi8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_o57h82n(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_2g3rqdg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7jgfnob(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ce9c14n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cebtc0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dp28gpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_egbi75q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_guqevu2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h78t8d0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ics51qh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mkltt0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o1952f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_s2bvqvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s8ajp67(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_t3f3kj5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_t6rdn7r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_found_text_sajr2an(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_kfrlfsf(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_introsort_03dnmbj(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_1n5fb1t(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_local_jek4n0p(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_byte_7lklilo(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_line_index_2necgtf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_line_of_n80f3n1(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_line_starts_oja6lfl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0hnol3g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6u1pehv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ctsmjqd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t5p1bue_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_loc_equal_h2ruinn(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_key_n6bogvi(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_loc_less_gf4kba8(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_location_in_v59u1gt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_mentions_gartt1q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_module_dirs_72lhhi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_narrow_5epj0dh(gm_b16_8 a0, uint32_t a1, uint32_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_package_roots_uh3pgnt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_path_to_uri_as8bikc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_prepare_rename_i0odtgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1drnucl(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1prlsnl(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_5gfgdpl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_drp4m1b(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sjirrjh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_range_json_hlsolbo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_real_path_al8cv9r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_references_all_9v25v88(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_references_pd7nt1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0vnv2gf(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4gke1e7(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a7ph4i8(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j0st377(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_khtui0p_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_rename_4pp7bs8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sort_by_m9qke1u(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_ucn5r3c(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_store_q18c91o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_text_in_sgvn4eo(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_text_of_qs06tq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_truncate_pphi06n(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_tgh982e(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern const uint8_t gm_s0rsc8ficqfcm[78];
extern const uint8_t gm_s1h6moflekgjg[15];
extern const uint8_t gm_s1n4l05blor5r[40];
extern const uint8_t gm_s29avse796bo5[18];
extern const uint8_t gm_s2l500n2vri8f[77];
extern const uint8_t gm_s3kin7bltuqdc[35];
extern const uint8_t gm_s4ss30tejbl47[32];
extern const uint8_t gm_s54jmbv61vfjv[12];
extern const uint8_t gm_s6rivmf4qclvb[71];
extern const uint8_t gm_s71u85v3jrj86[63];
extern const uint8_t gm_s7ulscihp75vl[73];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_seaesh4v4toj4[76];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sirg36m1q4cjm[55];
extern const uint8_t gm_sirpcjgkmlehr[8];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sle6a93dn88l8[26];
extern const uint8_t gm_smloid035k91b[55];
extern const uint8_t gm_smr39cplkqb3n[51];
extern const uint8_t gm_sn68oj251cu21[38];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcb7e7uf3pvdko[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srcn13n69hd0cht[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ssvcneuafnfds[8];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const gm_tmpl gm_tpl17t0pmu1kh1u;
extern const gm_tmpl gm_tpl3skngiao4gkl;
extern const gm_tmpl gm_tpl4bpbmadpmu1i;
extern const gm_tmpl gm_tpl4slc3so8u8pa;
extern const gm_tmpl gm_tpl7b2kb2ainpff;
extern const gm_tmpl gm_tpl8gtkip9ins8o;
extern const gm_tmpl gm_tpld0t14m1fld0p;
extern const gm_tmpl gm_tplg3ifbt1a32e1;
extern const gm_tmpl gm_tpllfgb42aufjqd;
extern const gm_tmpl gm_tplng1u8s5kl5ir;
extern const gm_tmpl gm_tplntk567ck43t8;
extern const gm_tmpl gm_tplrfab517u4o97;
extern const gm_tmpl gm_tplsjtf9pg2ergu;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_array_k32ana3 F;
    gm_b16_8 gm_l0; /* items */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_0202sdh_4((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4 = gm_l0;
    gm_tftb1odc739t5 t5 = gm_f_extend_qmfcuqk_3(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgp1oqfehgsjb, 42, 5);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_t8vuock3jnv59 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)4ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
    memmove(gm_out, ((uint8_t *)&t8), 24);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_722uua4(t5, t6);
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1du9kj1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1du9kj1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_esq6vjf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_esq6vjf F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kb2sr0i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kb2sr0i F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oab689p_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oab689p_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r0cer6a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r0cer6a F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_qmfcuqk_3(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_qmfcuqk_3 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_ddqa9t6_4(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 24);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcv1nri2247d2r, 82, 75);
    void * t7 = (void *)(((uint8_t *)t3) + (uintptr_t)t6 * 80);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_o57h82n(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_o57h82n F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
    void * t4 = gm_l0;
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 4));
    gm_tcc1rq4n1avb2 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t5);
    void * t7 = gm_l1;
    uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)t7));
    void * t9 = gm_l1;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 4));
    gm_tcc1rq4n1avb2 t11;
    GM_ST(uint32_t, ((uint8_t *)&t11 + 0), t8);
    GM_ST(uint32_t, ((uint8_t *)&t11 + 4), t10);
    uint8_t t12 = (uint8_t)gm_lt8l669blqfv(((uint8_t *)&t6), ((uint8_t *)&t11));
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_2g3rqdg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_2g3rqdg F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s29avse796bo5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3skngiao4gkl), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_45lt1qt F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t5hdnnftkup6m gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukbpodfceoe1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
    gm_f_format_s2bvqvf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4bpbmadpmu1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7jgfnob(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7jgfnob F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sirg36m1q4cjm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(54));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_mkltt0a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4slc3so8u8pa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ce9c14n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ce9c14n F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6rivmf4qclvb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(70));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg3ifbt1a32e1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cebtc0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cebtc0e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7ulscihp75vl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(72));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplntk567ck43t8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_egbi75q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_egbi75q F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sle6a93dn88l8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrfab517u4o97), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_guqevu2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_guqevu2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1h6moflekgjg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplng1u8s5kl5ir), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h78t8d0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h78t8d0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smr39cplkqb3n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsjtf9pg2ergu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ics51qh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ics51qh F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0rsc8ficqfcm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(77));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl17t0pmu1kh1u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_o1952f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o1952f2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smloid035k91b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8gtkip9ins8o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s8ajp67(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s8ajp67 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3kin7bltuqdc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllfgb42aufjqd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t3f3kj5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t3f3kj5 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_seaesh4v4toj4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(75));
    gm_tjftu5su6fh8e t3 = GM_LD(gm_tjftu5su6fh8e, ((uint8_t *)a0));
    gm_f_format_dp28gpk((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7b2kb2ainpff), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_t6rdn7r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t6rdn7r F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2l500n2vri8f));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(76));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpld0t14m1fld0p), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_char_boundary_h62fhaa F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = (t2 >= t3);
    if (t4) {
        uint64_t t5 = gm_l1;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t7 = ((t5 == t6));
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t10 = gm_l1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t8) + (uintptr_t)t10 * 1));
    uint8_t t12 = (uint8_t)(t11 & ((uint8_t)192ULL));
    uint8_t t13 = (!(t12 == ((uint8_t)128ULL)));
    F.ret = t13;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_byte_7lklilo(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_word_byte_7lklilo F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)128ULL));
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_ident_cont_317fpdb(t4);
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
inline __attribute__((aligned(4))) void gm_f_line_index_2necgtf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_line_index_2necgtf F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph gm_l1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l0;
    gm_tnpifv20irnph t4 = gm_f_line_starts_oja6lfl((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t4;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    memmove(gm_out, ((uint8_t *)&t6), 32);
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_4 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi_4();
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
inline __attribute__((aligned(4))) void gm_f_list_0hnol3g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0hnol3g F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1du9kj1();
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
inline __attribute__((aligned(4))) void gm_f_list_6u1pehv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6u1pehv F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r0cer6a();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ab79bmt_2();
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
inline __attribute__((aligned(4))) void gm_f_list_ctsmjqd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ctsmjqd F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_kb2sr0i();
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
inline __attribute__((aligned(4))) void gm_f_list_t5p1bue_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t5p1bue_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_oab689p_2();
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
inline __attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_u1fokev F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_esq6vjf();
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
inline __attribute__((aligned(4))) uint8_t gm_f_loc_equal_h2ruinn(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_loc_equal_h2ruinn F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
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
    uint8_t t6 = (gm_eq0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5)));
    if (t6) {
        void * t7 = gm_l0;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)t7) + 16));
        void * t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 16));
        uint8_t t11 = ((t8 == t10));
        gm_l3 = t11;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t12 = gm_l3;
    if (t12) {
        void * t13 = gm_l0;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 20));
        void * t15 = gm_l1;
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 20));
        uint8_t t17 = ((t14 == t16));
        gm_l2 = t17;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t18 = gm_l2;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_key_n6bogvi(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_loc_key_n6bogvi F;
    gm_t5hdnnftkup6m gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tabb710d9mq2e gm_l1;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 16));
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 20));
    gm_t5hdnnftkup6m t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 16), t3);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 20), t4);
    gm_f_format_45lt1qt((void *)&gm_l1, (void *)NULL, cx, NULL, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcb7e7uf3pvdko, 58, 48);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
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
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_obj_aoa7fl7 F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_792g2go_2((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t3;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 0), t2);
    memmove(gm_out, ((uint8_t *)&t3), 40);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj_4 F;
    void * gm_l0; /* l */
    gm_t8vuock3jnv59 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi_4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ddqa9t6_4(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1drnucl(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1drnucl F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_kb2sr0i();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0vnv2gf(t8, ((uint64_t)1ULL), t10);
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
    void * t18 = gm_l1;
    GM_ST(void *, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1prlsnl(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1prlsnl F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_r0cer6a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4gke1e7(t8, ((uint64_t)1ULL), t10);
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
    gm_tcc1rq4n1avb2 t18 = gm_l1;
    GM_ST(gm_tcc1rq4n1avb2, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_5gfgdpl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_5gfgdpl F;
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
        gm_b16_8 t9 = gm_f_empty_slice_esq6vjf();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j0st377(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_drp4m1b(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_drp4m1b F;
    void * gm_l0; /* l */
    gm_t5hdnnftkup6m gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
        gm_b16_8 t9 = gm_f_empty_slice_1du9kj1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_a7ph4i8(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)&gm_l1), 24);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sjirrjh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_sjirrjh F;
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
        gm_b16_8 t9 = gm_f_empty_slice_oab689p_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_khtui0p_2(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0vnv2gf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0vnv2gf F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4gke1e7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4gke1e7 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a7ph4i8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_a7ph4i8 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6_4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j0st377(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j0st377 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_khtui0p_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_khtui0p_2 F;
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
inline __attribute__((aligned(4))) void gm_f_sort_by_m9qke1u(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_m9qke1u F;
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
    gm_f_introsort_1n5fb1t(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_ucn5r3c(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_ucn5r3c F;
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
    gm_f_introsort_03dnmbj(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_text_7ru912k F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    uint32_t t4 = gm_l1;
    void * t5 = gm_f_file_o15fqku(t3, t4);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 16));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_f_strip_prefix_vl3t0h4((void *)&gm_l3, (void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
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
        gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
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
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_ls49dp6 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_t8vuock3jnv59 t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)3ULL));
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
inline __attribute__((aligned(4))) void gm_f_truncate_pphi06n(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_pphi06n F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_tgh982e(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_tgh982e F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_uri_of_t6r79gp F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* u */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = (void *)((uint8_t *)t4);
            gm_b16_8 t6 = gm_l1;
            gm_f_get_e3joqcc((void *)&gm_l3, t5, t6);
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_l2 = t8;
            gm_b16_8 t9 = gm_l2;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t10 = gm_l1;
    gm_b16_8 t11 = gm_f_path_to_uri_as8bikc((void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_pkuqal3 F;
    void * gm_l0; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    gm_t8vuock3jnv59 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)5ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 8), t3);
    memmove(gm_out, ((uint8_t *)&t4), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) < GM_LD(uint32_t, b);
}
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return gm_lt467d5cm1d5(a + 0, b + 0);
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return gm_lt467d5cm1d5(a + 4, b + 4);
    return false;
}
__attribute__((aligned(4))) void gm_f_check_clashes_cqluvhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_check_clashes_cqluvhl F;
    void * gm_l0; /* rq */
    void * gm_l1; /* t */
    void * gm_l2; /* found */
    gm_b16_8 gm_l3; /* new_name */
    gm_b16_8 gm_l4; /* old */
    gm_b16_8 gm_l5; /* home */
    void * gm_l6; /* module */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* m */
    void * gm_l10; /* m */
    gm_b16_8 gm_l11; /* names */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_t5hdnnftkup6m gm_l20; /* u */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    void * gm_l23; /* fm */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l27; /* line */
    gm_b16_8 gm_l28; /* body */
    GM_STACK_CHECK(&F, 2380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l29;
    gm_b16_8 gm_l30;
    void * gm_l31;
    gm_th2lqbo77pvsc gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_th2lqbo77pvsc gm_l36;
    gm_tabb710d9mq2e gm_l37;
    {
        void * t40 = gm_l1;
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t40) + 40));
        gm_l4 = t41;
        void * t42 = gm_l1;
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 16));
        gm_f_parent_lk0iero((void *)&gm_l29, (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44 = (uint8_t)((((uint8_t *)&gm_l29))[16] != 0);
        if (t44) {
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l29));
            gm_l30 = t45;
        } else {
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(0));
            gm_l30 = t46;
        }
        gm_b16_8 t47 = gm_l30;
        gm_l5 = t47;
        gm_l6 = ((void *)0);
        void * t48 = gm_l2;
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 40));
        gm_l7 = t49;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t50 = gm_l8;
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t52 = (t50 < t51);
                if (!t52) {
                    goto gm_lb1;
                }
                {
                    void * t53 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t55 = gm_l8;
                    {
                        {
                            void * t56 = GM_LD(void *, (((uint8_t *)t53) + (uintptr_t)t55 * 8));
                            gm_l9 = t56;
                            void * t57 = gm_l9;
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t57) + 8));
                            gm_b16_8 t59 = gm_l5;
                            uint8_t t60 = (gm_eq0iqiubfm7e(((uint8_t *)&t58), ((uint8_t *)&t59)));
                            if (t60) {
                                void * t61 = gm_l9;
                                gm_l6 = t61;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 275, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t62 = gm_l8;
                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                uint64_t t65 = r63;
                gm_l8 = t65;
            }
        }
gm_lb1:;
        uint8_t t66 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
        if (t66) {
            void * t67 = GM_LD(void *, ((uint8_t *)&gm_l6));
            gm_l31 = t67;
        } else {
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 8));
            gm_b16_8 t70 = gm_l5;
            void * t71 = gm_l0;
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)t71) + 32));
            void * t73 = gm_f_facts_s8nso10((void *)NULL, cx, NULL, t69, t70, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l31 = t73;
        }
        void * t74 = gm_l31;
        gm_l10 = t74;
        {
            {
                void * t75 = gm_l10;
                void * t76 = (void *)(((uint8_t *)t75) + 144);
                void * t77 = gm_l1;
                gm_b16_8 t78 = gm_f_loc_key_n6bogvi((void *)NULL, cx, NULL, &(*(gm_t5hdnnftkup6m *)(((uint8_t *)t77) + 16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_get_kfrlfsf((void *)&gm_l32, t76, t78);
                uint8_t t79 = (uint8_t)((((uint8_t *)&gm_l32))[16] != 0);
                if (!t79) {
                    goto gm_lb7;
                }
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l32));
                gm_l11 = t80;
                gm_b16_8 t81 = gm_l3;
                gm_b16_8 t82 = gm_l11;
                uint8_t t83 = 0;
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t82) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t82))) + i * 16, ((uint8_t *)&t81))) { t83 = 1; break; }
                uint8_t t84 = (uint8_t)(t83);
                if (t84) {
                    gm_b16_8 t85 = gm_l4;
                    gm_b16_8 t86 = gm_l3;
                    gm_b16_8 t87 = gm_l3;
                    gm_tfeab279c16rr t88;
                    GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t85);
                    GM_ST(gm_b16_8, ((uint8_t *)&t88 + 16), t86);
                    GM_ST(gm_b16_8, ((uint8_t *)&t88 + 32), t87);
                    gm_f_format_ce9c14n((void *)&gm_l33, (void *)NULL, cx, NULL, &t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcn13n69hd0cht, 281, 20);
                    gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                    gm_th2lqbo77pvsc t92;
                    GM_ST(uint8_t, (uint8_t *)&t92 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t92 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t92 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t92), t91);
                    ((uint8_t *)&t92)[16] = 1;
                    memmove(gm_out, ((uint8_t *)&t92), 24);
                    goto gm_fdone;
                }
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
    }
    {
        void * t93 = gm_l10;
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)t93) + 104));
        gm_l12 = t94;
        gm_l13 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t95 = gm_l13;
                uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint8_t t97 = (t95 < t96);
                if (!t97) {
                    goto gm_lb8;
                }
                {
                    void * t98 = GM_LD(void *, ((uint8_t *)&gm_l12));
                    uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint64_t t100 = gm_l13;
                    {
                        {
                            memmove(((uint8_t *)&F.l14), (((uint8_t *)t98) + (uintptr_t)t100 * 48), 48);
                            void * t101 = (void *)(((uint8_t *)&F.l14) + 16);
                            void * t102 = gm_l1;
                            void * t103 = (void *)(((uint8_t *)t102) + 16);
                            uint8_t t104 = gm_f_loc_equal_h2ruinn(t101, t103);
                            uint8_t t105 = (uint8_t)!t104;
                            if (t105) {
                                goto gm_lb10;
                            }
                            void * t106 = gm_l10;
                            void * t107 = (void *)(((uint8_t *)t106) + 200);
                            gm_b16_8 t108 = gm_l3;
                            uint8_t t109 = gm_f_contains_57n7jqd(t107, t108);
                            if (t109) {
                                gm_b16_8 t110 = gm_l4;
                                gm_b16_8 t111 = gm_l3;
                                gm_b16_8 t112 = gm_l3;
                                gm_tfeab279c16rr t113;
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t110);
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 16), t111);
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 32), t112);
                                gm_f_format_t6rdn7r((void *)&gm_l34, (void *)NULL, cx, NULL, &t113);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcn13n69hd0cht, 287, 20);
                                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                                gm_th2lqbo77pvsc t117;
                                GM_ST(uint8_t, (uint8_t *)&t117 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t117 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t117 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t117), t116);
                                ((uint8_t *)&t117)[16] = 1;
                                memmove(gm_out, ((uint8_t *)&t117), 24);
                                goto gm_fdone;
                            }
                            void * t118 = gm_l10;
                            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)t118) + 104));
                            gm_l15 = t119;
                            gm_l16 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t120 = gm_l16;
                                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint8_t t122 = (t120 < t121);
                                    if (!t122) {
                                        goto gm_lb13;
                                    }
                                    {
                                        void * t123 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint64_t t125 = gm_l16;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l17), (((uint8_t *)t123) + (uintptr_t)t125 * 48), 48);
                                                {
                                                    {
                                                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                                                        gm_b16_8 t127 = gm_l3;
                                                        uint8_t t128 = (gm_eq0iqiubfm7e(((uint8_t *)&t126), ((uint8_t *)&t127)));
                                                        if (!t128) {
                                                            goto gm_lb19;
                                                        }
                                                        void * t129 = (void *)(((uint8_t *)&F.l17) + 16);
                                                        void * t130 = (void *)(((uint8_t *)&F.l14) + 16);
                                                        uint8_t t131 = gm_f_loc_equal_h2ruinn(t129, t130);
                                                        uint8_t t132 = (uint8_t)!t131;
                                                        if (!t132) {
                                                            goto gm_lb19;
                                                        }
                                                        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 16));
                                                        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 16));
                                                        uint8_t t135 = (gm_eq0iqiubfm7e(((uint8_t *)&t133), ((uint8_t *)&t134)));
                                                        if (!t135) {
                                                            goto gm_lb19;
                                                        }
                                                        uint32_t t136 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 40));
                                                        uint32_t t137 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 44));
                                                        uint8_t t138 = (t136 <= t137);
                                                        if (!t138) {
                                                            goto gm_lb19;
                                                        }
                                                        uint32_t t139 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 40));
                                                        uint32_t t140 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 44));
                                                        uint8_t t141 = (t139 <= t140);
                                                        if (!t141) {
                                                            goto gm_lb19;
                                                        }
                                                        gm_b16_8 t142 = gm_l4;
                                                        gm_b16_8 t143 = gm_l3;
                                                        gm_b16_8 t144 = gm_l3;
                                                        gm_tfeab279c16rr t145;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t142);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 16), t143);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 32), t144);
                                                        gm_f_format_ics51qh((void *)&gm_l35, (void *)NULL, cx, NULL, &t145);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                        if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcn13n69hd0cht, 291, 24);
                                                        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                                        gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                                                        gm_th2lqbo77pvsc t149;
                                                        GM_ST(uint8_t, (uint8_t *)&t149 + 17, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t149 + 18, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t149 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t149), t148);
                                                        ((uint8_t *)&t149)[16] = 1;
                                                        memmove(gm_out, ((uint8_t *)&t149), 24);
                                                        goto gm_fdone;
                                                    }
gm_lb19:;
                                                }
gm_lb18:;
                                                goto gm_lb16;
                                            }
gm_lb17:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 289, 9);
                                        }
gm_lb16:;
                                    }
gm_lb15:;
                                    uint64_t t150 = gm_l16;
                                    uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                                    uint64_t t153 = r151;
                                    gm_l16 = t153;
                                }
                            }
gm_lb13:;
                            goto gm_lb8;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 284, 5);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t154 = gm_l13;
                uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                uint64_t t157 = r155;
                gm_l13 = t157;
            }
        }
gm_lb8:;
    }
    {
        void * t158 = gm_l2;
        gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)t158));
        gm_l18 = t159;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t160 = gm_l19;
                uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t162 = (t160 < t161);
                if (!t162) {
                    goto gm_lb20;
                }
                {
                    void * t163 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t165 = gm_l19;
                    {
                        {
                            memmove(((uint8_t *)&gm_l20), (((uint8_t *)t163) + (uintptr_t)t165 * 24), 24);
                            void * t166 = gm_l2;
                            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)t166) + 40));
                            gm_l21 = t167;
                            gm_l22 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t168 = gm_l22;
                                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                    uint8_t t170 = (t168 < t169);
                                    if (!t170) {
                                        goto gm_lb25;
                                    }
                                    {
                                        void * t171 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                        uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                        uint64_t t173 = gm_l22;
                                        {
                                            {
                                                void * t174 = GM_LD(void *, (((uint8_t *)t171) + (uintptr_t)t173 * 8));
                                                gm_l23 = t174;
                                                void * t175 = gm_l23;
                                                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)t175) + 104));
                                                gm_l24 = t176;
                                                gm_l25 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t177 = gm_l25;
                                                        uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                        uint8_t t179 = (t177 < t178);
                                                        if (!t179) {
                                                            goto gm_lb30;
                                                        }
                                                        {
                                                            void * t180 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                            uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                            uint64_t t182 = gm_l25;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&F.l26), (((uint8_t *)t180) + (uintptr_t)t182 * 48), 48);
                                                                    {
                                                                        {
                                                                            gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                                                            gm_b16_8 t184 = gm_l3;
                                                                            uint8_t t185 = (gm_eq0iqiubfm7e(((uint8_t *)&t183), ((uint8_t *)&t184)));
                                                                            if (!t185) {
                                                                                goto gm_lb36;
                                                                            }
                                                                            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 16));
                                                                            gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                            uint8_t t188 = (gm_eq0iqiubfm7e(((uint8_t *)&t186), ((uint8_t *)&t187)));
                                                                            if (!t188) {
                                                                                goto gm_lb36;
                                                                            }
                                                                            void * t189 = (void *)(((uint8_t *)&F.l26) + 16);
                                                                            void * t190 = gm_l1;
                                                                            void * t191 = (void *)(((uint8_t *)t190) + 16);
                                                                            uint8_t t192 = gm_f_loc_equal_h2ruinn(t189, t191);
                                                                            uint8_t t193 = (uint8_t)!t192;
                                                                            if (!t193) {
                                                                                goto gm_lb36;
                                                                            }
                                                                            uint32_t t194 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 40));
                                                                            uint32_t t195 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 16));
                                                                            uint8_t t196 = (t194 <= t195);
                                                                            if (!t196) {
                                                                                goto gm_lb36;
                                                                            }
                                                                            uint32_t t197 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 16));
                                                                            uint32_t t198 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 44));
                                                                            uint8_t t199 = (t197 <= t198);
                                                                            if (!t199) {
                                                                                goto gm_lb36;
                                                                            }
                                                                            gm_l27 = ((uint64_t)0ULL);
                                                                            {
                                                                                {
                                                                                    void * t200 = gm_l23;
                                                                                    gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                                    gm_f_text_in_sgvn4eo((void *)&gm_l36, t200, t201);
                                                                                    uint8_t t202 = (uint8_t)((((uint8_t *)&gm_l36))[16] != 0);
                                                                                    if (!t202) {
                                                                                        goto gm_lb38;
                                                                                    }
                                                                                    gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l36));
                                                                                    gm_l28 = t203;
                                                                                    gm_b16_8 t204 = gm_l28;
                                                                                    uint32_t t205 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 16));
                                                                                    uint64_t t206 = gm_f_line_of_n80f3n1(t204, t205);
                                                                                    gm_l27 = t206;
                                                                                    goto gm_lb37;
                                                                                }
gm_lb38:;
                                                                            }
gm_lb37:;
                                                                            gm_b16_8 t207 = gm_l4;
                                                                            gm_b16_8 t208 = gm_l3;
                                                                            gm_b16_8 t209 = gm_l3;
                                                                            gm_b16_8 t210 = gm_l4;
                                                                            gm_b16_8 t211 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                            uint64_t t212 = gm_l27;
                                                                            gm_tjftu5su6fh8e t213;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t213 + 0), t207);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t213 + 16), t208);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t213 + 32), t209);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t213 + 48), t210);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t213 + 64), t211);
                                                                            GM_ST(uint64_t, ((uint8_t *)&t213 + 80), t212);
                                                                            gm_f_format_t3f3kj5((void *)&gm_l37, (void *)NULL, cx, NULL, &t213);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t214 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                                            if (!t214) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srcn13n69hd0cht, 302, 28);
                                                                            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                                                            gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                                                                            gm_th2lqbo77pvsc t217;
                                                                            GM_ST(uint8_t, (uint8_t *)&t217 + 17, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&t217 + 18, 0);
                                                                            GM_ST(uint32_t, (uint8_t *)&t217 + 20, 0);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t217), t216);
                                                                            ((uint8_t *)&t217)[16] = 1;
                                                                            memmove(gm_out, ((uint8_t *)&t217), 24);
                                                                            goto gm_fdone;
                                                                        }
gm_lb36:;
                                                                    }
gm_lb35:;
                                                                    goto gm_lb33;
                                                                }
gm_lb34:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 298, 13);
                                                            }
gm_lb33:;
                                                        }
gm_lb32:;
                                                        uint64_t t218 = gm_l25;
                                                        uint64_t r219; bool o220 = __builtin_add_overflow(t218, ((uint64_t)1ULL), &r219);
                                                        uint64_t t221 = r219;
                                                        gm_l25 = t221;
                                                    }
                                                }
gm_lb30:;
                                                goto gm_lb28;
                                            }
gm_lb29:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 297, 9);
                                        }
gm_lb28:;
                                    }
gm_lb27:;
                                    uint64_t t222 = gm_l22;
                                    uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                                    uint64_t t225 = r223;
                                    gm_l22 = t225;
                                }
                            }
gm_lb25:;
                            goto gm_lb23;
                        }
gm_lb24:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 296, 5);
                    }
gm_lb23:;
                }
gm_lb22:;
                uint64_t t226 = gm_l19;
                uint64_t r227; bool o228 = __builtin_add_overflow(t226, ((uint64_t)1ULL), &r227);
                uint64_t t229 = r227;
                gm_l19 = t229;
            }
        }
gm_lb20:;
    }
    {
        gm_th2lqbo77pvsc t230;
        memset(&t230, 0, sizeof t230);
        memmove(gm_out, ((uint8_t *)&t230), 24);
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
inline __attribute__((aligned(4))) void gm_f_check_editable_56hen53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_check_editable_56hen53 F;
    void * gm_l0; /* rq */
    void * gm_l1; /* t */
    GM_STACK_CHECK(&F, 421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    void * t2 = gm_l1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 16));
    gm_b16_8 t4 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t3, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t6) {
        void * t7 = gm_l1;
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 40));
        gm_tnpifv20irnph t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
        gm_f_format_o1952f2((void *)&gm_l3, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcn13n69hd0cht, 46, 16);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_th2lqbo77pvsc t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t13), 24);
        goto gm_fdone;
    }
    void * t14 = gm_l0;
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 16));
    gm_f_package_roots_uh3pgnt((void *)&F.l2, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t16 = gm_l1;
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 16));
    void * t18 = GM_LD(void *, ((uint8_t *)&F.l2));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t19, gm_srcn13n69hd0cht, 49, 41);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t18) + (uintptr_t)((uint64_t)0ULL) * 16));
    uint8_t t21 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t17, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t22 = (uint8_t)!t21;
    if (t22) {
        void * t23 = gm_l1;
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 40));
        gm_tnpifv20irnph t25;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
        gm_f_format_cebtc0e((void *)&gm_l4, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcn13n69hd0cht, 50, 16);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_th2lqbo77pvsc t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t29), 24);
        goto gm_fdone;
    }
    gm_th2lqbo77pvsc t30;
    memset(&t30, 0, sizeof t30);
    memmove(gm_out, ((uint8_t *)&t30), 24);
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
inline __attribute__((aligned(4))) void gm_f_check_new_name_31k8n9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_check_new_name_31k8n9d F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    gm_tabb710d9mq2e gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_identifier_kvlcjjo(t2);
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        gm_b16_8 t5 = gm_l0;
        gm_tnpifv20irnph t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
        gm_f_format_egbi75q((void *)&gm_l1, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcn13n69hd0cht, 179, 46);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_th2lqbo77pvsc t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t10), 24);
        goto gm_fdone;
    }
    gm_b16_8 t11 = gm_l0;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sfdbfrv46lihd));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
    uint8_t t13 = (gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
    if (t13) {
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s1n4l05blor5r));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(39));
        gm_th2lqbo77pvsc t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t15), 24);
        goto gm_fdone;
    }
    gm_b16_8 t16 = gm_l0;
    void * t17 = (void *)((uint8_t *)gm_cka4oohkshv);
    uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t19 = (uintptr_t)((uint64_t)32ULL);
    if (t18 > t19 || t19 > (uintptr_t)((uint64_t)32ULL)) gm_trap_slice(t18, t19, ((uint64_t)32ULL), gm_srcn13n69hd0cht, 181, 16);
    uint64_t t20 = ((uintptr_t)((uint64_t)32ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20));
    uint8_t t22 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t21) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t21))) + i * 16, ((uint8_t *)&t16))) { t22 = 1; break; }
    uint8_t t23 = (uint8_t)(t22);
    if (t23) {
        gm_b16_8 t24 = gm_l0;
        gm_tnpifv20irnph t25;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
        gm_f_format_2g3rqdg((void *)&gm_l2, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcn13n69hd0cht, 181, 50);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_th2lqbo77pvsc t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t29), 24);
        goto gm_fdone;
    }
    gm_b16_8 t30 = gm_l0;
    uint8_t t31 = gm_f_is_reserved_type_name_tuq4qsj(t30);
    if (t31) {
        gm_b16_8 t32 = gm_l0;
        gm_tnpifv20irnph t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
        gm_f_format_s8ajp67((void *)&gm_l3, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcn13n69hd0cht, 182, 50);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_th2lqbo77pvsc t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t37), t36);
        ((uint8_t *)&t37)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t37), 24);
        goto gm_fdone;
    }
    gm_th2lqbo77pvsc t38;
    memset(&t38, 0, sizeof t38);
    memmove(gm_out, ((uint8_t *)&t38), 24);
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
__attribute__((aligned(4))) void gm_f_declaration_at_lquhblp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_declaration_at_lquhblp F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* t */
    gm_b16_8 gm_l5; /* name */
    gm_b16_8 gm_l6; /* path */
    gm_t5hdnnftkup6m gm_l7; /* loc */
    GM_STACK_CHECK(&F, 1108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_toceffb697p0u gm_l8;
    gm_tl874t2atnlh3 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_b16_8 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 120);
    uint32_t t7 = gm_l1;
    uint32_t t8 = gm_l2;
    gm_toceffb697p0u t9 = gm_f_definition_at_9jj9uuu(t6, t7, t8);
    gm_l8 = t9;
    uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l8))[12] != 0);
    if (t10) {
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l8));
        gm_l9 = t11;
    } else {
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s4ss30tejbl47));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(31));
        gm_tmkrnkep1qbgl t13;
        memset(&t13, 0, sizeof t13);
        ((uint8_t *)&t13)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t12);
        memmove(gm_out, ((uint8_t *)&t13), 72);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t14 = gm_l9;
    gm_l3 = t14;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
    uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    uint8_t t17 = (t15 <= t16);
    if (t17) {
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s71u85v3jrj86));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(62));
        gm_tmkrnkep1qbgl t19;
        memset(&t19, 0, sizeof t19);
        ((uint8_t *)&t19)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
        memmove(gm_out, ((uint8_t *)&t19), 72);
        goto gm_fdone;
    }
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
    gm_b16_8 t23 = gm_f_source_text_7ru912k(t21, t22);
    gm_l4 = t23;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(0));
    gm_l5 = t24;
    {
        {
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t26 = (uint64_t)t25;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t28 = (t26 <= t27);
            if (!t28) {
                goto gm_lb2;
            }
            gm_b16_8 t29 = gm_l4;
            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint64_t t31 = (uint64_t)t30;
            uint8_t t32 = gm_f_is_char_boundary_h62fhaa(t29, t31);
            if (!t32) {
                goto gm_lb2;
            }
            gm_b16_8 t33 = gm_l4;
            uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t35 = (uint64_t)t34;
            uint8_t t36 = gm_f_is_char_boundary_h62fhaa(t33, t35);
            if (!t36) {
                goto gm_lb2;
            }
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint64_t t40 = (uint64_t)t39;
            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t42 = (uint64_t)t41;
            uint64_t t43 = (uintptr_t)t40;
            uint64_t t44 = (uintptr_t)t42;
            if (t43 > t44 || t44 > (uintptr_t)t38) gm_trap_slice(t43, t44, t38, gm_srcn13n69hd0cht, 37, 121);
            uint64_t t45 = ((uintptr_t)t42) - (uintptr_t)t40;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t37 + (uintptr_t)t40 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45));
            gm_l5 = t46;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t47 = gm_l0;
    void * t48 = GM_LD(void *, ((uint8_t *)t47));
    uint32_t t49 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
    gm_f_real_path_al8cv9r((void *)&gm_l10, (void *)NULL, cx, NULL, t48, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
    if (t50) {
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
        gm_l11 = t51;
    } else {
        gm_b16_8 t52 = gm_l5;
        gm_tnpifv20irnph t53;
        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
        gm_f_format_h78t8d0((void *)&gm_l12, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcn13n69hd0cht, 38, 52);
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        gm_tmkrnkep1qbgl t57;
        memset(&t57, 0, sizeof t57);
        ((uint8_t *)&t57)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 8), t56);
        memmove(gm_out, ((uint8_t *)&t57), 72);
        goto gm_fdone;
    }
    gm_b16_8 t58 = gm_l11;
    gm_l6 = t58;
    gm_b16_8 t59 = gm_l6;
    uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
    gm_t5hdnnftkup6m t62;
    GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t59);
    GM_ST(uint32_t, ((uint8_t *)&t62 + 16), t60);
    GM_ST(uint32_t, ((uint8_t *)&t62 + 20), t61);
    gm_l7 = t62;
    gm_tl874t2atnlh3 t63 = gm_l3;
    gm_b16_8 t64 = gm_l5;
    void * t65 = gm_l0;
    gm_tl874t2atnlh3 t66 = gm_l3;
    gm_t5hdnnftkup6m t67 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)&gm_l7));
    uint8_t t68 = gm_f_is_local_jek4n0p(t65, GM_CONV(gm_cseahvg8v7p0vs, t66));
    gm_tdi3m8srnr9m6 t69;
    GM_ST(uint32_t, (uint8_t *)&t69 + 12, 0);
    GM_ST(uint8_t, (uint8_t *)&t69 + 57, 0);
    GM_ST(uint16_t, (uint8_t *)&t69 + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&t69 + 60, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t69 + 0), t63);
    GM_ST(gm_t5hdnnftkup6m, ((uint8_t *)&t69 + 16), t67);
    GM_ST(gm_b16_8, ((uint8_t *)&t69 + 40), t64);
    GM_ST(uint8_t, ((uint8_t *)&t69 + 56), t68);
    gm_tmkrnkep1qbgl t70;
    GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
    ((uint8_t *)&t70)[0] = 0;
    GM_ST(gm_tdi3m8srnr9m6, ((uint8_t *)&t70 + 8), t69);
    memmove(gm_out, ((uint8_t *)&t70), 72);
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
__attribute__((aligned(4))) void gm_f_find_m559mi8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_find_m559mi8 F;
    void * gm_l0; /* rq */
    void * gm_l1; /* t */
    uint8_t gm_l4; /* under */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* r */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* cand */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l14; /* m */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    GM_STACK_CHECK(&F, 1299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l18;
    gm_trqfvmjtt4sij gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_trqfvmjtt4sij t24;
    gm_f_list_ctsmjqd((void *)&t24, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t24;
    void * t25 = (void *)((uint8_t *)&F.l2);
    void * t26 = gm_l0;
    void * t27 = GM_LD(void *, (((uint8_t *)t26) + 8));
    void * t28 = gm_l0;
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 16));
    void * t30 = gm_l0;
    void * t31 = GM_LD(void *, ((uint8_t *)t30));
    void * t32 = gm_l0;
    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 32));
    void * t34 = gm_f_store_q18c91o((void *)NULL, cx, NULL, t27, t29, t31, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t35 = gm_f_push_1drnucl(t25, t34);
    gm_l18 = t35;
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcn13n69hd0cht, 59, 5);
    void * t37 = gm_l1;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + 56));
    uint8_t t39 = (uint8_t)!t38;
    if (t39) {
        void * t40 = gm_l0;
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t40) + 16));
        gm_f_package_roots_uh3pgnt((void *)&F.l3, (void *)NULL, cx, NULL, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = ((uint8_t)0);
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_l5 = t42;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t43 = gm_l6;
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t45 = (t43 < t44);
                if (!t45) {
                    goto gm_lb1;
                }
                {
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t48 = gm_l6;
                    {
                        {
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t46) + (uintptr_t)t48 * 16));
                            gm_l7 = t49;
                            void * t50 = gm_l1;
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)t50) + 16));
                            gm_b16_8 t52 = gm_l7;
                            uint8_t t53 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t51, t52);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (t53) {
                                gm_l4 = ((uint8_t)1);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 63, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t54 = gm_l6;
                uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                uint64_t t57 = r55;
                gm_l6 = t57;
            }
        }
gm_lb1:;
        uint8_t t58 = gm_l4;
        if (t58) {
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            void * t61 = gm_l0;
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t61) + 32));
            gm_f_module_dirs_72lhhi5((void *)&gm_l19, (void *)NULL, cx, NULL, t60, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
            gm_l8 = t63;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t64 = gm_l9;
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t66 = (t64 < t65);
                    if (!t66) {
                        goto gm_lb6;
                    }
                    {
                        void * t67 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t69 = gm_l9;
                        {
                            {
                                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)t67) + (uintptr_t)t69 * 16));
                                gm_l10 = t70;
                                {
                                    {
                                        gm_b16_8 t71 = gm_l10;
                                        void * t72 = gm_l0;
                                        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)t72) + 16));
                                        uint8_t t74 = (!gm_eq0iqiubfm7e(((uint8_t *)&t71), ((uint8_t *)&t73)));
                                        if (!t74) {
                                            goto gm_lb12;
                                        }
                                        gm_b16_8 t75 = gm_l10;
                                        void * t76 = gm_l1;
                                        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t76) + 40));
                                        void * t78 = gm_l0;
                                        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)t78) + 32));
                                        uint8_t t80 = gm_f_mentions_gartt1q((void *)NULL, cx, NULL, t75, t77, t79);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        if (!t80) {
                                            goto gm_lb12;
                                        }
                                        void * t81 = (void *)((uint8_t *)&F.l2);
                                        void * t82 = gm_l0;
                                        void * t83 = GM_LD(void *, (((uint8_t *)t82) + 8));
                                        gm_b16_8 t84 = gm_l10;
                                        void * t85 = gm_l0;
                                        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)t85) + 32));
                                        void * t87 = gm_f_facts_s8nso10((void *)NULL, cx, NULL, t83, t84, t86);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t88 = gm_f_push_1drnucl(t81, t87);
                                        gm_l20 = t88;
                                        uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                        if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcn13n69hd0cht, 69, 21);
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 67, 13);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t90 = gm_l9;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l9 = t93;
                }
            }
gm_lb6:;
        }
    }
    gm_trqfvmjtt4sij t94;
    gm_f_list_0hnol3g((void *)&t94, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l11 = t94;
    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_l12 = t95;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t96 = gm_l13;
            uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t98 = (t96 < t97);
            if (!t98) {
                goto gm_lb13;
            }
            {
                void * t99 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t101 = gm_l13;
                {
                    {
                        void * t102 = GM_LD(void *, (((uint8_t *)t99) + (uintptr_t)t101 * 8));
                        gm_l14 = t102;
                        void * t103 = gm_l14;
                        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)t103) + 24));
                        gm_l15 = t104;
                        gm_l16 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t105 = gm_l16;
                                uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint8_t t107 = (t105 < t106);
                                if (!t107) {
                                    goto gm_lb18;
                                }
                                {
                                    void * t108 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint64_t t110 = gm_l16;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l17), (((uint8_t *)t108) + (uintptr_t)t110 * 48), 48);
                                            void * t111 = (void *)(((uint8_t *)&F.l17) + 24);
                                            void * t112 = gm_l1;
                                            void * t113 = (void *)(((uint8_t *)t112) + 16);
                                            uint8_t t114 = gm_f_loc_equal_h2ruinn(t111, t113);
                                            if (t114) {
                                                void * t115 = (void *)((uint8_t *)&F.l11);
                                                gm_tftb1odc739t5 t116 = gm_f_push_drp4m1b(t115, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&F.l17)));
                                                gm_l21 = t116;
                                                uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                                if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcn13n69hd0cht, 77, 43);
                                            }
                                            goto gm_lb21;
                                        }
gm_lb22:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 76, 9);
                                    }
gm_lb21:;
                                }
gm_lb20:;
                                uint64_t t118 = gm_l16;
                                uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                uint64_t t121 = r119;
                                gm_l16 = t121;
                            }
                        }
gm_lb18:;
                        goto gm_lb16;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 75, 5);
                }
gm_lb16:;
            }
gm_lb15:;
            uint64_t t122 = gm_l13;
            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
            uint64_t t125 = r123;
            gm_l13 = t125;
        }
    }
gm_lb13:;
    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
    gm_closure t127 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_loc_less_gf4kba8)), NULL, cx});
    gm_f_sort_by_ucn5r3c(t126, t127);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t128 = (void *)((uint8_t *)&F.l11);
    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
    uint64_t t130 = gm_f_dedup_5og6p3h(t129);
    gm_f_truncate_pphi06n(t128, t130);
    gm_trqfvmjtt4sij t131 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l11));
    gm_trqfvmjtt4sij t132 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_td7pkamr64l0c t133;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t133 + 0), t131);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t133 + 40), t132);
    memmove(gm_out, ((uint8_t *)&t133), 80);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_o57h82n(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_local_jek4n0p(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_local_jek4n0p F;
    void * gm_l0; /* rq */
    gm_tl874t2atnlh3 gm_l1; /* def */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 120) + 136));
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
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t13) + (uintptr_t)t15 * 88), 88);
                        gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 64));
                        gm_tl874t2atnlh3 t17 = gm_l1;
                        uint8_t t18 = (gm_eqff85ac10o3(((uint8_t *)&t16), ((uint8_t *)&t17)));
                        if (t18) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 25, 5);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_word_byte_7lklilo(uint8_t a0);
inline __attribute__((aligned(4))) uint64_t gm_f_line_of_n80f3n1(gm_b16_8 a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_line_of_n80f3n1 F;
    gm_b16_8 gm_l0; /* t */
    uint32_t gm_l1; /* off */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    uint32_t t7 = gm_l1;
    uint64_t t8 = (uint64_t)t7;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t10 = (t8 > t9);
    if (t10) {
        gm_l6 = ((uint8_t)1);
    } else {
        gm_b16_8 t11 = gm_l0;
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        uint8_t t14 = gm_f_is_char_boundary_h62fhaa(t11, t13);
        uint8_t t15 = (uint8_t)!t14;
        gm_l6 = t15;
    }
    uint8_t t16 = gm_l6;
    if (t16) {
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
    }
    gm_l2 = ((uint64_t)0ULL);
    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint32_t t19 = gm_l1;
    uint64_t t20 = (uint64_t)t19;
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)t20;
    if (t21 > t22 || t22 > (uintptr_t)t18) gm_trap_slice(t21, t22, t18, gm_srcn13n69hd0cht, 189, 14);
    uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    gm_l3 = t24;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l4;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t30 = gm_l4;
                {
                    {
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)t30 * 1));
                        gm_l5 = t31;
                        uint8_t t32 = gm_l5;
                        uint8_t t33 = ((t32 == ((uint8_t)10ULL)));
                        if (t33) {
                            uint64_t t34 = gm_l2;
                            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                            if (o36) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 190, 24);
                            uint64_t t37 = r35;
                            gm_l2 = t37;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 189, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t38 = gm_l4;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            uint64_t t41 = r39;
            gm_l4 = t41;
        }
    }
gm_lb1:;
    uint64_t t42 = gm_l2;
    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
    if (o44) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 192, 12);
    uint64_t t45 = r43;
    F.ret = t45;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_location_in_v59u1gt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_location_in_v59u1gt F;
    void * gm_l0; /* rq */
    void * gm_l1; /* found */
    void * gm_l2; /* loc */
    gm_b16_8 gm_l3; /* t */
    gm_t6j6smj7qoctu gm_l4; /* ix */
    GM_STACK_CHECK(&F, 578u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l6;
    gm_b16_8 gm_l7;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    void * t4 = gm_l2;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    gm_f_text_of_qs06tq9((void *)&gm_l6, (void *)NULL, cx, NULL, t2, t3, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t6) {
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l7 = t7;
    } else {
        gm_t3ph3kbiqt1ah t8;
        memset(&t8, 0, sizeof t8);
        memmove(gm_out, ((uint8_t *)&t8), 32);
        goto gm_fdone;
    }
    gm_b16_8 t9 = gm_l7;
    gm_l3 = t9;
    gm_b16_8 t10 = gm_l3;
    gm_t6j6smj7qoctu t11;
    gm_f_line_index_2necgtf((void *)&t11, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t11;
    gm_tin97tt4ud0s1 t12;
    gm_f_obj_aoa7fl7((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t12;
    void * t13 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sbo52miqad506));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 56));
    void * t17 = gm_l2;
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)t17));
    gm_b16_8 t19 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t8vuock3jnv59 t20;
    gm_f_text_ls49dp6((void *)&t20, t19);
    gm_b0_1 t21 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t13, t14, &t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t22 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sl4ns6hobicht));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(5));
    void * t24 = (void *)((uint8_t *)&gm_l4);
    void * t25 = gm_l2;
    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 16));
    uint64_t t27 = (uint64_t)t26;
    void * t28 = gm_l2;
    uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)t28) + 20));
    uint64_t t30 = (uint64_t)t29;
    void * t31 = gm_l0;
    gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t31) + 48));
    gm_t8vuock3jnv59 t33;
    gm_f_range_json_hlsolbo((void *)&t33, (void *)NULL, cx, NULL, t24, t27, t30, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t34 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t22, t23, &t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t35 = (void *)((uint8_t *)&F.l5);
    gm_t8vuock3jnv59 t36;
    gm_f_value_pkuqal3((void *)&t36, t35);
    gm_t3ph3kbiqt1ah t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t37 + 28, 0);
    GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t37), t36);
    ((uint8_t *)&t37)[24] = 1;
    memmove(gm_out, ((uint8_t *)&t37), 32);
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
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_narrow_5epj0dh(gm_b16_8 a0, uint32_t a1, uint32_t a2, gm_b16_8 a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_narrow_5epj0dh F;
    gm_b16_8 gm_l0; /* t */
    uint32_t gm_l1; /* lo */
    uint32_t gm_l2; /* hi */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* slice */
    gm_tf5srh4mp4b3i gm_l5; /* best */
    uint64_t gm_l6; /* from */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* at */
    uint64_t gm_l9; /* end */
    uint8_t gm_l10; /* before_ok */
    uint8_t gm_l11; /* after_ok */
    uint64_t gm_l12; /* at */
    GM_STACK_CHECK(&F, 868u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    uint64_t gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    uint64_t gm_l20;
    uint32_t t7 = gm_l1;
    uint64_t t8 = (uint64_t)t7;
    uint32_t t9 = gm_l2;
    uint64_t t10 = (uint64_t)t9;
    uint8_t t11 = (t8 > t10);
    if (t11) {
        gm_l15 = ((uint8_t)1);
    } else {
        uint32_t t12 = gm_l2;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t15 = (t13 > t14);
        gm_l15 = t15;
    }
    uint8_t t16 = gm_l15;
    if (t16) {
        gm_l14 = ((uint8_t)1);
    } else {
        gm_b16_8 t17 = gm_l0;
        uint32_t t18 = gm_l1;
        uint64_t t19 = (uint64_t)t18;
        uint8_t t20 = gm_f_is_char_boundary_h62fhaa(t17, t19);
        uint8_t t21 = (uint8_t)!t20;
        gm_l14 = t21;
    }
    uint8_t t22 = gm_l14;
    if (t22) {
        gm_l13 = ((uint8_t)1);
    } else {
        gm_b16_8 t23 = gm_l0;
        uint32_t t24 = gm_l2;
        uint64_t t25 = (uint64_t)t24;
        uint8_t t26 = gm_f_is_char_boundary_h62fhaa(t23, t25);
        uint8_t t27 = (uint8_t)!t26;
        gm_l13 = t27;
    }
    uint8_t t28 = gm_l13;
    if (t28) {
        gm_t736djc66po0l t29;
        memset(&t29, 0, sizeof t29);
        F.ret = t29;
        goto gm_fret;
    }
    void * t30 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint32_t t32 = gm_l1;
    uint64_t t33 = (uint64_t)t32;
    uint32_t t34 = gm_l2;
    uint64_t t35 = (uint64_t)t34;
    uint64_t t36 = (uintptr_t)t33;
    uint64_t t37 = (uintptr_t)t35;
    if (t36 > t37 || t37 > (uintptr_t)t31) gm_trap_slice(t36, t37, t31, gm_srcn13n69hd0cht, 129, 14);
    uint64_t t38 = ((uintptr_t)t35) - (uintptr_t)t33;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t30 + (uintptr_t)t33 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
    gm_l4 = t39;
    gm_b16_8 t40 = gm_l4;
    gm_b16_8 t41 = gm_l3;
    uint8_t t42 = (gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
    if (t42) {
        uint32_t t43 = gm_l1;
        uint32_t t44 = gm_l2;
        gm_tcc1rq4n1avb2 t45;
        GM_ST(uint32_t, ((uint8_t *)&t45 + 0), t43);
        GM_ST(uint32_t, ((uint8_t *)&t45 + 4), t44);
        gm_t736djc66po0l t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t46), t45);
        ((uint8_t *)&t46)[8] = 1;
        F.ret = t46;
        goto gm_fret;
    }
    gm_tf5srh4mp4b3i t47;
    memset(&t47, 0, sizeof t47);
    gm_l5 = t47;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t48 = gm_l6;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t50 = (t48 <= t49);
            if (!t50) {
                goto gm_lb1;
            }
            {
                void * t51 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t53 = gm_l6;
                uint64_t t54 = (uintptr_t)t53;
                uint64_t t55 = (uintptr_t)t52;
                if (t54 > t55 || t55 > (uintptr_t)t52) gm_trap_slice(t54, t55, t52, gm_srcn13n69hd0cht, 134, 23);
                uint64_t t56 = ((uintptr_t)t52) - (uintptr_t)t53;
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t51 + (uintptr_t)t53 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
                gm_b16_8 t58 = gm_l3;
                gm_tf5srh4mp4b3i t59 = gm_f_find_nab29co(t57, t58);
                gm_l16 = t59;
                uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                if (t60) {
                    uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
                    gm_l17 = t61;
                } else {
                    goto gm_lb1;
                }
                uint64_t t62 = gm_l17;
                gm_l7 = t62;
                uint64_t t63 = gm_l6;
                uint64_t t64 = gm_l7;
                uint64_t r65; bool o66 = __builtin_add_overflow(t63, t64, &r65);
                if (o66) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 135, 15);
                uint64_t t67 = r65;
                gm_l8 = t67;
                uint64_t t68 = gm_l8;
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t r70; bool o71 = __builtin_add_overflow(t68, t69, &r70);
                if (o71) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 136, 16);
                uint64_t t72 = r70;
                gm_l9 = t72;
                uint64_t t73 = gm_l8;
                uint8_t t74 = ((t73 == ((uint64_t)0ULL)));
                if (t74) {
                    gm_l18 = ((uint8_t)1);
                } else {
                    void * t75 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t77 = gm_l8;
                    uint64_t r78; bool o79 = __builtin_sub_overflow(t77, ((uint64_t)1ULL), &r78);
                    if (o79) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 137, 56);
                    uint64_t t80 = r78;
                    if ((uintptr_t)t80 >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)t80, t76, gm_srcn13n69hd0cht, 137, 50);
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)t80 * 1));
                    uint8_t t82 = gm_f_is_word_byte_7lklilo(t81);
                    uint8_t t83 = (uint8_t)!t82;
                    gm_l18 = t83;
                }
                uint8_t t84 = gm_l18;
                gm_l10 = t84;
                uint64_t t85 = gm_l9;
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t87 = (t85 >= t86);
                if (t87) {
                    gm_l19 = ((uint8_t)1);
                } else {
                    void * t88 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t90 = gm_l9;
                    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)t90 * 1));
                    uint8_t t92 = gm_f_is_word_byte_7lklilo(t91);
                    uint8_t t93 = (uint8_t)!t92;
                    gm_l19 = t93;
                }
                uint8_t t94 = gm_l19;
                gm_l11 = t94;
                {
                    {
                        uint8_t t95 = gm_l10;
                        if (!t95) {
                            goto gm_lb5;
                        }
                        uint8_t t96 = gm_l11;
                        if (!t96) {
                            goto gm_lb5;
                        }
                        uint64_t t97 = gm_l8;
                        gm_tf5srh4mp4b3i t98;
                        GM_ST(uint8_t, (uint8_t *)&t98 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t98 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t98 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t98), t97);
                        ((uint8_t *)&t98)[8] = 1;
                        gm_l5 = t98;
                        goto gm_lb4;
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t99 = gm_l8;
                uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                if (o101) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 140, 16);
                uint64_t t102 = r100;
                gm_l6 = t102;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint8_t t103 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t103) {
        uint64_t t104 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l20 = t104;
    } else {
        gm_t736djc66po0l t105;
        memset(&t105, 0, sizeof t105);
        F.ret = t105;
        goto gm_fret;
    }
    uint64_t t106 = gm_l20;
    gm_l12 = t106;
    uint32_t t107 = gm_l1;
    uint64_t t108 = gm_l12;
    uint32_t t109 = (uint32_t)t108;
    uint32_t r110; bool o111 = __builtin_add_overflow(t107, t109, &r110);
    if (o111) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 143, 17);
    uint32_t t112 = r110;
    uint32_t t113 = gm_l1;
    uint64_t t114 = gm_l12;
    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t r116; bool o117 = __builtin_add_overflow(t114, t115, &r116);
    if (o117) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 143, 47);
    uint64_t t118 = r116;
    uint32_t t119 = (uint32_t)t118;
    uint32_t r120; bool o121 = __builtin_add_overflow(t113, t119, &r120);
    if (o121) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 143, 41);
    uint32_t t122 = r120;
    gm_tcc1rq4n1avb2 t123;
    GM_ST(uint32_t, ((uint8_t *)&t123 + 0), t112);
    GM_ST(uint32_t, ((uint8_t *)&t123 + 4), t122);
    gm_t736djc66po0l t124;
    GM_ST(uint8_t, (uint8_t *)&t124 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t124 + 10, 0);
    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t124), t123);
    ((uint8_t *)&t124)[8] = 1;
    F.ret = t124;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_prepare_rename_i0odtgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_prepare_rename_i0odtgj F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l4; /* msg */
    gm_b16_8 gm_l5; /* t_text */
    gm_toceffb697p0u gm_l6; /* best */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t2p74ki8sl4an gm_l9; /* r */
    gm_tl874t2atnlh3 gm_l10; /* b */
    gm_tl874t2atnlh3 gm_l11; /* b */
    gm_tl874t2atnlh3 gm_l12; /* span */
    gm_tcc1rq4n1avb2 gm_l13; /* r */
    gm_t6j6smj7qoctu gm_l14; /* ix */
    GM_STACK_CHECK(&F, 1337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tmkrnkep1qbgl gm_l16;
    gm_th2lqbo77pvsc gm_l17;
    gm_tl874t2atnlh3 gm_l18;
    gm_t736djc66po0l gm_l19;
    gm_tcc1rq4n1avb2 gm_l20;
    void * t17 = gm_l0;
    uint32_t t18 = gm_l1;
    uint32_t t19 = gm_l2;
    gm_f_declaration_at_lquhblp((void *)&gm_l16, (void *)NULL, cx, NULL, t17, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (t20) {
    } else {
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_t9l835tjp727s t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
        ((uint8_t *)&t22)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
        memmove(gm_out, ((uint8_t *)&t22), 32);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l16) + 8), 64);
    {
        {
            void * t23 = gm_l0;
            void * t24 = (void *)((uint8_t *)&F.l3);
            gm_f_check_editable_56hen53((void *)&gm_l17, (void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
            if (!t25) {
                goto gm_lb2;
            }
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
            gm_l4 = t26;
            gm_b16_8 t27 = gm_l4;
            gm_t9l835tjp727s t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 24, 0);
            ((uint8_t *)&t28)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 8), t27);
            memmove(gm_out, ((uint8_t *)&t28), 32);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    uint32_t t31 = gm_l1;
    gm_b16_8 t32 = gm_f_source_text_7ru912k(t30, t31);
    gm_l5 = t32;
    gm_toceffb697p0u t33;
    memset(&t33, 0, sizeof t33);
    gm_l6 = t33;
    void * t34 = gm_l0;
    void * t35 = GM_LD(void *, ((uint8_t *)t34));
    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 120));
    gm_l7 = t36;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t37 = gm_l8;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t39 = (t37 < t38);
            if (!t39) {
                goto gm_lb3;
            }
            {
                void * t40 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t42 = gm_l8;
                {
                    {
                        memmove(((uint8_t *)&gm_l9), (((uint8_t *)t40) + (uintptr_t)t42 * 24), 24);
                        {
                            {
                                uint32_t t43 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                uint32_t t44 = gm_l1;
                                uint8_t t45 = ((t43 == t44));
                                if (!t45) {
                                    goto gm_lb9;
                                }
                                uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                                uint32_t t47 = gm_l2;
                                uint8_t t48 = (t46 <= t47);
                                if (!t48) {
                                    goto gm_lb9;
                                }
                                uint32_t t49 = gm_l2;
                                uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                                uint8_t t51 = (t49 <= t50);
                                if (!t51) {
                                    goto gm_lb9;
                                }
                                gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 12));
                                gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&F.l3));
                                uint8_t t54 = (gm_eqff85ac10o3(((uint8_t *)&t52), ((uint8_t *)&t53)));
                                if (!t54) {
                                    goto gm_lb9;
                                }
                                {
                                    {
                                        uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l6))[12] != 0);
                                        if (!t55) {
                                            goto gm_lb11;
                                        }
                                        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l6));
                                        gm_l10 = t56;
                                        uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                                        uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                                        uint32_t r59; bool o60 = __builtin_sub_overflow(t57, t58, &r59);
                                        if (o60) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 156, 20);
                                        uint32_t t61 = r59;
                                        uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
                                        uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                                        uint32_t r64; bool o65 = __builtin_sub_overflow(t62, t63, &r64);
                                        if (o65) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 156, 45);
                                        uint32_t t66 = r64;
                                        uint8_t t67 = (t61 >= t66);
                                        if (t67) {
                                            goto gm_lb5;
                                        }
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l9));
                                gm_toceffb697p0u t69;
                                GM_ST(uint8_t, (uint8_t *)&t69 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t69 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t69), t68);
                                ((uint8_t *)&t69)[12] = 1;
                                gm_l6 = t69;
                                goto gm_lb8;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 153, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t70 = gm_l8;
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            uint64_t t73 = r71;
            gm_l8 = t73;
        }
    }
gm_lb3:;
    {
        {
            uint32_t t74 = GM_LD(uint32_t, ((uint8_t *)&F.l3));
            uint32_t t75 = gm_l1;
            uint8_t t76 = ((t74 == t75));
            if (!t76) {
                goto gm_lb13;
            }
            uint32_t t77 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 4));
            uint32_t t78 = gm_l2;
            uint8_t t79 = (t77 <= t78);
            if (!t79) {
                goto gm_lb13;
            }
            uint32_t t80 = gm_l2;
            uint32_t t81 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
            uint8_t t82 = (t80 <= t81);
            if (!t82) {
                goto gm_lb13;
            }
            {
                {
                    uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l6))[12] != 0);
                    if (!t83) {
                        goto gm_lb15;
                    }
                    gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l6));
                    gm_l11 = t84;
                    uint32_t t85 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
                    uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 4));
                    uint32_t r87; bool o88 = __builtin_sub_overflow(t85, t86, &r87);
                    if (o88) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 163, 16);
                    uint32_t t89 = r87;
                    uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                    uint32_t t91 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 4));
                    uint32_t r92; bool o93 = __builtin_sub_overflow(t90, t91, &r92);
                    if (o93) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 163, 40);
                    uint32_t t94 = r92;
                    uint8_t t95 = (t89 < t94);
                    if (t95) {
                        gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&F.l3));
                        gm_toceffb697p0u t97;
                        GM_ST(uint8_t, (uint8_t *)&t97 + 13, 0);
                        GM_ST(uint16_t, (uint8_t *)&t97 + 14, 0);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t97), t96);
                        ((uint8_t *)&t97)[12] = 1;
                        gm_l6 = t97;
                    }
                    goto gm_lb14;
                }
gm_lb15:;
                gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&F.l3));
                gm_toceffb697p0u t99;
                GM_ST(uint8_t, (uint8_t *)&t99 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t99 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t99), t98);
                ((uint8_t *)&t99)[12] = 1;
                gm_l6 = t99;
            }
gm_lb14:;
            goto gm_lb12;
        }
gm_lb13:;
    }
gm_lb12:;
    uint8_t t100 = (uint8_t)((((uint8_t *)&gm_l6))[12] != 0);
    if (t100) {
        gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l6));
        gm_l18 = t101;
    } else {
        gm_b16_8 t102;
        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_s4ss30tejbl47));
        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(31));
        gm_t9l835tjp727s t103;
        GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t103 + 24, 0);
        ((uint8_t *)&t103)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t103 + 8), t102);
        memmove(gm_out, ((uint8_t *)&t103), 32);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t104 = gm_l18;
    gm_l12 = t104;
    gm_b16_8 t105 = gm_l5;
    uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
    uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 8));
    gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
    gm_t736djc66po0l t109 = gm_f_narrow_5epj0dh(t105, t106, t107, t108);
    gm_l19 = t109;
    uint8_t t110 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
    if (t110) {
        gm_tcc1rq4n1avb2 t111 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&gm_l19));
        gm_l20 = t111;
    } else {
        gm_b16_8 t112;
        GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sn68oj251cu21));
        GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(37));
        gm_t9l835tjp727s t113;
        GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t113 + 24, 0);
        ((uint8_t *)&t113)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 8), t112);
        memmove(gm_out, ((uint8_t *)&t113), 32);
        goto gm_fdone;
    }
    gm_tcc1rq4n1avb2 t114 = gm_l20;
    gm_l13 = t114;
    gm_b16_8 t115 = gm_l5;
    gm_t6j6smj7qoctu t116;
    gm_f_line_index_2necgtf((void *)&t116, (void *)NULL, cx, NULL, t115);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t116;
    gm_tin97tt4ud0s1 t117;
    gm_f_obj_aoa7fl7((void *)&t117, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l15 = t117;
    void * t118 = (void *)((uint8_t *)&F.l15);
    gm_b16_8 t119;
    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sl4ns6hobicht));
    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(5));
    void * t120 = (void *)((uint8_t *)&gm_l14);
    uint32_t t121 = GM_LD(uint32_t, ((uint8_t *)&gm_l13));
    uint64_t t122 = (uint64_t)t121;
    uint32_t t123 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 4));
    uint64_t t124 = (uint64_t)t123;
    void * t125 = gm_l0;
    gm_t9qkh0rsooc8d t126 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t125) + 48));
    gm_t8vuock3jnv59 t127;
    gm_f_range_json_hlsolbo((void *)&t127, (void *)NULL, cx, NULL, t120, t122, t124, t126);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t128 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t118, t119, &t127);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t129 = (void *)((uint8_t *)&F.l15);
    gm_b16_8 t130;
    GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s54jmbv61vfjv));
    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(11));
    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
    gm_t8vuock3jnv59 t132;
    gm_f_text_ls49dp6((void *)&t132, t131);
    gm_b0_1 t133 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t129, t130, &t132);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t134 = (void *)((uint8_t *)&F.l15);
    gm_t8vuock3jnv59 t135;
    gm_f_value_pkuqal3((void *)&t135, t134);
    gm_t9l835tjp727s t136;
    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
    ((uint8_t *)&t136)[0] = 0;
    GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t136 + 8), t135);
    memmove(gm_out, ((uint8_t *)&t136), 32);
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
__attribute__((aligned(4))) void gm_f_references_all_9v25v88(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3) {
    struct gm_fr_references_all_9v25v88 F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    uint8_t gm_l3; /* include_declaration */
    gm_tf361jk9mn3il gm_l4; /* cx */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t5hdnnftkup6m gm_l10; /* u */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_t8vuock3jnv59 gm_l15; /* l */
    GM_STACK_CHECK(&F, 823u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tmkrnkep1qbgl gm_l16;
    gm_tdi3m8srnr9m6 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_t3ph3kbiqt1ah gm_l20;
    gm_tftb1odc739t5 gm_l21;
    void * t14 = gm_l0;
    uint32_t t15 = gm_l1;
    uint32_t t16 = gm_l2;
    gm_f_declaration_at_lquhblp((void *)&gm_l16, (void *)NULL, cx, NULL, t14, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (t17) {
        memmove(((uint8_t *)&gm_l17), (((uint8_t *)&gm_l16) + 8), 64);
    } else {
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, ((uint8_t *)t18));
        void * t20 = gm_l0;
        gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t20) + 48));
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 56));
        gm_tf361jk9mn3il t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t24 + 0), t19);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 8), t21);
        GM_ST(void *, ((uint8_t *)&t24 + 16), t23);
        gm_l4 = t24;
        void * t25 = (void *)((uint8_t *)&gm_l4);
        uint32_t t26 = gm_l1;
        uint32_t t27 = gm_l2;
        uint8_t t28 = gm_l3;
        gm_f_references_pd7nt1t((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t25, t26, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l17), 64);
    void * t29 = gm_l0;
    void * t30 = (void *)((uint8_t *)&F.l5);
    gm_td7pkamr64l0c t31;
    gm_f_find_m559mi8((void *)&t31, (void *)NULL, cx, NULL, t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t31;
    gm_trqfvmjtt4sij t32;
    gm_f_list_0hnol3g((void *)&t32, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t32;
    uint8_t t33 = gm_l3;
    if (t33) {
        void * t34 = (void *)((uint8_t *)&F.l7);
        gm_tftb1odc739t5 t35 = gm_f_push_drp4m1b(t34, &(*(gm_t5hdnnftkup6m *)(((uint8_t *)&F.l5) + 16)));
        gm_l18 = t35;
        uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcn13n69hd0cht, 114, 30);
    }
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_l8 = t37;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t38 = gm_l9;
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t40 = (t38 < t39);
            if (!t40) {
                goto gm_lb1;
            }
            {
                void * t41 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t43 = gm_l9;
                {
                    {
                        memmove(((uint8_t *)&gm_l10), (((uint8_t *)t41) + (uintptr_t)t43 * 24), 24);
                        void * t44 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t45 = gm_f_push_drp4m1b(t44, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&gm_l10)));
                        gm_l19 = t45;
                        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcn13n69hd0cht, 115, 33);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 115, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l9;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l9 = t50;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t51;
    gm_f_list_0202sdh_4((void *)&t51, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l11 = t51;
    gm_l13 = ((uint64_t)0ULL);
    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
    gm_l12 = t52;
    {
        for (;;) {
            uint64_t t53 = gm_l13;
            uint64_t t54 = gm_l12;
            uint8_t t55 = (t53 < t54);
            if (!t55) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t56 = gm_l13;
                        gm_l14 = t56;
                        {
                            {
                                void * t57 = gm_l0;
                                void * t58 = (void *)((uint8_t *)&F.l6);
                                void * t59 = GM_LD(void *, ((uint8_t *)&F.l7));
                                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                uint64_t t61 = gm_l14;
                                if ((uintptr_t)t61 >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)t61, t60, gm_srcn13n69hd0cht, 118, 42);
                                void * t62 = (void *)(((uint8_t *)t59) + (uintptr_t)t61 * 24);
                                gm_f_location_in_v59u1gt((void *)&gm_l20, (void *)NULL, cx, NULL, t57, t58, t62);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l20))[24] != 0);
                                if (!t63) {
                                    goto gm_lb12;
                                }
                                memmove(((uint8_t *)&gm_l15), ((uint8_t *)&gm_l20), 24);
                                void * t64 = (void *)((uint8_t *)&F.l11);
                                gm_tftb1odc739t5 t65 = gm_f_push_0pgacsj_4(t64, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l15)));
                                gm_l21 = t65;
                                uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcn13n69hd0cht, 118, 59);
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 117, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t67 = gm_l13;
            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
            uint64_t t70 = r68;
            gm_l13 = t70;
        }
    }
gm_lb6:;
    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
    gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t72);
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
__attribute__((aligned(4))) void gm_f_rename_4pp7bs8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3) {
    struct gm_fr_rename_4pp7bs8 F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l3; /* new_name */
    gm_b16_8 gm_l4; /* msg */
    gm_b16_8 gm_l6; /* msg */
    gm_b16_8 gm_l9; /* msg */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t5hdnnftkup6m gm_l15; /* u */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t5hdnnftkup6m gm_l18; /* loc */
    gm_th2lqbo77pvsc gm_l19; /* known */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t6j6smj7qoctu gm_l22; /* te */
    gm_b16_8 gm_l23; /* body */
    gm_b16_8 gm_l24; /* t_text */
    gm_tcc1rq4n1avb2 gm_l25; /* r */
    gm_tf5srh4mp4b3i gm_l26; /* idx */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* k */
    void * gm_l34; /* fe */
    gm_b16_8 gm_l35; /* body */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    gm_t6j6smj7qoctu gm_l38; /* te */
    gm_t6j6smj7qoctu gm_l39; /* ix */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l43; /* sp */
    GM_STACK_CHECK(&F, 3934u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l46;
    gm_tmkrnkep1qbgl gm_l47;
    gm_th2lqbo77pvsc gm_l48;
    gm_tin97tt4ud0s1 gm_l49;
    gm_th2lqbo77pvsc gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    uint8_t gm_l53;
    gm_th2lqbo77pvsc gm_l54;
    gm_b16_8 gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_b16_8 gm_l58;
    gm_t736djc66po0l gm_l59;
    gm_tcc1rq4n1avb2 gm_l60;
    gm_tabb710d9mq2e gm_l61;
    uint8_t gm_l62;
    gm_tftb1odc739t5 gm_l63;
    uint64_t gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    {
        {
            {
                gm_b16_8 t49 = gm_l3;
                gm_f_check_new_name_31k8n9d((void *)&gm_l46, (void *)NULL, cx, NULL, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l46))[16] != 0);
                if (!t50) {
                    goto gm_lb2;
                }
                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l46));
                gm_l4 = t51;
                gm_b16_8 t52 = gm_l4;
                gm_t9l835tjp727s t53;
                GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t53 + 24, 0);
                ((uint8_t *)&t53)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 8), t52);
                memmove(gm_out, ((uint8_t *)&t53), 32);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t54 = gm_l0;
        uint32_t t55 = gm_l1;
        uint32_t t56 = gm_l2;
        gm_f_declaration_at_lquhblp((void *)&gm_l47, (void *)NULL, cx, NULL, t54, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
        if (t57) {
        } else {
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
            gm_t9l835tjp727s t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t59 + 24, 0);
            ((uint8_t *)&t59)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t59 + 8), t58);
            memmove(gm_out, ((uint8_t *)&t59), 32);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l47) + 8), 64);
        {
            {
                void * t60 = gm_l0;
                void * t61 = (void *)((uint8_t *)&F.l5);
                gm_f_check_editable_56hen53((void *)&gm_l48, (void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l48))[16] != 0);
                if (!t62) {
                    goto gm_lb4;
                }
                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l48));
                gm_l6 = t63;
                gm_b16_8 t64 = gm_l6;
                gm_t9l835tjp727s t65;
                GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t65 + 24, 0);
                ((uint8_t *)&t65)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t65 + 8), t64);
                memmove(gm_out, ((uint8_t *)&t65), 32);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_b16_8 t67 = gm_l3;
        uint8_t t68 = (gm_eq0iqiubfm7e(((uint8_t *)&t66), ((uint8_t *)&t67)));
        if (t68) {
            gm_tin97tt4ud0s1 t69;
            gm_f_obj_aoa7fl7((void *)&t69, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l7 = t69;
            void * t70 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_ssvcneuafnfds));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(7));
            gm_tin97tt4ud0s1 t72;
            gm_f_obj_aoa7fl7((void *)&t72, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l49 = t72;
            void * t73 = (void *)((uint8_t *)&gm_l49);
            gm_t8vuock3jnv59 t74;
            gm_f_value_pkuqal3((void *)&t74, t73);
            gm_b0_1 t75 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t70, t71, &t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t76 = (void *)((uint8_t *)&F.l7);
            gm_t8vuock3jnv59 t77;
            gm_f_value_pkuqal3((void *)&t77, t76);
            gm_t9l835tjp727s t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            ((uint8_t *)&t78)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t78 + 8), t77);
            memmove(gm_out, ((uint8_t *)&t78), 32);
            goto gm_fdone;
        }
        void * t79 = gm_l0;
        void * t80 = (void *)((uint8_t *)&F.l5);
        gm_td7pkamr64l0c t81;
        gm_f_find_m559mi8((void *)&t81, (void *)NULL, cx, NULL, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t81;
        {
            {
                void * t82 = gm_l0;
                void * t83 = (void *)((uint8_t *)&F.l5);
                void * t84 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t85 = gm_l3;
                gm_f_check_clashes_cqluvhl((void *)&gm_l50, (void *)NULL, cx, NULL, t82, t83, t84, t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t86 = (uint8_t)((((uint8_t *)&gm_l50))[16] != 0);
                if (!t86) {
                    goto gm_lb6;
                }
                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l50));
                gm_l9 = t87;
                gm_b16_8 t88 = gm_l9;
                gm_t9l835tjp727s t89;
                GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t89 + 24, 0);
                ((uint8_t *)&t89)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t89 + 8), t88);
                memmove(gm_out, ((uint8_t *)&t89), 32);
                goto gm_fdone;
            }
gm_lb6:;
        }
gm_lb5:;
    }
    {
        gm_trqfvmjtt4sij t90;
        gm_f_list_t5p1bue_2((void *)&t90, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t90;
        gm_trqfvmjtt4sij t91;
        gm_f_list_u1fokev((void *)&t91, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t91;
        gm_trqfvmjtt4sij t92;
        gm_f_list_0hnol3g((void *)&t92, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t92;
        void * t93 = (void *)((uint8_t *)&F.l12);
        gm_tftb1odc739t5 t94 = gm_f_push_drp4m1b(t93, &(*(gm_t5hdnnftkup6m *)(((uint8_t *)&F.l5) + 16)));
        gm_l51 = t94;
        uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
        if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcn13n69hd0cht, 220, 5);
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        gm_l13 = t96;
        gm_l14 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t97 = gm_l14;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint8_t t99 = (t97 < t98);
                if (!t99) {
                    goto gm_lb7;
                }
                {
                    void * t100 = GM_LD(void *, ((uint8_t *)&gm_l13));
                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint64_t t102 = gm_l14;
                    {
                        {
                            memmove(((uint8_t *)&gm_l15), (((uint8_t *)t100) + (uintptr_t)t102 * 24), 24);
                            void * t103 = (void *)((uint8_t *)&F.l12);
                            gm_tftb1odc739t5 t104 = gm_f_push_drp4m1b(t103, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&gm_l15)));
                            gm_l52 = t104;
                            uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srcn13n69hd0cht, 221, 33);
                            goto gm_lb10;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 221, 5);
                    }
gm_lb10:;
                }
gm_lb9:;
                uint64_t t106 = gm_l14;
                uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                uint64_t t109 = r107;
                gm_l14 = t109;
            }
        }
gm_lb7:;
        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        gm_l16 = t110;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t111 = gm_l17;
                uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t113 = (t111 < t112);
                if (!t113) {
                    goto gm_lb12;
                }
                {
                    void * t114 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t116 = gm_l17;
                    {
                        {
                            memmove(((uint8_t *)&gm_l18), (((uint8_t *)t114) + (uintptr_t)t116 * 24), 24);
                            gm_th2lqbo77pvsc t117;
                            memset(&t117, 0, sizeof t117);
                            gm_l19 = t117;
                            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                            gm_l20 = t118;
                            gm_l21 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t119 = gm_l21;
                                    uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                    uint8_t t121 = (t119 < t120);
                                    if (!t121) {
                                        goto gm_lb17;
                                    }
                                    {
                                        void * t122 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                        uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                        uint64_t t124 = gm_l21;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l22), (((uint8_t *)t122) + (uintptr_t)t124 * 32), 32);
                                                gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                                                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                                uint8_t t127 = (gm_eq0iqiubfm7e(((uint8_t *)&t125), ((uint8_t *)&t126)));
                                                if (t127) {
                                                    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 16));
                                                    gm_th2lqbo77pvsc t129;
                                                    GM_ST(uint8_t, (uint8_t *)&t129 + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t129 + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t129 + 20, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t129), t128);
                                                    ((uint8_t *)&t129)[16] = 1;
                                                    gm_l19 = t129;
                                                }
                                                goto gm_lb20;
                                            }
gm_lb21:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 224, 9);
                                        }
gm_lb20:;
                                    }
gm_lb19:;
                                    uint64_t t130 = gm_l21;
                                    uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                                    uint64_t t133 = r131;
                                    gm_l21 = t133;
                                }
                            }
gm_lb17:;
                            {
                                {
                                    uint8_t t134 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                                    if (t134) {
                                        goto gm_lb23;
                                    }
                                    gm_l53 = ((uint8_t)1);
                                    goto gm_lb22;
                                }
gm_lb23:;
                                {
                                    gm_l53 = ((uint8_t)0);
                                    goto gm_lb22;
                                }
gm_lb24:;
                                gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 227, 12);
                            }
gm_lb22:;
                            uint8_t t135 = gm_l53;
                            if (t135) {
                                void * t136 = gm_l0;
                                void * t137 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                gm_f_text_of_qs06tq9((void *)&gm_l54, (void *)NULL, cx, NULL, t136, t137, t138);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t139 = (uint8_t)((((uint8_t *)&gm_l54))[16] != 0);
                                if (t139) {
                                    gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l54));
                                    gm_l55 = t140;
                                } else {
                                    gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                    gm_tnpifv20irnph t142;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t142 + 0), t141);
                                    gm_f_format_guqevu2((void *)&gm_l56, (void *)NULL, cx, NULL, t142);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                    if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_srcn13n69hd0cht, 228, 64);
                                    gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                                    gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                                    gm_t9l835tjp727s t146;
                                    GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t146 + 24, 0);
                                    ((uint8_t *)&t146)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t146 + 8), t145);
                                    memmove(gm_out, ((uint8_t *)&t146), 32);
                                    goto gm_fdone;
                                }
                                gm_b16_8 t147 = gm_l55;
                                gm_l23 = t147;
                                void * t148 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                gm_b16_8 t150 = gm_l23;
                                gm_t6j6smj7qoctu t151;
                                GM_ST(gm_b16_8, ((uint8_t *)&t151 + 0), t149);
                                GM_ST(gm_b16_8, ((uint8_t *)&t151 + 16), t150);
                                gm_tftb1odc739t5 t152 = gm_f_push_5gfgdpl(t148, &t151);
                                gm_l57 = t152;
                                uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcn13n69hd0cht, 229, 13);
                                gm_b16_8 t154 = gm_l23;
                                gm_th2lqbo77pvsc t155;
                                GM_ST(uint8_t, (uint8_t *)&t155 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t155 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t155 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t155), t154);
                                ((uint8_t *)&t155)[16] = 1;
                                gm_l19 = t155;
                            }
                            uint8_t t156 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                            if (t156) {
                                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
                                gm_l58 = t157;
                            } else {
                                gm_b16_8 t158;
                                GM_ST(void *, (uint8_t *)&t158, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(0));
                                gm_l58 = t158;
                            }
                            gm_b16_8 t159 = gm_l58;
                            gm_l24 = t159;
                            gm_b16_8 t160 = gm_l24;
                            uint32_t t161 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                            uint32_t t162 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 20));
                            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                            gm_t736djc66po0l t164 = gm_f_narrow_5epj0dh(t160, t161, t162, t163);
                            gm_l59 = t164;
                            uint8_t t165 = (uint8_t)((((uint8_t *)&gm_l59))[8] != 0);
                            if (t165) {
                                gm_tcc1rq4n1avb2 t166 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&gm_l59));
                                gm_l60 = t166;
                            } else {
                                gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                                gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                gm_b16_8 t169 = gm_l24;
                                uint32_t t170 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                                uint64_t t171 = gm_f_line_of_n80f3n1(t169, t170);
                                gm_tftksr4op114e t172;
                                GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t167);
                                GM_ST(gm_b16_8, ((uint8_t *)&t172 + 16), t168);
                                GM_ST(uint64_t, ((uint8_t *)&t172 + 32), t171);
                                gm_f_format_7jgfnob((void *)&gm_l61, (void *)NULL, cx, NULL, &t172);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_srcn13n69hd0cht, 234, 21);
                                gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                                gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                                gm_t9l835tjp727s t176;
                                GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t176 + 24, 0);
                                ((uint8_t *)&t176)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t176 + 8), t175);
                                memmove(gm_out, ((uint8_t *)&t176), 32);
                                goto gm_fdone;
                            }
                            gm_tcc1rq4n1avb2 t177 = gm_l60;
                            gm_l25 = t177;
                            gm_tf5srh4mp4b3i t178;
                            memset(&t178, 0, sizeof t178);
                            gm_l26 = t178;
                            gm_l28 = ((uint64_t)0ULL);
                            uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                            gm_l27 = t179;
                            {
                                for (;;) {
                                    uint64_t t180 = gm_l28;
                                    uint64_t t181 = gm_l27;
                                    uint8_t t182 = (t180 < t181);
                                    if (!t182) {
                                        goto gm_lb25;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t183 = gm_l28;
                                                gm_l29 = t183;
                                                void * t184 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                uint64_t t186 = gm_l29;
                                                if ((uintptr_t)t186 >= (uintptr_t)(t185)) gm_trap_index((uintptr_t)t186, t185, gm_srcn13n69hd0cht, 238, 16);
                                                gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)t184) + (uintptr_t)t186 * 56));
                                                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                                uint8_t t189 = (gm_eq0iqiubfm7e(((uint8_t *)&t187), ((uint8_t *)&t188)));
                                                if (t189) {
                                                    uint64_t t190 = gm_l29;
                                                    gm_tf5srh4mp4b3i t191;
                                                    GM_ST(uint8_t, (uint8_t *)&t191 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t191 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t191 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t191), t190);
                                                    ((uint8_t *)&t191)[8] = 1;
                                                    gm_l26 = t191;
                                                }
                                                goto gm_lb28;
                                            }
gm_lb29:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 237, 9);
                                        }
gm_lb28:;
                                    }
gm_lb27:;
                                    uint64_t t192 = gm_l28;
                                    uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                                    uint64_t t195 = r193;
                                    gm_l28 = t195;
                                }
                            }
gm_lb25:;
                            {
                                {
                                    uint8_t t196 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
                                    if (t196) {
                                        goto gm_lb31;
                                    }
                                    gm_l62 = ((uint8_t)1);
                                    goto gm_lb30;
                                }
gm_lb31:;
                                {
                                    gm_l62 = ((uint8_t)0);
                                    goto gm_lb30;
                                }
gm_lb32:;
                                gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 240, 12);
                            }
gm_lb30:;
                            uint8_t t197 = gm_l62;
                            if (t197) {
                                void * t198 = (void *)((uint8_t *)&F.l10);
                                gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                gm_trqfvmjtt4sij t200;
                                gm_f_list_6u1pehv((void *)&t200, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tl7hpf5nlilqq t201;
                                GM_ST(gm_b16_8, ((uint8_t *)&t201 + 0), t199);
                                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t201 + 16), t200);
                                gm_tftb1odc739t5 t202 = gm_f_push_sjirrjh(t198, &t201);
                                gm_l63 = t202;
                                uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srcn13n69hd0cht, 241, 13);
                                uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                uint64_t r205; bool o206 = __builtin_sub_overflow(t204, ((uint64_t)1ULL), &r205);
                                if (o206) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 242, 19);
                                uint64_t t207 = r205;
                                gm_tf5srh4mp4b3i t208;
                                GM_ST(uint8_t, (uint8_t *)&t208 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t208 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t208 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t208), t207);
                                ((uint8_t *)&t208)[8] = 1;
                                gm_l26 = t208;
                            }
                            void * t209 = GM_LD(void *, ((uint8_t *)&F.l10));
                            uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                            uint8_t t211 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
                            if (t211) {
                                uint64_t t212 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                                gm_l64 = t212;
                            } else {
                                gm_l64 = ((uint64_t)0ULL);
                            }
                            uint64_t t213 = gm_l64;
                            if ((uintptr_t)t213 >= (uintptr_t)(t210)) gm_trap_index((uintptr_t)t213, t210, gm_srcn13n69hd0cht, 244, 9);
                            void * t214 = (void *)((((uint8_t *)t209) + (uintptr_t)t213 * 56) + 16);
                            uint32_t t215 = GM_LD(uint32_t, ((uint8_t *)&gm_l25));
                            uint32_t t216 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 4));
                            gm_tcc1rq4n1avb2 t217;
                            GM_ST(uint32_t, ((uint8_t *)&t217 + 0), t215);
                            GM_ST(uint32_t, ((uint8_t *)&t217 + 4), t216);
                            gm_tftb1odc739t5 t218 = gm_f_push_1prlsnl(t214, GM_CONV(gm_cs9ufftihtbpo2, t217));
                            gm_l65 = t218;
                            uint8_t t219 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                            if (!t219) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srcn13n69hd0cht, 244, 9);
                            goto gm_lb15;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 222, 5);
                    }
gm_lb15:;
                }
gm_lb14:;
                uint64_t t220 = gm_l17;
                uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                uint64_t t223 = r221;
                gm_l17 = t223;
            }
        }
gm_lb12:;
    }
    {
        gm_tin97tt4ud0s1 t224;
        gm_f_obj_aoa7fl7((void *)&t224, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l30 = t224;
        gm_l32 = ((uint64_t)0ULL);
        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
        gm_l31 = t225;
        {
            for (;;) {
                uint64_t t226 = gm_l32;
                uint64_t t227 = gm_l31;
                uint8_t t228 = (t226 < t227);
                if (!t228) {
                    goto gm_lb33;
                }
                {
                    {
                        {
                            uint64_t t229 = gm_l32;
                            gm_l33 = t229;
                            void * t230 = GM_LD(void *, ((uint8_t *)&F.l10));
                            uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                            uint64_t t232 = gm_l33;
                            if ((uintptr_t)t232 >= (uintptr_t)(t231)) gm_trap_index((uintptr_t)t232, t231, gm_srcn13n69hd0cht, 248, 16);
                            void * t233 = (void *)(((uint8_t *)t230) + (uintptr_t)t232 * 56);
                            gm_l34 = t233;
                            void * t234 = gm_l34;
                            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)t234) + 16));
                            gm_closure t236 = ((gm_closure){GM_PTR_TAG(gm_f_fn_o57h82n), NULL, cx});
                            gm_f_sort_by_m9qke1u(t235, t236);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t237 = gm_l34;
                            void * t238 = (void *)(((uint8_t *)t237) + 16);
                            void * t239 = gm_l34;
                            gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)t239) + 16));
                            uint64_t t241 = gm_f_dedup_8ks2cpi(t240);
                            gm_f_truncate_tgh982e(t238, t241);
                            gm_b16_8 t242;
                            GM_ST(void *, (uint8_t *)&t242, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(0));
                            gm_l35 = t242;
                            gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                            gm_l36 = t243;
                            gm_l37 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t244 = gm_l37;
                                    uint64_t t245 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                    uint8_t t246 = (t244 < t245);
                                    if (!t246) {
                                        goto gm_lb38;
                                    }
                                    {
                                        void * t247 = GM_LD(void *, ((uint8_t *)&gm_l36));
                                        uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                        uint64_t t249 = gm_l37;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l38), (((uint8_t *)t247) + (uintptr_t)t249 * 32), 32);
                                                gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l38));
                                                void * t251 = gm_l34;
                                                gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)t251));
                                                uint8_t t253 = (gm_eq0iqiubfm7e(((uint8_t *)&t250), ((uint8_t *)&t252)));
                                                if (t253) {
                                                    gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 16));
                                                    gm_l35 = t254;
                                                }
                                                goto gm_lb41;
                                            }
gm_lb42:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 252, 9);
                                        }
gm_lb41:;
                                    }
gm_lb40:;
                                    uint64_t t255 = gm_l37;
                                    uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                                    uint64_t t258 = r256;
                                    gm_l37 = t258;
                                }
                            }
gm_lb38:;
                            gm_b16_8 t259 = gm_l35;
                            gm_t6j6smj7qoctu t260;
                            gm_f_line_index_2necgtf((void *)&t260, (void *)NULL, cx, NULL, t259);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l39 = t260;
                            gm_trqfvmjtt4sij t261;
                            gm_f_list_0202sdh_4((void *)&t261, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l40 = t261;
                            void * t262 = gm_l34;
                            gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)t262) + 16));
                            gm_l41 = t263;
                            gm_l42 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t264 = gm_l42;
                                    uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                    uint8_t t266 = (t264 < t265);
                                    if (!t266) {
                                        goto gm_lb43;
                                    }
                                    {
                                        void * t267 = GM_LD(void *, ((uint8_t *)&gm_l41));
                                        uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                        uint64_t t269 = gm_l42;
                                        {
                                            {
                                                gm_tcc1rq4n1avb2 t270 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)t267) + (uintptr_t)t269 * 8));
                                                gm_l43 = t270;
                                                gm_tin97tt4ud0s1 t271;
                                                gm_f_obj_aoa7fl7((void *)&t271, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.l44 = t271;
                                                void * t272 = (void *)((uint8_t *)&F.l44);
                                                gm_b16_8 t273;
                                                GM_ST(void *, (uint8_t *)&t273, (void *)(gm_sl4ns6hobicht));
                                                GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(5));
                                                void * t274 = (void *)((uint8_t *)&gm_l39);
                                                uint32_t t275 = GM_LD(uint32_t, ((uint8_t *)&gm_l43));
                                                uint64_t t276 = (uint64_t)t275;
                                                uint32_t t277 = GM_LD(uint32_t, (((uint8_t *)&gm_l43) + 4));
                                                uint64_t t278 = (uint64_t)t277;
                                                void * t279 = gm_l0;
                                                gm_t9qkh0rsooc8d t280 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t279) + 48));
                                                gm_t8vuock3jnv59 t281;
                                                gm_f_range_json_hlsolbo((void *)&t281, (void *)NULL, cx, NULL, t274, t276, t278, t280);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_b0_1 t282 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t272, t273, &t281);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t283 = (void *)((uint8_t *)&F.l44);
                                                gm_b16_8 t284;
                                                GM_ST(void *, (uint8_t *)&t284, (void *)(gm_sirpcjgkmlehr));
                                                GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(7));
                                                gm_b16_8 t285 = gm_l3;
                                                gm_t8vuock3jnv59 t286;
                                                gm_f_text_ls49dp6((void *)&t286, t285);
                                                gm_b0_1 t287 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t283, t284, &t286);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t288 = (void *)((uint8_t *)&F.l40);
                                                void * t289 = (void *)((uint8_t *)&F.l44);
                                                gm_t8vuock3jnv59 t290;
                                                gm_f_value_pkuqal3((void *)&t290, t289);
                                                gm_tftb1odc739t5 t291 = gm_f_push_0pgacsj_4(t288, &t290);
                                                gm_l66 = t291;
                                                uint8_t t292 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                                if (!t292) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srcn13n69hd0cht, 261, 13);
                                                goto gm_lb46;
                                            }
gm_lb47:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 257, 9);
                                        }
gm_lb46:;
                                    }
gm_lb45:;
                                    uint64_t t293 = gm_l42;
                                    uint64_t r294; bool o295 = __builtin_add_overflow(t293, ((uint64_t)1ULL), &r294);
                                    uint64_t t296 = r294;
                                    gm_l42 = t296;
                                }
                            }
gm_lb43:;
                            void * t297 = (void *)((uint8_t *)&F.l30);
                            void * t298 = gm_l0;
                            void * t299 = GM_LD(void *, (((uint8_t *)t298) + 56));
                            void * t300 = gm_l34;
                            gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)t300));
                            gm_b16_8 t302 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t299, t301);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&F.l40));
                            gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
                            gm_t8vuock3jnv59 t305;
                            gm_f_array_k32ana3((void *)&t305, (void *)NULL, cx, NULL, t304);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b0_1 t306 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t297, t302, &t305);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb36;
                        }
gm_lb37:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 247, 5);
                    }
gm_lb36:;
                }
gm_lb35:;
                uint64_t t307 = gm_l32;
                uint64_t r308; bool o309 = __builtin_add_overflow(t307, ((uint64_t)1ULL), &r308);
                uint64_t t310 = r308;
                gm_l32 = t310;
            }
        }
gm_lb33:;
    }
    {
        gm_tin97tt4ud0s1 t311;
        gm_f_obj_aoa7fl7((void *)&t311, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l45 = t311;
        void * t312 = (void *)((uint8_t *)&F.l45);
        gm_b16_8 t313;
        GM_ST(void *, (uint8_t *)&t313, (void *)(gm_ssvcneuafnfds));
        GM_ST(uintptr_t, (uint8_t *)&t313 + 8, (uintptr_t)(7));
        void * t314 = (void *)((uint8_t *)&F.l30);
        gm_t8vuock3jnv59 t315;
        gm_f_value_pkuqal3((void *)&t315, t314);
        gm_b0_1 t316 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t312, t313, &t315);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t317 = (void *)((uint8_t *)&F.l45);
        gm_t8vuock3jnv59 t318;
        gm_f_value_pkuqal3((void *)&t318, t317);
        gm_t9l835tjp727s t319;
        GM_ST(uint8_t, (uint8_t *)&t319 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t319 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t319 + 4, 0);
        ((uint8_t *)&t319)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t319 + 8), t318);
        memmove(gm_out, ((uint8_t *)&t319), 32);
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
inline __attribute__((aligned(4))) void gm_f_text_of_qs06tq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_text_of_qs06tq9 F;
    void * gm_l0; /* rq */
    void * gm_l1; /* found */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* t */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* o */
    gm_b16_8 gm_l7; /* bytes */
    GM_STACK_CHECK(&F, 548u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l8;
    gm_tgvh15qshf643 gm_l9;
    gm_b16_8 gm_l10;
    {
        {
            void * t9 = gm_l1;
            gm_b16_8 t10 = gm_l2;
            gm_f_found_text_sajr2an((void *)&gm_l8, t9, t10);
            uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
            if (!t11) {
                goto gm_lb2;
            }
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
            gm_l3 = t12;
            gm_b16_8 t13 = gm_l3;
            gm_th2lqbo77pvsc t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t14), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = gm_l0;
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 32));
    gm_l4 = t16;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l5;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb3;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t22 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t20) + (uintptr_t)t22 * 32), 32);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                        gm_b16_8 t24 = gm_l2;
                        uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
                        if (t25) {
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                            gm_th2lqbo77pvsc t27;
                            GM_ST(uint8_t, (uint8_t *)&t27 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t27 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t27 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t27), t26);
                            ((uint8_t *)&t27)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t27), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 88, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t28 = gm_l5;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l5 = t31;
        }
    }
gm_lb3:;
    gm_b16_8 t32 = gm_l2;
    gm_f_read_all_r9rh9qd((void *)&gm_l9, (void *)NULL, cx, NULL, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t33) {
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_l10 = t34;
    } else {
        gm_th2lqbo77pvsc t35;
        memset(&t35, 0, sizeof t35);
        memmove(gm_out, ((uint8_t *)&t35), 24);
        goto gm_fdone;
    }
    gm_b16_8 t36 = gm_l10;
    gm_l7 = t36;
    gm_b16_8 t37 = gm_l7;
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
    gm_th2lqbo77pvsc t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t39), t38);
    ((uint8_t *)&t39)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t39), 24);
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
