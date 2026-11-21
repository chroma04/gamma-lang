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
typedef struct gm_csqbqksqco2bm6 { uint32_t f0; void * f1; } gm_csqbqksqco2bm6;
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
_Static_assert(sizeof(gm_csqbqksqco2bm6) == 16 && _Alignof(gm_csqbqksqco2bm6) == 8 && offsetof(gm_csqbqksqco2bm6, f0) == 0 && offsetof(gm_csqbqksqco2bm6, f1) == 8, "layout of `FieldPat`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
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
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
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
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
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
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; void * f4; } gm_t150deeftog49;
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t150deeftog49) == 96 && _Alignof(gm_t150deeftog49) == 8 && offsetof(gm_t150deeftog49, f0) == 0 && offsetof(gm_t150deeftog49, f1) == 40 && offsetof(gm_t150deeftog49, f2) == 56 && offsetof(gm_t150deeftog49, f3) == 72 && offsetof(gm_t150deeftog49, f4) == 88, "layout of `WriteExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
struct gm_fr_clone_block_5tu7v2v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* stmts */
    gm_trqfvmjtt4sij l14; /* out */
    void * ret;
};
struct gm_fr_clone_conds_ljrasoa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_clone_expr_af0u1qc {
    gm_frame h;
    gm_tsth9qu1dv0g3 l25; /* callee */
    gm_tsth9qu1dv0g3 l27; /* c */
    gm_trqfvmjtt4sij l43; /* out */
    gm_t710lsrq1m8gk l52; /* x */
    gm_th1g69d7vpipg l61; /* sink */
    gm_th1g69d7vpipg l64; /* sink */
    gm_t710lsrq1m8gk l66; /* w */
    gm_trqfvmjtt4sij l67; /* hs */
    gm_t2b6tm8c0vgo1 l74; /* k */
    void * ret;
};
struct gm_fr_clone_exprs_umh77j3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_clone_opt_plt4ceg {
    gm_frame h;
    void * ret;
};
struct gm_fr_clone_pat_cq2uu68 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* out */
    gm_trqfvmjtt4sij l14; /* out */
    gm_tb38v5s9j1r32 l18; /* k */
    void * ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qlbp545 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uda0n0p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_i33ccm0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nh3d5t2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p0pjtrh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
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
struct gm_fr_push_31airv7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f5fsh94 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u2jka7o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_94j8fj1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_br5lm9l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j7fb9h1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_sqitcho {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
__attribute__((aligned(4))) void * gm_f_clone_block_5tu7v2v(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_clone_conds_ljrasoa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_clone_exprs_umh77j3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clone_pat_cq2uu68(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uda0n0p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_i33ccm0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ges19gd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_31airv7(void * a0, gm_csqbqksqco2bm6 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_br5lm9l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2);
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_kg8c0kikv62[40] __attribute__((aligned(8)));
extern uint8_t gm_kl8b626vj63[40] __attribute__((aligned(8)));
extern uint8_t gm_kmlih2j5ae5[40] __attribute__((aligned(8)));
extern uint8_t gm_ksksp29ghso[40] __attribute__((aligned(8)));
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgksioc7kn741[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_opt_plt4ceg F;
    void * gm_l0; /* e */
    void * gm_l1; /* x */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l2;
    uint8_t t2 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l0)) != 0);
    if (t2) {
        void * t3 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t3;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t4 = gm_l2;
    gm_l1 = t4;
    void * t5 = gm_l1;
    void * t6 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qlbp545 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3v2gjd4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nh3d5t2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_rtces00();
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
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p0pjtrh F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qlbp545();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lmbdkhb_2();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_2 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3v2gjd4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h9tbucc_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_vj3ckdj_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h08har5_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)&gm_l1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp_2 F;
    void * gm_l0; /* l */
    gm_tlr9dp305htm7 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_lmbdkhb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4bta8ne_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f5fsh94 F;
    void * gm_l0; /* l */
    gm_t4lnf71mruqpt gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_qlbp545();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_sqitcho(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)&gm_l1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_8q3kspo();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j7fb9h1(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u2jka7o F;
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
        gm_b16_8 t9 = gm_f_empty_slice_rtces00();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_94j8fj1(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_94j8fj1 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j7fb9h1 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_sqitcho F;
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
__attribute__((aligned(4))) void * gm_f_clone_block_5tu7v2v(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_block_5tu7v2v F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    uint32_t gm_l5; /* l */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* x */
    gm_b16_8 gm_l8; /* ls */
    void * gm_l9; /* x */
    void * gm_l10; /* p */
    void * gm_l11; /* v */
    gm_b16_8 gm_l12; /* ps */
    void * gm_l13; /* v */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* p */
    void * gm_l18; /* x */
    uint8_t gm_l19; /* is_err */
    void * gm_l20; /* x */
    GM_STACK_CHECK(&F, 1314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tq7s98ihonkju gm_l29;
    gm_trqfvmjtt4sij t20;
    gm_f_list_ir0h3tc_2((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t20;
    void * t21 = gm_l0;
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)t21));
    gm_l2 = t22;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l3;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t28 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t26) + (uintptr_t)t28 * 32), 32);
                        {
                            {
                                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t30 = (uint8_t)(t29 == ((uint8_t)0ULL));
                                if (!t30) {
                                    goto gm_lb7;
                                }
                                uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t31;
                                uint8_t t32 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0);
                                if (t32) {
                                    goto gm_lb7;
                                }
                                void * t33 = (void *)((uint8_t *)&F.l1);
                                uint32_t t34 = gm_l5;
                                gm_ttggq7ng4kp3p t35;
                                GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t35 + 0), t34);
                                GM_ST(void *, ((uint8_t *)&t35 + 8), ((void *)0));
                                gm_trcmmd12cefb1 t36;
                                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t36 + 8), t35);
                                gm_tftb1odc739t5 t37 = gm_f_push_d6c76ud_2(t33, &t36);
                                gm_l21 = t37;
                                uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcgksioc7kn741, 90, 30);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                                if (!t40) {
                                    goto gm_lb8;
                                }
                                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t41;
                                uint8_t t42 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0);
                                if (!t42) {
                                    goto gm_lb8;
                                }
                                void * t43 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t43;
                                void * t44 = (void *)((uint8_t *)&F.l1);
                                uint32_t t45 = gm_l6;
                                void * t46 = gm_l7;
                                void * t47 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t46);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_ttggq7ng4kp3p t48;
                                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t45);
                                GM_ST(void *, ((uint8_t *)&t48 + 8), t47);
                                gm_trcmmd12cefb1 t49;
                                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t49 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t49 + 8), t48);
                                gm_tftb1odc739t5 t50 = gm_f_push_d6c76ud_2(t44, &t49);
                                gm_l22 = t50;
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcgksioc7kn741, 91, 27);
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t53 = (uint8_t)(t52 == ((uint8_t)1ULL));
                                if (!t53) {
                                    goto gm_lb9;
                                }
                                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l8 = t54;
                                void * t55 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l9 = t55;
                                void * t56 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t57 = gm_l8;
                                void * t58 = gm_l9;
                                void * t59 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t58);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tpe1hv3fekn05 t60;
                                GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t57);
                                GM_ST(void *, ((uint8_t *)&t60 + 16), t59);
                                gm_trcmmd12cefb1 t61;
                                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t61 + 8), t60);
                                gm_tftb1odc739t5 t62 = gm_f_push_d6c76ud_2(t56, &t61);
                                gm_l23 = t62;
                                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcgksioc7kn741, 92, 33);
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
                                if (!t65) {
                                    goto gm_lb10;
                                }
                                void * t66 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l10 = t66;
                                void * t67 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l11 = t67;
                                void * t68 = (void *)((uint8_t *)&F.l1);
                                void * t69 = gm_l10;
                                void * t70 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t69);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t71 = gm_l11;
                                void * t72 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t71);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t3mf58gv9kf3n t73;
                                GM_ST(void *, ((uint8_t *)&t73 + 0), t70);
                                GM_ST(void *, ((uint8_t *)&t73 + 8), t72);
                                gm_trcmmd12cefb1 t74;
                                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t74 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
                                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t74 + 8), t73);
                                gm_tftb1odc739t5 t75 = gm_f_push_d6c76ud_2(t68, &t74);
                                gm_l24 = t75;
                                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcgksioc7kn741, 93, 30);
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t78 = (uint8_t)(t77 == ((uint8_t)3ULL));
                                if (!t78) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l12 = t79;
                                void * t80 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l13 = t80;
                                gm_trqfvmjtt4sij t81;
                                gm_f_list_pmp06kq((void *)&t81, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l14 = t81;
                                gm_b16_8 t82 = gm_l12;
                                gm_l15 = t82;
                                gm_l16 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t83 = gm_l16;
                                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint8_t t85 = (t83 < t84);
                                        if (!t85) {
                                            goto gm_lb12;
                                        }
                                        {
                                            void * t86 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                            uint64_t t88 = gm_l16;
                                            {
                                                {
                                                    void * t89 = GM_LD(void *, (((uint8_t *)t86) + (uintptr_t)t88 * 8));
                                                    gm_l17 = t89;
                                                    void * t90 = (void *)((uint8_t *)&F.l14);
                                                    void * t91 = gm_l17;
                                                    void * t92 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t91);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tftb1odc739t5 t93 = gm_f_push_r2k6rjf(t90, t92);
                                                    gm_l25 = t93;
                                                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                                    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcgksioc7kn741, 96, 31);
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 96, 17);
                                            }
