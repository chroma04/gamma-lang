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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t2lqu8jsj511d a[3]; } gm_t8o2b5qnbhu2s;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
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
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 a[27]; } gm_tlg1vmfbpbvtq;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t8o2b5qnbhu2s) == 48 && _Alignof(gm_t8o2b5qnbhu2s) == 8 && offsetof(gm_t8o2b5qnbhu2s, a) == 0, "layout of `[3](u64, u64)`");
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlg1vmfbpbvtq) == 432 && _Alignof(gm_tlg1vmfbpbvtq) == 8 && offsetof(gm_tlg1vmfbpbvtq, a) == 0, "layout of `[27]string`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_classify_complex_insp5d8 {
    gm_frame h;
    gm_tkv1vap90vht2 l12; /* u */
    gm_t5vbkdq81qfs7 l14; /* r */
    gm_t0ialelq3ql1k l15; /* v */
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_complex_call_8so3bss {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* args */
    gm_trqfvmjtt4sij l10; /* stmts */
    void * ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compound_number_binary_p7eerjs {
    gm_frame h;
    gm_trqfvmjtt4sij l33; /* stmts */
    gm_trqfvmjtt4sij l36; /* elems */
    void * ret;
};
struct gm_fr_compound_number_elem_6d1d2ni {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compound_number_unary_4h478fs {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* stmts */
    gm_trqfvmjtt4sij l14; /* elems */
    void * ret;
};
struct gm_fr_core_call_values_qq2la05 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* stmts */
    gm_trqfvmjtt4sij l5; /* written */
    gm_tc3oht4ksth9d l13; /* e */
    void * ret;
};
struct gm_fr_describe_operand_509et87 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
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
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
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
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_format_1oelod5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6up4rm5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_86pfrlj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9rhpk5t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8fgv1v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d0lfl1k {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_davqh84 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gdra5k8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gmfdekn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h4jcodi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_iepqo20 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jn4ifp1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ko5t8rr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mcgdakk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mrlevsk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p0j9lf7 {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pdashts {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s7bl5bp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_unf194k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uubl3s3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vq3bur7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_imaginary_literal_6d41gfv {
    gm_frame h;
    void * ret;
};
struct gm_fr_is_complex_var_bk0jb4c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_compound_number_kbipjl5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_float_3jk89qu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_math_vector_09mtuao {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
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
struct gm_fr_literal_var_3sb5sti {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_math_elem_iuerhch {
    gm_frame h;
    void * ret;
};
struct gm_fr_math_vector_conversion_0cttjm0 {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* stmts */
    gm_trqfvmjtt4sij l14; /* elems */
    void * ret;
};
struct gm_fr_math_vector_method_ko2814s {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* stmts */
    gm_trqfvmjtt4sij l24; /* elems */
    gm_t8o2b5qnbhu2s l25; /* <tmp> */
    gm_trqfvmjtt4sij l34; /* elems */
    gm_trqfvmjtt4sij l39; /* elems */
    void * ret;
};
struct gm_fr_mentions_complex_constan_lrgkqac {
    gm_frame h;
    gm_t5vbkdq81qfs7 l7; /* r */
    gm_t0ialelq3ql1k l8; /* v */
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_plain_0otlpn0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_promote_real_constant_v0nhili {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* parts */
    void * ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
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
struct gm_fr_push_jis5lhu {
    gm_frame h;
    gm_t7te2tdpebbt4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_08uc6h5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_settle_complex_operands_9439n4u {
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
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_zero_9r6o7sj {
    gm_frame h;
    gm_tngtuiuskn938 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arith_op_hsstk43(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_classify_complex_insp5d8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_complex_call_8so3bss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_compound_number_binary_p7eerjs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_compound_number_elem_6d1d2ni(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_compound_number_unary_4h478fs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_core_call_values_qq2la05(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_operand_509et87(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_1oelod5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_6up4rm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_86pfrlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c8fgv1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_davqh84(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gdra5k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_gmfdekn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_iepqo20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jn4ifp1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ko5t8rr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mcgdakk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_p0j9lf7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s7bl5bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_unf194k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uubl3s3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_vq3bur7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_imaginary_literal_6d41gfv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_compound_number_kbipjl5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_literal_var_3sb5sti(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_math_elem_iuerhch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_mentions_complex_constan_lrgkqac(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_fnepb5f(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_new_var_up2m1nq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_plain_0otlpn0(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f0fls2k(void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_settle_complex_operands_9439n4u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_untyped_op_7an9o22(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cukabrstocl[432] __attribute__((aligned(8)));
extern uint8_t gm_k0mnn4ntfo5[40] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_km0lis6h9ua[40] __attribute__((aligned(8)));
extern const uint8_t gm_s06nnq6fe98bg[53];
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s25euhn4i5qkh[72];
extern const uint8_t gm_s26md5sqfb6u7[7];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s3vn3760p6488[103];
extern const uint8_t gm_s40upcq09aufa[39];
extern const uint8_t gm_s4bo4cvutg59h[16];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s6fdn1h3c830p[5];
extern const uint8_t gm_s6tn8kl12f5ru[27];
extern const uint8_t gm_s7lb197di7d72[37];
extern const uint8_t gm_s7sn5glap1gkf[7];
extern const uint8_t gm_s9lh30p1g8vti[54];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd7vgd2pm42gk[14];
extern const uint8_t gm_sdib2k2ql9mgl[9];
extern const uint8_t gm_sdinre9qvo2vq[28];
extern const uint8_t gm_se62v1mjbig2r[21];
extern const uint8_t gm_sejjhtlr11ojd[60];
extern const uint8_t gm_sf372rql24e3n[17];
extern const uint8_t gm_sfa8f3bk569gj[26];
extern const uint8_t gm_sffcmqsfdatal[47];
extern const uint8_t gm_sg1h22ve4uqmq[4];
extern const uint8_t gm_sgc02r26g8j6o[57];
extern const uint8_t gm_sgvnpogt67see[6];
extern const uint8_t gm_shjfpng3ei3du[82];
extern const uint8_t gm_sjsojd1o1489e[34];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sla6dgu7p35mv[31];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_sp66nadh171v3[19];
extern const uint8_t gm_sprospqi1g16o[7];
extern const uint8_t gm_sptjupp93c9n4[56];
extern const uint8_t gm_sq5sjasaqn114[7];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_sqhh38dkd5qvg[43];
extern const uint8_t gm_sr486u97qk9sk[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr7qn5gtrg5md[114];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srci09ctgf0djoe[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs84t3n56p1c[8];
extern const uint8_t gm_ss6ubqdvd2q7k[60];
extern const uint8_t gm_ssnn9u3skptri[75];
extern const uint8_t gm_stoidbi3vfa99[30];
extern const uint8_t gm_su3gef13fgq4c[7];
extern const uint8_t gm_sump4nk3r1lgv[8];
extern const uint8_t gm_sv6o879d2okh4[39];
extern const gm_tmpl gm_tpl1okik3clcoee;
extern const gm_tmpl gm_tpl1qq6rf1tb5ds;
extern const gm_tmpl gm_tpl2lqr5lnbpsvm;
extern const gm_tmpl gm_tpl4phv0vr9as7f;
extern const gm_tmpl gm_tpl8pvdc7qbgge6;
extern const gm_tmpl gm_tpl98as72l6ukas;
extern const gm_tmpl gm_tplb86aokjj8d2b;
extern const gm_tmpl gm_tplbo3vrh9egnst;
extern const gm_tmpl gm_tplff85tslj1ph5;
extern const gm_tmpl gm_tplftg0h8omro11;
extern const gm_tmpl gm_tplfv85d3oogq60;
extern const gm_tmpl gm_tplgj9d3igk4i62;
extern const gm_tmpl gm_tpljcgfng3gohsk;
extern const gm_tmpl gm_tpllv3q9mfan4rg;
extern const gm_tmpl gm_tplo08f1kg3k97n;
extern const gm_tmpl gm_tplovio05rtno6h;
extern const gm_tmpl gm_tplq2jm9n7vekeo;
extern const gm_tmpl gm_tplsbqgr9k4itq0;
extern const gm_tmpl gm_tplt0hjq4nb3jer;
extern const gm_tmpl gm_tplt7inuen2vfdh;
extern const gm_tmpl gm_tplurkkmrij8g4f;
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
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 144)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
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
inline __attribute__((aligned(4))) void gm_f_format_1oelod5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1oelod5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgc02r26g8j6o));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljcgfng3gohsk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6up4rm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6up4rm5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sla6dgu7p35mv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplftg0h8omro11), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_86pfrlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_86pfrlj F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9lh30p1g8vti));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1qq6rf1tb5ds), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9rhpk5t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdinre9qvo2vq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplurkkmrij8g4f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c8fgv1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c8fgv1v F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sffcmqsfdatal));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbo3vrh9egnst), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d0lfl1k F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_stoidbi3vfa99));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(29));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2lqr5lnbpsvm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_davqh84(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_davqh84 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s06nnq6fe98bg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(52));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplo08f1kg3k97n), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_gdra5k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gdra5k8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sv6o879d2okh4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt7inuen2vfdh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gmfdekn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gmfdekn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sptjupp93c9n4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq2jm9n7vekeo), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_iepqo20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_iepqo20 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3vn3760p6488));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(102));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl98as72l6ukas), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jn4ifp1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jn4ifp1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s25euhn4i5qkh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplovio05rtno6h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ko5t8rr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ko5t8rr F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se62v1mjbig2r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplff85tslj1ph5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mcgdakk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mcgdakk F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s40upcq09aufa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplb86aokjj8d2b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mrlevsk F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqhh38dkd5qvg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(42));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4phv0vr9as7f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_p0j9lf7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p0j9lf7 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7lb197di7d72));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1okik3clcoee), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pdashts F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sejjhtlr11ojd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgj9d3igk4i62), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s7bl5bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s7bl5bp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shjfpng3ei3du));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(81));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllv3q9mfan4rg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_unf194k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_unf194k F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss6ubqdvd2q7k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt0hjq4nb3jer), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uubl3s3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_uubl3s3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srs84t3n56p1c));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8pvdc7qbgge6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vq3bur7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vq3bur7 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6tn8kl12f5ru));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfv85d3oogq60), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_complex_var_bk0jb4c F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 96);
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_taari1s0m51a7 t8 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t3, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t10;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[1] = 1;
    uint8_t t11 = (gm_eq5307j5k9ms(((uint8_t *)&t8), ((uint8_t *)&t10)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_3jk89qu F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)3ULL));
            if (!t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 10, 56);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_math_vector_09mtuao F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_tb7u19loo3j87 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_math_vector_parts_49c7865((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
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
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 26, 62);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fa4sjvq();
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fv6i5s8();
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
inline __attribute__((aligned(4))) uint8_t gm_f_plain_0otlpn0(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plain_0otlpn0 F;
    void * gm_l0; /* ts */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)11ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint8_t t9 = GM_LD(uint8_t, (((((uint8_t *)&gm_l3) + 8) + 12) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
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
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 463, 53);
    }
gm_lb1:;
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
        gm_t9805qtonbsag t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t8) + (uintptr_t)t13 * 4));
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
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    uint32_t t27 = gm_l2;
    gm_t9805qtonbsag t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[4] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
        gm_td8hfgklo8dil t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 40);
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
    memmove(((uint8_t *)&gm_l2), (((uint8_t *)t8) + (uintptr_t)t13 * 32), 32);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_t66j1im9ip128 t26 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)&gm_l2));
    gm_td8hfgklo8dil t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 36, 0);
    GM_ST(gm_t66j1im9ip128, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 40);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
        gm_tqetrgo0q24b5 t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 48);
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
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t8) + (uintptr_t)t13 * 40), 40);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_trqfvmjtt4sij t26 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_tqetrgo0q24b5 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 41, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 42, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 44, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[40] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_td8hfgklo8dil gm_l2;
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 56);
    gm_tqetrgo0q24b5 t6;
    gm_f_pop_dt2bd2s((void *)&t6, t5);
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 616);
    gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
    {
        {
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 656);
            gm_f_pop_8sodrk9((void *)&gm_l2, t11);
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l2))[32] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l2), 32);
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, ((uint8_t *)t13));
            gm_f_restore_scope_edqq1v8(t14, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7koe9lk F;
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
        gm_b16_8 t9 = gm_f_empty_slice_qm5tp1g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_emnjbt3(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)&F.l1), 48);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 48)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jis5lhu F;
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
        gm_b16_8 t9 = gm_f_empty_slice_fa4sjvq();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_08uc6h5(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 144), ((uint8_t *)&F.l1), 144);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 144)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_fv6i5s8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5719f1b(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_08uc6h5 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)144ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)a1));
    gm_f_truncate_7r782uc(t3, t4);
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 1648);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    gm_f_truncate_daelje0(t6, t7);
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 1728);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    gm_f_truncate_dsb08pk(t9, t10);
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 1688);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    gm_f_truncate_upeumbt(t12, t13);
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_9r6o7sj F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t2;
    gm_f_zero_s25l990((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t2);
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
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_classify_complex_insp5d8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_classify_complex_insp5d8 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* record */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    gm_ts6oqppjh0ba3 gm_l6; /* op */
    void * gm_l7; /* lhs */
    void * gm_l8; /* rhs */
    gm_tqs5p4n2tq19v gm_l9; /* l */
    gm_tqs5p4n2tq19v gm_l10; /* r */
    void * gm_l11; /* l */
    uint8_t gm_l13; /* mentions */
    gm_tqs5p4n2tq19v gm_l16; /* c */
    GM_STACK_CHECK(&F, 1073u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqs5p4n2tq19v gm_l17;
    gm_tqs5p4n2tq19v gm_l18;
    uint8_t gm_l19;
    gm_tkv1vap90vht2 gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    gm_tkv1vap90vht2 gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    gm_tkv1vap90vht2 gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    uint8_t gm_l32;
    gm_tftb1odc739t5 gm_l33;
    void * t40 = gm_l1;
    {
        {
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)9ULL));
            if (!t42) {
                goto gm_lb2;
            }
            void * t43 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
            gm_l3 = t43;
            void * t44 = gm_l0;
            void * t45 = gm_l3;
            gm_tqs5p4n2tq19v t46 = gm_f_classify_complex_insp5d8((void *)NULL, cx, NULL, t44, t45, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t46;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)12ULL));
            if (!t48) {
                goto gm_lb3;
            }
            uint8_t t49 = GM_LD(uint8_t, (((((uint8_t *)t40) + 8) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)0ULL));
            if (!t50) {
                goto gm_lb3;
            }
            void * t51 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 8));
            gm_l4 = t51;
            void * t52 = gm_l0;
            void * t53 = gm_l4;
            gm_tqs5p4n2tq19v t54 = gm_f_classify_complex_insp5d8((void *)NULL, cx, NULL, t52, t53, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t54;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)12ULL));
            if (!t56) {
                goto gm_lb4;
            }
            void * t57 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 8));
            gm_l5 = t57;
            void * t58 = gm_l0;
            void * t59 = gm_l5;
            gm_tqs5p4n2tq19v t60 = gm_f_classify_complex_insp5d8((void *)NULL, cx, NULL, t58, t59, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t60;
            uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)&gm_l18));
            void * t62 = gm_l0;
            void * t63 = gm_l1;
            gm_f_try_untyped_8j8up35((void *)&gm_l20, (void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l20))[208] != 0);
                    if (t64) {
                        goto gm_lb6;
                    }
                    gm_l19 = ((uint8_t)0);
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    gm_l19 = ((uint8_t)1);
                    goto gm_lb5;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 72, 93);
            }
gm_lb5:;
            uint8_t t65 = gm_l19;
            gm_tqs5p4n2tq19v t66;
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), t61);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 1), t65);
            gm_l17 = t66;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)13ULL));
            if (!t68) {
                goto gm_lb8;
            }
            gm_ts6oqppjh0ba3 t69 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t40) + 8) + 8));
            gm_l6 = t69;
            void * t70 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 8));
            gm_l7 = t70;
            void * t71 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 16));
            gm_l8 = t71;
            void * t72 = gm_l0;
            void * t73 = gm_l7;
            gm_tqs5p4n2tq19v t74 = gm_f_classify_complex_insp5d8((void *)NULL, cx, NULL, t72, t73, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t74;
            void * t75 = gm_l0;
            void * t76 = gm_l8;
            gm_tqs5p4n2tq19v t77 = gm_f_classify_complex_insp5d8((void *)NULL, cx, NULL, t75, t76, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t77;
            uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
            if (t78) {
                gm_l21 = ((uint8_t)1);
            } else {
                uint8_t t79 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                gm_l21 = t79;
            }
            uint8_t t80 = gm_l21;
            gm_ts6oqppjh0ba3 t81 = gm_l6;
            uint8_t t82 = gm_f_untyped_op_7an9o22(t81);
            if (t82) {
                uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 1));
                gm_l23 = t83;
            } else {
                gm_l23 = ((uint8_t)0);
            }
            uint8_t t84 = gm_l23;
            if (t84) {
                uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 1));
                gm_l22 = t85;
            } else {
                gm_l22 = ((uint8_t)0);
            }
            uint8_t t86 = gm_l22;
            gm_tqs5p4n2tq19v t87;
            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), t80);
            GM_ST(uint8_t, ((uint8_t *)&t87 + 1), t86);
            gm_l17 = t87;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)1ULL));
            if (!t89) {
                goto gm_lb9;
            }
            void * t90 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
            gm_l11 = t90;
            void * t91 = gm_l11;
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)t91) + 98));
            void * t93 = gm_l0;
            void * t94 = gm_l1;
            gm_f_try_untyped_8j8up35((void *)&gm_l25, (void *)NULL, cx, NULL, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l25))[208] != 0);
                    if (t95) {
                        goto gm_lb11;
                    }
                    gm_l24 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    gm_l24 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 78, 51);
            }
gm_lb10:;
            uint8_t t96 = gm_l24;
            gm_tqs5p4n2tq19v t97;
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), t92);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 1), t96);
            gm_l17 = t97;
            goto gm_lb1;
        }
gm_lb9:;
        {
            {
                {
                    uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)6ULL));
                    if (!t99) {
                        goto gm_lb15;
                    }
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t101 = (uint8_t)(t100 == ((uint8_t)22ULL));
                    if (!t101) {
                        goto gm_lb16;
                    }
                    goto gm_lb14;
                }
gm_lb16:;
                goto gm_lb13;
            }
gm_lb14:;
            void * t102 = gm_l0;
            void * t103 = gm_l1;
            gm_f_try_untyped_8j8up35((void *)&F.l12, (void *)NULL, cx, NULL, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = ((uint8_t)0);
            {
                {
                    uint8_t t104 = (uint8_t)((((uint8_t *)&F.l12))[208] != 0);
                    if (!t104) {
                        goto gm_lb18;
                    }
                    memmove(((uint8_t *)&F.l14), ((uint8_t *)&F.l12), 208);
                    {
                        {
                            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)0ULL));
                            if (!t106) {
                                goto gm_lb20;
                            }
                            memmove(((uint8_t *)&F.l15), (((uint8_t *)&F.l14) + 8), 200);
                            {
                                {
                                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                                    uint8_t t108 = (uint8_t)(t107 == ((uint8_t)4ULL));
                                    if (!t108) {
                                        goto gm_lb22;
                                    }
                                    gm_l26 = ((uint8_t)1);
                                    goto gm_lb21;
                                }
gm_lb22:;
                                {
                                    gm_l26 = ((uint8_t)0);
                                    goto gm_lb21;
                                }
gm_lb23:;
                                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 83, 36);
                            }
gm_lb21:;
                            uint8_t t109 = gm_l26;
                            if (!t109) {
                                goto gm_lb20;
                            }
                            gm_l13 = ((uint8_t)1);
                            goto gm_lb19;
                        }
gm_lb20:;
                    }
gm_lb19:;
                    goto gm_lb17;
                }
gm_lb18:;
            }
gm_lb17:;
            uint8_t t110 = gm_l13;
            {
                {
                    uint8_t t111 = (uint8_t)((((uint8_t *)&F.l12))[208] != 0);
                    if (t111) {
                        goto gm_lb25;
                    }
                    gm_l27 = ((uint8_t)0);
                    goto gm_lb24;
                }
gm_lb25:;
                {
                    gm_l27 = ((uint8_t)1);
                    goto gm_lb24;
                }
gm_lb26:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 85, 44);
            }
gm_lb24:;
            uint8_t t112 = gm_l27;
            gm_tqs5p4n2tq19v t113;
            GM_ST(uint8_t, ((uint8_t *)&t113 + 0), t110);
            GM_ST(uint8_t, ((uint8_t *)&t113 + 1), t112);
            gm_l17 = t113;
            goto gm_lb1;
        }
gm_lb13:;
        {
            void * t114 = gm_l0;
            void * t115 = gm_l1;
            gm_f_try_untyped_8j8up35((void *)&gm_l29, (void *)NULL, cx, NULL, t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l29))[208] != 0);
                    if (t116) {
                        goto gm_lb29;
                    }
                    gm_l28 = ((uint8_t)0);
                    goto gm_lb28;
                }
gm_lb29:;
                {
                    gm_l28 = ((uint8_t)1);
                    goto gm_lb28;
                }
gm_lb30:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 87, 42);
            }