gm_lb15:;
                                        }
gm_lb14:;
                                        uint64_t t95 = gm_l16;
                                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                                        uint64_t t98 = r96;
                                        gm_l16 = t98;
                                    }
                                }
gm_lb12:;
                                void * t99 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                void * t101 = gm_l13;
                                void * t102 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t101);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tpe1hv3fekn05 t103;
                                GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t100);
                                GM_ST(void *, ((uint8_t *)&t103 + 16), t102);
                                gm_trcmmd12cefb1 t104;
                                GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)3ULL));
                                GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t104 + 8), t103);
                                gm_tftb1odc739t5 t105 = gm_f_push_d6c76ud_2(t99, &t104);
                                gm_l26 = t105;
                                uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcgksioc7kn741, 97, 17);
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t108 = (uint8_t)(t107 == ((uint8_t)4ULL));
                                if (!t108) {
                                    goto gm_lb17;
                                }
                                void * t109 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l18 = t109;
                                void * t110 = (void *)((uint8_t *)&F.l1);
                                void * t111 = gm_l18;
                                void * t112 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t111);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_trcmmd12cefb1 t113;
                                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t113 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t113 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t113 + 8), t112);
                                gm_tftb1odc739t5 t114 = gm_f_push_d6c76ud_2(t110, &t113);
                                gm_l27 = t114;
                                uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcgksioc7kn741, 99, 25);
                                goto gm_lb6;
                            }
gm_lb17:;
                            {
                                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t117 = (uint8_t)(t116 == ((uint8_t)5ULL));
                                if (!t117) {
                                    goto gm_lb18;
                                }
                                uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l19 = t118;
                                void * t119 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l20 = t119;
                                void * t120 = (void *)((uint8_t *)&F.l1);
                                uint8_t t121 = gm_l19;
                                void * t122 = gm_l20;
                                void * t123 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t122);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tgtub0v94d5u5 t124;
                                GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t124 + 0), t121);
                                GM_ST(void *, ((uint8_t *)&t124 + 8), t123);
                                gm_trcmmd12cefb1 t125;
                                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)5ULL));
                                GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t125 + 8), t124);
                                gm_tftb1odc739t5 t126 = gm_f_push_d6c76ud_2(t120, &t125);
                                gm_l28 = t126;
                                uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcgksioc7kn741, 100, 34);
                                goto gm_lb6;
                            }
gm_lb18:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 89, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 88, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t128 = gm_l3;
            uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
            uint64_t t131 = r129;
            gm_l3 = t131;
        }
    }
gm_lb1:;
    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    void * t133 = gm_l0;
    void * t134 = GM_LD(void *, (((uint8_t *)t133) + 16));
    void * t135 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t134);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t136 = gm_l0;
    gm_tl874t2atnlh3 t137 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t136) + 24));
    gm_tgabaikr08mjg t138;
    GM_ST(uint32_t, (uint8_t *)&t138 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t132);
    GM_ST(void *, ((uint8_t *)&t138 + 16), t135);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t138 + 24), t137);
    gm_tq7s98ihonkju t139 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t138, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmlih2j5ae5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l29 = t139;
    uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcgksioc7kn741, 103, 12);
    void * t141 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
    F.ret = t141;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clone_conds_ljrasoa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_clone_conds_ljrasoa F;
    gm_b16_8 gm_l0; /* cond */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 415u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t10;
    gm_f_list_v5d1o9b_2((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t10;
    gm_b16_8 t11 = gm_l0;
    gm_l2 = t11;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l3;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t17 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t15) + (uintptr_t)t17 * 24), 24);
                        {
                            {
                                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t20;
                                void * t21 = (void *)((uint8_t *)&F.l1);
                                void * t22 = gm_l5;
                                void * t23 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t22);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tlr9dp305htm7 t24;
                                GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
                                GM_ST(void *, ((uint8_t *)&t24 + 8), t23);
                                gm_tftb1odc739t5 t25 = gm_f_push_eb7kbhp_2(t21, &t24);
                                gm_l8 = t25;
                                uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcgksioc7kn741, 110, 25);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                                if (!t28) {
                                    goto gm_lb8;
                                }
                                void * t29 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t29;
                                void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t30;
                                void * t31 = (void *)((uint8_t *)&F.l1);
                                void * t32 = gm_l6;
                                void * t33 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t32);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t34 = gm_l7;
                                void * t35 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t34);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t3mf58gv9kf3n t36;
                                GM_ST(void *, ((uint8_t *)&t36 + 0), t33);
                                GM_ST(void *, ((uint8_t *)&t36 + 8), t35);
                                gm_tlr9dp305htm7 t37;
                                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t37 + 8), t36);
                                gm_tftb1odc739t5 t38 = gm_f_push_eb7kbhp_2(t31, &t37);
                                gm_l9 = t38;
                                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcgksioc7kn741, 111, 29);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 109, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 108, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t40 = gm_l3;
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            uint64_t t43 = r41;
            gm_l3 = t43;
        }
    }
gm_lb1:;
    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    F.ret = t44;
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
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_expr_af0u1qc F;
    void * gm_l0; /* e */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint32_t gm_l3; /* i */
    void * gm_l4; /* b */
    void * gm_l5; /* i */
    uint8_t gm_l6; /* c */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    void * gm_l8; /* a */
    void * gm_l9; /* a */
    void * gm_l10; /* a */
    void * gm_l11; /* a */
    gm_t9qkh0rsooc8d gm_l12; /* op */
    void * gm_l13; /* a */
    gm_ts6oqppjh0ba3 gm_l14; /* op */
    void * gm_l15; /* a */
    void * gm_l16; /* b */
    uint8_t gm_l17; /* and_ */
    void * gm_l18; /* a */
    void * gm_l19; /* b */
    gm_tmn7n0n9raou5 gm_l20; /* c */
    void * gm_l21; /* a */
    gm_tvdg94bk7aa22 gm_l22; /* op */
    gm_b16_8 gm_l23; /* args */
    gm_tandvbt63uk7n gm_l24; /* x */
    gm_b16_8 gm_l26; /* args */
    void * gm_l28; /* x */
    gm_b16_8 gm_l29; /* xs */
    gm_b16_8 gm_l30; /* xs */
    void * gm_l31; /* a */
    gm_ttggq7ng4kp3p gm_l32; /* v */
    void * gm_l33; /* a */
    void * gm_l34; /* a */
    uint8_t gm_l35; /* r */
    gm_t3dcoc7077t1q gm_l36; /* x */
    gm_t083kd7b1kkag gm_l37; /* x */
    void * gm_l38; /* a */
    void * gm_l39; /* b */
    uint8_t gm_l40; /* n */
    void * gm_l41; /* sc */
    gm_b16_8 gm_l42; /* arms */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l46; /* a */
    void * gm_l47; /* a */
    void * gm_l48; /* b */
    uint8_t gm_l49; /* n */
    void * gm_l50; /* b */
    gm_trl2khmuphncd gm_l51; /* x */
    gm_teo9d4mncjfdu gm_l53; /* x */
    uint32_t gm_l54; /* l */
    void * gm_l55; /* v */
    void * gm_l56; /* a */
    void * gm_l57; /* k */
    void * gm_l58; /* v */
    void * gm_l59; /* k */
    void * gm_l60; /* w */
    void * gm_l62; /* x */
    void * gm_l63; /* w */
    void * gm_l65; /* x */
    gm_b16_8 gm_l68; /* <tmp> */
    uint64_t gm_l69; /* <tmp> */
    gm_t4lnf71mruqpt gm_l70; /* h */
    void * gm_l71; /* x */
    gm_b16_8 gm_l72; /* decls */
    void * gm_l73; /* x */
    GM_STACK_CHECK(&F, 6648u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2b6tm8c0vgo1 gm_l75;
    gm_tftb1odc739t5 gm_l76;
    gm_tq7s98ihonkju gm_l77;
    gm_tq7s98ihonkju gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    {
        void * t60 = gm_l0;
        {
            {
                {
                    uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)18ULL));
                    if (!t62) {
                        goto gm_lb2;
                    }
                    void * t63 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l1 = t63;
                    void * t64 = gm_l1;
                    void * t65 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t66;
                    memset(&t66, 0, sizeof t66);
                    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)18ULL));
                    GM_ST(void *, ((uint8_t *)&t66 + 8), t65);
                    gm_l75 = t66;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)19ULL));
                    if (!t68) {
                        goto gm_lb3;
                    }
                    void * t69 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l2 = t69;
                    uint32_t t70 = GM_LD(uint32_t, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l3 = t70;
                    void * t71 = gm_l2;
                    void * t72 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t71);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t73 = gm_l3;
                    gm_tbl8il92l97ut t74;
                    GM_ST(uint32_t, (uint8_t *)&t74 + 12, 0);
                    GM_ST(void *, ((uint8_t *)&t74 + 0), t72);
                    GM_ST(uint32_t, ((uint8_t *)&t74 + 8), t73);
                    gm_t2b6tm8c0vgo1 t75;
                    memset(&t75, 0, sizeof t75);
                    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t75 + 8), t74);
                    gm_l75 = t75;
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)20ULL));
                    if (!t77) {
                        goto gm_lb4;
                    }
                    void * t78 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l4 = t78;
                    void * t79 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l5 = t79;
                    uint8_t t80 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l6 = t80;
                    void * t81 = gm_l4;
                    void * t82 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t81);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t83 = gm_l5;
                    void * t84 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t83);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t85 = gm_l6;
                    gm_tlcl4u558l6p5 t86;
                    GM_ST(uint8_t, (uint8_t *)&t86 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t86 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t86 + 20, 0);
                    GM_ST(void *, ((uint8_t *)&t86 + 0), t82);
                    GM_ST(void *, ((uint8_t *)&t86 + 8), t84);
                    GM_ST(uint8_t, ((uint8_t *)&t86 + 16), t85);
                    gm_t2b6tm8c0vgo1 t87;
                    memset(&t87, 0, sizeof t87);
                    GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t87 + 8), t86);
                    gm_l75 = t87;
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)21ULL));
                    if (!t89) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t60) + 8) + 8), 32);
                    void * t90 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    void * t91 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t90);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t92 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                    void * t93 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t94 = GM_LD(void *, (((uint8_t *)&gm_l7) + 16));
                    void * t95 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 24));
                    gm_tao63pk3r18v5 t97;
                    GM_ST(uint8_t, (uint8_t *)&t97 + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&t97 + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&t97 + 28, 0);
                    GM_ST(void *, ((uint8_t *)&t97 + 0), t91);
                    GM_ST(void *, ((uint8_t *)&t97 + 8), t93);
                    GM_ST(void *, ((uint8_t *)&t97 + 16), t95);
                    GM_ST(uint8_t, ((uint8_t *)&t97 + 24), t96);
                    gm_t2b6tm8c0vgo1 t98;
                    GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t98 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t98 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t98 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)21ULL));
                    GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t98 + 8), t97);
                    gm_l75 = t98;
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t100 = (uint8_t)(t99 == ((uint8_t)22ULL));
                    if (!t100) {
                        goto gm_lb6;
                    }
                    void * t101 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l8 = t101;
                    void * t102 = gm_l8;
                    void * t103 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t102);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t104;
                    memset(&t104, 0, sizeof t104);
                    GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)22ULL));
                    GM_ST(void *, ((uint8_t *)&t104 + 8), t103);
                    gm_l75 = t104;
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t106 = (uint8_t)(t105 == ((uint8_t)23ULL));
                    if (!t106) {
                        goto gm_lb7;
                    }
                    void * t107 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l9 = t107;
                    void * t108 = gm_l9;
                    void * t109 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t110;
                    memset(&t110, 0, sizeof t110);
                    GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)23ULL));
                    GM_ST(void *, ((uint8_t *)&t110 + 8), t109);
                    gm_l75 = t110;
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t112 = (uint8_t)(t111 == ((uint8_t)24ULL));
                    if (!t112) {
                        goto gm_lb8;
                    }
                    void * t113 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l10 = t113;
                    void * t114 = gm_l10;
                    void * t115 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t114);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t116;
                    memset(&t116, 0, sizeof t116);
                    GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)24ULL));
                    GM_ST(void *, ((uint8_t *)&t116 + 8), t115);
                    gm_l75 = t116;
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)25ULL));
                    if (!t118) {
                        goto gm_lb9;
                    }
                    void * t119 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l11 = t119;
                    void * t120 = gm_l11;
                    void * t121 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t122;
                    memset(&t122, 0, sizeof t122);
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)25ULL));
                    GM_ST(void *, ((uint8_t *)&t122 + 8), t121);
                    gm_l75 = t122;
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)29ULL));
                    if (!t124) {
                        goto gm_lb10;
                    }
                    gm_t9qkh0rsooc8d t125 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t60) + 8) + 8));
                    gm_l12 = t125;
                    void * t126 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l13 = t126;
                    gm_t9qkh0rsooc8d t127 = gm_l12;
                    void * t128 = gm_l13;
                    void * t129 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t8ghgl2jj0qha t130;
                    GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t130 + 0), t127);
                    GM_ST(void *, ((uint8_t *)&t130 + 8), t129);
                    gm_t2b6tm8c0vgo1 t131;
                    memset(&t131, 0, sizeof t131);
                    GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)29ULL));
                    GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t131 + 8), t130);
                    gm_l75 = t131;
                    goto gm_lb1;
                }