gm_lb28:;
            uint8_t t117 = gm_l28;
            gm_tqs5p4n2tq19v t118;
            GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t118 + 1), t117);
            gm_l17 = t118;
            goto gm_lb1;
        }
gm_lb27:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 69, 43);
    }
gm_lb1:;
    gm_tqs5p4n2tq19v t119 = gm_l17;
    gm_l16 = t119;
    {
        {
            uint8_t t120 = gm_l2;
            if (!t120) {
                goto gm_lb32;
            }
            uint8_t t121 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
            if (!t121) {
                goto gm_lb32;
            }
            void * t122 = gm_l1;
            {
                {
                    uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t122) + 8) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)13ULL));
                    if (!t124) {
                        goto gm_lb34;
                    }
                    gm_l31 = ((uint8_t)1);
                    goto gm_lb33;
                }
gm_lb34:;
                {
                    gm_l31 = ((uint8_t)0);
                    goto gm_lb33;
                }
gm_lb35:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 89, 35);
            }
gm_lb33:;
            uint8_t t125 = gm_l31;
            if (t125) {
                gm_l30 = ((uint8_t)1);
            } else {
                void * t126 = gm_l1;
                {
                    {
                        uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t126) + 8) + 0));
                        uint8_t t128 = (uint8_t)(t127 == ((uint8_t)12ULL));
                        if (!t128) {
                            goto gm_lb37;
                        }
                        gm_l32 = ((uint8_t)1);
                        goto gm_lb36;
                    }
gm_lb37:;
                    {
                        gm_l32 = ((uint8_t)0);
                        goto gm_lb36;
                    }
gm_lb38:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 89, 59);
                }
gm_lb36:;
                uint8_t t129 = gm_l32;
                gm_l30 = t129;
            }
            uint8_t t130 = gm_l30;
            if (!t130) {
                goto gm_lb32;
            }
            void * t131 = gm_l0;
            void * t132 = (void *)(((uint8_t *)t131) + 1296);
            void * t133 = gm_l1;
            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 1));
            gm_tftb1odc739t5 t135 = gm_f_put_f0fls2k(t132, t133, t134);
            gm_l33 = t135;
            uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
            if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srci09ctgf0djoe, 89, 82);
            goto gm_lb31;
        }
gm_lb32:;
    }
gm_lb31:;
    gm_tqs5p4n2tq19v t137 = gm_l16;
    F.ret = t137;
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
__attribute__((aligned(4))) void * gm_f_complex_call_8so3bss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_complex_call_8so3bss F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* args0 */
    uint8_t gm_l3; /* swap */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* f */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* a */
    void * gm_l11; /* first */
    gm_t5fru0lp11n7t gm_l12; /* <temporary array> */
    void * gm_l13; /* call */
    GM_STACK_CHECK(&F, 830u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t9805qtonbsag gm_l14;
    uint32_t gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t7 = gm_l0;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sump4nk3r1lgv));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(7));
    gm_b16_8 t9 = gm_l1;
    gm_t9805qtonbsag t10 = gm_f_core_fn_uts2vhe((void *)NULL, cx, NULL, t7, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t10;
    uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
    if (t11) {
        uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
        gm_l15 = t12;
    } else {
        void * t13 = gm_l0;
        gm_tl874t2atnlh3 t14 = gm_l4;
        gm_b16_8 t15 = gm_l1;
        gm_tnpifv20irnph t16;
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
        gm_f_format_unf194k((void *)&gm_l16, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srci09ctgf0djoe, 327, 24);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_b0_1 t20 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t13, t14, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, ((uint8_t *)t21));
        gm_tl874t2atnlh3 t23 = gm_l4;
        void * t24 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t24;
        goto gm_fret;
    }
    uint32_t t25 = gm_l15;
    gm_l5 = t25;
    gm_trqfvmjtt4sij t26;
    gm_f_list_s1kpkpr((void *)&t26, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t26;
    gm_b16_8 t27 = gm_l2;
    gm_l7 = t27;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l8;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t33 = gm_l8;
                {
                    {
                        void * t34 = GM_LD(void *, (((uint8_t *)t31) + (uintptr_t)t33 * 8));
                        gm_l9 = t34;
                        void * t35 = (void *)((uint8_t *)&F.l6);
                        void * t36 = gm_l9;
                        gm_tftb1odc739t5 t37 = gm_f_push_pf02nv6(t35, t36);
                        gm_l17 = t37;
                        uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srci09ctgf0djoe, 331, 22);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 331, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t39 = gm_l8;
            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
            uint64_t t42 = r40;
            gm_l8 = t42;
        }
    }
gm_lb1:;
    uint8_t t43 = gm_l3;
    if (t43) {
        gm_trqfvmjtt4sij t44;
        gm_f_list_ir0h3tc_2((void *)&t44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t44;
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, ((uint8_t *)&F.l6));
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t47, gm_srci09ctgf0djoe, 335, 31);
        void * t48 = GM_LD(void *, (((uint8_t *)t46) + (uintptr_t)((uint64_t)0ULL) * 8));
        void * t49 = (void *)((uint8_t *)&F.l10);
        void * t50 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t45, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t50;
        void * t51 = gm_l0;
        uint32_t t52 = gm_l5;
        void * t53 = GM_LD(void *, ((uint8_t *)&F.l6));
        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t54, gm_srci09ctgf0djoe, 336, 49);
        void * t55 = GM_LD(void *, (((uint8_t *)t53) + (uintptr_t)((uint64_t)1ULL) * 8));
        void * t56 = gm_l11;
        gm_t5fru0lp11n7t t57;
        GM_ST(void *, ((uint8_t *)&t57 + 0), t55);
        GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
        gm_l12 = t57;
        void * t58 = (void *)((uint8_t *)&gm_l12);
        uint64_t t59 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t60 = (uintptr_t)((uint64_t)2ULL);
        if (t59 > t60 || t60 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t59, t60, ((uint64_t)2ULL), gm_srci09ctgf0djoe, 336, 40);
        uint64_t t61 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t58 + (uintptr_t)((uint64_t)0ULL) * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(t61));
        gm_tl874t2atnlh3 t63 = gm_l4;
        void * t64 = gm_f_core_call_values_qq2la05((void *)NULL, cx, NULL, t51, t52, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t64;
        void * t65 = gm_l13;
        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t65));
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        void * t68 = gm_l13;
        gm_tl874t2atnlh3 t69 = gm_l4;
        void * t70 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t66, t67, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t70;
        goto gm_fret;
    }
    void * t71 = gm_l0;
    uint32_t t72 = gm_l5;
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
    gm_tl874t2atnlh3 t75 = gm_l4;
    void * t76 = gm_f_core_call_values_qq2la05((void *)NULL, cx, NULL, t71, t72, t74, t75);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t76;
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
__attribute__((aligned(4))) void * gm_f_compound_number_binary_p7eerjs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_compound_number_binary_p7eerjs F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs0 */
    void * gm_l3; /* r0 */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* lhs */
    void * gm_l7; /* r */
    gm_b16_8 gm_l8; /* opn */
    gm_t88rcucg1b7uk gm_l9; /* lt */
    gm_t88rcucg1b7uk gm_l10; /* rt */
    uint8_t gm_l11; /* promoted */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    void * gm_l13; /* p */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    void * gm_l15; /* p */
    uint8_t gm_l16; /* left_is_agg */
    gm_t88rcucg1b7uk gm_l17; /* agg */
    gm_t88rcucg1b7uk gm_l18; /* elem */
    gm_t88rcucg1b7uk gm_l19; /* other */
    uint8_t gm_l20; /* both */
    uint8_t gm_l21; /* complex */
    gm_b16_8 gm_l22; /* what */
    uint8_t gm_l23; /* shift */
    uint8_t gm_l24; /* scales */
    gm_b16_8 gm_l25; /* hint */
    gm_b16_8 gm_l26; /* l */
    gm_b16_8 gm_l27; /* rr */
    gm_b16_8 gm_l28; /* shown */
    gm_b16_8 gm_l29; /* name */
    uint8_t gm_l30; /* swap */
    gm_t5fru0lp11n7t gm_l31; /* <temporary array> */
    uint64_t gm_l32; /* n */
    void * gm_l34; /* l */
    void * gm_l35; /* rv */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* i */
    void * gm_l40; /* a */
    void * gm_l41; /* b */
    void * gm_l42; /* e */
    gm_t9qkh0rsooc8d gm_l43; /* f */
    gm_ts6oqppjh0ba3 gm_l44; /* top */
    void * gm_l45; /* arr */
    GM_STACK_CHECK(&F, 4297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l46;
    uint8_t gm_l47;
    uint8_t gm_l48;
    gm_tkn42u7na3t81 gm_l49;
    void * gm_l50;
    gm_tkn42u7na3t81 gm_l51;
    void * gm_l52;
    gm_t88rcucg1b7uk gm_l53;
    gm_tkn42u7na3t81 gm_l54;
    gm_t88rcucg1b7uk gm_l55;
    gm_t88rcucg1b7uk gm_l56;
    gm_b16_8 gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tabb710d9mq2e gm_l59;
    uint8_t gm_l60;
    uint8_t gm_l61;
    uint8_t gm_l62;
    uint8_t gm_l63;
    uint8_t gm_l64;
    gm_tabb710d9mq2e gm_l65;
    uint8_t gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tabb710d9mq2e gm_l68;
    uint8_t gm_l69;
    uint8_t gm_l70;
    uint8_t gm_l71;
    gm_b16_8 gm_l72;
    gm_b16_8 gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_tabb710d9mq2e gm_l75;
    uint8_t gm_l76;
    gm_t9qkh0rsooc8d gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_b16_8 gm_l79;
    gm_b16_8 gm_l80;
    gm_b16_8 gm_l81;
    uint8_t gm_l82;
    gm_tb7u19loo3j87 gm_l83;
    gm_tlcm4lvu3g1a8 gm_l84;
    void * gm_l85;
    void * gm_l86;
    uint8_t gm_l87;
    gm_tftb1odc739t5 gm_l88;
    gm_ts6oqppjh0ba3 gm_l89;
    gm_tftb1odc739t5 gm_l90;
    {
        void * t48 = gm_l0;
        void * t49 = GM_LD(void *, ((uint8_t *)t48));
        void * t50 = (void *)(((uint8_t *)t49) + 8);
        gm_l5 = t50;
        void * t51 = gm_l2;
        gm_l6 = t51;
        void * t52 = gm_l3;
        gm_l7 = t52;
        gm_ts6oqppjh0ba3 t53 = gm_l1;
        gm_b16_8 t54 = gm_f_binop_text_0ljfc0r(t53);
        gm_l8 = t54;
        void * t55 = gm_l0;
        gm_ts6oqppjh0ba3 t56 = gm_l1;
        void * t57 = gm_l6;
        void * t58 = gm_l7;
        gm_tl874t2atnlh3 t59 = gm_l4;
        gm_b0_1 t60 = gm_f_settle_complex_operands_9439n4u((void *)NULL, cx, NULL, t55, t56, t57, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t61 = gm_l0;
        void * t62 = (void *)(((uint8_t *)t61) + 96);
        void * t63 = gm_l0;
        void * t64 = GM_LD(void *, ((uint8_t *)t63));
        void * t65 = gm_l6;
        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t65));
        gm_t88rcucg1b7uk t67 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t62, t64, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t67;
        void * t68 = gm_l0;
        void * t69 = (void *)(((uint8_t *)t68) + 96);
        void * t70 = gm_l0;
        void * t71 = GM_LD(void *, ((uint8_t *)t70));
        void * t72 = gm_l7;
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t72));
        gm_t88rcucg1b7uk t74 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t69, t71, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t74;
        void * t75 = gm_l5;
        gm_t88rcucg1b7uk t76 = gm_l9;
        uint8_t t77 = gm_f_is_error_8k27gd1(t75, GM_CONV(gm_csguankko4lmhd, t76));
        if (t77) {
            gm_l46 = ((uint8_t)1);
        } else {
            void * t78 = gm_l5;
            gm_t88rcucg1b7uk t79 = gm_l10;
            uint8_t t80 = gm_f_is_error_8k27gd1(t78, GM_CONV(gm_csguankko4lmhd, t79));
            gm_l46 = t80;
        }
        uint8_t t81 = gm_l46;
        if (t81) {
            void * t82 = gm_l0;
            void * t83 = GM_LD(void *, ((uint8_t *)t82));
            gm_tl874t2atnlh3 t84 = gm_l4;
            void * t85 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t85;
            goto gm_fret;
        }
        gm_ts6oqppjh0ba3 t86 = gm_l1;
        gm_t9qkh0rsooc8d t87;
        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)0ULL));
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t86) + 0));
        uint8_t t89 = (uint8_t)(t88 == ((uint8_t)0ULL));
        if (t89) {
            gm_t9qkh0rsooc8d t90 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t86) + 1));
            t89 = (uint8_t)gm_eqehhdtpeti1(((uint8_t *)&t90), ((uint8_t *)&t87));
        }
        uint8_t t91 = (uint8_t)(t89);
        gm_ts6oqppjh0ba3 t92;
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t92 + 1), t87);
        if (t91) {
            gm_l48 = ((uint8_t)1);
        } else {
            gm_ts6oqppjh0ba3 t93 = gm_l1;
            gm_t9qkh0rsooc8d t94;
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)0ULL));
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)1ULL));
            if (t96) {
                gm_t9qkh0rsooc8d t97 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t93) + 1));
                t96 = (uint8_t)gm_eqehhdtpeti1(((uint8_t *)&t97), ((uint8_t *)&t94));
            }
            uint8_t t98 = (uint8_t)(t96);
            gm_ts6oqppjh0ba3 t99;
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t99 + 1), t94);
            gm_l48 = t98;
        }
        uint8_t t100 = gm_l48;
        if (t100) {
            gm_l47 = ((uint8_t)1);
        } else {
            gm_ts6oqppjh0ba3 t101 = gm_l1;
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t101) + 0));
            uint8_t t103 = (uint8_t)((t102 == ((uint8_t)4ULL)));
            gm_ts6oqppjh0ba3 t104;
            GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)4ULL));
            gm_l47 = t103;
        }
    }
    {
        uint8_t t105 = gm_l47;
        if (t105) {
            gm_l11 = ((uint8_t)0);
            {
                {
                    void * t106 = gm_l0;
                    void * t107 = GM_LD(void *, ((uint8_t *)t106));
                    gm_t88rcucg1b7uk t108 = gm_l9;
                    gm_tkn42u7na3t81 t109 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t107, t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l49 = t109;
                    uint8_t t110 = (uint8_t)((((uint8_t *)&gm_l49))[4] != 0);
                    if (!t110) {
                        goto gm_lb2;
                    }
                    gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l49));
                    gm_l12 = t111;
                    gm_ts6oqppjh0ba3 t112 = gm_l1;
                    uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&t112) + 0));
                    uint8_t t114 = (uint8_t)(!(t113 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t115;
                    GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)4ULL));
                    if (t114) {
                        {
                            {
                                void * t116 = gm_l0;
                                void * t117 = gm_l7;
                                gm_t88rcucg1b7uk t118 = gm_l9;
                                gm_t88rcucg1b7uk t119 = gm_l12;
                                void * t120 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t116, t117, t118, t119);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l50 = t120;
                                uint8_t t121 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l50)) != 0);
                                if (!t121) {
                                    goto gm_lb4;
                                }
                                void * t122 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                gm_l13 = t122;
                                void * t123 = gm_l13;
                                gm_l7 = t123;
                                gm_t88rcucg1b7uk t124 = gm_l9;
                                gm_l10 = t124;
                                gm_l11 = ((uint8_t)1);
                                goto gm_lb3;
                            }
gm_lb4:;
                        }
gm_lb3:;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
            }
gm_lb1:;
            uint8_t t125 = gm_l11;
            uint8_t t126 = (uint8_t)!t125;
            if (t126) {
                {
                    {
                        void * t127 = gm_l0;
                        void * t128 = GM_LD(void *, ((uint8_t *)t127));
                        gm_t88rcucg1b7uk t129 = gm_l10;
                        gm_tkn42u7na3t81 t130 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t128, t129);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l51 = t130;
                        uint8_t t131 = (uint8_t)((((uint8_t *)&gm_l51))[4] != 0);
                        if (!t131) {
                            goto gm_lb6;
                        }
                        gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l51));
                        gm_l14 = t132;
                        {
                            {
                                void * t133 = gm_l0;
                                void * t134 = gm_l6;
                                gm_t88rcucg1b7uk t135 = gm_l10;
                                gm_t88rcucg1b7uk t136 = gm_l14;
                                void * t137 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t133, t134, t135, t136);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l52 = t137;
                                uint8_t t138 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l52)) != 0);
                                if (!t138) {
                                    goto gm_lb8;
                                }
                                void * t139 = GM_LD(void *, ((uint8_t *)&gm_l52));
                                gm_l15 = t139;
                                void * t140 = gm_l15;
                                gm_l6 = t140;
                                gm_t88rcucg1b7uk t141 = gm_l10;
                                gm_l9 = t141;
                                goto gm_lb7;
                            }
gm_lb8:;
                        }
gm_lb7:;
                        goto gm_lb5;
                    }
gm_lb6:;
                }
gm_lb5:;
            }
        }
    }
    {
        void * t142 = gm_l0;
        gm_t88rcucg1b7uk t143 = gm_l9;
        uint8_t t144 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t142, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t144;
        uint8_t t145 = gm_l16;
        if (t145) {
            gm_t88rcucg1b7uk t146 = gm_l9;
            gm_l53 = t146;
        } else {
            gm_t88rcucg1b7uk t147 = gm_l10;
            gm_l53 = t147;
        }
        gm_t88rcucg1b7uk t148 = gm_l53;
        gm_l17 = t148;
        void * t149 = gm_l0;
        gm_t88rcucg1b7uk t150 = gm_l17;
        gm_tkn42u7na3t81 t151 = gm_f_compound_number_elem_6d1d2ni((void *)NULL, cx, NULL, t149, t150);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l54 = t151;
        uint8_t t152 = (uint8_t)((((uint8_t *)&gm_l54))[4] != 0);
        if (t152) {
            gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l54));
            gm_l55 = t153;
        } else {
            void * t154 = gm_l0;
            void * t155 = GM_LD(void *, ((uint8_t *)t154));
            gm_tl874t2atnlh3 t156 = gm_l4;
            void * t157 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t157;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t158 = gm_l55;
        gm_l18 = t158;
        uint8_t t159 = gm_l16;
        if (t159) {
            gm_t88rcucg1b7uk t160 = gm_l10;
            gm_l56 = t160;
        } else {
            gm_t88rcucg1b7uk t161 = gm_l9;
            gm_l56 = t161;
        }
        gm_t88rcucg1b7uk t162 = gm_l56;
        gm_l19 = t162;
        void * t163 = gm_l0;
        gm_t88rcucg1b7uk t164 = gm_l19;
        uint8_t t165 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t163, t164);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t165;
        void * t166 = gm_l5;
        gm_t88rcucg1b7uk t167 = gm_l17;
        uint8_t t168 = gm_f_is_math_vector_09mtuao(t166, GM_CONV(gm_csguankko4lmhd, t167));
        uint8_t t169 = (uint8_t)!t168;
        gm_l21 = t169;
        uint8_t t170 = gm_l21;
        if (t170) {
            gm_b16_8 t171;
            GM_ST(void *, (uint8_t *)&t171, (void *)(gm_s4bo4cvutg59h));
            GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(15));
            gm_l57 = t171;
        } else {
            gm_b16_8 t172;
            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sr486u97qk9sk));
            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(12));
            gm_l57 = t172;
        }
        gm_b16_8 t173 = gm_l57;
        gm_l22 = t173;
        {
            {
                {
                    {
                        uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t175 = (uint8_t)(t174 == ((uint8_t)16ULL));
                        if (!t175) {
                            goto gm_lb12;
                        }
                        goto gm_lb11;
                    }
gm_lb12:;
                    {
                        uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t177 = (uint8_t)(t176 == ((uint8_t)17ULL));
                        if (!t177) {
                            goto gm_lb13;
                        }
                        goto gm_lb11;
                    }
gm_lb13:;
                    {
                        uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t179 = (uint8_t)(t178 == ((uint8_t)18ULL));
                        if (!t179) {
                            goto gm_lb14;
                        }
                        goto gm_lb11;
                    }
gm_lb14:;
                    {
                        uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t181 = (uint8_t)(t180 == ((uint8_t)19ULL));
                        if (!t181) {
                            goto gm_lb15;
                        }
                        goto gm_lb11;
                    }
gm_lb15:;
                    goto gm_lb10;
                }
gm_lb11:;
                void * t182 = gm_l0;
                gm_tl874t2atnlh3 t183 = gm_l4;
                gm_b16_8 t184 = gm_l22;
                gm_tnpifv20irnph t185;
                GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t184);
                gm_f_format_vq3bur7((void *)&gm_l58, (void *)NULL, cx, NULL, t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srci09ctgf0djoe, 195, 28);
                gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                gm_b0_1 t189 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t182, t183, t188);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t190 = gm_l0;
                void * t191 = GM_LD(void *, ((uint8_t *)t190));
                gm_tl874t2atnlh3 t192 = gm_l4;
                void * t193 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t191, t192);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t193;
                goto gm_fret;
            }