gm_lb10:;
                {
                    uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t133 = (uint8_t)(t132 == ((uint8_t)30ULL));
                    if (!t133) {
                        goto gm_lb11;
                    }
                    gm_ts6oqppjh0ba3 t134 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t60) + 8) + 8));
                    gm_l14 = t134;
                    void * t135 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l15 = t135;
                    void * t136 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l16 = t136;
                    gm_ts6oqppjh0ba3 t137 = gm_l14;
                    void * t138 = gm_l15;
                    void * t139 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t140 = gm_l16;
                    void * t141 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_trl9d7m1k5lbd t142;
                    GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t142 + 0), t137);
                    GM_ST(void *, ((uint8_t *)&t142 + 8), t139);
                    GM_ST(void *, ((uint8_t *)&t142 + 16), t141);
                    gm_t2b6tm8c0vgo1 t143;
                    memset(&t143, 0, sizeof t143);
                    GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t143 + 8), t142);
                    gm_l75 = t143;
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t145 = (uint8_t)(t144 == ((uint8_t)31ULL));
                    if (!t145) {
                        goto gm_lb12;
                    }
                    uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 8));
                    gm_l17 = t146;
                    void * t147 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l18 = t147;
                    void * t148 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l19 = t148;
                    uint8_t t149 = gm_l17;
                    void * t150 = gm_l18;
                    void * t151 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t152 = gm_l19;
                    void * t153 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ta612fil812pm t154;
                    GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t154 + 0), t149);
                    GM_ST(void *, ((uint8_t *)&t154 + 8), t151);
                    GM_ST(void *, ((uint8_t *)&t154 + 16), t153);
                    gm_t2b6tm8c0vgo1 t155;
                    memset(&t155, 0, sizeof t155);
                    GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_ta612fil812pm, ((uint8_t *)&t155 + 8), t154);
                    gm_l75 = t155;
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t157 = (uint8_t)(t156 == ((uint8_t)32ULL));
                    if (!t157) {
                        goto gm_lb13;
                    }
                    gm_tmn7n0n9raou5 t158 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t60) + 8) + 8));
                    gm_l20 = t158;
                    void * t159 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l21 = t159;
                    gm_tmn7n0n9raou5 t160 = gm_l20;
                    void * t161 = gm_l21;
                    void * t162 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t8jsc4n9vp2ib t163;
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t163 + 0), t160);
                    GM_ST(void *, ((uint8_t *)&t163 + 8), t162);
                    gm_t2b6tm8c0vgo1 t164;
                    memset(&t164, 0, sizeof t164);
                    GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)32ULL));
                    GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t164 + 8), t163);
                    gm_l75 = t164;
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)33ULL));
                    if (!t166) {
                        goto gm_lb14;
                    }
                    gm_tvdg94bk7aa22 t167 = GM_LD(gm_tvdg94bk7aa22, ((((uint8_t *)t60) + 8) + 8));
                    gm_l22 = t167;
                    gm_b16_8 t168 = GM_LD(gm_b16_8, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l23 = t168;
                    gm_tvdg94bk7aa22 t169 = gm_l22;
                    gm_b16_8 t170 = gm_l23;
                    gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
                    gm_b16_8 t172 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tt8o5kebou62r t173;
                    GM_ST(uint32_t, (uint8_t *)&t173 + 12, 0);
                    GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t173 + 0), t169);
                    GM_ST(gm_b16_8, ((uint8_t *)&t173 + 16), t172);
                    gm_t2b6tm8c0vgo1 t174;
                    GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t174 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t174 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t174 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)33ULL));
                    GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t174 + 8), t173);
                    gm_l75 = t174;
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)58ULL));
                    if (!t176) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&gm_l24), ((((uint8_t *)t60) + 8) + 8), 24);
                    gm_tesaltf0mhcki t177 = GM_LD(gm_tesaltf0mhcki, ((uint8_t *)&gm_l24));
                    gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                    gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                    gm_b16_8 t180 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tandvbt63uk7n t181;
                    GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                    GM_ST(gm_tesaltf0mhcki, ((uint8_t *)&t181 + 0), t177);
                    GM_ST(gm_b16_8, ((uint8_t *)&t181 + 8), t180);
                    gm_t2b6tm8c0vgo1 t182;
                    memset(&t182, 0, sizeof t182);
                    GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)58ULL));
                    GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t182 + 8), t181);
                    gm_l75 = t182;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)34ULL));
                    if (!t184) {
                        goto gm_lb16;
                    }
                    memmove(((uint8_t *)&F.l25), ((((uint8_t *)t60) + 8) + 8), 40);
                    gm_b16_8 t185 = GM_LD(gm_b16_8, (((((uint8_t *)t60) + 8) + 8) + 40));
                    gm_l26 = t185;
                    memmove(((uint8_t *)&F.l27), ((uint8_t *)&F.l25), 40);
                    {
                        {
                            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                            uint8_t t187 = (uint8_t)(t186 == ((uint8_t)2ULL));
                            if (!t187) {
                                goto gm_lb18;
                            }
                            void * t188 = GM_LD(void *, (((uint8_t *)&F.l25) + 8));
                            gm_l28 = t188;
                            void * t189 = gm_l28;
                            void * t190 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t189);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tsth9qu1dv0g3 t191;
                            GM_ST(uint8_t, (uint8_t *)&t191 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t191 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t191 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t191 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&t191 + 32, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)2ULL));
                            GM_ST(void *, ((uint8_t *)&t191 + 8), t190);
                            F.l27 = t191;
                            goto gm_lb17;
                        }
gm_lb18:;
                    }
gm_lb17:;
                    gm_b16_8 t192 = gm_l26;
                    gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                    gm_tsth9qu1dv0g3 t194 = GM_LD(gm_tsth9qu1dv0g3, ((uint8_t *)&F.l27));
                    gm_b16_8 t195 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t193);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_to76jli3bqddq t196;
                    GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t196 + 0), t194);
                    GM_ST(gm_b16_8, ((uint8_t *)&t196 + 40), t195);
                    gm_t2b6tm8c0vgo1 t197;
                    GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)34ULL));
                    GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t197 + 8), t196);
                    gm_l75 = t197;
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t198 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t199 = (uint8_t)(t198 == ((uint8_t)35ULL));
                    if (!t199) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t200 = GM_LD(gm_b16_8, ((((uint8_t *)t60) + 8) + 8));
                    gm_l29 = t200;
                    gm_b16_8 t201 = gm_l29;
                    gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                    gm_b16_8 t203 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t202);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t204;
                    memset(&t204, 0, sizeof t204);
                    GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t204 + 8), t203);
                    gm_l75 = t204;
                    goto gm_lb1;
                }
gm_lb19:;
                {
                    uint8_t t205 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t206 = (uint8_t)(t205 == ((uint8_t)36ULL));
                    if (!t206) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t207 = GM_LD(gm_b16_8, ((((uint8_t *)t60) + 8) + 8));
                    gm_l30 = t207;
                    gm_b16_8 t208 = gm_l30;
                    gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                    gm_b16_8 t210 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t209);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t211;
                    memset(&t211, 0, sizeof t211);
                    GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)36ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t211 + 8), t210);
                    gm_l75 = t211;
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t213 = (uint8_t)(t212 == ((uint8_t)37ULL));
                    if (!t213) {
                        goto gm_lb21;
                    }
                    void * t214 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l31 = t214;
                    void * t215 = gm_l31;
                    void * t216 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t215);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t217;
                    memset(&t217, 0, sizeof t217);
                    GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)37ULL));
                    GM_ST(void *, ((uint8_t *)&t217 + 8), t216);
                    gm_l75 = t217;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t219 = (uint8_t)(t218 == ((uint8_t)38ULL));
                    if (!t219) {
                        goto gm_lb22;
                    }
                    gm_ttggq7ng4kp3p t220 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t60) + 8) + 8));
                    gm_l32 = t220;
                    uint32_t t221 = GM_LD(uint32_t, ((uint8_t *)&gm_l32));
                    void * t222 = GM_LD(void *, (((uint8_t *)&gm_l32) + 8));
                    void * t223 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t222);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttggq7ng4kp3p t224;
                    GM_ST(uint32_t, (uint8_t *)&t224 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t224 + 0), t221);
                    GM_ST(void *, ((uint8_t *)&t224 + 8), t223);
                    gm_t2b6tm8c0vgo1 t225;
                    memset(&t225, 0, sizeof t225);
                    GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)38ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t225 + 8), t224);
                    gm_l75 = t225;
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t226 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t227 = (uint8_t)(t226 == ((uint8_t)39ULL));
                    if (!t227) {
                        goto gm_lb23;
                    }
                    void * t228 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l33 = t228;
                    void * t229 = gm_l33;
                    void * t230 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t231;
                    memset(&t231, 0, sizeof t231);
                    GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)39ULL));
                    GM_ST(void *, ((uint8_t *)&t231 + 8), t230);
                    gm_l75 = t231;
                    goto gm_lb1;
                }
gm_lb23:;
                {
                    uint8_t t232 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t233 = (uint8_t)(t232 == ((uint8_t)40ULL));
                    if (!t233) {
                        goto gm_lb24;
                    }
                    void * t234 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l34 = t234;
                    uint8_t t235 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l35 = t235;
                    void * t236 = gm_l34;
                    void * t237 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t236);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t238 = gm_l35;
                    gm_tsplfr33dsvji t239;
                    GM_ST(uint8_t, (uint8_t *)&t239 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t239 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t239 + 12, 0);
                    GM_ST(void *, ((uint8_t *)&t239 + 0), t237);
                    GM_ST(uint8_t, ((uint8_t *)&t239 + 8), t238);
                    gm_t2b6tm8c0vgo1 t240;
                    memset(&t240, 0, sizeof t240);
                    GM_ST(uint8_t, ((uint8_t *)&t240 + 0), ((uint8_t)40ULL));
                    GM_ST(gm_tsplfr33dsvji, ((uint8_t *)&t240 + 8), t239);
                    gm_l75 = t240;
                    goto gm_lb1;
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t241 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t242 = (uint8_t)(t241 == ((uint8_t)41ULL));
                    if (!t242) {
                        goto gm_lb25;
                    }
                    memmove(((uint8_t *)&gm_l36), ((((uint8_t *)t60) + 8) + 8), 24);
                    void * t243 = GM_LD(void *, ((uint8_t *)&gm_l36));
                    void * t244 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t243);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t9805qtonbsag t245 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l36) + 8));
                    void * t246 = GM_LD(void *, (((uint8_t *)&gm_l36) + 16));
                    void * t247 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t246);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t3dcoc7077t1q t248;
                    GM_ST(void *, ((uint8_t *)&t248 + 0), t244);
                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t248 + 8), t245);
                    GM_ST(void *, ((uint8_t *)&t248 + 16), t247);
                    gm_t2b6tm8c0vgo1 t249;
                    memset(&t249, 0, sizeof t249);
                    GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)41ULL));
                    GM_ST(gm_t3dcoc7077t1q, ((uint8_t *)&t249 + 8), t248);
                    gm_l75 = t249;
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t250 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t251 = (uint8_t)(t250 == ((uint8_t)42ULL));
                    if (!t251) {
                        goto gm_lb26;
                    }
                    memmove(((uint8_t *)&gm_l37), ((((uint8_t *)t60) + 8) + 8), 24);
                    void * t252 = GM_LD(void *, ((uint8_t *)&gm_l37));
                    void * t253 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t252);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t9805qtonbsag t254 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l37) + 8));
                    uint32_t t255 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 16));
                    gm_t083kd7b1kkag t256;
                    GM_ST(uint32_t, (uint8_t *)&t256 + 20, 0);
                    GM_ST(void *, ((uint8_t *)&t256 + 0), t253);
                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t256 + 8), t254);
                    GM_ST(uint32_t, ((uint8_t *)&t256 + 16), t255);
                    gm_t2b6tm8c0vgo1 t257;
                    memset(&t257, 0, sizeof t257);
                    GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)42ULL));
                    GM_ST(gm_t083kd7b1kkag, ((uint8_t *)&t257 + 8), t256);
                    gm_l75 = t257;
                    goto gm_lb1;
                }
gm_lb26:;
                {
                    uint8_t t258 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t259 = (uint8_t)(t258 == ((uint8_t)43ULL));
                    if (!t259) {
                        goto gm_lb27;
                    }
                    void * t260 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l38 = t260;
                    void * t261 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l39 = t261;
                    uint8_t t262 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l40 = t262;
                    void * t263 = gm_l38;
                    void * t264 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t263);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t265 = gm_l39;
                    void * t266 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t265);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t267 = gm_l40;
                    gm_tlcl4u558l6p5 t268;
                    GM_ST(uint8_t, (uint8_t *)&t268 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t268 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t268 + 20, 0);
                    GM_ST(void *, ((uint8_t *)&t268 + 0), t264);
                    GM_ST(void *, ((uint8_t *)&t268 + 8), t266);
                    GM_ST(uint8_t, ((uint8_t *)&t268 + 16), t267);
                    gm_t2b6tm8c0vgo1 t269;
                    memset(&t269, 0, sizeof t269);
                    GM_ST(uint8_t, ((uint8_t *)&t269 + 0), ((uint8_t)43ULL));
                    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t269 + 8), t268);
                    gm_l75 = t269;
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t271 = (uint8_t)(t270 == ((uint8_t)44ULL));
                    if (!t271) {
                        goto gm_lb28;
                    }
                    void * t272 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l41 = t272;
                    gm_b16_8 t273 = GM_LD(gm_b16_8, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l42 = t273;
                    gm_trqfvmjtt4sij t274;
                    gm_f_list_58nr42v_2((void *)&t274, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l43 = t274;
                    gm_b16_8 t275 = gm_l42;
                    gm_l44 = t275;
                    gm_l45 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t276 = gm_l45;
                            uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                            uint8_t t278 = (t276 < t277);
                            if (!t278) {
                                goto gm_lb29;
                            }
                            {
                                void * t279 = GM_LD(void *, ((uint8_t *)&gm_l44));
                                uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                                uint64_t t281 = gm_l45;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l46), (((uint8_t *)t279) + (uintptr_t)t281 * 24), 24);
                                        void * t282 = (void *)((uint8_t *)&F.l43);
                                        void * t283 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                        void * t284 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t283);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t285 = GM_LD(void *, (((uint8_t *)&gm_l46) + 8));
                                        void * t286 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t285);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t287 = GM_LD(void *, (((uint8_t *)&gm_l46) + 16));
                                        void * t288 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t287);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tbbqefuhv2r8h t289;
                                        GM_ST(void *, ((uint8_t *)&t289 + 0), t284);
                                        GM_ST(void *, ((uint8_t *)&t289 + 8), t286);
                                        GM_ST(void *, ((uint8_t *)&t289 + 16), t288);
                                        gm_tftb1odc739t5 t290 = gm_f_push_bpovr2k_2(t282, &t289);
                                        gm_l76 = t290;
                                        uint8_t t291 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                        if (!t291) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 1, gm_srcgksioc7kn741, 38, 29);
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 38, 13);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t292 = gm_l45;
                            uint64_t r293; bool o294 = __builtin_add_overflow(t292, ((uint64_t)1ULL), &r293);
                            uint64_t t295 = r293;
                            gm_l45 = t295;
                        }
                    }
gm_lb29:;
                    void * t296 = gm_l41;
                    void * t297 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t296);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                    gm_tkaapcii86udk t299;
                    GM_ST(void *, ((uint8_t *)&t299 + 0), t297);
                    GM_ST(gm_b16_8, ((uint8_t *)&t299 + 8), t298);
                    gm_t2b6tm8c0vgo1 t300;
                    memset(&t300, 0, sizeof t300);
                    GM_ST(uint8_t, ((uint8_t *)&t300 + 0), ((uint8_t)44ULL));
                    GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t300 + 8), t299);
                    gm_l75 = t300;
                    goto gm_lb1;
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t301 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t302 = (uint8_t)(t301 == ((uint8_t)45ULL));
                    if (!t302) {
                        goto gm_lb34;
                    }
                    void * t303 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l47 = t303;
                    void * t304 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l48 = t304;
                    uint8_t t305 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l49 = t305;
                    void * t306 = gm_l47;
                    void * t307 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t306);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t308 = gm_l48;
                    void * t309 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t308);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t310 = gm_l49;
                    gm_tlcl4u558l6p5 t311;
                    GM_ST(uint8_t, (uint8_t *)&t311 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t311 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t311 + 20, 0);
                    GM_ST(void *, ((uint8_t *)&t311 + 0), t307);
                    GM_ST(void *, ((uint8_t *)&t311 + 8), t309);
                    GM_ST(uint8_t, ((uint8_t *)&t311 + 16), t310);
                    gm_t2b6tm8c0vgo1 t312;
                    memset(&t312, 0, sizeof t312);
                    GM_ST(uint8_t, ((uint8_t *)&t312 + 0), ((uint8_t)45ULL));
                    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t312 + 8), t311);
                    gm_l75 = t312;
                    goto gm_lb1;
                }