gm_lb10:;
            {
                goto gm_lb9;
            }
gm_lb16:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 193, 5);
        }
gm_lb9:;
    }
    {
        gm_ts6oqppjh0ba3 t194 = gm_l1;
        gm_t9qkh0rsooc8d t195;
        GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)3ULL));
        gm_t9qkh0rsooc8d t196;
        GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)3ULL));
        gm_t9qkh0rsooc8d t197;
        GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)3ULL));
        gm_t9qkh0rsooc8d t198;
        GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t199;
        GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t199 + 1), t195);
        gm_ts6oqppjh0ba3 t200;
        GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t200 + 1), t196);
        gm_ts6oqppjh0ba3 t201;
        GM_ST(uint8_t, ((uint8_t *)&t201 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t201 + 1), t197);
        gm_ts6oqppjh0ba3 t202;
        GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t202 + 1), t198);
        gm_t6htphat2tk14 t203;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t203 + 0), t199);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t203 + 2), t200);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t203 + 4), t201);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t203 + 6), t202);
        uint8_t t204 = 0;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t203) + 0, ((uint8_t *)&t194))) t204 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t203) + 2, ((uint8_t *)&t194))) t204 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t203) + 4, ((uint8_t *)&t194))) t204 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t203) + 6, ((uint8_t *)&t194))) t204 = 1;
        uint8_t t205 = (uint8_t)(t204);
        if (t205) {
            void * t206 = gm_l0;
            gm_tl874t2atnlh3 t207 = gm_l4;
            gm_b16_8 t208 = gm_l8;
            gm_b16_8 t209 = gm_l22;
            gm_t6j6smj7qoctu t210;
            GM_ST(gm_b16_8, ((uint8_t *)&t210 + 0), t208);
            GM_ST(gm_b16_8, ((uint8_t *)&t210 + 16), t209);
            gm_f_format_1oelod5((void *)&gm_l59, (void *)NULL, cx, NULL, &t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t211 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
            if (!t211) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 8, gm_srci09ctgf0djoe, 201, 24);
            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
            gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
            gm_b0_1 t214 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t206, t207, t213);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t215 = gm_l0;
            void * t216 = GM_LD(void *, ((uint8_t *)t215));
            gm_tl874t2atnlh3 t217 = gm_l4;
            void * t218 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t216, t217);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t218;
            goto gm_fret;
        }
        {
            {
                uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t220 = (uint8_t)(t219 == ((uint8_t)3ULL));
                if (!t220) {
                    goto gm_lb18;
                }
                gm_l61 = ((uint8_t)1);
                goto gm_lb17;
            }
gm_lb18:;
            {
                gm_l61 = ((uint8_t)0);
                goto gm_lb17;
            }
gm_lb19:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 204, 14);
        }
gm_lb17:;
        uint8_t t221 = gm_l61;
        if (t221) {
            gm_l60 = ((uint8_t)1);
        } else {
            gm_ts6oqppjh0ba3 t222 = gm_l1;
            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&t222) + 0));
            uint8_t t224 = (uint8_t)((t223 == ((uint8_t)6ULL)));
            gm_ts6oqppjh0ba3 t225;
            GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)6ULL));
            gm_l60 = t224;
        }
        uint8_t t226 = gm_l60;
        gm_l23 = t226;
        {
            {
                uint8_t t227 = gm_l21;
                if (!t227) {
                    goto gm_lb21;
                }
                gm_ts6oqppjh0ba3 t228 = gm_l1;
                gm_t9qkh0rsooc8d t229;
                GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)0ULL));
                uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&t228) + 0));
                uint8_t t231 = (uint8_t)(t230 == ((uint8_t)0ULL));
                if (t231) {
                    gm_t9qkh0rsooc8d t232 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t228) + 1));
                    t231 = (uint8_t)gm_eqehhdtpeti1(((uint8_t *)&t232), ((uint8_t *)&t229));
                }
                uint8_t t233 = (uint8_t)(t231);
                gm_ts6oqppjh0ba3 t234;
                GM_ST(uint8_t, ((uint8_t *)&t234 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t234 + 1), t229);
                if (t233) {
                    gm_l64 = ((uint8_t)1);
                } else {
                    gm_ts6oqppjh0ba3 t235 = gm_l1;
                    gm_t9qkh0rsooc8d t236;
                    GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)0ULL));
                    uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&t235) + 0));
                    uint8_t t238 = (uint8_t)(t237 == ((uint8_t)1ULL));
                    if (t238) {
                        gm_t9qkh0rsooc8d t239 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t235) + 1));
                        t238 = (uint8_t)gm_eqehhdtpeti1(((uint8_t *)&t239), ((uint8_t *)&t236));
                    }
                    uint8_t t240 = (uint8_t)(t238);
                    gm_ts6oqppjh0ba3 t241;
                    GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t241 + 1), t236);
                    gm_l64 = t240;
                }
                uint8_t t242 = gm_l64;
                if (t242) {
                    gm_l63 = ((uint8_t)1);
                } else {
                    gm_ts6oqppjh0ba3 t243 = gm_l1;
                    gm_t9qkh0rsooc8d t244;
                    GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)0ULL));
                    uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&t243) + 0));
                    uint8_t t246 = (uint8_t)(t245 == ((uint8_t)2ULL));
                    if (t246) {
                        gm_t9qkh0rsooc8d t247 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t243) + 1));
                        t246 = (uint8_t)gm_eqehhdtpeti1(((uint8_t *)&t247), ((uint8_t *)&t244));
                    }
                    uint8_t t248 = (uint8_t)(t246);
                    gm_ts6oqppjh0ba3 t249;
                    GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t249 + 1), t244);
                    gm_l63 = t248;
                }
                uint8_t t250 = gm_l63;
                if (t250) {
                    gm_l62 = ((uint8_t)1);
                } else {
                    gm_ts6oqppjh0ba3 t251 = gm_l1;
                    uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&t251) + 0));
                    uint8_t t253 = (uint8_t)((t252 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t254;
                    GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)4ULL));
                    gm_l62 = t253;
                }
                uint8_t t255 = gm_l62;
                uint8_t t256 = (uint8_t)!t255;
                if (!t256) {
                    goto gm_lb21;
                }
                void * t257 = gm_l0;
                gm_tl874t2atnlh3 t258 = gm_l4;
                gm_b16_8 t259 = gm_l8;
                gm_tnpifv20irnph t260;
                GM_ST(gm_b16_8, ((uint8_t *)&t260 + 0), t259);
                gm_f_format_s7bl5bp((void *)&gm_l65, (void *)NULL, cx, NULL, t260);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                if (!t261) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srci09ctgf0djoe, 206, 24);
                gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
                gm_b0_1 t264 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t257, t258, t263);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t265 = gm_l0;
                void * t266 = GM_LD(void *, ((uint8_t *)t265));
                gm_tl874t2atnlh3 t267 = gm_l4;
                void * t268 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t266, t267);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t268;
                goto gm_fret;
            }
gm_lb21:;
        }
gm_lb20:;
    }
    {
        uint8_t t269 = gm_l23;
        if (t269) {
            uint8_t t270 = gm_l16;
            uint8_t t271 = (uint8_t)!t270;
            if (t271) {
                gm_l66 = ((uint8_t)1);
            } else {
                uint8_t t272 = gm_l20;
                gm_l66 = t272;
            }
            uint8_t t273 = gm_l66;
            if (t273) {
                void * t274 = gm_l0;
                gm_tl874t2atnlh3 t275 = gm_l4;
                gm_b16_8 t276;
                GM_ST(void *, (uint8_t *)&t276, (void *)(gm_ssnn9u3skptri));
                GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(74));
                gm_b0_1 t277 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t274, t275, t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t278 = gm_l0;
                void * t279 = GM_LD(void *, ((uint8_t *)t278));
                gm_tl874t2atnlh3 t280 = gm_l4;
                void * t281 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t279, t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t281;
                goto gm_fret;
            }
            void * t282 = gm_l0;
            void * t283 = (void *)(((uint8_t *)t282) + 96);
            void * t284 = gm_l5;
            void * t285 = gm_l7;
            gm_t88rcucg1b7uk t286 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t285));
            gm_t9qkh0rsooc8d t287;
            GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)1ULL));
            uint8_t t288 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t283, t284, t286, t287);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t289 = (uint8_t)!t288;
            if (t289) {
                void * t290 = gm_l0;
                void * t291 = gm_l7;
                gm_tl874t2atnlh3 t292 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t291) + 72));
                void * t293 = gm_l0;
                gm_t88rcucg1b7uk t294 = gm_l10;
                gm_b16_8 t295 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t293, t294);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t296;
                GM_ST(gm_b16_8, ((uint8_t *)&t296 + 0), t295);
                gm_f_format_pdashts((void *)&gm_l67, (void *)NULL, cx, NULL, t296);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t297 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                if (!t297) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_srci09ctgf0djoe, 216, 30);
                gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
                gm_b0_1 t300 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t290, t292, t299);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t301 = gm_l0;
                void * t302 = GM_LD(void *, ((uint8_t *)t301));
                gm_tl874t2atnlh3 t303 = gm_l4;
                void * t304 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t302, t303);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t304;
                goto gm_fret;
            }
        } else {
            uint8_t t305 = gm_l20;
            if (t305) {
                void * t306 = gm_l5;
                void * t307 = gm_l0;
                gm_t88rcucg1b7uk t308 = gm_l9;
                gm_t88rcucg1b7uk t309 = gm_l10;
                gm_tl874t2atnlh3 t310 = gm_l4;
                gm_b16_8 t311 = gm_l8;
                gm_tnpifv20irnph t312;
                GM_ST(gm_b16_8, ((uint8_t *)&t312 + 0), t311);
                gm_f_format_ko5t8rr((void *)&gm_l68, (void *)NULL, cx, NULL, t312);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t313 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                if (!t313) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_srci09ctgf0djoe, 220, 57);
                gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                gm_t88rcucg1b7uk t316 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t307, t308, t309, t310, t315);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t317 = gm_f_is_error_8k27gd1(t306, GM_CONV(gm_csguankko4lmhd, t316));
                if (t317) {
                    void * t318 = gm_l0;
                    void * t319 = GM_LD(void *, ((uint8_t *)t318));
                    gm_tl874t2atnlh3 t320 = gm_l4;
                    void * t321 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t319, t320);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t321;
                    goto gm_fret;
                }
            } else {
                {
                    {
                        uint8_t t322 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t323 = (uint8_t)(t322 == ((uint8_t)2ULL));
                        if (!t323) {
                            goto gm_lb23;
                        }
                        gm_l70 = ((uint8_t)1);
                        goto gm_lb22;
                    }
gm_lb23:;
                    {
                        gm_l70 = ((uint8_t)0);
                        goto gm_lb22;
                    }
gm_lb24:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 223, 19);
                }
gm_lb22:;
                uint8_t t324 = gm_l70;
                if (t324) {
                    gm_l69 = ((uint8_t)1);
                } else {
                    gm_ts6oqppjh0ba3 t325 = gm_l1;
                    uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&t325) + 0));
                    uint8_t t327 = (uint8_t)((t326 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t328;
                    GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t328 + 0), ((uint8_t)4ULL));
                    if (t327) {
                        uint8_t t329 = gm_l16;
                        gm_l71 = t329;
                    } else {
                        gm_l71 = ((uint8_t)0);
                    }
                    uint8_t t330 = gm_l71;
                    gm_l69 = t330;
                }
                uint8_t t331 = gm_l69;
                gm_l24 = t331;
                uint8_t t332 = gm_l24;
                uint8_t t333 = (uint8_t)!t332;
                if (t333) {
                    {
                        {
                            uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t335 = (uint8_t)(t334 == ((uint8_t)4ULL));
                            if (!t335) {
                                goto gm_lb26;
                            }
                            gm_b16_8 t336;
                            GM_ST(void *, (uint8_t *)&t336, (void *)(gm_sjsojd1o1489e));
                            GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(33));
                            gm_l72 = t336;
                            goto gm_lb25;
                        }
gm_lb26:;
                        {
                            {
                                {
                                    uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t338 = (uint8_t)(t337 == ((uint8_t)0ULL));
                                    if (!t338) {
                                        goto gm_lb29;
                                    }
                                    goto gm_lb28;
                                }
gm_lb29:;
                                {
                                    uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t340 = (uint8_t)(t339 == ((uint8_t)1ULL));
                                    if (!t340) {
                                        goto gm_lb30;
                                    }
                                    goto gm_lb28;
                                }
gm_lb30:;
                                goto gm_lb27;
                            }
gm_lb28:;
                            uint8_t t341 = gm_l21;
                            if (t341) {
                                gm_b16_8 t342;
                                GM_ST(void *, (uint8_t *)&t342, (void *)(gm_sr7qn5gtrg5md));
                                GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(113));
                                gm_l73 = t342;
                            } else {
                                gm_b16_8 t343 = gm_l22;
                                gm_tnpifv20irnph t344;
                                GM_ST(gm_b16_8, ((uint8_t *)&t344 + 0), t343);
                                gm_f_format_jn4ifp1((void *)&gm_l74, (void *)NULL, cx, NULL, t344);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t345 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                if (!t345) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_srci09ctgf0djoe, 230, 21);
                                gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
                                gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
                                gm_l73 = t347;
                            }
                            gm_b16_8 t348 = gm_l73;
                            gm_l72 = t348;
                            goto gm_lb25;
                        }
gm_lb27:;
                        {
                            gm_b16_8 t349;
                            GM_ST(void *, (uint8_t *)&t349, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(0));
                            gm_l72 = t349;
                            goto gm_lb25;
                        }
gm_lb31:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 225, 28);
                    }
gm_lb25:;
                    gm_b16_8 t350 = gm_l72;
                    gm_l25 = t350;
                    void * t351 = gm_l0;
                    gm_t88rcucg1b7uk t352 = gm_l9;
                    gm_b16_8 t353 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t351, t352);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l26 = t353;
                    void * t354 = gm_l0;
                    gm_t88rcucg1b7uk t355 = gm_l10;
                    gm_b16_8 t356 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t354, t355);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t356;
                    void * t357 = gm_l0;
                    gm_tl874t2atnlh3 t358 = gm_l4;
                    gm_b16_8 t359 = gm_l8;
                    void * t360 = gm_l0;
                    gm_t88rcucg1b7uk t361 = gm_l17;
                    gm_b16_8 t362 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t360, t361);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t363 = gm_l26;
                    gm_b16_8 t364 = gm_l27;
                    gm_b16_8 t365 = gm_l25;
                    gm_tud7oovfvnf43 t366;
                    GM_ST(gm_b16_8, ((uint8_t *)&t366 + 0), t359);
                    GM_ST(gm_b16_8, ((uint8_t *)&t366 + 16), t362);
                    GM_ST(gm_b16_8, ((uint8_t *)&t366 + 32), t363);
                    GM_ST(gm_b16_8, ((uint8_t *)&t366 + 48), t364);
                    GM_ST(gm_b16_8, ((uint8_t *)&t366 + 64), t365);
                    gm_f_format_p0j9lf7((void *)&gm_l75, (void *)NULL, cx, NULL, &t366);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t367 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                    if (!t367) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 8, gm_srci09ctgf0djoe, 236, 28);
                    gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                    gm_b16_8 t369 = GM_LD(gm_b16_8, ((uint8_t *)&t368));
                    gm_b0_1 t370 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t357, t358, t369);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t371 = gm_l0;
                    void * t372 = GM_LD(void *, ((uint8_t *)t371));
                    gm_tl874t2atnlh3 t373 = gm_l4;
                    void * t374 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t372, t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t374;
                    goto gm_fret;
                }
                void * t375 = gm_l0;
                void * t376 = (void *)(((uint8_t *)t375) + 96);
                void * t377 = gm_l5;
                gm_t88rcucg1b7uk t378 = gm_l19;
                gm_t88rcucg1b7uk t379 = gm_l18;
                gm_t9qkh0rsooc8d t380 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t376, t377, t378, t379);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l77 = t380;
                {
                    {
                        uint8_t t381 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                        if (t381) {
                            goto gm_lb33;
                        }
                        gm_l76 = ((uint8_t)1);
                        goto gm_lb32;
                    }
gm_lb33:;
                    {
                        gm_l76 = ((uint8_t)0);
                        goto gm_lb32;
                    }
gm_lb34:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 239, 12);
                }
gm_lb32:;
                uint8_t t382 = gm_l76;
                if (t382) {
                    void * t383 = gm_l0;
                    void * t384 = gm_l0;
                    void * t385 = (void *)(((uint8_t *)t384) + 96);
                    void * t386 = gm_l0;
                    void * t387 = GM_LD(void *, ((uint8_t *)t386));
                    gm_t88rcucg1b7uk t388 = gm_l19;
                    gm_t88rcucg1b7uk t389 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t385, t387, t388);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t390 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t383, t389);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t390;
                    void * t391 = gm_l0;
                    gm_tl874t2atnlh3 t392 = gm_l4;
                    gm_b16_8 t393 = gm_l8;
                    void * t394 = gm_l0;
                    gm_t88rcucg1b7uk t395 = gm_l17;
                    gm_b16_8 t396 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t394, t395);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t397 = gm_l0;
                    gm_t88rcucg1b7uk t398 = gm_l18;
                    gm_b16_8 t399 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t397, t398);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t400 = gm_l28;
                    gm_t0uuhaljsf410 t401;
                    GM_ST(gm_b16_8, ((uint8_t *)&t401 + 0), t393);
                    GM_ST(gm_b16_8, ((uint8_t *)&t401 + 16), t396);
                    GM_ST(gm_b16_8, ((uint8_t *)&t401 + 32), t399);
                    GM_ST(gm_b16_8, ((uint8_t *)&t401 + 48), t400);
                    gm_f_format_davqh84((void *)&gm_l78, (void *)NULL, cx, NULL, &t401);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t402 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                    if (!t402) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srci09ctgf0djoe, 241, 28);
                    gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                    gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
                    gm_b0_1 t405 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t391, t392, t404);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t406 = gm_l0;
                    void * t407 = GM_LD(void *, ((uint8_t *)t406));
                    gm_tl874t2atnlh3 t408 = gm_l4;
                    void * t409 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t407, t408);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t409;
                    goto gm_fret;
                }
            }
        }
    }
    {
        uint8_t t410 = gm_l21;
        if (t410) {
            {
                {
                    uint8_t t411 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t412 = (uint8_t)(t411 == ((uint8_t)0ULL));
                    if (!t412) {
                        goto gm_lb36;
                    }
                    gm_b16_8 t413;
                    GM_ST(void *, (uint8_t *)&t413, (void *)(gm_s26md5sqfb6u7));
                    GM_ST(uintptr_t, (uint8_t *)&t413 + 8, (uintptr_t)(6));
                    gm_l79 = t413;
                    goto gm_lb35;
                }
gm_lb36:;
                {
                    uint8_t t414 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t415 = (uint8_t)(t414 == ((uint8_t)1ULL));
                    if (!t415) {
                        goto gm_lb37;
                    }
                    gm_b16_8 t416;
                    GM_ST(void *, (uint8_t *)&t416, (void *)(gm_s7sn5glap1gkf));
                    GM_ST(uintptr_t, (uint8_t *)&t416 + 8, (uintptr_t)(6));
                    gm_l79 = t416;
                    goto gm_lb35;
                }
gm_lb37:;
                {
                    uint8_t t417 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t418 = (uint8_t)(t417 == ((uint8_t)2ULL));
                    if (!t418) {
                        goto gm_lb38;
                    }
                    uint8_t t419 = gm_l20;
                    if (t419) {
                        gm_b16_8 t420;
                        GM_ST(void *, (uint8_t *)&t420, (void *)(gm_su3gef13fgq4c));
                        GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)(6));
                        gm_l80 = t420;
                    } else {
                        gm_b16_8 t421;
                        GM_ST(void *, (uint8_t *)&t421, (void *)(gm_sdib2k2ql9mgl));
                        GM_ST(uintptr_t, (uint8_t *)&t421 + 8, (uintptr_t)(8));
                        gm_l80 = t421;
                    }
                    gm_b16_8 t422 = gm_l80;
                    gm_l79 = t422;
                    goto gm_lb35;
                }
gm_lb38:;
                {
                    uint8_t t423 = gm_l20;
                    if (t423) {
                        gm_b16_8 t424;
                        GM_ST(void *, (uint8_t *)&t424, (void *)(gm_sq5sjasaqn114));
                        GM_ST(uintptr_t, (uint8_t *)&t424 + 8, (uintptr_t)(6));
                        gm_l81 = t424;
                    } else {
                        gm_b16_8 t425;
                        GM_ST(void *, (uint8_t *)&t425, (void *)(gm_sd7vgd2pm42gk));
                        GM_ST(uintptr_t, (uint8_t *)&t425 + 8, (uintptr_t)(13));
                        gm_l81 = t425;
                    }
                    gm_b16_8 t426 = gm_l81;
                    gm_l79 = t426;
                    goto gm_lb35;
                }
gm_lb39:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 246, 24);
            }