gm_lb34:;
                {
                    uint8_t t313 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t314 = (uint8_t)(t313 == ((uint8_t)46ULL));
                    if (!t314) {
                        goto gm_lb35;
                    }
                    void * t315 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l50 = t315;
                    void * t316 = gm_l50;
                    void * t317 = gm_f_clone_block_5tu7v2v((void *)NULL, cx, NULL, t316);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t318;
                    memset(&t318, 0, sizeof t318);
                    GM_ST(uint8_t, ((uint8_t *)&t318 + 0), ((uint8_t)46ULL));
                    GM_ST(void *, ((uint8_t *)&t318 + 8), t317);
                    gm_l75 = t318;
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    uint8_t t319 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t320 = (uint8_t)(t319 == ((uint8_t)47ULL));
                    if (!t320) {
                        goto gm_lb36;
                    }
                    memmove(((uint8_t *)&gm_l51), ((((uint8_t *)t60) + 8) + 8), 32);
                    gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l51));
                    gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
                    gm_b16_8 t323 = gm_f_clone_conds_ljrasoa((void *)NULL, cx, NULL, t322);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t324 = GM_LD(void *, (((uint8_t *)&gm_l51) + 16));
                    void * t325 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t324);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t326 = GM_LD(void *, (((uint8_t *)&gm_l51) + 24));
                    void * t327 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t326);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_trl2khmuphncd t328;
                    GM_ST(gm_b16_8, ((uint8_t *)&t328 + 0), t323);
                    GM_ST(void *, ((uint8_t *)&t328 + 16), t325);
                    GM_ST(void *, ((uint8_t *)&t328 + 24), t327);
                    gm_t2b6tm8c0vgo1 t329;
                    GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t329 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t329 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t329 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t329 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)47ULL));
                    GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t329 + 8), t328);
                    gm_l75 = t329;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    uint8_t t330 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t331 = (uint8_t)(t330 == ((uint8_t)48ULL));
                    if (!t331) {
                        goto gm_lb37;
                    }
                    memmove(((uint8_t *)&F.l52), ((((uint8_t *)t60) + 8) + 8), 40);
                    uint32_t t332 = GM_LD(uint32_t, ((uint8_t *)&F.l52));
                    gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 8));
                    gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
                    gm_b16_8 t335 = gm_f_clone_conds_ljrasoa((void *)NULL, cx, NULL, t334);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t336 = GM_LD(void *, (((uint8_t *)&F.l52) + 24));
                    void * t337 = gm_f_clone_block_5tu7v2v((void *)NULL, cx, NULL, t336);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t338 = GM_LD(void *, (((uint8_t *)&F.l52) + 32));
                    void * t339 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t338);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t710lsrq1m8gk t340;
                    GM_ST(uint32_t, (uint8_t *)&t340 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t340 + 0), t332);
                    GM_ST(gm_b16_8, ((uint8_t *)&t340 + 8), t335);
                    GM_ST(void *, ((uint8_t *)&t340 + 24), t337);
                    GM_ST(void *, ((uint8_t *)&t340 + 32), t339);
                    gm_t2b6tm8c0vgo1 t341;
                    GM_ST(uint8_t, (uint8_t *)&t341 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t341 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t341 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t341 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t341 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t341 + 0), ((uint8_t)48ULL));
                    GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t341 + 8), t340);
                    gm_l75 = t341;
                    goto gm_lb1;
                }
gm_lb37:;
            }
            {
                {
                    uint8_t t342 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t343 = (uint8_t)(t342 == ((uint8_t)49ULL));
                    if (!t343) {
                        goto gm_lb38;
                    }
                    memmove(((uint8_t *)&gm_l53), ((((uint8_t *)t60) + 8) + 8), 24);
                    gm_te9hrk0u35rev t344 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l53));
                    void * t345 = GM_LD(void *, (((uint8_t *)&gm_l53) + 16));
                    void * t346 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t345);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_teo9d4mncjfdu t347;
                    GM_ST(uint32_t, (uint8_t *)&t347 + 12, 0);
                    GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t347 + 0), t344);
                    GM_ST(void *, ((uint8_t *)&t347 + 16), t346);
                    gm_t2b6tm8c0vgo1 t348;
                    memset(&t348, 0, sizeof t348);
                    GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)49ULL));
                    GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&t348 + 8), t347);
                    gm_l75 = t348;
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t349 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t350 = (uint8_t)(t349 == ((uint8_t)51ULL));
                    if (!t350) {
                        goto gm_lb39;
                    }
                    uint32_t t351 = GM_LD(uint32_t, ((((uint8_t *)t60) + 8) + 8));
                    gm_l54 = t351;
                    void * t352 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l55 = t352;
                    uint32_t t353 = gm_l54;
                    void * t354 = gm_l55;
                    void * t355 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t354);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttggq7ng4kp3p t356;
                    GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t356 + 0), t353);
                    GM_ST(void *, ((uint8_t *)&t356 + 8), t355);
                    gm_t2b6tm8c0vgo1 t357;
                    memset(&t357, 0, sizeof t357);
                    GM_ST(uint8_t, ((uint8_t *)&t357 + 0), ((uint8_t)51ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t357 + 8), t356);
                    gm_l75 = t357;
                    goto gm_lb1;
                }
gm_lb39:;
                {
                    uint8_t t358 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t359 = (uint8_t)(t358 == ((uint8_t)52ULL));
                    if (!t359) {
                        goto gm_lb40;
                    }
                    void * t360 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l56 = t360;
                    void * t361 = gm_l56;
                    void * t362 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t361);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t363;
                    memset(&t363, 0, sizeof t363);
                    GM_ST(uint8_t, ((uint8_t *)&t363 + 0), ((uint8_t)52ULL));
                    GM_ST(void *, ((uint8_t *)&t363 + 8), t362);
                    gm_l75 = t363;
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t364 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t365 = (uint8_t)(t364 == ((uint8_t)53ULL));
                    if (!t365) {
                        goto gm_lb41;
                    }
                    void * t366 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l57 = t366;
                    void * t367 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l58 = t367;
                    void * t368 = gm_l57;
                    void * t369 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t368);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t370 = gm_l58;
                    void * t371 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t370);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t3mf58gv9kf3n t372;
                    GM_ST(void *, ((uint8_t *)&t372 + 0), t369);
                    GM_ST(void *, ((uint8_t *)&t372 + 8), t371);
                    gm_t2b6tm8c0vgo1 t373;
                    memset(&t373, 0, sizeof t373);
                    GM_ST(uint8_t, ((uint8_t *)&t373 + 0), ((uint8_t)53ULL));
                    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t373 + 8), t372);
                    gm_l75 = t373;
                    goto gm_lb1;
                }
gm_lb41:;
                {
                    uint8_t t374 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t375 = (uint8_t)(t374 == ((uint8_t)54ULL));
                    if (!t375) {
                        goto gm_lb42;
                    }
                    void * t376 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l59 = t376;
                    void * t377 = gm_l59;
                    void * t378 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t377);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2b6tm8c0vgo1 t379;
                    memset(&t379, 0, sizeof t379);
                    GM_ST(uint8_t, ((uint8_t *)&t379 + 0), ((uint8_t)54ULL));
                    GM_ST(void *, ((uint8_t *)&t379 + 8), t378);
                    gm_l75 = t379;
                    goto gm_lb1;
                }
gm_lb42:;
            }
            {
                {
                    uint8_t t380 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t381 = (uint8_t)(t380 == ((uint8_t)55ULL));
                    if (!t381) {
                        goto gm_lb43;
                    }
                    void * t382 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l60 = t382;
                    void * t383 = gm_l60;
                    memmove(((uint8_t *)&F.l61), ((uint8_t *)t383), 40);
                    {
                        {
                            void * t384 = gm_l60;
                            uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)t384) + 0));
                            uint8_t t386 = (uint8_t)(t385 == ((uint8_t)1ULL));
                            if (!t386) {
                                goto gm_lb45;
                            }
                            void * t387 = GM_LD(void *, (((uint8_t *)t384) + 8));
                            gm_l62 = t387;
                            void * t388 = gm_l62;
                            void * t389 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t388);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_th1g69d7vpipg t390;
                            GM_ST(uint8_t, (uint8_t *)&t390 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t390 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t390 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t390 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t390 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&t390 + 32, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t390 + 0), ((uint8_t)1ULL));
                            GM_ST(void *, ((uint8_t *)&t390 + 8), t389);
                            F.l61 = t390;
                            goto gm_lb44;
                        }
gm_lb45:;
                    }