gm_lb35:;
            gm_b16_8 t427 = gm_l79;
            gm_l29 = t427;
            uint8_t t428 = gm_l20;
            uint8_t t429 = (uint8_t)!t428;
            if (t429) {
                uint8_t t430 = gm_l16;
                uint8_t t431 = (uint8_t)!t430;
                gm_l82 = t431;
            } else {
                gm_l82 = ((uint8_t)0);
            }
            uint8_t t432 = gm_l82;
            gm_l30 = t432;
            void * t433 = gm_l0;
            gm_b16_8 t434 = gm_l29;
            void * t435 = gm_l6;
            void * t436 = gm_l7;
            gm_t5fru0lp11n7t t437;
            GM_ST(void *, ((uint8_t *)&t437 + 0), t435);
            GM_ST(void *, ((uint8_t *)&t437 + 8), t436);
            gm_l31 = t437;
            void * t438 = (void *)((uint8_t *)&gm_l31);
            uint64_t t439 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t440 = (uintptr_t)((uint64_t)2ULL);
            if (t439 > t440 || t440 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t439, t440, ((uint64_t)2ULL), gm_srci09ctgf0djoe, 254, 38);
            uint64_t t441 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t442;
            GM_ST(void *, (uint8_t *)&t442, (void *)(((uint8_t *)t438 + (uintptr_t)((uint64_t)0ULL) * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t442 + 8, (uintptr_t)(t441));
            uint8_t t443 = gm_l30;
            gm_tl874t2atnlh3 t444 = gm_l4;
            void * t445 = gm_f_complex_call_8so3bss((void *)NULL, cx, NULL, t433, t434, t442, t443, t444);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t445;
            goto gm_fret;
        }
        void * t446 = gm_l0;
        void * t447 = GM_LD(void *, ((uint8_t *)t446));
        void * t448 = (void *)(((uint8_t *)t447) + 8);
        gm_t88rcucg1b7uk t449 = gm_l17;
        gm_f_math_vector_parts_49c7865((void *)&gm_l83, t448, GM_CONV(gm_csguankko4lmhd, t449));
    }
    {
        uint8_t t450 = (uint8_t)((((uint8_t *)&gm_l83))[16] != 0);
        if (!t450) gm_trap("unwrap of null", gm_srci09ctgf0djoe, 256, 10);
        gm_tlcm4lvu3g1a8 t451 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l83));
        gm_l84 = t451;
        uint64_t t452 = GM_LD(uint64_t, ((uint8_t *)&gm_l84));
        gm_l32 = t452;
        gm_trqfvmjtt4sij t453;
        gm_f_list_ir0h3tc_2((void *)&t453, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l33 = t453;
        void * t454 = gm_l0;
        void * t455 = gm_l6;
        void * t456 = (void *)((uint8_t *)&F.l33);
        void * t457 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t454, t455, t456);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l34 = t457;
        void * t458 = gm_l0;
        void * t459 = gm_l7;
        void * t460 = (void *)((uint8_t *)&F.l33);
        void * t461 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t458, t459, t460);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l35 = t461;
        gm_trqfvmjtt4sij t462;
        gm_f_list_vp1djv1_2((void *)&t462, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l36 = t462;
        gm_l38 = ((uint64_t)0ULL);
        uint64_t t463 = gm_l32;
        gm_l37 = t463;
        {
            for (;;) {
                uint64_t t464 = gm_l38;
                uint64_t t465 = gm_l37;
                uint8_t t466 = (t464 < t465);
                if (!t466) {
                    goto gm_lb40;
                }
                {
                    {
                        {
                            uint64_t t467 = gm_l38;
                            gm_l39 = t467;
                            uint8_t t468 = gm_l16;
                            if (t468) {
                                void * t469 = gm_l0;
                                void * t470 = gm_l34;
                                gm_t88rcucg1b7uk t471 = gm_l18;
                                uint64_t t472 = gm_l39;
                                void * t473 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t469, t470, t471, t472);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l85 = t473;
                            } else {
                                void * t474 = gm_l34;
                                gm_l85 = t474;
                            }
                            void * t475 = gm_l85;
                            gm_l40 = t475;
                            uint8_t t476 = gm_l16;
                            uint8_t t477 = (uint8_t)!t476;
                            if (t477) {
                                gm_l87 = ((uint8_t)1);
                            } else {
                                uint8_t t478 = gm_l20;
                                gm_l87 = t478;
                            }
                            uint8_t t479 = gm_l87;
                            if (t479) {
                                void * t480 = gm_l0;
                                void * t481 = gm_l35;
                                gm_t88rcucg1b7uk t482 = gm_l18;
                                uint64_t t483 = gm_l39;
                                void * t484 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t480, t481, t482, t483);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l86 = t484;
                            } else {
                                void * t485 = gm_l35;
                                gm_l86 = t485;
                            }
                            void * t486 = gm_l86;
                            gm_l41 = t486;
                            void * t487 = gm_l0;
                            void * t488 = GM_LD(void *, ((uint8_t *)t487));
                            gm_tl874t2atnlh3 t489 = gm_l4;
                            void * t490 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t488, t489);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l42 = t490;
                            uint8_t t491 = gm_l23;
                            if (t491) {
                                void * t492 = gm_l0;
                                void * t493 = (void *)(((uint8_t *)t492) + 320);
                                gm_t88rcucg1b7uk t494 = gm_l18;
                                gm_tl874t2atnlh3 t495 = gm_l4;
                                gm_b16_8 t496 = gm_l8;
                                gm_t91vm1g0k2rpm t497;
                                GM_ST(uint16_t, (uint8_t *)&t497 + 34, 0);
                                GM_ST(uint32_t, (uint8_t *)&t497 + 36, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t497 + 0), t494);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t497 + 4), t495);
                                GM_ST(gm_b16_8, ((uint8_t *)&t497 + 16), t496);
                                GM_ST(uint8_t, ((uint8_t *)&t497 + 32), ((uint8_t)1));
                                GM_ST(uint8_t, ((uint8_t *)&t497 + 33), ((uint8_t)0));
                                gm_tpm220j5qhjke t498;
                                GM_ST(uint8_t, (uint8_t *)&t498 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t498 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t498 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t498 + 0), ((uint8_t)3ULL));
                                GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t498 + 8), t497);
                                gm_tftb1odc739t5 t499 = gm_f_push_7koe9lk(t493, &t498);
                                gm_l88 = t499;
                                uint8_t t500 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                                if (!t500) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 1, gm_srci09ctgf0djoe, 266, 13);
                                {
                                    {
                                        uint8_t t501 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                        uint8_t t502 = (uint8_t)(t501 == ((uint8_t)3ULL));
                                        if (!t502) {
                                            goto gm_lb46;
                                        }
                                        gm_t9qkh0rsooc8d t503 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                                        gm_l43 = t503;
                                        gm_t9qkh0rsooc8d t504 = gm_l43;
                                        gm_ts6oqppjh0ba3 t505;
                                        GM_ST(uint8_t, ((uint8_t *)&t505 + 0), ((uint8_t)3ULL));
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t505 + 1), t504);
                                        gm_l89 = t505;
                                        goto gm_lb45;
                                    }
gm_lb46:;
                                    gm_ts6oqppjh0ba3 t506;
                                    GM_ST(uint8_t, (uint8_t *)&t506 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t506 + 0), ((uint8_t)6ULL));
                                    gm_l89 = t506;
                                }
gm_lb45:;
                                gm_ts6oqppjh0ba3 t507 = gm_l89;
                                gm_l44 = t507;
                                gm_t88rcucg1b7uk t508 = gm_l18;
                                gm_ts6oqppjh0ba3 t509 = gm_l44;
                                void * t510 = gm_l40;
                                void * t511 = gm_l41;
                                gm_trl9d7m1k5lbd t512;
                                GM_ST(uint16_t, (uint8_t *)&t512 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t512 + 4, 0);
                                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t512 + 0), t509);
                                GM_ST(void *, ((uint8_t *)&t512 + 8), t510);
                                GM_ST(void *, ((uint8_t *)&t512 + 16), t511);
                                gm_tl874t2atnlh3 t513 = gm_l4;
                                gm_t2b6tm8c0vgo1 t514;
                                memset(&t514, 0, sizeof t514);
                                GM_ST(uint8_t, ((uint8_t *)&t514 + 0), ((uint8_t)30ULL));
                                GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t514 + 8), t512);
                                void * t515 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t508, &t514, t513);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l42 = t515;
                            } else {
                                void * t516 = gm_l0;
                                gm_ts6oqppjh0ba3 t517 = gm_l1;
                                void * t518 = gm_l40;
                                void * t519 = gm_l41;
                                gm_tl874t2atnlh3 t520 = gm_l4;
                                void * t521 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t516, t517, t518, t519, t520);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l42 = t521;
                            }
                            void * t522 = gm_l5;
                            void * t523 = gm_l42;
                            gm_t88rcucg1b7uk t524 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t523));
                            uint8_t t525 = gm_f_is_error_8k27gd1(t522, GM_CONV(gm_csguankko4lmhd, t524));
                            if (t525) {
                                void * t526 = gm_l42;
                                F.ret = t526;
                                goto gm_fret;
                            }
                            void * t527 = (void *)((uint8_t *)&F.l36);
                            void * t528 = gm_l42;
                            gm_tftb1odc739t5 t529 = gm_f_push_d943vgp_2(t527, &(*(gm_tcgrjq9toscke *)((uint8_t *)t528)));
                            gm_l90 = t529;
                            uint8_t t530 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                            if (!t530) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l90) + 1, gm_srci09ctgf0djoe, 273, 9);
                            goto gm_lb43;
                        }
gm_lb44:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 261, 5);
                    }
gm_lb43:;
                }
gm_lb42:;
                uint64_t t531 = gm_l38;
                uint64_t r532; bool o533 = __builtin_add_overflow(t531, ((uint64_t)1ULL), &r532);
                uint64_t t534 = r532;
                gm_l38 = t534;
            }
        }
gm_lb40:;
    }
    {
        gm_t88rcucg1b7uk t535 = gm_l17;
        gm_b16_8 t536 = GM_LD(gm_b16_8, ((uint8_t *)&F.l36));
        gm_tl874t2atnlh3 t537 = gm_l4;
        gm_t2b6tm8c0vgo1 t538;
        memset(&t538, 0, sizeof t538);
        GM_ST(uint8_t, ((uint8_t *)&t538 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t538 + 8), t536);
        void * t539 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t535, &t538, t537);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l45 = t539;
        gm_t88rcucg1b7uk t540 = gm_l17;
        gm_b16_8 t541 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
        void * t542 = gm_l45;
        gm_tl874t2atnlh3 t543 = gm_l4;
        void * t544 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t540, t541, t542, t543);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t544;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_compound_number_elem_6d1d2ni(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_compound_number_elem_6d1d2ni F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tlcm4lvu3g1a8 gm_l2; /* p */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tb7u19loo3j87 gm_l3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = (void *)(((uint8_t *)t5) + 8);
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_f_math_vector_parts_49c7865((void *)&gm_l3, t6, GM_CONV(gm_csguankko4lmhd, t7));
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l3));
            gm_l2 = t9;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
            gm_tkn42u7na3t81 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[4] = 1;
            F.ret = t11;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    gm_t88rcucg1b7uk t14 = gm_l1;
    gm_tkn42u7na3t81 t15 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t15;
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
__attribute__((aligned(4))) void * gm_f_compound_number_unary_4h478fs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_compound_number_unary_4h478fs F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* v */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tn3r4rvlh0rsv gm_l6; /* <temporary array> */
    gm_tlcm4lvu3g1a8 gm_l7; /* p */
    gm_t9qkh0rsooc8d gm_l8; /* top */
    uint8_t gm_l9; /* int_only */
    uint8_t gm_l10; /* signed_only */
    gm_b16_8 gm_l11; /* s */
    void * gm_l13; /* x */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* arr */
    GM_STACK_CHECK(&F, 1517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tb7u19loo3j87 gm_l19;
    gm_tlcm4lvu3g1a8 gm_l20;
    gm_b16_8 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_l4 = t13;
    void * t14 = gm_l0;
    void * t15 = gm_l2;
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t15));
    uint8_t t17 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t14, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t17) {
        void * t18 = gm_l0;
        void * t19 = gm_l2;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t19));
        gm_tl874t2atnlh3 t21 = gm_l3;
        gm_t88rcucg1b7uk t22 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t18, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t23 = gm_l0;
    void * t24 = (void *)(((uint8_t *)t23) + 96);
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, ((uint8_t *)t25));
    void * t27 = gm_l2;
    gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t27));
    gm_t88rcucg1b7uk t29 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t24, t26, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t29;
    void * t30 = gm_l4;
    gm_t88rcucg1b7uk t31 = gm_l5;
    uint8_t t32 = gm_f_is_error_8k27gd1(t30, GM_CONV(gm_csguankko4lmhd, t31));
    if (t32) {
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, ((uint8_t *)t33));
        gm_tl874t2atnlh3 t35 = gm_l3;
        void * t36 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t36;
        goto gm_fret;
    }
    void * t37 = gm_l4;
    gm_t88rcucg1b7uk t38 = gm_l5;
    gm_f_math_vector_parts_49c7865((void *)&gm_l19, t37, GM_CONV(gm_csguankko4lmhd, t38));
    uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
    if (t39) {
        gm_tlcm4lvu3g1a8 t40 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l19));
        gm_l20 = t40;
    } else {
        gm_t9qkh0rsooc8d t41 = gm_l1;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
        uint8_t t43 = (uint8_t)((t42 == ((uint8_t)0ULL)));
        gm_t9qkh0rsooc8d t44;
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
        if (t43) {
            void * t45 = gm_l0;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sprospqi1g16o));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
            void * t47 = gm_l2;
            gm_tn3r4rvlh0rsv t48;
            GM_ST(void *, ((uint8_t *)&t48 + 0), t47);
            gm_l6 = t48;
            void * t49 = (void *)((uint8_t *)&gm_l6);
            uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t51 = (uintptr_t)((uint64_t)1ULL);
            if (t50 > t51 || t51 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t50, t51, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 286, 58);
            uint64_t t52 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t49 + (uintptr_t)((uint64_t)0ULL) * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52));
            gm_tl874t2atnlh3 t54 = gm_l3;
            void * t55 = gm_f_complex_call_8so3bss((void *)NULL, cx, NULL, t45, t46, t53, ((uint8_t)0), t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t55;
            goto gm_fret;
        }
        void * t56 = gm_l0;
        gm_tl874t2atnlh3 t57 = gm_l3;
        gm_t9qkh0rsooc8d t58 = gm_l1;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        uint8_t t60 = (uint8_t)((t59 == ((uint8_t)1ULL)));
        gm_t9qkh0rsooc8d t61;
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1ULL));
        if (t60) {
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(1));
            gm_l21 = t62;
        } else {
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_scvhpklutvpjg));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(3));
            gm_l21 = t63;
        }
        gm_b16_8 t64 = gm_l21;
        gm_tnpifv20irnph t65;
        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
        gm_f_format_gdra5k8((void *)&gm_l22, (void *)NULL, cx, NULL, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srci09ctgf0djoe, 287, 24);
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        gm_b0_1 t69 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t56, t57, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t70 = gm_l0;
        void * t71 = GM_LD(void *, ((uint8_t *)t70));
        gm_tl874t2atnlh3 t72 = gm_l3;
        void * t73 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t73;
        goto gm_fret;
    }
    gm_tlcm4lvu3g1a8 t74 = gm_l20;
    gm_l7 = t74;
    gm_t9qkh0rsooc8d t75;
    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
    gm_l8 = t75;
    gm_l9 = ((uint8_t)0);
    gm_l10 = ((uint8_t)0);
    gm_b16_8 t76;
    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(1));
    gm_l11 = t76;
    {
        {
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)0ULL));
            if (!t78) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)1ULL));
            if (!t80) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t81;
            GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)1ULL));
            gm_l8 = t81;
            gm_l9 = ((uint8_t)1);
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(1));
            gm_l11 = t82;
            goto gm_lb1;
        }
gm_lb3:;
        {
            void * t83 = gm_l0;
            gm_tl874t2atnlh3 t84 = gm_l3;
            void * t85 = gm_l0;
            gm_t88rcucg1b7uk t86 = gm_l5;
            gm_b16_8 t87 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t88;
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
            gm_f_format_6up4rm5((void *)&gm_l23, (void *)NULL, cx, NULL, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srci09ctgf0djoe, 302, 28);
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            gm_b0_1 t92 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t83, t84, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t93 = gm_l0;
            void * t94 = GM_LD(void *, ((uint8_t *)t93));
            gm_tl874t2atnlh3 t95 = gm_l3;
            void * t96 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t96;
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 294, 5);
    }
gm_lb1:;
    gm_trqfvmjtt4sij t97;
    gm_f_list_ir0h3tc_2((void *)&t97, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l12 = t97;
    void * t98 = gm_l0;
    void * t99 = gm_l2;
    void * t100 = (void *)((uint8_t *)&F.l12);
    void * t101 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t98, t99, t100);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t101;
    gm_trqfvmjtt4sij t102;
    gm_f_list_vp1djv1_2((void *)&t102, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l14 = t102;
    gm_l16 = ((uint64_t)0ULL);
    uint64_t t103 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
    gm_l15 = t103;
    {
        for (;;) {
            uint64_t t104 = gm_l16;
            uint64_t t105 = gm_l15;
            uint8_t t106 = (t104 < t105);
            if (!t106) {
                goto gm_lb5;
            }
            {
                {
                    {
                        uint64_t t107 = gm_l16;
                        gm_l17 = t107;
                        void * t108 = gm_l0;
                        void * t109 = (void *)(((uint8_t *)t108) + 320);
                        gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        gm_tl874t2atnlh3 t111 = gm_l3;
                        gm_b16_8 t112 = gm_l11;
                        uint8_t t113 = gm_l9;
                        uint8_t t114 = gm_l10;
                        gm_t91vm1g0k2rpm t115;
                        GM_ST(uint16_t, (uint8_t *)&t115 + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&t115 + 36, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t115 + 0), t110);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t115 + 4), t111);
                        GM_ST(gm_b16_8, ((uint8_t *)&t115 + 16), t112);
                        GM_ST(uint8_t, ((uint8_t *)&t115 + 32), t113);
                        GM_ST(uint8_t, ((uint8_t *)&t115 + 33), t114);
                        gm_tpm220j5qhjke t116;
                        GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t116 + 8), t115);
                        gm_tftb1odc739t5 t117 = gm_f_push_7koe9lk(t109, &t116);
                        gm_l24 = t117;
                        uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srci09ctgf0djoe, 310, 9);
                        void * t119 = (void *)((uint8_t *)&F.l14);
                        gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        gm_t9qkh0rsooc8d t121 = gm_l8;
                        void * t122 = gm_l0;
                        void * t123 = gm_l13;
                        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t125 = gm_l17;
                        void * t126 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t122, t123, t124, t125);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t8ghgl2jj0qha t127;
                        GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t127 + 0), t121);
                        GM_ST(void *, ((uint8_t *)&t127 + 8), t126);
                        gm_tl874t2atnlh3 t128 = gm_l3;
                        gm_t2b6tm8c0vgo1 t129;
                        memset(&t129, 0, sizeof t129);
                        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)29ULL));
                        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t129 + 8), t127);
                        void * t130 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t120, &t129, t128);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t131 = gm_f_push_d943vgp_2(t119, &(*(gm_tcgrjq9toscke *)((uint8_t *)t130)));
                        gm_l25 = t131;
                        uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srci09ctgf0djoe, 311, 9);
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 309, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t133 = gm_l16;
            uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
            uint64_t t136 = r134;
            gm_l16 = t136;
        }
    }