gm_lb44:;
                    void * t391 = gm_l60;
                    gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)t391) + 40));
                    void * t393 = gm_l60;
                    gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)t393) + 56));
                    void * t395 = gm_l60;
                    gm_b16_8 t396 = GM_LD(gm_b16_8, (((uint8_t *)t395) + 72));
                    gm_b16_8 t397 = GM_LD(gm_b16_8, ((uint8_t *)&t396));
                    gm_th1g69d7vpipg t398 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)&F.l61));
                    gm_b16_8 t399 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t397);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t400 = gm_l60;
                    void * t401 = GM_LD(void *, (((uint8_t *)t400) + 88));
                    void * t402 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t401);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t150deeftog49 t403;
                    GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t403 + 0), t398);
                    GM_ST(gm_b16_8, ((uint8_t *)&t403 + 40), t392);
                    GM_ST(gm_b16_8, ((uint8_t *)&t403 + 56), t394);
                    GM_ST(gm_b16_8, ((uint8_t *)&t403 + 72), t399);
                    GM_ST(void *, ((uint8_t *)&t403 + 88), t402);
                    gm_tq7s98ihonkju t404 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t403, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl8b626vj63)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l77 = t404;
                    uint8_t t405 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                    if (!t405) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_srcgksioc7kn741, 53, 28);
                    void * t406 = GM_LD(void *, (((uint8_t *)&gm_l77) + 8));
                    gm_t2b6tm8c0vgo1 t407;
                    memset(&t407, 0, sizeof t407);
                    GM_ST(uint8_t, ((uint8_t *)&t407 + 0), ((uint8_t)55ULL));
                    GM_ST(void *, ((uint8_t *)&t407 + 8), t406);
                    gm_l75 = t407;
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t408 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t409 = (uint8_t)(t408 == ((uint8_t)56ULL));
                    if (!t409) {
                        goto gm_lb46;
                    }
                    void * t410 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l63 = t410;
                    void * t411 = gm_l63;
                    memmove(((uint8_t *)&F.l64), ((uint8_t *)t411), 40);
                    {
                        {
                            void * t412 = gm_l63;
                            uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)t412) + 0));
                            uint8_t t414 = (uint8_t)(t413 == ((uint8_t)1ULL));
                            if (!t414) {
                                goto gm_lb48;
                            }
                            void * t415 = GM_LD(void *, (((uint8_t *)t412) + 8));
                            gm_l65 = t415;
                            void * t416 = gm_l65;
                            void * t417 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t416);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_th1g69d7vpipg t418;
                            GM_ST(uint8_t, (uint8_t *)&t418 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t418 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t418 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t418 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t418 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&t418 + 32, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t418 + 0), ((uint8_t)1ULL));
                            GM_ST(void *, ((uint8_t *)&t418 + 8), t417);
                            F.l64 = t418;
                            goto gm_lb47;
                        }
gm_lb48:;
                    }
gm_lb47:;
                    void * t419 = gm_l63;
                    void * t420 = GM_LD(void *, (((uint8_t *)t419) + 40));
                    gm_th1g69d7vpipg t421 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)&F.l64));
                    void * t422 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t420);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t423 = gm_l63;
                    void * t424 = GM_LD(void *, (((uint8_t *)t423) + 48));
                    void * t425 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t424);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t426 = gm_l63;
                    uint32_t t427 = GM_LD(uint32_t, (((uint8_t *)t426) + 56));
                    gm_t9i38ih07bojq t428;
                    GM_ST(uint32_t, (uint8_t *)&t428 + 60, 0);
                    GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t428 + 0), t421);
                    GM_ST(void *, ((uint8_t *)&t428 + 40), t422);
                    GM_ST(void *, ((uint8_t *)&t428 + 48), t425);
                    GM_ST(uint32_t, ((uint8_t *)&t428 + 56), t427);
                    gm_tq7s98ihonkju t429 = gm_f_new_ges19gd((void *)NULL, cx, NULL, &t428, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kg8c0kikv62)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l78 = t429;
                    uint8_t t430 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                    if (!t430) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srcgksioc7kn741, 58, 36);
                    void * t431 = GM_LD(void *, (((uint8_t *)&gm_l78) + 8));
                    gm_t2b6tm8c0vgo1 t432;
                    memset(&t432, 0, sizeof t432);
                    GM_ST(uint8_t, ((uint8_t *)&t432 + 0), ((uint8_t)56ULL));
                    GM_ST(void *, ((uint8_t *)&t432 + 8), t431);
                    gm_l75 = t432;
                    goto gm_lb1;
                }
gm_lb46:;
            }
            {
                {
                    uint8_t t433 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t434 = (uint8_t)(t433 == ((uint8_t)59ULL));
                    if (!t434) {
                        goto gm_lb49;
                    }
                    memmove(((uint8_t *)&F.l66), ((((uint8_t *)t60) + 8) + 8), 40);
                    gm_trqfvmjtt4sij t435;
                    gm_f_list_p0pjtrh((void *)&t435, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l67 = t435;
                    gm_b16_8 t436 = GM_LD(gm_b16_8, (((uint8_t *)&F.l66) + 8));
                    gm_l68 = t436;
                    gm_l69 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t437 = gm_l69;
                            uint64_t t438 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                            uint8_t t439 = (t437 < t438);
                            if (!t439) {
                                goto gm_lb50;
                            }
                            {
                                void * t440 = GM_LD(void *, ((uint8_t *)&gm_l68));
                                uint64_t t441 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                                uint64_t t442 = gm_l69;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l70), (((uint8_t *)t440) + (uintptr_t)t442 * 32), 32);
                                        {
                                            {
                                                uint8_t t443 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                                                uint8_t t444 = (uint8_t)(t443 == ((uint8_t)0ULL));
                                                if (!t444) {
                                                    goto gm_lb56;
                                                }
                                                void * t445 = GM_LD(void *, (((uint8_t *)&gm_l70) + 8));
                                                gm_l71 = t445;
                                                void * t446 = (void *)((uint8_t *)&F.l67);
                                                void * t447 = gm_l71;
                                                void * t448 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t447);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_t4lnf71mruqpt t449;
                                                GM_ST(uint8_t, (uint8_t *)&t449 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t449 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t449 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t449 + 16, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t449 + 24, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t449 + 0), ((uint8_t)0ULL));
                                                GM_ST(void *, ((uint8_t *)&t449 + 8), t448);
                                                gm_tftb1odc739t5 t450 = gm_f_push_f5fsh94(t446, &t449);
                                                gm_l79 = t450;
                                                uint8_t t451 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                                if (!t451) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srcgksioc7kn741, 64, 36);
                                                goto gm_lb55;
                                            }
gm_lb56:;
                                            {
                                                uint8_t t452 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                                                uint8_t t453 = (uint8_t)(t452 == ((uint8_t)1ULL));
                                                if (!t453) {
                                                    goto gm_lb57;
                                                }
                                                gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                                gm_l72 = t454;
                                                void * t455 = GM_LD(void *, ((((uint8_t *)&gm_l70) + 8) + 16));
                                                gm_l73 = t455;
                                                void * t456 = (void *)((uint8_t *)&F.l67);
                                                gm_b16_8 t457 = gm_l72;
                                                void * t458 = gm_l73;
                                                void * t459 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t458);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tpe1hv3fekn05 t460;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t460 + 0), t457);
                                                GM_ST(void *, ((uint8_t *)&t460 + 16), t459);
                                                gm_t4lnf71mruqpt t461;
                                                GM_ST(uint8_t, (uint8_t *)&t461 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t461 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t461 + 4, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t461 + 0), ((uint8_t)1ULL));
                                                GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t461 + 8), t460);
                                                gm_tftb1odc739t5 t462 = gm_f_push_f5fsh94(t456, &t461);
                                                gm_l80 = t462;
                                                uint8_t t463 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                                if (!t463) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srcgksioc7kn741, 65, 40);
                                                goto gm_lb55;
                                            }
gm_lb57:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 63, 17);
                                        }
gm_lb55:;
                                        goto gm_lb53;
                                    }
gm_lb54:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 62, 13);
                                }
gm_lb53:;
                            }
gm_lb52:;
                            uint64_t t464 = gm_l69;
                            uint64_t r465; bool o466 = __builtin_add_overflow(t464, ((uint64_t)1ULL), &r465);
                            uint64_t t467 = r465;
                            gm_l69 = t467;
                        }
                    }
gm_lb50:;
                    uint32_t t468 = GM_LD(uint32_t, ((uint8_t *)&F.l66));
                    gm_b16_8 t469 = GM_LD(gm_b16_8, ((uint8_t *)&F.l67));
                    void * t470 = GM_LD(void *, (((uint8_t *)&F.l66) + 24));
                    void * t471 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t470);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t472 = GM_LD(void *, (((uint8_t *)&F.l66) + 32));
                    void * t473 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t472);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t710lsrq1m8gk t474;
                    GM_ST(uint32_t, (uint8_t *)&t474 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t474 + 0), t468);
                    GM_ST(gm_b16_8, ((uint8_t *)&t474 + 8), t469);
                    GM_ST(void *, ((uint8_t *)&t474 + 24), t471);
                    GM_ST(void *, ((uint8_t *)&t474 + 32), t473);
                    gm_t2b6tm8c0vgo1 t475;
                    GM_ST(uint8_t, (uint8_t *)&t475 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t475 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t475 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t475 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t475 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t475 + 0), ((uint8_t)59ULL));
                    GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t475 + 8), t474);
                    gm_l75 = t475;
                    goto gm_lb1;
                }