gm_lb5:;
    gm_t88rcucg1b7uk t137 = gm_l5;
    gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
    gm_tl874t2atnlh3 t139 = gm_l3;
    gm_t2b6tm8c0vgo1 t140;
    memset(&t140, 0, sizeof t140);
    GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)36ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t140 + 8), t138);
    void * t141 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t140, t139);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l18 = t141;
    gm_t88rcucg1b7uk t142 = gm_l5;
    gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
    void * t144 = gm_l18;
    gm_tl874t2atnlh3 t145 = gm_l3;
    void * t146 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t142, t143, t144, t145);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t146;
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
__attribute__((aligned(4))) void * gm_f_core_call_values_qq2la05(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_core_call_values_qq2la05 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* v */
    gm_t88rcucg1b7uk gm_l9; /* vt */
    gm_b16_8 gm_l10; /* local_name */
    uint32_t gm_l11; /* local */
    gm_t08olq97rscn3 gm_l12; /* id */
    void * gm_l14; /* call */
    GM_STACK_CHECK(&F, 1656u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ir0h3tc_2((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t7;
    gm_trqfvmjtt4sij t8;
    gm_f_list_6oos54v((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t8;
    void * t9 = gm_l0;
    gm_b0_1 t10 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t11 = gm_l2;
    gm_l6 = t11;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l7;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t17 = gm_l7;
                {
                    {
                        void * t18 = GM_LD(void *, (((uint8_t *)t15) + (uintptr_t)t17 * 8));
                        gm_l8 = t18;
                        void * t19 = gm_l0;
                        void * t20 = (void *)(((uint8_t *)t19) + 96);
                        void * t21 = gm_l0;
                        void * t22 = GM_LD(void *, ((uint8_t *)t21));
                        void * t23 = gm_l8;
                        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t23));
                        gm_t88rcucg1b7uk t25 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t20, t22, t24);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l9 = t25;
                        void * t26 = gm_l0;
                        uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t26) + 16) + 8));
                        gm_t4ce0jd3tdro4 t28;
                        GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t27);
                        gm_f_format_uubl3s3((void *)&gm_l15, (void *)NULL, cx, NULL, t28);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srci09ctgf0djoe, 351, 23);
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                        gm_l10 = t30;
                        void * t31 = gm_l0;
                        gm_b16_8 t32 = gm_l10;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                        gm_t88rcucg1b7uk t34 = gm_l9;
                        void * t35 = gm_l8;
                        gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t35) + 72));
                        uint32_t t37 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t31, t33, t34, ((uint8_t)0), t36);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t37;
                        void * t38 = (void *)((uint8_t *)&F.l4);
                        uint32_t t39 = gm_l11;
                        void * t40 = gm_l8;
                        gm_ttggq7ng4kp3p t41;
                        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t41 + 0), t39);
                        GM_ST(void *, ((uint8_t *)&t41 + 8), t40);
                        gm_trcmmd12cefb1 t42;
                        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t42 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t42 + 8), t41);
                        gm_tftb1odc739t5 t43 = gm_f_push_d6c76ud_2(t38, &t42);
                        gm_l16 = t43;
                        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srci09ctgf0djoe, 353, 9);
                        void * t45 = gm_l0;
                        gm_b16_8 t46 = gm_l10;
                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                        uint32_t t48 = gm_l11;
                        void * t49 = gm_l8;
                        gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 72));
                        gm_t930q5l38hdap t51;
                        memset(&t51, 0, sizeof t51);
                        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t51 + 8), t48);
                        gm_b0_1 t52 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t45, t47, &t51, t50);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t53 = gm_l10;
                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                        void * t55 = gm_l8;
                        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 72));
                        gm_t08olq97rscn3 t57;
                        GM_ST(uint32_t, (uint8_t *)&t57 + 28, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t54);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t57 + 16), t56);
                        gm_l12 = t57;
                        gm_t08olq97rscn3 t58 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l12));
                        void * t59 = gm_l8;
                        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 72));
                        gm_t2gtbh39ptekd t61;
                        GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t61 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t61 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t61 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t61 + 8), t58);
                        gm_tc3oht4ksth9d t62;
                        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t62 + 84, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t62 + 0), ((uint32_t)4294967295ULL));
                        GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t62 + 8), t61);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t62 + 72), t60);
                        F.l13 = t62;
                        void * t63 = (void *)((uint8_t *)&F.l5);
                        gm_tc3oht4ksth9d t64 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l13));
                        void * t65 = gm_l8;
                        gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                        gm_t22mtv3sd9prf t67;
                        memset(&t67, 0, sizeof t67);
                        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t67 + 8), t64);
                        gm_t7te2tdpebbt4 t68;
                        GM_ST(uint32_t, (uint8_t *)&t68 + 140, 0);
                        GM_ST(gm_t22mtv3sd9prf, ((uint8_t *)&t68 + 0), t67);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t68 + 128), t66);
                        gm_tftb1odc739t5 t69 = gm_f_push_jis5lhu(t63, &t68);
                        gm_l17 = t69;
                        uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srci09ctgf0djoe, 357, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 349, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t71 = gm_l7;
            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
            uint64_t t74 = r72;
            gm_l7 = t74;
        }
    }
gm_lb1:;
    void * t75 = gm_l0;
    uint32_t t76 = gm_l1;
    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
    gm_tl874t2atnlh3 t79 = gm_l3;
    void * t80 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t75, t76, ((void *)0), t78, t79);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t80;
    void * t81 = gm_l0;
    gm_f_pop_scope_g5j7o4e(t81);
    void * t82 = gm_l14;
    gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t82));
    gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    void * t85 = gm_l14;
    gm_tl874t2atnlh3 t86 = gm_l3;
    void * t87 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t83, t84, t85, t86);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_operand_509et87(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_describe_operand_509et87 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 287u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_taari1s0m51a7 gm_l2;
    gm_tabb710d9mq2e gm_l3;
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
    gm_l2 = t13;
    {
        {
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l2))[1] != 0);
            if (!t14) {
                goto gm_lb2;
            }
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
            if (!t16) {
                goto gm_lb2;
            }
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sp66nadh171v3));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(18));
            F.ret = t17;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l2))[1] != 0);
            if (!t18) {
                goto gm_lb3;
            }
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb3;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sf372rql24e3n));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(16));
            F.ret = t21;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l2))[1] != 0);
            if (!t22) {
                goto gm_lb4;
            }
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
            if (!t24) {
                goto gm_lb4;
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sfa8f3bk569gj));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(25));
            F.ret = t25;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l2))[1] != 0);
            if (t26) {
                goto gm_lb5;
            }
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 27, 5);
    }
gm_lb1:;
    void * t27 = gm_l0;
    void * t28 = gm_l0;
    void * t29 = (void *)(((uint8_t *)t28) + 96);
    void * t30 = gm_l0;
    void * t31 = GM_LD(void *, ((uint8_t *)t30));
    gm_t88rcucg1b7uk t32 = gm_l1;
    gm_t88rcucg1b7uk t33 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t29, t31, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t34 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t27, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t35;
    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
    gm_f_format_h4jcodi((void *)&gm_l3, (void *)NULL, cx, NULL, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srci09ctgf0djoe, 33, 12);
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
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
__attribute__((aligned(4))) void * gm_f_imaginary_literal_6d41gfv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_imaginary_literal_6d41gfv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* l */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_t9qkh0rsooc8d gm_l5; /* suffix */
    gm_t88rcucg1b7uk gm_l6; /* part */
    uint32_t gm_l7; /* c */
    gm_tnqk2qt7j42o9 gm_l8; /* <temporary array> */
    GM_STACK_CHECK(&F, 569u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint16_t gm_l9;
    gm_t9805qtonbsag gm_l10;
    uint8_t gm_l11;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_l3 = t17;
    void * t18 = gm_l3;
    gm_t88rcucg1b7uk t19 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t19;
    {
        {
            void * t20 = gm_l1;
            uint8_t t21 = (uint8_t)(((((uint8_t *)t20) + 96))[1] != 0);
            if (!t21) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t20) + 96));
            gm_l5 = t22;
            void * t23 = gm_l3;
            {
                {
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)0ULL));
                    if (!t25) {
                        goto gm_lb4;
                    }
                    gm_l9 = ((uint16_t)16ULL);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                    if (!t27) {
                        goto gm_lb5;
                    }
                    gm_l9 = ((uint16_t)32ULL);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)2ULL));
                    if (!t29) {
                        goto gm_lb6;
                    }
                    gm_l9 = ((uint16_t)64ULL);
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 41, 35);
            }
gm_lb3:;
            uint16_t t30 = gm_l9;
            gm_t53at80pfdus3 t31;
            memset(&t31, 0, sizeof t31);
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t31 + 8), t30);
            gm_t88rcucg1b7uk t32 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t23, &t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t32;
            {
                {
                    void * t33 = gm_l0;
                    void * t34 = GM_LD(void *, ((uint8_t *)t33));
                    gm_b16_8 t35;
                    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s0rphlcbno91v));
                    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(7));
                    gm_t9805qtonbsag t36 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t34, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t36;
                    uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
                    if (!t37) {
                        goto gm_lb8;
                    }
                    uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                    gm_l7 = t38;
                    void * t39 = gm_l0;
                    void * t40 = GM_LD(void *, ((uint8_t *)t39));
                    uint32_t t41 = gm_l7;
                    gm_t88rcucg1b7uk t42 = gm_l6;
                    gm_tnqk2qt7j42o9 t43;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 0), t42);
                    gm_l8 = t43;
                    void * t44 = (void *)((uint8_t *)&gm_l8);
                    uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t46 = (uintptr_t)((uint64_t)1ULL);
                    if (t45 > t46 || t46 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t45, t46, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 46, 81);
                    uint64_t t47 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t44 + (uintptr_t)((uint64_t)0ULL) * 4)));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
                    gm_t88rcucg1b7uk t49 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t40, t41, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l4 = t49;
                    goto gm_lb7;
                }
gm_lb8:;
            }
gm_lb7:;
            goto gm_lb1;
        }
gm_lb2:;
        void * t50 = gm_l0;
        gm_t9qkh0rsooc8d t51;
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)2ULL));
        gm_t88rcucg1b7uk t52 = gm_f_literal_var_3sb5sti((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t52;
    }
gm_lb1:;
    {
        {
            void * t53 = gm_l0;
            {
                {
                    uint8_t t54 = (uint8_t)((((((uint8_t *)t53) + 96) + 56))[8] != 0);
                    if (t54) {
                        goto gm_lb12;
                    }
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb11;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 50, 8);
            }
gm_lb11:;
            uint8_t t55 = gm_l11;
            if (!t55) {
                goto gm_lb10;
            }
            void * t56 = gm_l3;
            gm_t88rcucg1b7uk t57 = gm_l4;
            uint8_t t58 = gm_f_is_error_8k27gd1(t56, GM_CONV(gm_csguankko4lmhd, t57));
            uint8_t t59 = (uint8_t)!t58;
            if (!t59) {
                goto gm_lb10;
            }
            void * t60 = gm_l0;
            gm_t9qkh0rsooc8d t61;
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t62 = gm_f_literal_var_3sb5sti((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb9;
        }
gm_lb10:;
    }
gm_lb9:;
    gm_t88rcucg1b7uk t63 = gm_l4;
    gm_tngtuiuskn938 t64;
    gm_f_zero_9r6o7sj((void *)&t64, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t65 = gm_l1;
    gm_t2b6tm8c0vgo1 t66;
    gm_f_untyped_complex_20agrmu((void *)&t66, (void *)NULL, cx, NULL, &t64, &(*(gm_tngtuiuskn938 *)((uint8_t *)t65)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t67 = gm_l2;
    void * t68 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t63, &t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t68;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_compound_number_kbipjl5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_compound_number_kbipjl5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t0 */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    gm_tkn42u7na3t81 gm_l6;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 96);
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_t88rcucg1b7uk t10 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_t88rcucg1b7uk t14 = gm_l2;
    uint8_t t15 = gm_f_is_math_vector_09mtuao(t13, GM_CONV(gm_csguankko4lmhd, t14));
    if (t15) {
        gm_l4 = ((uint8_t)1);
    } else {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, ((uint8_t *)t16));
        gm_t88rcucg1b7uk t18 = gm_l2;
        gm_tkn42u7na3t81 t19 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t19;
        {
            {
                uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l6))[4] != 0);
                if (t20) {
                    goto gm_lb2;
                }
                gm_l5 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l5 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 15, 42);
        }
gm_lb1:;
        uint8_t t21 = gm_l5;
        gm_l4 = t21;
    }
    uint8_t t22 = gm_l4;
    if (t22) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l2;
        uint8_t t25 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t25;
    }
    uint8_t t26 = gm_l3;
    F.ret = t26;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_literal_var_3sb5sti(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_literal_var_3sb5sti F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    uint32_t gm_l2; /* c */
    gm_tnqk2qt7j42o9 gm_l3; /* <temporary array> */
    GM_STACK_CHECK(&F, 330u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    gm_t9805qtonbsag gm_l5;
    {
        {
            gm_t9qkh0rsooc8d t9 = gm_l1;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
            uint8_t t11 = (uint8_t)((t10 == ((uint8_t)2ULL)));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = gm_l0;
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)2ULL));
            {
                {
                    uint8_t t14 = (uint8_t)((((((uint8_t *)t12) + 96) + 56))[8] != 0);
                    if (t14) {
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
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 56, 29);
            }
gm_lb3:;
            uint8_t t15 = gm_l4;
            if (!t15) {
                goto gm_lb2;
            }
            {
                {
                    void * t16 = gm_l0;
                    void * t17 = GM_LD(void *, ((uint8_t *)t16));
                    gm_b16_8 t18;
                    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s0rphlcbno91v));
                    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
                    gm_t9805qtonbsag t19 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t17, t18);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l5 = t19;
                    uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
                    if (!t20) {
                        goto gm_lb7;
                    }
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                    gm_l2 = t21;
                    void * t22 = gm_l0;
                    uint32_t t23 = gm_l2;
                    void * t24 = gm_l0;
                    void * t25 = GM_LD(void *, ((uint8_t *)t24));
                    uint32_t t26 = gm_l2;
                    void * t27 = gm_l0;
                    void * t28 = GM_LD(void *, ((uint8_t *)t27));
                    void * t29 = (void *)(((uint8_t *)t28) + 8);
                    gm_t53at80pfdus3 t30;
                    memset(&t30, 0, sizeof t30);
                    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)3ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t30 + 8), ((uint16_t)64ULL));
                    gm_t88rcucg1b7uk t31 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t29, &t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnqk2qt7j42o9 t32;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32 + 0), t31);
                    gm_l3 = t32;
                    void * t33 = (void *)((uint8_t *)&gm_l3);
                    uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t35 = (uintptr_t)((uint64_t)1ULL);
                    if (t34 > t35 || t35 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t34, t35, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 58, 74);
                    uint64_t t36 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t37;
                    GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t33 + (uintptr_t)((uint64_t)0ULL) * 4)));
                    GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36));
                    gm_t88rcucg1b7uk t38 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t25, t26, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tplpbqf3duvn3 t39;
                    GM_ST(uint32_t, ((uint8_t *)&t39 + 0), t23);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t39 + 4), t38);
                    gm_thdmr9qicg35r t40;
                    GM_ST(uint8_t, (uint8_t *)&t40 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t40 + 10, 0);
                    GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)&t40), t39);
                    ((uint8_t *)&t40)[8] = 1;
                    GM_ST(gm_thdmr9qicg35r, ((((uint8_t *)t22) + 96) + 56), t40);
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t41 = gm_l0;
    void * t42 = (void *)(((uint8_t *)t41) + 96);
    void * t43 = gm_l0;
    void * t44 = GM_LD(void *, ((uint8_t *)t43));
    void * t45 = (void *)(((uint8_t *)t44) + 8);
    gm_t9qkh0rsooc8d t46 = gm_l1;
    gm_t88rcucg1b7uk t47 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t42, t45, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t47;
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
inline __attribute__((aligned(4))) void * gm_f_math_elem_iuerhch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3) {
    struct gm_fr_math_elem_iuerhch F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* index */
    GM_STACK_CHECK(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t6 = gm_l3;
    gm_tbci5ospk5nnn t7;
    gm_f_from_u64_ud13q5v((void *)&t7, (void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = gm_l1;
    gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 72));
    gm_t2b6tm8c0vgo1 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t10 + 8), t7);
    void * t11 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t5, &t10, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t11;
    gm_t88rcucg1b7uk t12 = gm_l2;
    void * t13 = gm_l1;
    void * t14 = gm_l4;
    gm_tlcl4u558l6p5 t15;
    GM_ST(uint8_t, (uint8_t *)&t15 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t15 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t15 + 20, 0);
    GM_ST(void *, ((uint8_t *)&t15 + 0), t13);
    GM_ST(void *, ((uint8_t *)&t15 + 8), t14);
    GM_ST(uint8_t, ((uint8_t *)&t15 + 16), ((uint8_t)0));
    void * t16 = gm_l1;
    gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
    gm_t2b6tm8c0vgo1 t18;
    memset(&t18, 0, sizeof t18);
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)20ULL));
    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t18 + 8), t15);
    void * t19 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t12, &t18, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t19;
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
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_math_vector_conversion_0cttjm0 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* target */
    void * gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* st */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* a */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* b */
    uint8_t gm_l10; /* both */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    void * gm_l13; /* v */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_tn3r4rvlh0rsv gm_l18; /* <temporary array> */
    void * gm_l19; /* arr */
    gm_b16_8 gm_l20; /* why */
    GM_STACK_CHECK(&F, 1949u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l21;
    gm_t53at80pfdus3 gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_b16_8 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tabb710d9mq2e gm_l31;
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        gm_l4 = t21;
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 96);
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, ((uint8_t *)t24));
        void * t26 = gm_l2;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t26));
        gm_t88rcucg1b7uk t28 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t23, t25, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t28;
        {
            {
                void * t29 = gm_l4;
                gm_t88rcucg1b7uk t30 = gm_l1;
                uint8_t t31 = gm_f_is_math_vector_09mtuao(t29, GM_CONV(gm_csguankko4lmhd, t30));
                uint8_t t32 = (uint8_t)!t31;
                if (!t32) {
                    goto gm_lb2;
                }
                void * t33 = gm_l4;
                gm_t88rcucg1b7uk t34 = gm_l5;
                uint8_t t35 = gm_f_is_math_vector_09mtuao(t33, GM_CONV(gm_csguankko4lmhd, t34));
                uint8_t t36 = (uint8_t)!t35;
                if (!t36) {
                    goto gm_lb2;
                }
                F.ret = ((void *)0);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                void * t37 = gm_l4;
                gm_t88rcucg1b7uk t38 = gm_l5;
                gm_f_kind_jcnt4hm((void *)&gm_l21, t37, GM_CONV(gm_csguankko4lmhd, t38));
                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)11ULL));
                if (!t40) {
                    goto gm_lb4;
                }
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                gm_l6 = t41;
                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l21) + 8) + 8));
                gm_l7 = t42;
                {
                    {
                        void * t43 = gm_l4;
                        gm_t88rcucg1b7uk t44 = gm_l1;
                        gm_f_kind_jcnt4hm((void *)&gm_l22, t43, GM_CONV(gm_csguankko4lmhd, t44));
                        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                        uint8_t t46 = (uint8_t)(t45 == ((uint8_t)11ULL));
                        if (!t46) {
                            goto gm_lb6;
                        }
                        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                        gm_l8 = t47;
                        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 8));
                        gm_l9 = t48;
                        void * t49 = gm_l4;
                        gm_t88rcucg1b7uk t50 = gm_l5;
                        uint8_t t51 = gm_f_is_math_vector_09mtuao(t49, GM_CONV(gm_csguankko4lmhd, t50));
                        if (t51) {
                            void * t52 = gm_l4;
                            gm_t88rcucg1b7uk t53 = gm_l1;
                            uint8_t t54 = gm_f_is_math_vector_09mtuao(t52, GM_CONV(gm_csguankko4lmhd, t53));
                            gm_l23 = t54;
                        } else {
                            gm_l23 = ((uint8_t)0);
                        }
                        uint8_t t55 = gm_l23;
                        gm_l10 = t55;
                        {
                            {
                                uint64_t t56 = gm_l6;
                                uint64_t t57 = gm_l8;
                                uint8_t t58 = ((t56 == t57));
                                if (!t58) {
                                    goto gm_lb8;
                                }
                                gm_t88rcucg1b7uk t59 = gm_l7;
                                gm_t88rcucg1b7uk t60 = gm_l9;
                                uint8_t t61 = (gm_eq9m6mvtmgde(((uint8_t *)&t59), ((uint8_t *)&t60)));
                                if (!t61) {
                                    goto gm_lb8;
                                }
                                uint8_t t62 = gm_l10;
                                if (t62) {
                                    gm_l25 = ((uint8_t)1);
                                } else {
                                    void * t63 = gm_l4;
                                    gm_t88rcucg1b7uk t64 = gm_l5;
                                    uint8_t t65 = gm_f_plain_0otlpn0(t63, GM_CONV(gm_csguankko4lmhd, t64));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l25 = t65;
                                }
                                uint8_t t66 = gm_l25;
                                if (t66) {
                                    gm_l24 = ((uint8_t)1);
                                } else {
                                    void * t67 = gm_l4;
                                    gm_t88rcucg1b7uk t68 = gm_l1;
                                    uint8_t t69 = gm_f_plain_0otlpn0(t67, GM_CONV(gm_csguankko4lmhd, t68));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l24 = t69;
                                }
                                uint8_t t70 = gm_l24;
                                if (!t70) {
                                    goto gm_lb8;
                                }
                                gm_t88rcucg1b7uk t71 = gm_l1;
                                void * t72 = gm_l2;
                                gm_tn3r4rvlh0rsv t73;
                                GM_ST(void *, ((uint8_t *)&t73 + 0), t72);
                                gm_tvdg94bk7aa22 t74;
                                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t74 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)7ULL));
                                gm_l11 = t73;
                                void * t75 = (void *)((uint8_t *)&gm_l11);
                                uint64_t t76 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t77 = (uintptr_t)((uint64_t)1ULL);
                                if (t76 > t77 || t77 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t76, t77, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 465, 109);
                                uint64_t t78 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t79;
                                GM_ST(void *, (uint8_t *)&t79, (void *)(((uint8_t *)t75 + (uintptr_t)((uint64_t)0ULL) * 8)));
                                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(t78));
                                gm_tl874t2atnlh3 t80 = gm_l3;
                                void * t81 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t74, t71, t79, t80);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t81;
                                goto gm_fret;
                            }
gm_lb8:;
                        }
gm_lb7:;
                        {
                            {
                                uint8_t t82 = gm_l10;
                                if (!t82) {
                                    goto gm_lb10;
                                }
                                uint64_t t83 = gm_l6;
                                uint64_t t84 = gm_l8;
                                uint8_t t85 = ((t83 == t84));
                                if (!t85) {
                                    goto gm_lb10;
                                }
                                void * t86 = gm_l0;
                                void * t87 = GM_LD(void *, ((uint8_t *)t86));
                                gm_t88rcucg1b7uk t88 = gm_l7;
                                gm_t88rcucg1b7uk t89 = gm_l9;
                                uint8_t t90 = gm_f_lossless_a54o69s(t87, GM_CONV(gm_csguankko4lmhd, t88), GM_CONV(gm_csguankko4lmhd, t89));
                                if (!t90) {
                                    goto gm_lb10;
                                }
                                gm_trqfvmjtt4sij t91;
                                gm_f_list_ir0h3tc_2((void *)&t91, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l12 = t91;
                                void * t92 = gm_l0;
                                void * t93 = gm_l2;
                                void * t94 = (void *)((uint8_t *)&F.l12);
                                void * t95 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t92, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l13 = t95;
                                gm_trqfvmjtt4sij t96;
                                gm_f_list_vp1djv1_2((void *)&t96, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l14 = t96;
                                gm_l16 = ((uint64_t)0ULL);
                                uint64_t t97 = gm_l6;
                                gm_l15 = t97;
                                {
                                    for (;;) {
                                        uint64_t t98 = gm_l16;
                                        uint64_t t99 = gm_l15;
                                        uint8_t t100 = (t98 < t99);
                                        if (!t100) {
                                            goto gm_lb11;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t101 = gm_l16;
                                                    gm_l17 = t101;
                                                    void * t102 = (void *)((uint8_t *)&F.l14);
                                                    gm_t88rcucg1b7uk t103 = gm_l9;
                                                    void * t104 = gm_l0;
                                                    void * t105 = gm_l13;
                                                    gm_t88rcucg1b7uk t106 = gm_l7;
                                                    uint64_t t107 = gm_l17;
                                                    gm_tvdg94bk7aa22 t108;
                                                    GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t108 + 8, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)0ULL));
                                                    void * t109 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t104, t105, t106, t107);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tn3r4rvlh0rsv t110;
                                                    GM_ST(void *, ((uint8_t *)&t110 + 0), t109);
                                                    gm_l18 = t110;
                                                    void * t111 = (void *)((uint8_t *)&gm_l18);
                                                    uint64_t t112 = (uintptr_t)((uint64_t)0ULL);
                                                    uint64_t t113 = (uintptr_t)((uint64_t)1ULL);
                                                    if (t112 > t113 || t113 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t112, t113, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 470, 57);
                                                    uint64_t t114 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                    gm_b16_8 t115;
                                                    GM_ST(void *, (uint8_t *)&t115, (void *)(((uint8_t *)t111 + (uintptr_t)((uint64_t)0ULL) * 8)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(t114));
                                                    gm_tl874t2atnlh3 t116 = gm_l3;
                                                    void * t117 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t108, t103, t115, t116);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tftb1odc739t5 t118 = gm_f_push_d943vgp_2(t102, &(*(gm_tcgrjq9toscke *)((uint8_t *)t117)));
                                                    gm_l26 = t118;
                                                    uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                                    if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srci09ctgf0djoe, 470, 25);
                                                    goto gm_lb14;
                                                }
gm_lb15:;
                                                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 470, 9);
                                            }
gm_lb14:;
                                        }
gm_lb13:;
                                        uint64_t t120 = gm_l16;
                                        uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                                        uint64_t t123 = r121;
                                        gm_l16 = t123;
                                    }
                                }
gm_lb11:;
                                gm_t88rcucg1b7uk t124 = gm_l1;
                                gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                gm_tl874t2atnlh3 t126 = gm_l3;
                                gm_t2b6tm8c0vgo1 t127;
                                memset(&t127, 0, sizeof t127);
                                GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)36ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t127 + 8), t125);
                                void * t128 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t124, &t127, t126);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l19 = t128;
                                gm_t88rcucg1b7uk t129 = gm_l1;
                                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                void * t131 = gm_l19;
                                gm_tl874t2atnlh3 t132 = gm_l3;
                                void * t133 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t129, t130, t131, t132);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t133;
                                goto gm_fret;
                            }
gm_lb10:;
                        }
gm_lb9:;
                        {
                            {
                                uint8_t t134 = gm_l10;
                                if (!t134) {
                                    goto gm_lb17;
                                }
                                uint64_t t135 = gm_l6;
                                uint64_t t136 = gm_l8;
                                uint8_t t137 = ((t135 == t136));
                                if (!t137) {
                                    goto gm_lb17;
                                }
                                void * t138 = gm_l0;
                                gm_t88rcucg1b7uk t139 = gm_l7;
                                gm_b16_8 t140 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t138, t139);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t141 = gm_l0;
                                gm_t88rcucg1b7uk t142 = gm_l9;
                                gm_b16_8 t143 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t141, t142);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t144;
                                GM_ST(gm_b16_8, ((uint8_t *)&t144 + 0), t140);
                                GM_ST(gm_b16_8, ((uint8_t *)&t144 + 16), t143);
                                gm_f_format_mrlevsk((void *)&gm_l28, (void *)NULL, cx, NULL, &t144);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t145 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t145) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srci09ctgf0djoe, 474, 40);
                                gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                                gm_l27 = t147;
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_b16_8 t148;
                            GM_ST(void *, (uint8_t *)&t148, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(0));
                            gm_l27 = t148;
                        }
gm_lb16:;
                        gm_b16_8 t149 = gm_l27;
                        gm_l20 = t149;
                        void * t150 = gm_l0;
                        gm_tl874t2atnlh3 t151 = gm_l3;
                        void * t152 = gm_l0;
                        gm_t88rcucg1b7uk t153 = gm_l5;
                        gm_b16_8 t154 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t152, t153);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t155 = gm_l0;
                        gm_t88rcucg1b7uk t156 = gm_l1;
                        gm_b16_8 t157 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t155, t156);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t158 = gm_l20;
                        gm_tfeab279c16rr t159;
                        GM_ST(gm_b16_8, ((uint8_t *)&t159 + 0), t154);
                        GM_ST(gm_b16_8, ((uint8_t *)&t159 + 16), t157);
                        GM_ST(gm_b16_8, ((uint8_t *)&t159 + 32), t158);
                        gm_f_format_d0lfl1k((void *)&gm_l29, (void *)NULL, cx, NULL, &t159);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                        if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srci09ctgf0djoe, 475, 20);
                        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                        gm_b0_1 t163 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t150, t151, t162);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t164 = gm_l0;
                        void * t165 = GM_LD(void *, ((uint8_t *)t164));
                        gm_tl874t2atnlh3 t166 = gm_l3;
                        void * t167 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t165, t166);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t167;
                        goto gm_fret;
                    }
gm_lb6:;
                    void * t168 = gm_l0;
                    gm_tl874t2atnlh3 t169 = gm_l3;
                    void * t170 = gm_l0;
                    gm_t88rcucg1b7uk t171 = gm_l5;
                    gm_b16_8 t172 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t173 = gm_l0;
                    gm_t88rcucg1b7uk t174 = gm_l1;
                    gm_b16_8 t175 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t173, t174);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t176;
                    GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t172);
                    GM_ST(gm_b16_8, ((uint8_t *)&t176 + 16), t175);
                    gm_f_format_9rhpk5t((void *)&gm_l30, (void *)NULL, cx, NULL, &t176);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srci09ctgf0djoe, 460, 24);
                    gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                    gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                    gm_b0_1 t180 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t168, t169, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t181 = gm_l0;
                    void * t182 = GM_LD(void *, ((uint8_t *)t181));
                    gm_tl874t2atnlh3 t183 = gm_l3;
                    void * t184 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t182, t183);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t184;
                    goto gm_fret;
                }
gm_lb5:;
            }
gm_lb4:;
            void * t185 = gm_l0;
            gm_tl874t2atnlh3 t186 = gm_l3;
            void * t187 = gm_l0;
            gm_t88rcucg1b7uk t188 = gm_l5;
            gm_b16_8 t189 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t187, t188);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t190 = gm_l0;
            gm_t88rcucg1b7uk t191 = gm_l1;
            gm_b16_8 t192 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t190, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t193;
            GM_ST(gm_b16_8, ((uint8_t *)&t193 + 0), t189);
            GM_ST(gm_b16_8, ((uint8_t *)&t193 + 16), t192);
            gm_f_format_9rhpk5t((void *)&gm_l31, (void *)NULL, cx, NULL, &t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t194) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srci09ctgf0djoe, 456, 24);
            gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
            gm_b0_1 t197 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t185, t186, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t198 = gm_l0;
            void * t199 = GM_LD(void *, ((uint8_t *)t198));
            gm_tl874t2atnlh3 t200 = gm_l3;
            void * t201 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t199, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t201;
            goto gm_fret;
        }
gm_lb3:;
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
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_math_vector_method_ko2814s F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    void * gm_l11; /* x */
    gm_ts6oqppjh0ba3 gm_l12; /* mul */
    gm_ts6oqppjh0ba3 gm_l13; /* add */
    gm_ts6oqppjh0ba3 gm_l14; /* sub */
    void * gm_l15; /* y0 */
    void * gm_l16; /* y */
    void * gm_l17; /* tail */
    void * gm_l18; /* acc */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    void * gm_l22; /* p */
    void * gm_l23; /* a */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t2lqu8jsj511d gm_l28; /* ij */
    uint64_t gm_l29; /* i */
    uint64_t gm_l30; /* j */
    void * gm_l31; /* p */
    void * gm_l32; /* q */
    gm_tvdg94bk7aa22 gm_l33; /* op */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* i */
    gm_t5fru0lp11n7t gm_l38; /* <temporary array> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    gm_tn3r4rvlh0rsv gm_l43; /* <temporary array> */
    void * gm_l44; /* tail */
    void * gm_l45; /* acc */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* i */
    void * gm_l49; /* e */
    void * gm_l50; /* p */
    void * gm_l51; /* a */
    void * gm_l52; /* total */
    gm_tn3r4rvlh0rsv gm_l53; /* <temporary array> */
    void * gm_l54; /* tail */
    gm_b16_8 gm_l55; /* hint */
    GM_STACK_CHECK(&F, 5057u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tb7u19loo3j87 gm_l56;
    gm_tlcm4lvu3g1a8 gm_l57;
    gm_tabb710d9mq2e gm_l58;
    void * gm_l59;
    void * gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tvdg94bk7aa22 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    uint8_t gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tabb710d9mq2e gm_l67;
    void * gm_l68;
    void * gm_l69;
    gm_b16_8 gm_l70;
    uint8_t gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_tabb710d9mq2e gm_l73;
    {
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, ((uint8_t *)t43));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_l6 = t45;
        void * t46 = gm_l6;
        gm_t88rcucg1b7uk t47 = gm_l2;
        gm_f_math_vector_parts_49c7865((void *)&gm_l56, t46, GM_CONV(gm_csguankko4lmhd, t47));
        uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l56))[16] != 0);
        if (t48) {
            gm_tlcm4lvu3g1a8 t49 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l56));
            gm_l57 = t49;
        } else {
            void * t50 = gm_l0;
            void * t51 = GM_LD(void *, ((uint8_t *)t50));
            gm_tl874t2atnlh3 t52 = gm_l5;
            void * t53 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t53;
            goto gm_fret;
        }
        gm_tlcm4lvu3g1a8 t54 = gm_l57;
        gm_l7 = t54;
        uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l8 = t55;
        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l9 = t56;
        gm_trqfvmjtt4sij t57;
        gm_f_list_ir0h3tc_2((void *)&t57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t57;
        void * t58 = gm_l0;
        void * t59 = gm_l1;
        void * t60 = (void *)((uint8_t *)&F.l10);
        void * t61 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t61;
        gm_t9qkh0rsooc8d t62;
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t63;
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t63 + 1), t62);
        gm_l12 = t63;
        gm_t9qkh0rsooc8d t64;
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t65;
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t65 + 1), t64);
        gm_l13 = t65;
        gm_t9qkh0rsooc8d t66;
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t67;
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 1), t66);
        gm_l14 = t67;
        {
            {
                {
                    {
                        {
                            {
                                gm_b16_8 t68 = gm_l3;
                                gm_b16_8 t69;
                                GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sg1h22ve4uqmq));
                                GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(3));
                                uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                                if (!t70) {
                                    goto gm_lb4;
                                }
                                goto gm_lb3;
                            }
gm_lb4:;
                            {
                                gm_b16_8 t71 = gm_l3;
                                gm_b16_8 t72;
                                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sgvnpogt67see));
                                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(5));
                                uint8_t t73 = (gm_eq0iqiubfm7e(((uint8_t *)&t71), ((uint8_t *)&t72)));
                                if (!t73) {
                                    goto gm_lb5;
                                }
                                goto gm_lb3;
                            }
gm_lb5:;
                            {
                                gm_b16_8 t74 = gm_l3;
                                gm_b16_8 t75;
                                GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sm3o0uerjumpm));
                                GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(3));
                                uint8_t t76 = (gm_eq0iqiubfm7e(((uint8_t *)&t74), ((uint8_t *)&t75)));
                                if (!t76) {
                                    goto gm_lb6;
                                }
                                goto gm_lb3;
                            }
gm_lb6:;
                            {
                                gm_b16_8 t77 = gm_l3;
                                gm_b16_8 t78;
                                GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s2mtoab140jp6));
                                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(3));
                                uint8_t t79 = (gm_eq0iqiubfm7e(((uint8_t *)&t77), ((uint8_t *)&t78)));
                                if (!t79) {
                                    goto gm_lb7;
                                }
                                goto gm_lb3;
                            }
gm_lb7:;
                            goto gm_lb2;
                        }
gm_lb3:;
                        void * t80 = gm_l0;
                        gm_b16_8 t81 = gm_l4;
                        gm_b16_8 t82 = gm_l3;
                        gm_tl874t2atnlh3 t83 = gm_l5;
                        uint8_t t84 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t80, t81, ((uint64_t)1ULL), t82, t83);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t85 = (uint8_t)!t84;
                        if (t85) {
                            void * t86 = gm_l0;
                            void * t87 = GM_LD(void *, ((uint8_t *)t86));
                            gm_tl874t2atnlh3 t88 = gm_l5;
                            void * t89 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t87, t88);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t89;
                            goto gm_fret;
                        }
                        void * t90 = gm_l0;
                        gm_b16_8 t91 = gm_l4;
                        gm_t88rcucg1b7uk t92 = gm_l2;
                        gm_tl874t2atnlh3 t93 = gm_l5;
                        void * t94 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t90, t91, ((uint64_t)0ULL), t92, t93);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l15 = t94;
                        void * t95 = gm_l0;
                        void * t96 = gm_l15;
                        void * t97 = (void *)((uint8_t *)&F.l10);
                        void * t98 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t95, t96, t97);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l16 = t98;
                        {
                            {
                                gm_b16_8 t99 = gm_l3;
                                gm_b16_8 t100;
                                GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sgvnpogt67see));
                                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(5));
                                uint8_t t101 = (gm_eq0iqiubfm7e(((uint8_t *)&t99), ((uint8_t *)&t100)));
                                if (!t101) {
                                    goto gm_lb9;
                                }
                                uint64_t t102 = gm_l8;
                                uint8_t t103 = (!(t102 == ((uint64_t)3ULL)));
                                if (!t103) {
                                    goto gm_lb9;
                                }
                                void * t104 = gm_l0;
                                gm_tl874t2atnlh3 t105 = gm_l5;
                                void * t106 = gm_l0;
                                gm_t88rcucg1b7uk t107 = gm_l2;
                                gm_b16_8 t108 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t106, t107);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t t109 = gm_l8;
                                gm_t3877pk7lv748 t110;
                                GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t108);
                                GM_ST(uint64_t, ((uint8_t *)&t110 + 16), t109);
                                gm_f_format_c8fgv1v((void *)&gm_l58, (void *)NULL, cx, NULL, &t110);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srci09ctgf0djoe, 381, 32);
                                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                                gm_b0_1 t114 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t104, t105, t113);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t115 = gm_l0;
                                void * t116 = GM_LD(void *, ((uint8_t *)t115));
                                gm_tl874t2atnlh3 t117 = gm_l5;
                                void * t118 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t116, t117);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t118;
                                goto gm_fret;
                            }
gm_lb9:;
                        }
gm_lb8:;
                    }
                    {
                        void * t119 = gm_l0;
                        void * t120 = GM_LD(void *, ((uint8_t *)t119));
                        gm_tl874t2atnlh3 t121 = gm_l5;
                        void * t122 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t120, t121);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l17 = t122;
                        gm_b16_8 t123 = gm_l3;
                        gm_b16_8 t124;
                        GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sg1h22ve4uqmq));
                        GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(3));
                        uint8_t t125 = (gm_eq0iqiubfm7e(((uint8_t *)&t123), ((uint8_t *)&t124)));
                        if (t125) {
                            gm_l18 = ((void *)0);
                            gm_l20 = ((uint64_t)0ULL);
                            uint64_t t126 = gm_l8;
                            gm_l19 = t126;
                            {
                                for (;;) {
                                    uint64_t t127 = gm_l20;
                                    uint64_t t128 = gm_l19;
                                    uint8_t t129 = (t127 < t128);
                                    if (!t129) {
                                        goto gm_lb10;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t130 = gm_l20;
                                                gm_l21 = t130;
                                                void * t131 = gm_l0;
                                                gm_ts6oqppjh0ba3 t132 = gm_l12;
                                                void * t133 = gm_l0;
                                                void * t134 = gm_l11;
                                                gm_t88rcucg1b7uk t135 = gm_l9;
                                                uint64_t t136 = gm_l21;
                                                void * t137 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t133, t134, t135, t136);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t138 = gm_l0;
                                                void * t139 = gm_l16;
                                                gm_t88rcucg1b7uk t140 = gm_l9;
                                                uint64_t t141 = gm_l21;
                                                void * t142 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t138, t139, t140, t141);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tl874t2atnlh3 t143 = gm_l5;
                                                void * t144 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t131, t132, t137, t142, t143);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l22 = t144;
                                                {
                                                    {
                                                        uint8_t t145 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l18)) != 0);
                                                        if (!t145) {
                                                            goto gm_lb16;
                                                        }
                                                        void * t146 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                                        gm_l23 = t146;
                                                        void * t147 = gm_l0;
                                                        gm_ts6oqppjh0ba3 t148 = gm_l13;
                                                        void * t149 = gm_l23;
                                                        void * t150 = gm_l22;
                                                        gm_tl874t2atnlh3 t151 = gm_l5;
                                                        void * t152 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t147, t148, t149, t150, t151);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l59 = t152;
                                                        goto gm_lb15;
                                                    }
gm_lb16:;
                                                    void * t153 = gm_l22;
                                                    gm_l59 = t153;
                                                }
gm_lb15:;
                                                void * t154 = gm_l59;
                                                gm_l18 = t154;
                                                goto gm_lb13;
                                            }
gm_lb14:;
                                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 387, 17);
                                        }
gm_lb13:;
                                    }
gm_lb12:;
                                    uint64_t t155 = gm_l20;
                                    uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                                    uint64_t t158 = r156;
                                    gm_l20 = t158;
                                }
                            }