gm_lb49:;
            }
            {
                {
                    void * t476 = gm_l0;
                    memmove(((uint8_t *)&gm_l75), (((uint8_t *)t476) + 8), 64);
                    goto gm_lb1;
                }
gm_lb58:;
                gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 7, 19);
            }
        }
gm_lb1:;
    }
    {
        memmove(((uint8_t *)&F.l74), ((uint8_t *)&gm_l75), 64);
        void * t477 = gm_l0;
        gm_t88rcucg1b7uk t478 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t477));
        void * t479 = gm_l0;
        gm_tl874t2atnlh3 t480 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t479) + 72));
        void * t481 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t478, &(*(gm_t2b6tm8c0vgo1 *)((uint8_t *)&F.l74)), t480);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t481;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clone_exprs_umh77j3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_clone_exprs_umh77j3 F;
    gm_b16_8 gm_l0; /* xs */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t7;
    gm_f_list_vp1djv1_2((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l3;
                        gm_l4 = t12;
                        void * t13 = (void *)((uint8_t *)&F.l1);
                        void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t16 = gm_l4;
                        void * t17 = (void *)(((uint8_t *)t14) + (uintptr_t)t16 * 88);
                        void * t18 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t17);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t19 = gm_f_push_d943vgp_2(t13, &(*(gm_tcgrjq9toscke *)((uint8_t *)t18)));
                        gm_l5 = t19;
                        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgksioc7kn741, 82, 26);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 82, 5);
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
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
extern __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clone_pat_cq2uu68(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_pat_cq2uu68 F;
    void * gm_l0; /* p */
    void * gm_l1; /* e */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    uint32_t gm_l5; /* v */
    uint32_t gm_l6; /* v */
    void * gm_l7; /* q */
    gm_b16_8 gm_l8; /* fs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l12; /* f */
    gm_b16_8 gm_l13; /* ps */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* q */
    GM_STACK_CHECK(&F, 1818u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tb38v5s9j1r32 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    void * t22 = gm_l0;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
            if (!t24) {
                goto gm_lb2;
            }
            void * t25 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l1 = t25;
            void * t26 = gm_l1;
            void * t27 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t28;
            memset(&t28, 0, sizeof t28);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
            GM_ST(void *, ((uint8_t *)&t28 + 8), t27);
            gm_l19 = t28;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)5ULL));
            if (!t30) {
                goto gm_lb3;
            }
            void * t31 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l2 = t31;
            void * t32 = gm_l2;
            void * t33 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t34;
            memset(&t34, 0, sizeof t34);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)5ULL));
            GM_ST(void *, ((uint8_t *)&t34 + 8), t33);
            gm_l19 = t34;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)6ULL));
            if (!t36) {
                goto gm_lb4;
            }
            void * t37 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l3 = t37;
            void * t38 = gm_l3;
            void * t39 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)6ULL));
            GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
            gm_l19 = t40;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)7ULL));
            if (!t42) {
                goto gm_lb5;
            }
            void * t43 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l4 = t43;
            void * t44 = gm_l4;
            void * t45 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t46;
            memset(&t46, 0, sizeof t46);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)7ULL));
            GM_ST(void *, ((uint8_t *)&t46 + 8), t45);
            gm_l19 = t46;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)8ULL));
            if (!t48) {
                goto gm_lb6;
            }
            uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)t22) + 8) + 8));
            gm_l5 = t49;
            uint8_t t50 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8)) != 0);
            if (t50) {
                goto gm_lb6;
            }
            uint32_t t51 = gm_l5;
            gm_ttggq7ng4kp3p t52;
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t52 + 0), t51);
            GM_ST(void *, ((uint8_t *)&t52 + 8), ((void *)0));
            gm_tb38v5s9j1r32 t53;
            memset(&t53, 0, sizeof t53);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)8ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t53 + 8), t52);
            gm_l19 = t53;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)8ULL));
            if (!t55) {
                goto gm_lb7;
            }
            uint32_t t56 = GM_LD(uint32_t, ((((uint8_t *)t22) + 8) + 8));
            gm_l6 = t56;
            uint8_t t57 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8)) != 0);
            if (!t57) {
                goto gm_lb7;
            }
            void * t58 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l7 = t58;
            uint32_t t59 = gm_l6;
            void * t60 = gm_l7;
            void * t61 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttggq7ng4kp3p t62;
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t62 + 0), t59);
            GM_ST(void *, ((uint8_t *)&t62 + 8), t61);
            gm_tb38v5s9j1r32 t63;
            memset(&t63, 0, sizeof t63);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)8ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t63 + 8), t62);
            gm_l19 = t63;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)9ULL));
            if (!t65) {
                goto gm_lb8;
            }
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 8) + 8));
            gm_l8 = t66;
            gm_trqfvmjtt4sij t67;
            gm_f_list_i33ccm0((void *)&t67, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l9 = t67;
            gm_b16_8 t68 = gm_l8;
            gm_l10 = t68;
            gm_l11 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t69 = gm_l11;
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t71 = (t69 < t70);
                    if (!t71) {
                        goto gm_lb9;
                    }
                    {
                        void * t72 = GM_LD(void *, ((uint8_t *)&gm_l10));
                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t t74 = gm_l11;
                        {
                            {
                                gm_ttggq7ng4kp3p t75 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t72) + (uintptr_t)t74 * 16));
                                gm_l12 = t75;
                                void * t76 = (void *)((uint8_t *)&F.l9);
                                uint32_t t77 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                                void * t78 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
                                void * t79 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t78);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_ttggq7ng4kp3p t80;
                                GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t80 + 0), t77);
                                GM_ST(void *, ((uint8_t *)&t80 + 8), t79);
                                gm_tftb1odc739t5 t81 = gm_f_push_31airv7(t76, GM_CONV(gm_csqbqksqco2bm6, t80));
                                gm_l20 = t81;
                                uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcgksioc7kn741, 127, 27);
                                goto gm_lb12;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 127, 13);
                        }
gm_lb12:;
                    }
gm_lb11:;
                    uint64_t t83 = gm_l11;
                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                    uint64_t t86 = r84;
                    gm_l11 = t86;
                }
            }
gm_lb9:;
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
            gm_tb38v5s9j1r32 t88;
            memset(&t88, 0, sizeof t88);
            GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)9ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 8), t87);
            gm_l19 = t88;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)10ULL));
            if (!t90) {
                goto gm_lb14;
            }
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 8) + 8));
            gm_l13 = t91;
            gm_trqfvmjtt4sij t92;
            gm_f_list_nh3d5t2((void *)&t92, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l14 = t92;
            gm_b16_8 t93 = gm_l13;
            gm_l15 = t93;
            gm_l16 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t94 = gm_l16;
                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint8_t t96 = (t94 < t95);
                    if (!t96) {
                        goto gm_lb15;
                    }
                    {
                        void * t97 = GM_LD(void *, ((uint8_t *)&gm_l15));
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint64_t t99 = gm_l16;
                        {
                            {
                                void * t100 = GM_LD(void *, (((uint8_t *)t97) + (uintptr_t)t99 * 8));
                                gm_l17 = t100;
                                void * t101 = (void *)((uint8_t *)&F.l14);
                                void * t102 = gm_l17;
                                void * t103 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t102);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t104 = gm_f_push_u2jka7o(t101, t103);
                                gm_l21 = t104;
                                uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcgksioc7kn741, 132, 27);
                                goto gm_lb18;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 132, 13);
                        }
gm_lb18:;
                    }
gm_lb17:;
                    uint64_t t106 = gm_l16;
                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                    uint64_t t109 = r107;
                    gm_l16 = t109;
                }
            }
gm_lb15:;
            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
            gm_tb38v5s9j1r32 t111;
            memset(&t111, 0, sizeof t111);
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)10ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
            gm_l19 = t111;
            goto gm_lb1;
        }
gm_lb14:;
        {
            void * t112 = gm_l0;
            memmove(((uint8_t *)&gm_l19), (((uint8_t *)t112) + 8), 112);
            goto gm_lb1;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 118, 18);
    }
gm_lb1:;
    memmove(((uint8_t *)&F.l18), ((uint8_t *)&gm_l19), 112);
    void * t113 = gm_l0;
    gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t113));
    void * t115 = gm_l0;
    gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t115) + 120));
    void * t117 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t114, &(*(gm_tb38v5s9j1r32 *)((uint8_t *)&F.l18)), t116);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t117;
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