gm_lb10:;
                            uint8_t t159 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l18)) != 0);
                            if (t159) {
                                void * t160 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                gm_l60 = t160;
                            } else {
                                void * t161 = gm_l0;
                                void * t162 = GM_LD(void *, ((uint8_t *)t161));
                                gm_tl874t2atnlh3 t163 = gm_l5;
                                void * t164 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t162, t163);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t164;
                                goto gm_fret;
                            }
                            void * t165 = gm_l60;
                            gm_l17 = t165;
                        } else {
                            gm_b16_8 t166 = gm_l3;
                            gm_b16_8 t167;
                            GM_ST(void *, (uint8_t *)&t167, (void *)(gm_sgvnpogt67see));
                            GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(5));
                            uint8_t t168 = (gm_eq0iqiubfm7e(((uint8_t *)&t166), ((uint8_t *)&t167)));
                            if (t168) {
                                gm_trqfvmjtt4sij t169;
                                gm_f_list_vp1djv1_2((void *)&t169, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l24 = t169;
                                gm_t2lqu8jsj511d t170;
                                GM_ST(uint64_t, ((uint8_t *)&t170 + 0), ((uint64_t)1ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t170 + 8), ((uint64_t)2ULL));
                                gm_t2lqu8jsj511d t171;
                                GM_ST(uint64_t, ((uint8_t *)&t171 + 0), ((uint64_t)2ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t171 + 8), ((uint64_t)0ULL));
                                gm_t2lqu8jsj511d t172;
                                GM_ST(uint64_t, ((uint8_t *)&t172 + 0), ((uint64_t)0ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t172 + 8), ((uint64_t)1ULL));
                                gm_t8o2b5qnbhu2s t173;
                                GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t173 + 0), t170);
                                GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t173 + 16), t171);
                                GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t173 + 32), t172);
                                F.l25 = t173;
                                void * t174 = (void *)((uint8_t *)&F.l25);
                                uint64_t t175 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t176 = (uintptr_t)((uint64_t)3ULL);
                                if (t175 > t176 || t176 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t175, t176, ((uint64_t)3ULL), gm_srci09ctgf0djoe, 394, 17);
                                uint64_t t177 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t178;
                                GM_ST(void *, (uint8_t *)&t178, (void *)(((uint8_t *)t174 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(t177));
                                gm_l26 = t178;
                                gm_l27 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t179 = gm_l27;
                                        uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                        uint8_t t181 = (t179 < t180);
                                        if (!t181) {
                                            goto gm_lb17;
                                        }
                                        {
                                            void * t182 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                            uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                            uint64_t t184 = gm_l27;
                                            {
                                                {
                                                    gm_t2lqu8jsj511d t185 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)t182) + (uintptr_t)t184 * 16));
                                                    gm_l28 = t185;
                                                    uint64_t t186 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                                                    gm_l29 = t186;
                                                    uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                    gm_l30 = t187;
                                                    void * t188 = gm_l0;
                                                    gm_ts6oqppjh0ba3 t189 = gm_l12;
                                                    void * t190 = gm_l0;
                                                    void * t191 = gm_l11;
                                                    gm_t88rcucg1b7uk t192 = gm_l9;
                                                    uint64_t t193 = gm_l29;
                                                    void * t194 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t190, t191, t192, t193);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t195 = gm_l0;
                                                    void * t196 = gm_l16;
                                                    gm_t88rcucg1b7uk t197 = gm_l9;
                                                    uint64_t t198 = gm_l30;
                                                    void * t199 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t195, t196, t197, t198);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tl874t2atnlh3 t200 = gm_l5;
                                                    void * t201 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t188, t189, t194, t199, t200);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l31 = t201;
                                                    void * t202 = gm_l0;
                                                    gm_ts6oqppjh0ba3 t203 = gm_l12;
                                                    void * t204 = gm_l0;
                                                    void * t205 = gm_l11;
                                                    gm_t88rcucg1b7uk t206 = gm_l9;
                                                    uint64_t t207 = gm_l30;
                                                    void * t208 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t204, t205, t206, t207);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t209 = gm_l0;
                                                    void * t210 = gm_l16;
                                                    gm_t88rcucg1b7uk t211 = gm_l9;
                                                    uint64_t t212 = gm_l29;
                                                    void * t213 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t209, t210, t211, t212);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tl874t2atnlh3 t214 = gm_l5;
                                                    void * t215 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t202, t203, t208, t213, t214);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l32 = t215;
                                                    void * t216 = (void *)((uint8_t *)&F.l24);
                                                    void * t217 = gm_l0;
                                                    gm_ts6oqppjh0ba3 t218 = gm_l14;
                                                    void * t219 = gm_l31;
                                                    void * t220 = gm_l32;
                                                    gm_tl874t2atnlh3 t221 = gm_l5;
                                                    void * t222 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t217, t218, t219, t220, t221);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tftb1odc739t5 t223 = gm_f_push_d943vgp_2(t216, &(*(gm_tcgrjq9toscke *)((uint8_t *)t222)));
                                                    gm_l61 = t223;
                                                    uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                                    if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srci09ctgf0djoe, 399, 21);
                                                    goto gm_lb20;
                                                }
gm_lb21:;
                                                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 394, 17);
                                            }
gm_lb20:;
                                        }
gm_lb19:;
                                        uint64_t t225 = gm_l27;
                                        uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                                        uint64_t t228 = r226;
                                        gm_l27 = t228;
                                    }
                                }
gm_lb17:;
                                gm_t88rcucg1b7uk t229 = gm_l2;
                                gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                gm_tl874t2atnlh3 t231 = gm_l5;
                                gm_t2b6tm8c0vgo1 t232;
                                memset(&t232, 0, sizeof t232);
                                GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)36ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t232 + 8), t230);
                                void * t233 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t229, &t232, t231);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l17 = t233;
                            } else {
                                gm_b16_8 t234 = gm_l3;
                                gm_b16_8 t235;
                                GM_ST(void *, (uint8_t *)&t235, (void *)(gm_sm3o0uerjumpm));
                                GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(3));
                                uint8_t t236 = (gm_eq0iqiubfm7e(((uint8_t *)&t234), ((uint8_t *)&t235)));
                                if (t236) {
                                    gm_tvdg94bk7aa22 t237;
                                    GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t237 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)16ULL));
                                    gm_l62 = t237;
                                } else {
                                    gm_tvdg94bk7aa22 t238;
                                    GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t238 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)17ULL));
                                    gm_l62 = t238;
                                }
                                gm_tvdg94bk7aa22 t239 = gm_l62;
                                gm_l33 = t239;
                                gm_trqfvmjtt4sij t240;
                                gm_f_list_vp1djv1_2((void *)&t240, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l34 = t240;
                                gm_l36 = ((uint64_t)0ULL);
                                uint64_t t241 = gm_l8;
                                gm_l35 = t241;
                                {
                                    for (;;) {
                                        uint64_t t242 = gm_l36;
                                        uint64_t t243 = gm_l35;
                                        uint8_t t244 = (t242 < t243);
                                        if (!t244) {
                                            goto gm_lb22;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t245 = gm_l36;
                                                    gm_l37 = t245;
                                                    void * t246 = (void *)((uint8_t *)&F.l34);
                                                    gm_tvdg94bk7aa22 t247 = gm_l33;
                                                    gm_t88rcucg1b7uk t248 = gm_l9;
                                                    void * t249 = gm_l0;
                                                    void * t250 = gm_l11;
                                                    gm_t88rcucg1b7uk t251 = gm_l9;
                                                    uint64_t t252 = gm_l37;
                                                    void * t253 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t249, t250, t251, t252);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t254 = gm_l0;
                                                    void * t255 = gm_l16;
                                                    gm_t88rcucg1b7uk t256 = gm_l9;
                                                    uint64_t t257 = gm_l37;
                                                    void * t258 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t254, t255, t256, t257);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_t5fru0lp11n7t t259;
                                                    GM_ST(void *, ((uint8_t *)&t259 + 0), t253);
                                                    GM_ST(void *, ((uint8_t *)&t259 + 8), t258);
                                                    gm_l38 = t259;
                                                    void * t260 = (void *)((uint8_t *)&gm_l38);
                                                    uint64_t t261 = (uintptr_t)((uint64_t)0ULL);
                                                    uint64_t t262 = (uintptr_t)((uint64_t)2ULL);
                                                    if (t261 > t262 || t262 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t261, t262, ((uint64_t)2ULL), gm_srci09ctgf0djoe, 405, 64);
                                                    uint64_t t263 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                    gm_b16_8 t264;
                                                    GM_ST(void *, (uint8_t *)&t264, (void *)(((uint8_t *)t260 + (uintptr_t)((uint64_t)0ULL) * 8)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t264 + 8, (uintptr_t)(t263));
                                                    gm_tl874t2atnlh3 t265 = gm_l5;
                                                    void * t266 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t247, t248, t264, t265);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tftb1odc739t5 t267 = gm_f_push_d943vgp_2(t246, &(*(gm_tcgrjq9toscke *)((uint8_t *)t266)));
                                                    gm_l63 = t267;
                                                    uint8_t t268 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                                    if (!t268) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srci09ctgf0djoe, 405, 35);
                                                    goto gm_lb25;
                                                }
gm_lb26:;
                                                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 405, 17);
                                            }
gm_lb25:;
                                        }
gm_lb24:;
                                        uint64_t t269 = gm_l36;
                                        uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                                        uint64_t t272 = r270;
                                        gm_l36 = t272;
                                    }
                                }
gm_lb22:;
                                gm_t88rcucg1b7uk t273 = gm_l2;
                                gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                                gm_tl874t2atnlh3 t275 = gm_l5;
                                gm_t2b6tm8c0vgo1 t276;
                                memset(&t276, 0, sizeof t276);
                                GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)36ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t276 + 8), t274);
                                void * t277 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t273, &t276, t275);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l17 = t277;
                            }
                        }
                    }
                    {
                        void * t278 = gm_l17;
                        gm_t88rcucg1b7uk t279 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t278));
                        gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                        void * t281 = gm_l17;
                        gm_tl874t2atnlh3 t282 = gm_l5;
                        void * t283 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t279, t280, t281, t282);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t283;
                        goto gm_fret;
                    }
                }
gm_lb2:;
            }
            {
                {
                    gm_b16_8 t284 = gm_l3;
                    gm_b16_8 t285;
                    GM_ST(void *, (uint8_t *)&t285, (void *)(gm_sbl3pbs9195ho));
                    GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(3));
                    uint8_t t286 = (gm_eq0iqiubfm7e(((uint8_t *)&t284), ((uint8_t *)&t285)));
                    if (!t286) {
                        goto gm_lb27;
                    }
                    void * t287 = gm_l0;
                    gm_b16_8 t288 = gm_l4;
                    gm_b16_8 t289 = gm_l3;
                    gm_tl874t2atnlh3 t290 = gm_l5;
                    uint8_t t291 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t287, t288, ((uint64_t)0ULL), t289, t290);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t292 = (uint8_t)!t291;
                    if (t292) {
                        void * t293 = gm_l0;
                        void * t294 = GM_LD(void *, ((uint8_t *)t293));
                        gm_tl874t2atnlh3 t295 = gm_l5;
                        void * t296 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t294, t295);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t296;
                        goto gm_fret;
                    }
                    void * t297 = gm_l6;
                    gm_t88rcucg1b7uk t298 = gm_l9;
                    uint8_t t299 = gm_f_is_signed_integer_s66djm7(t297, GM_CONV(gm_csguankko4lmhd, t298));
                    if (t299) {
                        gm_l64 = ((uint8_t)1);
                    } else {
                        void * t300 = gm_l6;
                        gm_t88rcucg1b7uk t301 = gm_l9;
                        uint8_t t302 = gm_f_is_float_3jk89qu(t300, GM_CONV(gm_csguankko4lmhd, t301));
                        gm_l64 = t302;
                    }
                    uint8_t t303 = gm_l64;
                    uint8_t t304 = (uint8_t)!t303;
                    if (t304) {
                        void * t305 = gm_l0;
                        gm_tl874t2atnlh3 t306 = gm_l5;
                        void * t307 = gm_l0;
                        gm_t88rcucg1b7uk t308 = gm_l9;
                        gm_b16_8 t309 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t307, t308);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t310;
                        GM_ST(gm_b16_8, ((uint8_t *)&t310 + 0), t309);
                        gm_f_format_gmfdekn((void *)&gm_l65, (void *)NULL, cx, NULL, t310);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t311 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                        if (!t311) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srci09ctgf0djoe, 413, 32);
                        gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                        gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&t312));
                        gm_b0_1 t314 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t305, t306, t313);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t315 = gm_l0;
                        void * t316 = GM_LD(void *, ((uint8_t *)t315));
                        gm_tl874t2atnlh3 t317 = gm_l5;
                        void * t318 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t316, t317);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t318;
                        goto gm_fret;
                    }
                    gm_trqfvmjtt4sij t319;
                    gm_f_list_vp1djv1_2((void *)&t319, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l39 = t319;
                    gm_l41 = ((uint64_t)0ULL);
                    uint64_t t320 = gm_l8;
                    gm_l40 = t320;
                    {
                        for (;;) {
                            uint64_t t321 = gm_l41;
                            uint64_t t322 = gm_l40;
                            uint8_t t323 = (t321 < t322);
                            if (!t323) {
                                goto gm_lb28;
                            }
                            {
                                {
                                    {
                                        uint64_t t324 = gm_l41;
                                        gm_l42 = t324;
                                        void * t325 = (void *)((uint8_t *)&F.l39);
                                        gm_t88rcucg1b7uk t326 = gm_l9;
                                        void * t327 = gm_l0;
                                        void * t328 = gm_l11;
                                        gm_t88rcucg1b7uk t329 = gm_l9;
                                        uint64_t t330 = gm_l42;
                                        gm_tvdg94bk7aa22 t331;
                                        GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t331 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)19ULL));
                                        void * t332 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t327, t328, t329, t330);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tn3r4rvlh0rsv t333;
                                        GM_ST(void *, ((uint8_t *)&t333 + 0), t332);
                                        gm_l43 = t333;
                                        void * t334 = (void *)((uint8_t *)&gm_l43);
                                        uint64_t t335 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t336 = (uintptr_t)((uint64_t)1ULL);
                                        if (t335 > t336 || t336 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t335, t336, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 417, 62);
                                        uint64_t t337 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                        gm_b16_8 t338;
                                        GM_ST(void *, (uint8_t *)&t338, (void *)(((uint8_t *)t334 + (uintptr_t)((uint64_t)0ULL) * 8)));
                                        GM_ST(uintptr_t, (uint8_t *)&t338 + 8, (uintptr_t)(t337));
                                        gm_tl874t2atnlh3 t339 = gm_l5;
                                        void * t340 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t331, t326, t338, t339);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t341 = gm_f_push_d943vgp_2(t325, &(*(gm_tcgrjq9toscke *)((uint8_t *)t340)));
                                        gm_l66 = t341;
                                        uint8_t t342 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                        if (!t342) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srci09ctgf0djoe, 417, 31);
                                        goto gm_lb31;
                                    }
gm_lb32:;
                                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 417, 13);
                                }
gm_lb31:;
                            }
gm_lb30:;
                            uint64_t t343 = gm_l41;
                            uint64_t r344; bool o345 = __builtin_add_overflow(t343, ((uint64_t)1ULL), &r344);
                            uint64_t t346 = r344;
                            gm_l41 = t346;
                        }
                    }
gm_lb28:;
                    gm_t88rcucg1b7uk t347 = gm_l2;
                    gm_b16_8 t348 = GM_LD(gm_b16_8, ((uint8_t *)&F.l39));
                    gm_tl874t2atnlh3 t349 = gm_l5;
                    gm_t2b6tm8c0vgo1 t350;
                    memset(&t350, 0, sizeof t350);
                    GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)36ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t350 + 8), t348);
                    void * t351 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t347, &t350, t349);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l44 = t351;
                    gm_t88rcucg1b7uk t352 = gm_l2;
                    gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                    void * t354 = gm_l44;
                    gm_tl874t2atnlh3 t355 = gm_l5;
                    void * t356 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t352, t353, t354, t355);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t356;
                    goto gm_fret;
                }
gm_lb27:;
            }
            {
                {
                    gm_b16_8 t357 = gm_l3;
                    gm_b16_8 t358;
                    GM_ST(void *, (uint8_t *)&t358, (void *)(gm_s6fdn1h3c830p));
                    GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(4));
                    uint8_t t359 = (gm_eq0iqiubfm7e(((uint8_t *)&t357), ((uint8_t *)&t358)));
                    if (!t359) {
                        goto gm_lb33;
                    }
                    void * t360 = gm_l0;
                    gm_b16_8 t361 = gm_l4;
                    gm_b16_8 t362 = gm_l3;
                    gm_tl874t2atnlh3 t363 = gm_l5;
                    uint8_t t364 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t360, t361, ((uint64_t)0ULL), t362, t363);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t365 = (uint8_t)!t364;
                    if (t365) {
                        void * t366 = gm_l0;
                        void * t367 = GM_LD(void *, ((uint8_t *)t366));
                        gm_tl874t2atnlh3 t368 = gm_l5;
                        void * t369 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t367, t368);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t369;
                        goto gm_fret;
                    }
                    void * t370 = gm_l6;
                    gm_t88rcucg1b7uk t371 = gm_l9;
                    uint8_t t372 = gm_f_is_float_3jk89qu(t370, GM_CONV(gm_csguankko4lmhd, t371));
                    uint8_t t373 = (uint8_t)!t372;
                    if (t373) {
                        void * t374 = gm_l0;
                        gm_tl874t2atnlh3 t375 = gm_l5;
                        void * t376 = gm_l0;
                        gm_t88rcucg1b7uk t377 = gm_l9;
                        gm_b16_8 t378 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t376, t377);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t379;
                        GM_ST(gm_b16_8, ((uint8_t *)&t379 + 0), t378);
                        gm_f_format_mcgdakk((void *)&gm_l67, (void *)NULL, cx, NULL, t379);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t380 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                        if (!t380) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_srci09ctgf0djoe, 424, 32);
                        gm_b16_8 t381 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                        gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&t381));
                        gm_b0_1 t383 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t374, t375, t382);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t384 = gm_l0;
                        void * t385 = GM_LD(void *, ((uint8_t *)t384));
                        gm_tl874t2atnlh3 t386 = gm_l5;
                        void * t387 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t385, t386);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t387;
                        goto gm_fret;
                    }
                    gm_l45 = ((void *)0);
                    gm_l47 = ((uint64_t)0ULL);
                    uint64_t t388 = gm_l8;
                    gm_l46 = t388;
                    {
                        for (;;) {
                            uint64_t t389 = gm_l47;
                            uint64_t t390 = gm_l46;
                            uint8_t t391 = (t389 < t390);
                            if (!t391) {
                                goto gm_lb34;
                            }
                            {
                                {
                                    {
                                        uint64_t t392 = gm_l47;
                                        gm_l48 = t392;
                                        void * t393 = gm_l0;
                                        void * t394 = gm_l11;
                                        gm_t88rcucg1b7uk t395 = gm_l9;
                                        uint64_t t396 = gm_l48;
                                        void * t397 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t393, t394, t395, t396);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l49 = t397;
                                        void * t398 = gm_l0;
                                        gm_ts6oqppjh0ba3 t399 = gm_l12;
                                        void * t400 = gm_l49;
                                        void * t401 = gm_l49;
                                        gm_tl874t2atnlh3 t402 = gm_l5;
                                        void * t403 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t398, t399, t400, t401, t402);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l50 = t403;
                                        {
                                            {
                                                uint8_t t404 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l45)) != 0);
                                                if (!t404) {
                                                    goto gm_lb40;
                                                }
                                                void * t405 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                                gm_l51 = t405;
                                                void * t406 = gm_l0;
                                                gm_ts6oqppjh0ba3 t407 = gm_l13;
                                                void * t408 = gm_l51;
                                                void * t409 = gm_l50;
                                                gm_tl874t2atnlh3 t410 = gm_l5;
                                                void * t411 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t406, t407, t408, t409, t410);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l68 = t411;
                                                goto gm_lb39;
                                            }
gm_lb40:;
                                            void * t412 = gm_l50;
                                            gm_l68 = t412;
                                        }
gm_lb39:;
                                        void * t413 = gm_l68;
                                        gm_l45 = t413;
                                        goto gm_lb37;
                                    }
gm_lb38:;
                                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 428, 13);
                                }
gm_lb37:;
                            }
gm_lb36:;
                            uint64_t t414 = gm_l47;
                            uint64_t r415; bool o416 = __builtin_add_overflow(t414, ((uint64_t)1ULL), &r415);
                            uint64_t t417 = r415;
                            gm_l47 = t417;
                        }
                    }
gm_lb34:;
                    uint8_t t418 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l45)) != 0);
                    if (t418) {
                        void * t419 = GM_LD(void *, ((uint8_t *)&gm_l45));
                        gm_l69 = t419;
                    } else {
                        void * t420 = gm_l0;
                        void * t421 = GM_LD(void *, ((uint8_t *)t420));
                        gm_tl874t2atnlh3 t422 = gm_l5;
                        void * t423 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t421, t422);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t423;
                        goto gm_fret;
                    }
                    void * t424 = gm_l69;
                    gm_l52 = t424;
                    gm_t88rcucg1b7uk t425 = gm_l9;
                    void * t426 = gm_l52;
                    gm_tn3r4rvlh0rsv t427;
                    GM_ST(void *, ((uint8_t *)&t427 + 0), t426);
                    gm_tvdg94bk7aa22 t428;
                    GM_ST(uint8_t, (uint8_t *)&t428 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t428 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t428 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t428 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t428 + 0), ((uint8_t)26ULL));
                    gm_l53 = t427;
                    void * t429 = (void *)((uint8_t *)&gm_l53);
                    uint64_t t430 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t431 = (uintptr_t)((uint64_t)1ULL);
                    if (t430 > t431 || t431 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t430, t431, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 434, 42);
                    uint64_t t432 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t433;
                    GM_ST(void *, (uint8_t *)&t433, (void *)(((uint8_t *)t429 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t433 + 8, (uintptr_t)(t432));
                    gm_tl874t2atnlh3 t434 = gm_l5;
                    void * t435 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t428, t425, t433, t434);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l54 = t435;
                    void * t436 = gm_l54;
                    gm_t88rcucg1b7uk t437 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t436));
                    gm_b16_8 t438 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                    void * t439 = gm_l54;
                    gm_tl874t2atnlh3 t440 = gm_l5;
                    void * t441 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t437, t438, t439, t440);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t441;
                    goto gm_fret;
                }
gm_lb33:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb41:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 375, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t442 = gm_l3;
        gm_tlg1vmfbpbvtq t443 = GM_LD(gm_tlg1vmfbpbvtq, ((uint8_t *)gm_cukabrstocl));
        uint8_t t444 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(27); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t443) + i * 16, ((uint8_t *)&t442))) { t444 = 1; break; }
        uint8_t t445 = (uint8_t)(t444);
        if (t445) {
            gm_l71 = ((uint8_t)1);
        } else {
            gm_b16_8 t446 = gm_l3;
            gm_b16_8 t447;
            GM_ST(void *, (uint8_t *)&t447, (void *)(gm_sbb1ri5er9djq));
            GM_ST(uintptr_t, (uint8_t *)&t447 + 8, (uintptr_t)(3));
            gm_b16_8 t448;
            GM_ST(void *, (uint8_t *)&t448, (void *)(gm_snivf25e5fme8));
            GM_ST(uintptr_t, (uint8_t *)&t448 + 8, (uintptr_t)(5));
            gm_b16_8 t449;
            GM_ST(void *, (uint8_t *)&t449, (void *)(gm_s4pmpk07ugvq6));
            GM_ST(uintptr_t, (uint8_t *)&t449 + 8, (uintptr_t)(4));
            gm_tfeab279c16rr t450;
            GM_ST(gm_b16_8, ((uint8_t *)&t450 + 0), t447);
            GM_ST(gm_b16_8, ((uint8_t *)&t450 + 16), t448);
            GM_ST(gm_b16_8, ((uint8_t *)&t450 + 32), t449);
            uint8_t t451 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t450) + 0, ((uint8_t *)&t446))) t451 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t450) + 16, ((uint8_t *)&t446))) t451 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t450) + 32, ((uint8_t *)&t446))) t451 = 1;
            uint8_t t452 = (uint8_t)(t451);
            gm_l71 = t452;
        }
        uint8_t t453 = gm_l71;
        if (t453) {
            gm_b16_8 t454 = gm_l3;
            gm_tnpifv20irnph t455;
            GM_ST(gm_b16_8, ((uint8_t *)&t455 + 0), t454);
            gm_f_format_86pfrlj((void *)&gm_l72, (void *)NULL, cx, NULL, t455);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t456 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
            if (!t456) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_srci09ctgf0djoe, 440, 9);
            gm_b16_8 t457 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
            gm_b16_8 t458 = GM_LD(gm_b16_8, ((uint8_t *)&t457));
            gm_l70 = t458;
        } else {
            gm_b16_8 t459;
            GM_ST(void *, (uint8_t *)&t459, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t459 + 8, (uintptr_t)(0));
            gm_l70 = t459;
        }
        gm_b16_8 t460 = gm_l70;
        gm_l55 = t460;
        void * t461 = gm_l0;
        gm_tl874t2atnlh3 t462 = gm_l5;
        gm_b16_8 t463 = gm_l3;
        gm_b16_8 t464 = gm_l55;
        gm_t6j6smj7qoctu t465;
        GM_ST(gm_b16_8, ((uint8_t *)&t465 + 0), t463);
        GM_ST(gm_b16_8, ((uint8_t *)&t465 + 16), t464);
        gm_f_format_iepqo20((void *)&gm_l73, (void *)NULL, cx, NULL, &t465);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t466 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
        if (!t466) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 8, gm_srci09ctgf0djoe, 444, 20);
        gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
        gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
        gm_b0_1 t469 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t461, t462, t468);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t470 = gm_l0;
        void * t471 = GM_LD(void *, ((uint8_t *)t470));
        gm_tl874t2atnlh3 t472 = gm_l5;
        void * t473 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t471, t472);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t473;
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_complex_constan_lrgkqac(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_mentions_complex_constan_lrgkqac F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* l */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* lhs */
    void * gm_l6; /* rhs */
    GM_STACK_CHECK(&F, 436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    gm_tkv1vap90vht2 gm_l10;
    uint8_t gm_l11;
    void * t19 = gm_l1;
    {
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
            if (!t21) {
                goto gm_lb2;
            }
            void * t22 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l2 = t22;
            void * t23 = gm_l2;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t23) + 98));
            F.ret = t24;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)9ULL));
            if (!t26) {
                goto gm_lb3;
            }
            void * t27 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l3 = t27;
            void * t28 = gm_l0;
            void * t29 = gm_l3;
            uint8_t t30 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t30;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)12ULL));
            if (!t32) {
                goto gm_lb4;
            }
            void * t33 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 8));
            gm_l4 = t33;
            void * t34 = gm_l0;
            void * t35 = gm_l4;
            uint8_t t36 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t36;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)13ULL));
            if (!t38) {
                goto gm_lb5;
            }
            void * t39 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 8));
            gm_l5 = t39;
            void * t40 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 16));
            gm_l6 = t40;
            void * t41 = gm_l0;
            void * t42 = gm_l5;
            uint8_t t43 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t43) {
                gm_l9 = ((uint8_t)1);
            } else {
                void * t44 = gm_l0;
                void * t45 = gm_l6;
                uint8_t t46 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t46;
            }
            uint8_t t47 = gm_l9;
            F.ret = t47;
            goto gm_fret;
        }
gm_lb5:;
        {
            {
                {
                    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)6ULL));
                    if (!t49) {
                        goto gm_lb8;
                    }
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)22ULL));
                    if (!t51) {
                        goto gm_lb9;
                    }
                    goto gm_lb7;
                }
gm_lb9:;
                goto gm_lb6;
            }
gm_lb7:;
            {
                {
                    void * t52 = gm_l0;
                    void * t53 = gm_l1;
                    gm_f_try_untyped_8j8up35((void *)&gm_l10, (void *)NULL, cx, NULL, t52, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l10))[208] != 0);
                    if (!t54) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l10), 208);
                    {
                        {
                            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)0ULL));
                            if (!t56) {
                                goto gm_lb13;
                            }
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)&F.l7) + 8), 200);
                            {
                                {
                                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)4ULL));
                                    if (!t58) {
                                        goto gm_lb15;
                                    }
                                    gm_l11 = ((uint8_t)1);
                                    goto gm_lb14;
                                }
gm_lb15:;
                                {
                                    gm_l11 = ((uint8_t)0);
                                    goto gm_lb14;
                                }
gm_lb16:;
                                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 103, 36);
                            }
gm_lb14:;
                            uint8_t t59 = gm_l11;
                            if (!t59) {
                                goto gm_lb13;
                            }
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb13:;
                    }
gm_lb12:;
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb6:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 96, 5);
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
extern __attribute__((aligned(4))) uint8_t gm_f_plain_0otlpn0(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_promote_real_constant_v0nhili F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* agg */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_taari1s0m51a7 gm_l4; /* k */
    void * gm_l5; /* zero */
    GM_STACK_CHECK(&F, 576u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_t9qkh0rsooc8d gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t12 = gm_l1;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_l7 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_l7 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l7 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 143, 8);
    }
gm_lb1:;
    uint8_t t17 = gm_l7;
    if (t17) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t18 = gm_l0;
    void * t19 = (void *)(((uint8_t *)t18) + 96);
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = (void *)(((uint8_t *)t21) + 8);
    void * t23 = gm_l1;
    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t23));
    gm_taari1s0m51a7 t25 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t19, t22, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t25;
    {
        {
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l4))[1] != 0);
            if (t26) {
                goto gm_lb6;
            }
            gm_l9 = ((uint8_t)1);
            goto gm_lb5;
        }
gm_lb6:;
        {
            gm_l9 = ((uint8_t)0);
            goto gm_lb5;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 145, 8);
    }
gm_lb5:;
    uint8_t t27 = gm_l9;
    if (t27) {
        gm_l8 = ((uint8_t)1);
    } else {
        gm_taari1s0m51a7 t28 = gm_l4;
        gm_t9qkh0rsooc8d t29;
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)2ULL));
        gm_taari1s0m51a7 t30;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[1] = 1;
        uint8_t t31 = (gm_eq5307j5k9ms(((uint8_t *)&t28), ((uint8_t *)&t30)));
        gm_l8 = t31;
    }
    uint8_t t32 = gm_l8;
    if (t32) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t33 = gm_l0;
    void * t34 = (void *)(((uint8_t *)t33) + 96);
    void * t35 = gm_l0;
    void * t36 = GM_LD(void *, ((uint8_t *)t35));
    void * t37 = (void *)(((uint8_t *)t36) + 8);
    void * t38 = gm_l1;
    gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t38));
    gm_t88rcucg1b7uk t40 = gm_l3;
    gm_t9qkh0rsooc8d t41 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t34, t37, t39, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t41;
    {
        {
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (t42) {
                goto gm_lb9;
            }
            gm_l10 = ((uint8_t)1);
            goto gm_lb8;
        }
gm_lb9:;
        {
            gm_l10 = ((uint8_t)0);
            goto gm_lb8;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 146, 8);
    }
gm_lb8:;
    uint8_t t43 = gm_l10;
    if (t43) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t44 = gm_l3;
    gm_tngtuiuskn938 t45;
    gm_f_zero_9r6o7sj((void *)&t45, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t2b6tm8c0vgo1 t46;
    gm_f_untyped_float_m6lbr0g((void *)&t46, (void *)NULL, cx, NULL, &t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t47 = gm_l1;
    gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 72));
    void * t49 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t44, &t46, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t49;
    gm_trqfvmjtt4sij t50;
    gm_f_list_vp1djv1_2((void *)&t50, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t50;
    void * t51 = (void *)((uint8_t *)&F.l6);
    void * t52 = gm_l1;
    gm_tftb1odc739t5 t53 = gm_f_push_d943vgp_2(t51, &(*(gm_tcgrjq9toscke *)((uint8_t *)t52)));
    gm_l12 = t53;
    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srci09ctgf0djoe, 149, 5);
    void * t55 = (void *)((uint8_t *)&F.l6);
    void * t56 = gm_l5;
    gm_tftb1odc739t5 t57 = gm_f_push_d943vgp_2(t55, &(*(gm_tcgrjq9toscke *)((uint8_t *)t56)));
    gm_l13 = t57;
    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srci09ctgf0djoe, 150, 5);
    gm_t88rcucg1b7uk t59 = gm_l2;
    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    void * t61 = gm_l1;
    gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 72));
    gm_t2b6tm8c0vgo1 t63;
    memset(&t63, 0, sizeof t63);
    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 8), t60);
    void * t64 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t59, &t63, t62);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t64;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_settle_complex_operands_9439n4u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_settle_complex_operands_9439n4u F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* r */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* lc */
    uint8_t gm_l7; /* rc */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* side */
    gm_t88rcucg1b7uk gm_l11; /* var */
    gm_t88rcucg1b7uk gm_l12; /* other0 */
    uint8_t gm_l13; /* var_left */
    gm_t88rcucg1b7uk gm_l14; /* other */
    uint32_t gm_l15; /* c */
    gm_tnqk2qt7j42o9 gm_l16; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l17; /* t */
    gm_tqsk8dt6ib3qq gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_t88rcucg1b7uk gm_l21; /* t */
    GM_STACK_CHECK(&F, 962u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t88rcucg1b7uk gm_l22;
    gm_t88rcucg1b7uk gm_l23;
    uint8_t gm_l24;
    gm_tkn42u7na3t81 gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_t53at80pfdus3 gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    gm_t9805qtonbsag gm_l32;
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    void * t31 = (void *)(((uint8_t *)t30) + 8);
    gm_l5 = t31;
    void * t32 = gm_l0;
    void * t33 = gm_l2;
    gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
    uint8_t t35 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t32, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t35;
    void * t36 = gm_l0;
    void * t37 = gm_l3;
    gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t37));
    uint8_t t39 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t36, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t39;
    {
        {
            uint8_t t40 = gm_l6;
            uint8_t t41 = (uint8_t)!t40;
            if (!t41) {
                goto gm_lb2;
            }
            uint8_t t42 = gm_l7;
            uint8_t t43 = (uint8_t)!t42;
            if (!t43) {
                goto gm_lb2;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t44 = gm_l6;
            if (!t44) {
                goto gm_lb4;
            }
            uint8_t t45 = gm_l7;
            if (!t45) {
                goto gm_lb4;
            }
            void * t46 = gm_l0;
            void * t47 = (void *)(((uint8_t *)t46) + 96);
            void * t48 = gm_l5;
            void * t49 = gm_l2;
            gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t49));
            void * t51 = gm_l3;
            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t51));
            gm_t9qkh0rsooc8d t53 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t47, t48, t50, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_l9 = ((uint64_t)0ULL);
    gm_l8 = ((uint64_t)2ULL);
    {
        for (;;) {
            uint64_t t54 = gm_l9;
            uint64_t t55 = gm_l8;
            uint8_t t56 = (t54 < t55);
            if (!t56) {
                goto gm_lb5;
            }
            {
                {
                    {
                        uint64_t t57 = gm_l9;
                        gm_l10 = t57;
                        uint64_t t58 = gm_l10;
                        uint8_t t59 = ((t58 == ((uint64_t)0ULL)));
                        if (t59) {
                            void * t60 = gm_l2;
                            gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t60));
                            gm_l22 = t61;
                        } else {
                            void * t62 = gm_l3;
                            gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t62));
                            gm_l22 = t63;
                        }
                        gm_t88rcucg1b7uk t64 = gm_l22;
                        gm_l11 = t64;
                        uint64_t t65 = gm_l10;
                        uint8_t t66 = ((t65 == ((uint64_t)0ULL)));
                        if (t66) {
                            void * t67 = gm_l3;
                            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t67));
                            gm_l23 = t68;
                        } else {
                            void * t69 = gm_l2;
                            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t69));
                            gm_l23 = t70;
                        }
                        gm_t88rcucg1b7uk t71 = gm_l23;
                        gm_l12 = t71;
                        uint64_t t72 = gm_l10;
                        uint8_t t73 = ((t72 == ((uint64_t)0ULL)));
                        gm_l13 = t73;
                        void * t74 = gm_l0;
                        gm_t88rcucg1b7uk t75 = gm_l11;
                        uint8_t t76 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t74, t75);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t77 = (uint8_t)!t76;
                        if (t77) {
                            goto gm_lb7;
                        }
                        void * t78 = gm_l0;
                        void * t79 = (void *)(((uint8_t *)t78) + 96);
                        void * t80 = gm_l0;
                        void * t81 = GM_LD(void *, ((uint8_t *)t80));
                        gm_t88rcucg1b7uk t82 = gm_l12;
                        gm_t88rcucg1b7uk t83 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t79, t81, t82);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l14 = t83;
                        void * t84 = gm_l0;
                        void * t85 = GM_LD(void *, ((uint8_t *)t84));
                        gm_t88rcucg1b7uk t86 = gm_l14;
                        gm_tkn42u7na3t81 t87 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t85, t86);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l25 = t87;
                        {
                            {
                                uint8_t t88 = (uint8_t)((((uint8_t *)&gm_l25))[4] != 0);
                                if (t88) {
                                    goto gm_lb11;
                                }
                                gm_l24 = ((uint8_t)0);
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                gm_l24 = ((uint8_t)1);
                                goto gm_lb10;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 125, 12);
                        }
gm_lb10:;
                        uint8_t t89 = gm_l24;
                        if (t89) {
                            void * t90 = gm_l0;
                            void * t91 = (void *)(((uint8_t *)t90) + 96);
                            void * t92 = gm_l5;
                            gm_t88rcucg1b7uk t93 = gm_l11;
                            gm_t88rcucg1b7uk t94 = gm_l14;
                            gm_t9qkh0rsooc8d t95 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t91, t92, t93, t94);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            {
                                {
                                    void * t96 = gm_l5;
                                    gm_t88rcucg1b7uk t97 = gm_l14;
                                    uint8_t t98 = gm_f_is_float_3jk89qu(t96, GM_CONV(gm_csguankko4lmhd, t97));
                                    if (t98) {
                                        gm_l26 = ((uint8_t)1);
                                    } else {
                                        void * t99 = gm_l5;
                                        gm_t88rcucg1b7uk t100 = gm_l14;
                                        gm_f_kind_jcnt4hm((void *)&gm_l28, t99, GM_CONV(gm_csguankko4lmhd, t100));
                                        {
                                            {
                                                uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                                                uint8_t t102 = (uint8_t)(t101 == ((uint8_t)19ULL));
                                                if (!t102) {
                                                    goto gm_lb16;
                                                }
                                                gm_l27 = ((uint8_t)1);
                                                goto gm_lb15;
                                            }
gm_lb16:;
                                            {
                                                gm_l27 = ((uint8_t)0);
                                                goto gm_lb15;
                                            }
gm_lb17:;
                                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 127, 43);
                                        }
gm_lb15:;
                                        uint8_t t103 = gm_l27;
                                        gm_l26 = t103;
                                    }
                                    uint8_t t104 = gm_l26;
                                    if (!t104) {
                                        goto gm_lb14;
                                    }
                                    {
                                        {
                                            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)2ULL));
                                            if (!t106) {
                                                goto gm_lb19;
                                            }
                                            gm_l30 = ((uint8_t)1);
                                            goto gm_lb18;
                                        }
gm_lb19:;
                                        {
                                            gm_l30 = ((uint8_t)0);
                                            goto gm_lb18;
                                        }
gm_lb20:;
                                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 127, 78);
                                    }
gm_lb18:;
                                    uint8_t t107 = gm_l30;
                                    if (t107) {
                                        gm_l29 = ((uint8_t)1);
                                    } else {
                                        gm_ts6oqppjh0ba3 t108 = gm_l1;
                                        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&t108) + 0));
                                        uint8_t t110 = (uint8_t)((t109 == ((uint8_t)4ULL)));
                                        gm_ts6oqppjh0ba3 t111;
                                        GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)4ULL));
                                        if (t110) {
                                            uint8_t t112 = gm_l13;
                                            gm_l31 = t112;
                                        } else {
                                            gm_l31 = ((uint8_t)0);
                                        }
                                        uint8_t t113 = gm_l31;
                                        gm_l29 = t113;
                                    }
                                    uint8_t t114 = gm_l29;
                                    if (!t114) {
                                        goto gm_lb14;
                                    }
                                    {
                                        {
                                            void * t115 = gm_l0;
                                            void * t116 = GM_LD(void *, ((uint8_t *)t115));
                                            gm_b16_8 t117;
                                            GM_ST(void *, (uint8_t *)&t117, (void *)(gm_s0rphlcbno91v));
                                            GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(7));
                                            gm_t9805qtonbsag t118 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t116, t117);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l32 = t118;
                                            uint8_t t119 = (uint8_t)((((uint8_t *)&gm_l32))[4] != 0);
                                            if (!t119) {
                                                goto gm_lb22;
                                            }
                                            uint32_t t120 = GM_LD(uint32_t, ((uint8_t *)&gm_l32));
                                            gm_l15 = t120;
                                            void * t121 = gm_l0;
                                            void * t122 = GM_LD(void *, ((uint8_t *)t121));
                                            uint32_t t123 = gm_l15;
                                            gm_t88rcucg1b7uk t124 = gm_l14;
                                            gm_tnqk2qt7j42o9 t125;
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t125 + 0), t124);
                                            gm_l16 = t125;
                                            void * t126 = (void *)((uint8_t *)&gm_l16);
                                            uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t128 = (uintptr_t)((uint64_t)1ULL);
                                            if (t127 > t128 || t128 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t127, t128, ((uint64_t)1ULL), gm_srci09ctgf0djoe, 130, 46);
                                            uint64_t t129 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                            gm_b16_8 t130;
                                            GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t126 + (uintptr_t)((uint64_t)0ULL) * 4)));
                                            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(t129));
                                            gm_t88rcucg1b7uk t131 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t122, t123, t130);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l17 = t131;
                                            void * t132 = gm_l0;
                                            void * t133 = (void *)(((uint8_t *)t132) + 96);
                                            void * t134 = gm_l5;
                                            gm_t88rcucg1b7uk t135 = gm_l11;
                                            gm_t88rcucg1b7uk t136 = gm_l17;
                                            gm_t9qkh0rsooc8d t137 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t133, t134, t135, t136);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb21;
                                        }
gm_lb22:;
                                    }
gm_lb21:;
                                    goto gm_lb13;
                                }
gm_lb14:;
                            }
gm_lb13:;
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 119, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t138 = gm_l9;
            uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
            uint64_t t141 = r139;
            gm_l9 = t141;
        }
    }
gm_lb5:;
    void * t142 = gm_l2;
    gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t142));
    void * t144 = gm_l3;
    gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t144));
    gm_tqsk8dt6ib3qq t146;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t146 + 0), t143);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t146 + 4), t145);
    gm_l18 = t146;
    void * t147 = (void *)((uint8_t *)&gm_l18);
    uint64_t t148 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t149 = (uintptr_t)((uint64_t)2ULL);
    if (t148 > t149 || t149 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t148, t149, ((uint64_t)2ULL), gm_srci09ctgf0djoe, 135, 5);
    uint64_t t150 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t151;
    GM_ST(void *, (uint8_t *)&t151, (void *)(((uint8_t *)t147 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(t150));
    gm_l19 = t151;
    gm_l20 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t152 = gm_l20;
            uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
            uint8_t t154 = (t152 < t153);
            if (!t154) {
                goto gm_lb23;
            }
            {
                void * t155 = GM_LD(void *, ((uint8_t *)&gm_l19));
                uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint64_t t157 = gm_l20;
                {
                    {
                        gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t155) + (uintptr_t)t157 * 4));
                        gm_l21 = t158;
                        void * t159 = gm_l0;
                        gm_t88rcucg1b7uk t160 = gm_l21;
                        uint8_t t161 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t159, t160);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t161) {
                            void * t162 = gm_l0;
                            gm_t88rcucg1b7uk t163 = gm_l21;
                            gm_tl874t2atnlh3 t164 = gm_l4;
                            gm_t88rcucg1b7uk t165 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t162, t163, t164);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb26;
                    }
gm_lb27:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 135, 5);
                }
gm_lb26:;
            }
gm_lb25:;
            uint64_t t166 = gm_l20;
            uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
            uint64_t t169 = r167;
            gm_l20 = t169;
        }
    }
gm_lb23:;
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
