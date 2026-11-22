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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; } gm_t6igi6646q59o;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { uint64_t f0; uint64_t f1; gm_th2lqbo77pvsc f2; } gm_tvin65ttqbr1d;
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t6igi6646q59o) == 56 && _Alignof(gm_t6igi6646q59o) == 8 && offsetof(gm_t6igi6646q59o, f0) == 0 && offsetof(gm_t6igi6646q59o, f1) == 40, "layout of `SplitSignature`");
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_t6ok5tvcjc200 f1; } gm_t8scu1osgcra5;
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
typedef struct { gm_tvin65ttqbr1d f0; uint8_t f1; uint8_t pe[7]; } gm_t9nnki79sf2e1;
typedef struct { gm_t6igi6646q59o f0; uint8_t f1; uint8_t pe[7]; } gm_tec7dnv8fjfj8;
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { gm_t8vuock3jnv59 a[1]; } gm_tq47bkpmd2b9a;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tvin65ttqbr1d) == 40 && _Alignof(gm_tvin65ttqbr1d) == 8 && offsetof(gm_tvin65ttqbr1d, f0) == 0 && offsetof(gm_tvin65ttqbr1d, f1) == 8 && offsetof(gm_tvin65ttqbr1d, f2) == 16, "layout of `CallContext`");
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_t8scu1osgcra5) == 56 && _Alignof(gm_t8scu1osgcra5) == 8 && offsetof(gm_t8scu1osgcra5, f0) == 0 && offsetof(gm_t8scu1osgcra5, f1) == 16, "layout of `HoverResult`");
_Static_assert(sizeof(gm_t9nnki79sf2e1) == 48 && _Alignof(gm_t9nnki79sf2e1) == 8 && offsetof(gm_t9nnki79sf2e1, f0) == 0 && offsetof(gm_t9nnki79sf2e1, f1) == 40, "layout of `?CallContext`");
_Static_assert(sizeof(gm_tec7dnv8fjfj8) == 64 && _Alignof(gm_tec7dnv8fjfj8) == 8 && offsetof(gm_tec7dnv8fjfj8, f0) == 0 && offsetof(gm_tec7dnv8fjfj8, f1) == 56, "layout of `?SplitSignature`");
typedef struct { gm_t8scu1osgcra5 f0; uint8_t f1; uint8_t pe[7]; } gm_teijtv56moffm;
_Static_assert(sizeof(gm_tq47bkpmd2b9a) == 24 && _Alignof(gm_tq47bkpmd2b9a) == 8 && offsetof(gm_tq47bkpmd2b9a, a) == 0, "layout of `[1]Value`");
_Static_assert(sizeof(gm_teijtv56moffm) == 64 && _Alignof(gm_teijtv56moffm) == 8 && offsetof(gm_teijtv56moffm, f0) == 0 && offsetof(gm_teijtv56moffm, f1) == 56, "layout of `?HoverResult`");
struct gm_fr_array_k32ana3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_call_context_r6lbirq {
    gm_frame h;
    gm_t9nnki79sf2e1 ret;
};
struct gm_fr_completion_kind_flii2l4 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_definition_kaqo3c6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_document_symbols_0nlnk2i {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_qmfcuqk_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_oucukrb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9ug250p {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gej351p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_full_edit_82h286k {
    gm_frame h;
    gm_tin97tt4ud0s1 l4; /* e */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hover_ub02g3b {
    gm_frame h;
    gm_t8scu1osgcra5 l3; /* h */
    gm_tin97tt4ud0s1 l4; /* contents */
    gm_tin97tt4ud0s1 l5; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_int_95caidg {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
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
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_uen38t5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_item_json_ml3fbup {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* o */
    gm_tin97tt4ud0s1 l4; /* m */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_line_index_2necgtf {
    gm_frame h;
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_list_0202sdh_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_location_i7gnofg {
    gm_frame h;
    gm_tin97tt4ud0s1 l3; /* o */
    gm_t3ph3kbiqt1ah ret;
};
struct gm_fr_markdown_posoa59 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_member_context_p58kq5l {
    gm_frame h;
    gm_tc70a9dbbmgkg ret;
};
struct gm_fr_members_8m2g0sl {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* out */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_members_at_do67lp0 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_names_ms1jmv4 {
    gm_frame h;
    gm_t4c7ipjj9s3fp l4; /* seen */
    gm_trqfvmjtt4sij l5; /* out */
    gm_trqfvmjtt4sij l6; /* locals */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_obj_aoa7fl7 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_offset_at_73sli4j {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_push_0pgacsj_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_references_pd7nt1t {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* spans */
    gm_trqfvmjtt4sij l6; /* out */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_signature_help_16c2a8f {
    gm_frame h;
    gm_tvin65ttqbr1d l4; /* cc */
    gm_t8scu1osgcra5 l6; /* h */
    gm_t6igi6646q59o l8; /* sp */
    gm_trqfvmjtt4sij l9; /* params */
    gm_trqfvmjtt4sij l13; /* kept */
    gm_trqfvmjtt4sij l24; /* ps */
    gm_tin97tt4ud0s1 l28; /* po */
    gm_tin97tt4ud0s1 l29; /* sig */
    gm_tin97tt4ud0s1 l31; /* m */
    gm_tin97tt4ud0s1 l32; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_source_text_7ru912k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_split_signature_3r31d7h {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* params */
    gm_tec7dnv8fjfj8 ret;
};
struct gm_fr_stable_sort_by_qutfkbd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_symbol_json_b065ihj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* children */
    gm_tin97tt4ud0s1 l6; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_symbol_kind_g6njvq1 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_text_ls49dp6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_uri_of_t6r79gp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_value_pkuqal3 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_write_8qoqt62 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_call_context_r6lbirq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) int64_t gm_f_completion_kind_flii2l4(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_definition_at_9jj9uuu(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_definition_kaqo3c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_document_symbols_0nlnk2i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_qmfcuqk_3(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fence_hq2pe32(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_oucukrb(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9ug250p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eara94q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gej351p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_format_he6jnp0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_full_edit_82h286k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_get_7o29gjt(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hover_at_19okc4g(void *gm_out, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hover_ub02g3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) void gm_f_item_json_ml3fbup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_line_index_2necgtf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_line_starts_oja6lfl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_lines_0mpphhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_locals_at_2n5h0d6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_location_i7gnofg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_markdown_posoa59(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_member_context_p58kq5l(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tc70a9dbbmgkg gm_f_member_context_p58kq5l_c(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_members_8m2g0sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
inline __attribute__((aligned(4))) void gm_f_members_at_do67lp0(void *gm_out, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_merge_sort_qr74lj4(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_names_ms1jmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) uint64_t gm_f_offset_hdhse2v(void * a0, uint32_t a1, uint32_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_open_path_6gmq4nk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_outline_of_01eujq2(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_path_to_uri_as8bikc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_range_demc0bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_range_json_hlsolbo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_references_pd7nt1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_remove_qvip58u(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signature_help_16c2a8f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_signature_3r31d7h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_qutfkbd(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_symbol_json_b065ihj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) int64_t gm_f_symbol_kind_g6njvq1(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_uses_of_nalefva(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8qoqt62(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
extern uint8_t gm_k7bjnihrifk[40] __attribute__((aligned(8)));
extern uint8_t gm_k8nmk7je8im[40] __attribute__((aligned(8)));
extern uint8_t gm_klfj89orh68[40] __attribute__((aligned(8)));
extern uint8_t gm_kn81j57eee2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1b629p19p3qq[4];
extern const uint8_t gm_s22mk3qhbqci2[11];
extern const uint8_t gm_s2dssfe3261t2[2];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s54psrmgl6ti7[9];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_s7b8bvi5kqaqt[7];
extern const uint8_t gm_s7j2u3ofu66j8[5];
extern const uint8_t gm_s7unnudd0kkri[14];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_sb6smklmjl60o[7];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_schvua5hkahp9[16];
extern const uint8_t gm_sd6a57g2jmtks[11];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgekdmqbrecpp[9];
extern const uint8_t gm_si1j2hanh22ll[16];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sirpcjgkmlehr[8];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sm06nsg0msdgd[9];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const char gm_srcvksed7nkdegg[];
extern const uint8_t gm_srdnkonbfvqdq[9];
extern const uint8_t gm_srplcc0p49iui[9];
extern const uint8_t gm_st562h9so873c[14];
extern const uint8_t gm_sttp5u65omp7t[15];
extern const uint8_t gm_su137hja0ebcd[6];
extern const uint8_t gm_sum9vj65rg4jb[4];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplpcqqtntqphus;
extern const gm_tmpl gm_tplrtd64pk59l9s;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_oucukrb(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_oucukrb F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((((uint8_t *)t2) + 64) + 4));
    void * t4 = gm_l1;
    uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)t4) + 64) + 4));
    uint8_t t6 = (t3 > t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_format_9ug250p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9ug250p F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgekdmqbrecpp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_he6jnp0((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrtd64pk59l9s), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_gej351p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0) {
    struct gm_fr_format_gej351p F;
    gm_tm98ap4112hee gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tm98ap4112hee gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tm98ap4112hee t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_eara94q((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0) {
    struct gm_fr_int_95caidg F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    int64_t t2 = gm_l0;
    gm_tm98ap4112hee t3;
    GM_ST(int64_t, ((uint8_t *)&t3 + 0), t2);
    gm_f_format_gej351p((void *)&gm_l1, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcgp1oqfehgsjb, 33, 74);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_tnpifv20irnph t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
    gm_t8vuock3jnv59 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)2ULL));
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t8 + 8), t7);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_uen38t5 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint8_t)32ULL)));
    if (t3) {
        gm_l5 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)9ULL)));
        gm_l5 = t5;
    }
    uint8_t t6 = gm_l5;
    if (t6) {
        gm_l4 = ((uint8_t)1);
    } else {
        uint8_t t7 = gm_l0;
        uint8_t t8 = ((t7 == ((uint8_t)10ULL)));
        gm_l4 = t8;
    }
    uint8_t t9 = gm_l4;
    if (t9) {
        gm_l3 = ((uint8_t)1);
    } else {
        uint8_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint8_t)13ULL)));
        gm_l3 = t11;
    }
    uint8_t t12 = gm_l3;
    if (t12) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint8_t t13 = gm_l0;
        uint8_t t14 = ((t13 == ((uint8_t)11ULL)));
        gm_l2 = t14;
    }
    uint8_t t15 = gm_l2;
    if (t15) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t16 = gm_l0;
        uint8_t t17 = ((t16 == ((uint8_t)12ULL)));
        gm_l1 = t17;
    }
    uint8_t t18 = gm_l1;
    F.ret = t18;
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
inline __attribute__((aligned(4))) void gm_f_members_at_do67lp0(void *gm_out, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_members_at_do67lp0 F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* lo */
    uint32_t gm_l3; /* hi */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 272);
    uint32_t t4 = gm_l1;
    uint32_t t5 = gm_l2;
    uint32_t t6 = gm_l3;
    gm_tl874t2atnlh3 t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t4);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t5);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 8), t6);
    gm_f_get_7o29gjt((void *)&(*(gm_th2lqbo77pvsc *)gm_out), t3, GM_CONV(gm_cseahvg8v7p0vs, t7));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    struct gm_fr_offset_at_73sli4j F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* line */
    uint32_t gm_l3; /* character */
    gm_t6j6smj7qoctu gm_l4; /* ix */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint32_t t4 = gm_l1;
    gm_t6j6smj7qoctu t5;
    gm_f_lines_0mpphhg((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t5;
    void * t6 = (void *)((uint8_t *)&gm_l4);
    uint32_t t7 = gm_l2;
    uint32_t t8 = gm_l3;
    void * t9 = gm_l0;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t9) + 8));
    uint64_t t11 = gm_f_offset_hdhse2v(t6, t7, t8, t10);
    uint32_t t12 = (uint32_t)t11;
    F.ret = t12;
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
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_qutfkbd(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stable_sort_by_qutfkbd F;
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
    gm_f_merge_sort_qr74lj4(t2, t3);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8qoqt62(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8qoqt62 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st562h9so873c));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplpcqqtntqphus), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_call_context_r6lbirq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_call_context_r6lbirq F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* off_in */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* off */
    uint64_t gm_l4; /* depth */
    uint64_t gm_l5; /* commas */
    uint64_t gm_l6; /* i */
    gm_tf5srh4mp4b3i gm_l7; /* last_comma */
    uint64_t gm_l8; /* open */
    uint8_t gm_l9; /* c */
    uint64_t gm_l10; /* j */
    uint64_t gm_l11; /* end */
    uint64_t gm_l12; /* start */
    uint64_t gm_l13; /* c */
    gm_b16_8 gm_l14; /* seg */
    gm_th2lqbo77pvsc gm_l15; /* named */
    gm_t6j6smj7qoctu gm_l16; /* sp */
    gm_b16_8 gm_l17; /* n */
    uint8_t gm_l18; /* ok */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint8_t gm_l21; /* ch */
    GM_STACK_CHECK(&F, 1546u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    gm_ttu09lij36f8c gm_l29;
    {
        gm_b16_8 t33 = gm_l0;
        gm_l2 = t33;
        uint64_t t34 = gm_l1;
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t36 = (t34 < t35 ? t34 : t35);
        gm_l3 = t36;
        gm_l4 = ((uint64_t)0ULL);
        gm_l5 = ((uint64_t)0ULL);
        uint64_t t37 = gm_l3;
        gm_l6 = t37;
        gm_tf5srh4mp4b3i t38;
        memset(&t38, 0, sizeof t38);
        gm_l7 = t38;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb1;
                }
                {
                    uint64_t t39 = gm_l6;
                    uint8_t t40 = ((t39 == ((uint64_t)0ULL)));
                    if (t40) {
                        gm_t9nnki79sf2e1 t41;
                        memset(&t41, 0, sizeof t41);
                        memmove(gm_out, ((uint8_t *)&t41), 48);
                        goto gm_fdone;
                    }
                    uint64_t t42 = gm_l6;
                    uint64_t r43; bool o44 = __builtin_sub_overflow(t42, ((uint64_t)1ULL), &r43);
                    if (o44) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 242, 9);
                    uint64_t t45 = r43;
                    gm_l6 = t45;
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t48 = gm_l6;
                    if ((uintptr_t)t48 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t48, t47, gm_srcvksed7nkdegg, 243, 14);
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)t48 * 1));
                    gm_l9 = t49;
                    uint8_t t50 = gm_l9;
                    uint8_t t51 = ((t50 == ((uint8_t)41ULL)));
                    if (t51) {
                        gm_l23 = ((uint8_t)1);
                    } else {
                        uint8_t t52 = gm_l9;
                        uint8_t t53 = ((t52 == ((uint8_t)93ULL)));
                        gm_l23 = t53;
                    }
                    uint8_t t54 = gm_l23;
                    if (t54) {
                        gm_l22 = ((uint8_t)1);
                    } else {
                        uint8_t t55 = gm_l9;
                        uint8_t t56 = ((t55 == ((uint8_t)125ULL)));
                        gm_l22 = t56;
                    }
                    uint8_t t57 = gm_l22;
                    if (t57) {
                        uint64_t t58 = gm_l4;
                        uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                        if (o60) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 245, 13);
                        uint64_t t61 = r59;
                        gm_l4 = t61;
                    } else {
                        {
                            {
                                uint8_t t62 = gm_l9;
                                uint8_t t63 = ((t62 == ((uint8_t)40ULL)));
                                if (!t63) {
                                    goto gm_lb5;
                                }
                                uint64_t t64 = gm_l4;
                                uint8_t t65 = ((t64 == ((uint64_t)0ULL)));
                                if (!t65) {
                                    goto gm_lb5;
                                }
                                uint64_t t66 = gm_l6;
                                gm_l8 = t66;
                                goto gm_lb1;
                            }
gm_lb5:;
                            uint8_t t67 = gm_l9;
                            uint8_t t68 = ((t67 == ((uint8_t)40ULL)));
                            if (t68) {
                                uint64_t t69 = gm_l4;
                                uint64_t r70; bool o71 = __builtin_sub_overflow(t69, ((uint64_t)1ULL), &r70);
                                if (o71) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 250, 13);
                                uint64_t t72 = r70;
                                gm_l4 = t72;
                            } else {
                                {
                                    {
                                        uint8_t t73 = gm_l9;
                                        uint8_t t74 = ((t73 == ((uint8_t)91ULL)));
                                        if (t74) {
                                            gm_l24 = ((uint8_t)1);
                                        } else {
                                            uint8_t t75 = gm_l9;
                                            uint8_t t76 = ((t75 == ((uint8_t)123ULL)));
                                            gm_l24 = t76;
                                        }
                                        uint8_t t77 = gm_l24;
                                        if (!t77) {
                                            goto gm_lb7;
                                        }
                                        uint64_t t78 = gm_l4;
                                        uint8_t t79 = ((t78 == ((uint64_t)0ULL)));
                                        if (!t79) {
                                            goto gm_lb7;
                                        }
                                        gm_t9nnki79sf2e1 t80;
                                        memset(&t80, 0, sizeof t80);
                                        memmove(gm_out, ((uint8_t *)&t80), 48);
                                        goto gm_fdone;
                                    }
gm_lb7:;
                                    uint8_t t81 = gm_l9;
                                    uint8_t t82 = ((t81 == ((uint8_t)91ULL)));
                                    if (t82) {
                                        gm_l25 = ((uint8_t)1);
                                    } else {
                                        uint8_t t83 = gm_l9;
                                        uint8_t t84 = ((t83 == ((uint8_t)123ULL)));
                                        gm_l25 = t84;
                                    }
                                    uint8_t t85 = gm_l25;
                                    if (t85) {
                                        uint64_t t86 = gm_l4;
                                        uint64_t r87; bool o88 = __builtin_sub_overflow(t86, ((uint64_t)1ULL), &r87);
                                        if (o88) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 254, 13);
                                        uint64_t t89 = r87;
                                        gm_l4 = t89;
                                    } else {
                                        {
                                            {
                                                uint8_t t90 = gm_l9;
                                                uint8_t t91 = ((t90 == ((uint8_t)44ULL)));
                                                if (!t91) {
                                                    goto gm_lb9;
                                                }
                                                uint64_t t92 = gm_l4;
                                                uint8_t t93 = ((t92 == ((uint64_t)0ULL)));
                                                if (!t93) {
                                                    goto gm_lb9;
                                                }
                                                uint64_t t94 = gm_l5;
                                                uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                                                if (o96) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 256, 13);
                                                uint64_t t97 = r95;
                                                gm_l5 = t97;
                                                {
                                                    {
                                                        uint8_t t98 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                                                        if (t98) {
                                                            goto gm_lb11;
                                                        }
                                                        gm_l26 = ((uint8_t)1);
                                                        goto gm_lb10;
                                                    }
gm_lb11:;
                                                    {
                                                        gm_l26 = ((uint8_t)0);
                                                        goto gm_lb10;
                                                    }
gm_lb12:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 257, 16);
                                                }
gm_lb10:;
                                                uint8_t t99 = gm_l26;
                                                if (t99) {
                                                    uint64_t t100 = gm_l6;
                                                    gm_tf5srh4mp4b3i t101;
                                                    GM_ST(uint8_t, (uint8_t *)&t101 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t101 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t101 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t101), t100);
                                                    ((uint8_t *)&t101)[8] = 1;
                                                    gm_l7 = t101;
                                                }
                                                goto gm_lb8;
                                            }
gm_lb9:;
                                            uint8_t t102 = gm_l9;
                                            uint8_t t103 = ((t102 == ((uint8_t)34ULL)));
                                            if (t103) {
                                                uint64_t t104 = gm_l6;
                                                gm_l10 = t104;
                                                {
                                                    for (;;) {
                                                        uint64_t t105 = gm_l10;
                                                        uint8_t t106 = (t105 > ((uint64_t)0ULL));
                                                        if (!t106) {
                                                            goto gm_lb13;
                                                        }
                                                        void * t107 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                        uint64_t t109 = gm_l10;
                                                        uint64_t r110; bool o111 = __builtin_sub_overflow(t109, ((uint64_t)1ULL), &r110);
                                                        if (o111) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 261, 31);
                                                        uint64_t t112 = r110;
                                                        if ((uintptr_t)t112 >= (uintptr_t)(t108)) gm_trap_index((uintptr_t)t112, t108, gm_srcvksed7nkdegg, 261, 29);
                                                        uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)t107) + (uintptr_t)t112 * 1));
                                                        uint8_t t114 = (!(t113 == ((uint8_t)10ULL)));
                                                        if (!t114) {
                                                            goto gm_lb13;
                                                        }
                                                        {
                                                            uint64_t t115 = gm_l10;
                                                            uint64_t r116; bool o117 = __builtin_sub_overflow(t115, ((uint64_t)1ULL), &r116);
                                                            if (o117) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 262, 17);
                                                            uint64_t t118 = r116;
                                                            gm_l10 = t118;
                                                            {
                                                                {
                                                                    void * t119 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                                    uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                                    uint64_t t121 = gm_l10;
                                                                    if ((uintptr_t)t121 >= (uintptr_t)(t120)) gm_trap_index((uintptr_t)t121, t120, gm_srcvksed7nkdegg, 263, 20);
                                                                    uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)t119) + (uintptr_t)t121 * 1));
                                                                    uint8_t t123 = ((t122 == ((uint8_t)34ULL)));
                                                                    if (!t123) {
                                                                        goto gm_lb17;
                                                                    }
                                                                    uint64_t t124 = gm_l10;
                                                                    uint8_t t125 = ((t124 == ((uint64_t)0ULL)));
                                                                    if (t125) {
                                                                        gm_l27 = ((uint8_t)1);
                                                                    } else {
                                                                        void * t126 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                                        uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                                        uint64_t t128 = gm_l10;
                                                                        uint64_t r129; bool o130 = __builtin_sub_overflow(t128, ((uint64_t)1ULL), &r129);
                                                                        if (o130) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 263, 49);
                                                                        uint64_t t131 = r129;
                                                                        if ((uintptr_t)t131 >= (uintptr_t)(t127)) gm_trap_index((uintptr_t)t131, t127, gm_srcvksed7nkdegg, 263, 47);
                                                                        uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)t126) + (uintptr_t)t131 * 1));
                                                                        uint8_t t133 = (!(t132 == ((uint8_t)92ULL)));
                                                                        gm_l27 = t133;
                                                                    }
                                                                    uint8_t t134 = gm_l27;
                                                                    if (!t134) {
                                                                        goto gm_lb17;
                                                                    }
                                                                    goto gm_lb13;
                                                                }
gm_lb17:;
                                                            }
gm_lb16:;
                                                        }
gm_lb15:;
                                                    }
                                                }
gm_lb13:;
                                                uint64_t t135 = gm_l10;
                                                gm_l6 = t135;
                                            }
                                        }
gm_lb8:;
                                    }
                                }
gm_lb6:;
                            }
                        }
gm_lb4:;
                    }
                }
gm_lb3:;
            }
        }
gm_lb1:;
    }
    {
        uint64_t t136 = gm_l8;
        gm_l11 = t136;
        {
            for (;;) {
                uint64_t t137 = gm_l11;
                uint8_t t138 = (t137 > ((uint64_t)0ULL));
                if (!t138) {
                    goto gm_lb18;
                }
                void * t139 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t141 = gm_l11;
                uint64_t r142; bool o143 = __builtin_sub_overflow(t141, ((uint64_t)1ULL), &r142);
                if (o143) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 269, 40);
                uint64_t t144 = r142;
                if ((uintptr_t)t144 >= (uintptr_t)(t140)) gm_trap_index((uintptr_t)t144, t140, gm_srcvksed7nkdegg, 269, 38);
                uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)t139) + (uintptr_t)t144 * 1));
                uint8_t t146 = gm_f_is_space_uen38t5(t145);
                if (!t146) {
                    goto gm_lb18;
                }
                void * t147 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t149 = gm_l11;
                uint64_t r150; bool o151 = __builtin_sub_overflow(t149, ((uint64_t)1ULL), &r150);
                if (o151) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 269, 56);
                uint64_t t152 = r150;
                if ((uintptr_t)t152 >= (uintptr_t)(t148)) gm_trap_index((uintptr_t)t152, t148, gm_srcvksed7nkdegg, 269, 54);
                uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)t147) + (uintptr_t)t152 * 1));
                uint8_t t154 = (!(t153 == ((uint8_t)10ULL)));
                if (!t154) {
                    goto gm_lb18;
                }
                {
                    uint64_t t155 = gm_l11;
                    uint64_t r156; bool o157 = __builtin_sub_overflow(t155, ((uint64_t)1ULL), &r156);
                    if (o157) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 269, 79);
                    uint64_t t158 = r156;
                    gm_l11 = t158;
                }
gm_lb20:;
            }
        }
gm_lb18:;
        uint64_t t159 = gm_l11;
        uint8_t t160 = ((t159 == ((uint64_t)0ULL)));
        if (t160) {
            gm_l28 = ((uint8_t)1);
        } else {
            void * t161 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t163 = gm_l11;
            uint64_t r164; bool o165 = __builtin_sub_overflow(t163, ((uint64_t)1ULL), &r164);
            if (o165) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 270, 47);
            uint64_t t166 = r164;
            if ((uintptr_t)t166 >= (uintptr_t)(t162)) gm_trap_index((uintptr_t)t166, t162, gm_srcvksed7nkdegg, 270, 45);
            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)t161) + (uintptr_t)t166 * 1));
            uint8_t t168 = gm_f_is_ident_cont_317fpdb(t167);
            uint8_t t169 = (uint8_t)!t168;
            gm_l28 = t169;
        }
        uint8_t t170 = gm_l28;
        if (t170) {
            gm_t9nnki79sf2e1 t171;
            memset(&t171, 0, sizeof t171);
            memmove(gm_out, ((uint8_t *)&t171), 48);
            goto gm_fdone;
        }
        uint64_t t172 = gm_l8;
        uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)1ULL), &r173);
        if (o174) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 272, 14);
        uint64_t t175 = r173;
        gm_l12 = t175;
        {
            {
                uint8_t t176 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                if (!t176) {
                    goto gm_lb22;
                }
                uint64_t t177 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                gm_l13 = t177;
                uint64_t t178 = gm_l13;
                uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                if (o180) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 273, 34);
                uint64_t t181 = r179;
                gm_l12 = t181;
                goto gm_lb21;
            }
gm_lb22:;
        }
gm_lb21:;
        gm_b16_8 t182;
        GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(0));
        gm_l14 = t182;
        {
            {
                uint64_t t183 = gm_l12;
                uint64_t t184 = gm_l3;
                uint8_t t185 = (t183 <= t184);
                if (!t185) {
                    goto gm_lb24;
                }
                gm_b16_8 t186 = gm_l0;
                uint64_t t187 = gm_l12;
                uint8_t t188 = gm_f_is_char_boundary_h62fhaa(t186, t187);
                if (!t188) {
                    goto gm_lb24;
                }
                gm_b16_8 t189 = gm_l0;
                uint64_t t190 = gm_l3;
                uint8_t t191 = gm_f_is_char_boundary_h62fhaa(t189, t190);
                if (!t191) {
                    goto gm_lb24;
                }
                void * t192 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t194 = gm_l12;
                uint64_t t195 = gm_l3;
                uint64_t t196 = (uintptr_t)t194;
                uint64_t t197 = (uintptr_t)t195;
                if (t196 > t197 || t197 > (uintptr_t)t193) gm_trap_slice(t196, t197, t193, gm_srcvksed7nkdegg, 275, 104);
                uint64_t t198 = ((uintptr_t)t195) - (uintptr_t)t194;
                gm_b16_8 t199;
                GM_ST(void *, (uint8_t *)&t199, (void *)(((uint8_t *)t192 + (uintptr_t)t194 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(t198));
                gm_b16_8 t200 = gm_f_trim_start_179crak(t199);
                gm_l14 = t200;
                goto gm_lb23;
            }
gm_lb24:;
        }
gm_lb23:;
    }
    {
        gm_th2lqbo77pvsc t201;
        memset(&t201, 0, sizeof t201);
        gm_l15 = t201;
        {
            {
                gm_b16_8 t202 = gm_l14;
                gm_b16_8 t203;
                GM_ST(void *, (uint8_t *)&t203, (void *)(gm_s39mljh669qgl));
                GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(1));
                gm_f_split_once_mqcu1ce((void *)&gm_l29, t202, t203);
                uint8_t t204 = (uint8_t)((((uint8_t *)&gm_l29))[32] != 0);
                if (!t204) {
                    goto gm_lb26;
                }
                memmove(((uint8_t *)&gm_l16), ((uint8_t *)&gm_l29), 32);
                gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                gm_b16_8 t206 = gm_f_trim_fdrfe6i(t205);
                gm_l17 = t206;
                uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t208 = (t207 > ((uint64_t)0ULL));
                gm_l18 = t208;
                gm_b16_8 t209 = gm_l17;
                gm_l19 = t209;
                gm_l20 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t210 = gm_l20;
                        uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                        uint8_t t212 = (t210 < t211);
                        if (!t212) {
                            goto gm_lb27;
                        }
                        {
                            void * t213 = GM_LD(void *, ((uint8_t *)&gm_l19));
                            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                            uint64_t t215 = gm_l20;
                            {
                                {
                                    uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)t213) + (uintptr_t)t215 * 1));
                                    gm_l21 = t216;
                                    uint8_t t217 = gm_l21;
                                    uint8_t t218 = gm_f_is_ident_cont_317fpdb(t217);
                                    uint8_t t219 = (uint8_t)!t218;
                                    if (t219) {
                                        gm_l18 = ((uint8_t)0);
                                    }
                                    goto gm_lb30;
                                }
gm_lb31:;
                                gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 280, 9);
                            }
gm_lb30:;
                        }
gm_lb29:;
                        uint64_t t220 = gm_l20;
                        uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                        uint64_t t223 = r221;
                        gm_l20 = t223;
                    }
                }
gm_lb27:;
                uint8_t t224 = gm_l18;
                if (t224) {
                    gm_b16_8 t225 = gm_l17;
                    gm_th2lqbo77pvsc t226;
                    GM_ST(uint8_t, (uint8_t *)&t226 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t226 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t226 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t226), t225);
                    ((uint8_t *)&t226)[16] = 1;
                    gm_l15 = t226;
                }
                goto gm_lb25;
            }
gm_lb26:;
        }
gm_lb25:;
        uint64_t t227 = gm_l11;
        uint64_t t228 = gm_l5;
        gm_th2lqbo77pvsc t229 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l15));
        gm_tvin65ttqbr1d t230;
        GM_ST(uint64_t, ((uint8_t *)&t230 + 0), t227);
        GM_ST(uint64_t, ((uint8_t *)&t230 + 8), t228);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t230 + 16), t229);
        gm_t9nnki79sf2e1 t231;
        GM_ST(uint8_t, (uint8_t *)&t231 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t231 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t231 + 44, 0);
        GM_ST(gm_tvin65ttqbr1d, ((uint8_t *)&t231), t230);
        ((uint8_t *)&t231)[40] = 1;
        memmove(gm_out, ((uint8_t *)&t231), 48);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) int64_t gm_f_completion_kind_flii2l4(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_completion_kind_flii2l4 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb2;
            }
            gm_l1 = ((int64_t)9LL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)13ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                goto gm_lb3;
            }
gm_lb4:;
            gm_l1 = ((int64_t)3LL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)2ULL));
                    if (!t33) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)12ULL));
                    if (!t35) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                goto gm_lb7;
            }
gm_lb8:;
            gm_l1 = ((int64_t)2LL);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)3ULL));
            if (!t37) {
                goto gm_lb11;
            }
            gm_l1 = ((int64_t)22LL);
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)4ULL));
            if (!t39) {
                goto gm_lb12;
            }
            gm_l1 = ((int64_t)13LL);
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)5ULL));
            if (!t41) {
                goto gm_lb13;
            }
            gm_l1 = ((int64_t)8LL);
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)6ULL));
            if (!t43) {
                goto gm_lb14;
            }
            gm_l1 = ((int64_t)7LL);
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)7ULL));
            if (!t45) {
                goto gm_lb15;
            }
            gm_l1 = ((int64_t)21LL);
            goto gm_lb1;
        }
gm_lb15:;
        {
            {
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)8ULL));
                    if (!t47) {
                        goto gm_lb18;
                    }
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)9ULL));
                    if (!t49) {
                        goto gm_lb19;
                    }
                    goto gm_lb17;
                }
gm_lb19:;
                goto gm_lb16;
            }
gm_lb17:;
            gm_l1 = ((int64_t)6LL);
            goto gm_lb1;
        }
gm_lb16:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)10ULL));
            if (!t51) {
                goto gm_lb20;
            }
            gm_l1 = ((int64_t)5LL);
            goto gm_lb1;
        }
gm_lb20:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)11ULL));
            if (!t53) {
                goto gm_lb21;
            }
            gm_l1 = ((int64_t)20LL);
            goto gm_lb1;
        }
gm_lb21:;
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)14ULL));
            if (!t55) {
                goto gm_lb22;
            }
            gm_l1 = ((int64_t)7LL);
            goto gm_lb1;
        }
gm_lb22:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)15ULL));
            if (!t57) {
                goto gm_lb23;
            }
            gm_l1 = ((int64_t)25LL);
            goto gm_lb1;
        }
gm_lb23:;
        {
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)16ULL));
            if (!t59) {
                goto gm_lb24;
            }
            gm_l1 = ((int64_t)14LL);
            goto gm_lb1;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 80, 46);
    }
gm_lb1:;
    int64_t t60 = gm_l1;
    F.ret = t60;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_definition_kaqo3c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_definition_kaqo3c6 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_tl874t2atnlh3 gm_l3; /* s */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_toceffb697p0u gm_l4;
    gm_tl874t2atnlh3 gm_l5;
    gm_t3ph3kbiqt1ah gm_l6;
    gm_t8vuock3jnv59 gm_l7;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 120);
    uint32_t t5 = gm_l1;
    uint32_t t6 = gm_l2;
    gm_toceffb697p0u t7 = gm_f_definition_at_9jj9uuu(t4, t5, t6);
    gm_l4 = t7;
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l4))[12] != 0);
    if (t8) {
        gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l4));
        gm_l5 = t9;
    } else {
        memmove(gm_out, ((uint8_t *)gm_cro0h2panct), 24);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t10 = gm_l5;
    gm_l3 = t10;
    void * t11 = gm_l0;
    gm_tl874t2atnlh3 t12 = gm_l3;
    gm_f_location_i7gnofg((void *)&gm_l6, (void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l6))[24] != 0);
    if (t13) {
        memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l6), 24);
    } else {
        memmove(((uint8_t *)&gm_l7), ((uint8_t *)gm_cro0h2panct), 24);
    }
    memmove(gm_out, ((uint8_t *)&gm_l7), 24);
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
inline __attribute__((aligned(4))) void gm_f_document_symbols_0nlnk2i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_document_symbols_0nlnk2i F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    gm_b16_8 gm_l2; /* syms */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l7;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 120);
    uint32_t t10 = gm_l1;
    gm_b16_8 t11 = gm_f_outline_of_01eujq2(t9, t10);
    gm_l2 = t11;
    gm_trqfvmjtt4sij t12;
    gm_f_list_0202sdh_4((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t12;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_l4 = t13;
    {
        for (;;) {
            uint64_t t14 = gm_l5;
            uint64_t t15 = gm_l4;
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t17 = gm_l5;
                        gm_l6 = t17;
                        void * t18 = (void *)((uint8_t *)&F.l3);
                        void * t19 = gm_l0;
                        void * t20 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t22 = gm_l6;
                        void * t23 = (void *)(((uint8_t *)t20) + (uintptr_t)t22 * 80);
                        gm_t8vuock3jnv59 t24;
                        gm_f_symbol_json_b065ihj((void *)&t24, (void *)NULL, cx, NULL, t19, t23);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t25 = gm_f_push_0pgacsj_4(t18, &t24);
                        gm_l7 = t25;
                        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcvksed7nkdegg, 113, 28);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 113, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t27 = gm_l5;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
            uint64_t t30 = r28;
            gm_l5 = t30;
        }
    }
gm_lb1:;
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
    gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t32);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_oucukrb(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_full_edit_82h286k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_full_edit_82h286k F;
    gm_b16_8 gm_l0; /* old */
    gm_b16_8 gm_l1; /* new */
    gm_t9qkh0rsooc8d gm_l2; /* enc */
    gm_t6j6smj7qoctu gm_l3; /* ix */
    gm_tq47bkpmd2b9a gm_l5; /* <temporary array> */
    GM_STACK_CHECK(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t2 = gm_l0;
    gm_t6j6smj7qoctu t3;
    gm_f_line_index_2necgtf((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t3;
    gm_tin97tt4ud0s1 t4;
    gm_f_obj_aoa7fl7((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t4;
    void * t5 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sl4ns6hobicht));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
    void * t7 = (void *)((uint8_t *)&gm_l3);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_t9qkh0rsooc8d t9 = gm_l2;
    gm_t8vuock3jnv59 t10;
    gm_f_range_json_hlsolbo((void *)&t10, (void *)NULL, cx, NULL, t7, ((uint64_t)0ULL), t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t11 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t5, t6, &t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sirpcjgkmlehr));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
    gm_b16_8 t14 = gm_l1;
    gm_t8vuock3jnv59 t15;
    gm_f_text_ls49dp6((void *)&t15, t14);
    gm_b0_1 t16 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t12, t13, &t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t17 = (void *)((uint8_t *)&F.l4);
    gm_t8vuock3jnv59 t18;
    gm_f_value_pkuqal3((void *)&t18, t17);
    gm_tq47bkpmd2b9a t19;
    GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t19 + 0), t18);
    gm_l5 = t19;
    void * t20 = (void *)((uint8_t *)&gm_l5);
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
    if (t21 > t22 || t22 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t21, t22, ((uint64_t)1ULL), gm_srcvksed7nkdegg, 389, 18);
    uint64_t t23 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t24);
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
inline __attribute__((aligned(4))) void gm_f_hover_ub02g3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_hover_ub02g3b F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    GM_STACK_CHECK(&F, 542u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_teijtv56moffm gm_l6;
    gm_t8scu1osgcra5 gm_l7;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 120);
    uint32_t t5 = gm_l1;
    uint32_t t6 = gm_l2;
    gm_f_hover_at_19okc4g((void *)&gm_l6, t4, t5, t6);
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l6))[56] != 0);
    if (t7) {
        memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l6), 56);
    } else {
        memmove(gm_out, ((uint8_t *)gm_cro0h2panct), 24);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l7), 56);
    gm_tin97tt4ud0s1 t8;
    gm_f_obj_aoa7fl7((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t8;
    void * t9 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s7j2u3ofu66j8));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sm06nsg0msdgd));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(8));
    gm_t8vuock3jnv59 t12;
    gm_f_text_ls49dp6((void *)&t12, t11);
    gm_b0_1 t13 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t9, t10, &t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t14 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s5qkai093c1q0));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(5));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    gm_b16_8 t17 = gm_f_markdown_posoa59((void *)NULL, cx, NULL, t16, &(*(gm_th2lqbo77pvsc *)((((uint8_t *)&F.l3) + 16) + 16)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t8vuock3jnv59 t18;
    gm_f_text_ls49dp6((void *)&t18, t17);
    gm_b0_1 t19 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t14, t15, &t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t20;
    gm_f_obj_aoa7fl7((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t20;
    void * t21 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_srplcc0p49iui));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(8));
    void * t23 = (void *)((uint8_t *)&F.l4);
    gm_t8vuock3jnv59 t24;
    gm_f_value_pkuqal3((void *)&t24, t23);
    gm_b0_1 t25 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t21, t22, &t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t26 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sl4ns6hobicht));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(5));
    void * t28 = gm_l0;
    void * t29 = GM_LD(void *, ((uint8_t *)t28));
    gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&F.l3));
    void * t31 = gm_l0;
    gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t31) + 8));
    gm_t8vuock3jnv59 t33;
    gm_f_range_demc0bt((void *)&t33, (void *)NULL, cx, NULL, t29, t30, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t34 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t26, t27, &t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t35 = (void *)((uint8_t *)&F.l5);
    gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t35);
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
__attribute__((aligned(4))) void gm_f_item_json_ml3fbup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_item_json_ml3fbup F;
    void * gm_l0; /* i */
    gm_th2lqbo77pvsc gm_l1; /* sort */
    gm_b16_8 gm_l3; /* d */
    gm_b16_8 gm_l5; /* s */
    GM_STACK_CHECK(&F, 952u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tin97tt4ud0s1 t6;
    gm_f_obj_aoa7fl7((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t6;
    void * t7 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_su137hja0ebcd));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
    void * t9 = gm_l0;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t9));
    gm_f_dupe_g9iqpvn((void *)&gm_l6, (void *)NULL, cx, NULL, t10, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klfj89orh68)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcvksed7nkdegg, 119, 25);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
    gm_t8vuock3jnv59 t14;
    gm_f_text_ls49dp6((void *)&t14, t13);
    gm_b0_1 t15 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t7, t8, &t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t16 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s7j2u3ofu66j8));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
    void * t18 = gm_l0;
    gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t18) + 16));
    int64_t t20 = gm_f_completion_kind_flii2l4(t19);
    gm_t8vuock3jnv59 t21;
    gm_f_int_95caidg((void *)&t21, (void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t22 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t16, t17, &t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t23) + 24) + 8));
    uint8_t t25 = (t24 > ((uint64_t)0ULL));
    if (t25) {
        void * t26 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sb6smklmjl60o));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(6));
        void * t28 = gm_l0;
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 24));
        gm_f_dupe_g9iqpvn((void *)&gm_l7, (void *)NULL, cx, NULL, t29, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8nmk7je8im)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcvksed7nkdegg, 121, 48);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_t8vuock3jnv59 t33;
        gm_f_text_ls49dp6((void *)&t33, t32);
        gm_b0_1 t34 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t26, t27, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t35 = gm_l0;
            uint8_t t36 = (uint8_t)(((((uint8_t *)t35) + 40))[16] != 0);
            if (!t36) {
                goto gm_lb2;
            }
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 40));
            gm_l3 = t37;
            gm_tin97tt4ud0s1 t38;
            gm_f_obj_aoa7fl7((void *)&t38, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l4 = t38;
            void * t39 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s7j2u3ofu66j8));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(4));
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sm06nsg0msdgd));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(8));
            gm_t8vuock3jnv59 t42;
            gm_f_text_ls49dp6((void *)&t42, t41);
            gm_b0_1 t43 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t39, t40, &t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t44 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s5qkai093c1q0));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(5));
            gm_b16_8 t46 = gm_l3;
            gm_f_dupe_g9iqpvn((void *)&gm_l8, (void *)NULL, cx, NULL, t46, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kn81j57eee2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcvksed7nkdegg, 125, 29);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_t8vuock3jnv59 t50;
            gm_f_text_ls49dp6((void *)&t50, t49);
            gm_b0_1 t51 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t44, t45, &t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t52 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s7unnudd0kkri));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(13));
            void * t54 = (void *)((uint8_t *)&F.l4);
            gm_t8vuock3jnv59 t55;
            gm_f_value_pkuqal3((void *)&t55, t54);
            gm_b0_1 t56 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t52, t53, &t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
            if (!t57) {
                goto gm_lb4;
            }
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_l5 = t58;
            void * t59 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s54psrmgl6ti7));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(8));
            gm_b16_8 t61 = gm_l5;
            void * t62 = gm_l0;
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)t62));
            gm_t6j6smj7qoctu t64;
            GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t61);
            GM_ST(gm_b16_8, ((uint8_t *)&t64 + 16), t63);
            gm_f_format_8n626pi((void *)&gm_l9, (void *)NULL, cx, NULL, &t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcvksed7nkdegg, 128, 43);
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
            gm_t8vuock3jnv59 t68;
            gm_f_text_ls49dp6((void *)&t68, t67);
            gm_b0_1 t69 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t59, t60, &t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t70 = (void *)((uint8_t *)&F.l2);
    gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t70);
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
inline __attribute__((aligned(4))) void gm_f_location_i7gnofg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_location_i7gnofg F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* path */
    GM_STACK_CHECK(&F, 446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l4;
    gm_b16_8 gm_l5;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_f_open_path_6gmq4nk((void *)&gm_l4, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
    if (t5) {
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l5 = t6;
    } else {
        gm_t3ph3kbiqt1ah t7;
        memset(&t7, 0, sizeof t7);
        memmove(gm_out, ((uint8_t *)&t7), 32);
        goto gm_fdone;
    }
    gm_b16_8 t8 = gm_l5;
    gm_l2 = t8;
    gm_tin97tt4ud0s1 t9;
    gm_f_obj_aoa7fl7((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t9;
    void * t10 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sbo52miqad506));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 16));
    gm_b16_8 t14 = gm_l2;
    gm_b16_8 t15 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t8vuock3jnv59 t16;
    gm_f_text_ls49dp6((void *)&t16, t15);
    gm_b0_1 t17 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t10, t11, &t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t18 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sl4ns6hobicht));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(5));
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    gm_tl874t2atnlh3 t22 = gm_l1;
    void * t23 = gm_l0;
    gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t23) + 8));
    gm_t8vuock3jnv59 t25;
    gm_f_range_demc0bt((void *)&t25, (void *)NULL, cx, NULL, t21, t22, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t26 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t18, t19, &t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t27 = (void *)((uint8_t *)&F.l3);
    gm_t8vuock3jnv59 t28;
    gm_f_value_pkuqal3((void *)&t28, t27);
    gm_t3ph3kbiqt1ah t29;
    GM_ST(uint8_t, (uint8_t *)&t29 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t29 + 28, 0);
    GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t29), t28);
    ((uint8_t *)&t29)[24] = 1;
    memmove(gm_out, ((uint8_t *)&t29), 32);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_markdown_posoa59(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_markdown_posoa59 F;
    gm_b16_8 gm_l0; /* code */
    gm_th2lqbo77pvsc gm_l1; /* doc_text */
    gm_b16_8 gm_l2; /* fence */
    gm_b16_8 gm_l4; /* d */
    GM_STACK_CHECK(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_b16_8 t4 = gm_l0;
    gm_b16_8 t5 = gm_f_fence_hq2pe32((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t5;
    gm_trqfvmjtt4sij t6;
    gm_f_list_uvfu1k1((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t6;
    void * t7 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t8 = gm_l2;
    gm_b16_8 t9 = gm_l0;
    gm_b16_8 t10 = gm_l2;
    gm_tfeab279c16rr t11;
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t9);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 32), t10);
    gm_tftb1odc739t5 t12 = gm_f_write_8qoqt62(t7, &t11);
    gm_l5 = t12;
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcvksed7nkdegg, 26, 5);
    {
        {
            uint8_t t14 = (uint8_t)((((uint8_t *)a1))[16] != 0);
            if (!t14) {
                goto gm_lb2;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_l4 = t15;
            void * t16 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t18 = gm_f_extend_0cs85gs(t16, t17);
            gm_l6 = t18;
            uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcvksed7nkdegg, 28, 9);
            void * t20 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t21 = gm_l4;
            gm_tftb1odc739t5 t22 = gm_f_extend_0cs85gs(t20, t21);
            gm_l7 = t22;
            uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcvksed7nkdegg, 29, 9);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
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
__attribute__((aligned(4))) void gm_f_member_context_p58kq5l(void *gm_out, gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_context_p58kq5l F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* off */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* p */
    uint64_t gm_l4; /* dot */
    uint64_t gm_l5; /* i */
    uint8_t gm_l6; /* c */
    uint8_t gm_l7; /* open */
    uint8_t gm_l8; /* close */
    int64_t gm_l9; /* depth */
    uint64_t gm_l10; /* j */
    GM_STACK_CHECK(&F, 726u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_b16_8 t14 = gm_l0;
    gm_l2 = t14;
    uint64_t t15 = gm_l1;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t17 = (t15 < t16 ? t15 : t16);
    gm_l3 = t17;
    {
        for (;;) {
            uint64_t t18 = gm_l3;
            uint8_t t19 = (t18 > ((uint64_t)0ULL));
            if (!t19) {
                goto gm_lb1;
            }
            void * t20 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t22 = gm_l3;
            uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
            if (o24) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 188, 44);
            uint64_t t25 = r23;
            if ((uintptr_t)t25 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t25, t21, gm_srcvksed7nkdegg, 188, 42);
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t20) + (uintptr_t)t25 * 1));
            uint8_t t27 = gm_f_is_ident_cont_317fpdb(t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                uint64_t t28 = gm_l3;
                uint64_t r29; bool o30 = __builtin_sub_overflow(t28, ((uint64_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 188, 54);
                uint64_t t31 = r29;
                gm_l3 = t31;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t32 = gm_l3;
    uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
    if (t33) {
        gm_l12 = ((uint8_t)1);
    } else {
        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t36 = gm_l3;
        uint64_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint64_t)1ULL), &r37);
        if (o38) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 189, 20);
        uint64_t t39 = r37;
        if ((uintptr_t)t39 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t39, t35, gm_srcvksed7nkdegg, 189, 18);
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t34) + (uintptr_t)t39 * 1));
        uint8_t t41 = (!(t40 == ((uint8_t)46ULL)));
        gm_l12 = t41;
    }
    uint8_t t42 = gm_l12;
    if (t42) {
        gm_l11 = ((uint8_t)1);
    } else {
        uint64_t t43 = gm_l3;
        uint8_t t44 = (t43 >= ((uint64_t)2ULL));
        if (t44) {
            void * t45 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t47 = gm_l3;
            uint64_t r48; bool o49 = __builtin_sub_overflow(t47, ((uint64_t)2ULL), &r48);
            if (o49) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 189, 55);
            uint64_t t50 = r48;
            if ((uintptr_t)t50 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t50, t46, gm_srcvksed7nkdegg, 189, 53);
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t45) + (uintptr_t)t50 * 1));
            uint8_t t52 = ((t51 == ((uint8_t)46ULL)));
            gm_l13 = t52;
        } else {
            gm_l13 = ((uint8_t)0);
        }
        uint8_t t53 = gm_l13;
        gm_l11 = t53;
    }
    uint8_t t54 = gm_l11;
    if (t54) {
        gm_tc70a9dbbmgkg t55;
        memset(&t55, 0, sizeof t55);
        memmove(gm_out, ((uint8_t *)&t55), 24);
        goto gm_fdone;
    }
    uint64_t t56 = gm_l3;
    uint64_t r57; bool o58 = __builtin_sub_overflow(t56, ((uint64_t)1ULL), &r57);
    if (o58) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 190, 12);
    uint64_t t59 = r57;
    gm_l4 = t59;
    uint64_t t60 = gm_l4;
    gm_l5 = t60;
    {
        for (;;) {
            uint64_t t61 = gm_l5;
            uint8_t t62 = (t61 > ((uint64_t)0ULL));
            if (!t62) {
                goto gm_lb4;
            }
            {
                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t65 = gm_l5;
                uint64_t r66; bool o67 = __builtin_sub_overflow(t65, ((uint64_t)1ULL), &r66);
                if (o67) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 193, 16);
                uint64_t t68 = r66;
                if ((uintptr_t)t68 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t68, t64, gm_srcvksed7nkdegg, 193, 14);
                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t63) + (uintptr_t)t68 * 1));
                gm_l6 = t69;
                uint8_t t70 = gm_l6;
                uint8_t t71 = gm_f_is_ident_cont_317fpdb(t70);
                if (t71) {
                    gm_l15 = ((uint8_t)1);
                } else {
                    uint8_t t72 = gm_l6;
                    uint8_t t73 = ((t72 == ((uint8_t)46ULL)));
                    gm_l15 = t73;
                }
                uint8_t t74 = gm_l15;
                if (t74) {
                    gm_l14 = ((uint8_t)1);
                } else {
                    uint8_t t75 = gm_l6;
                    uint8_t t76 = ((t75 == ((uint8_t)94ULL)));
                    gm_l14 = t76;
                }
                uint8_t t77 = gm_l14;
                if (t77) {
                    uint64_t t78 = gm_l5;
                    uint64_t r79; bool o80 = __builtin_sub_overflow(t78, ((uint64_t)1ULL), &r79);
                    if (o80) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 195, 13);
                    uint64_t t81 = r79;
                    gm_l5 = t81;
                } else {
                    uint8_t t82 = gm_l6;
                    uint8_t t83 = ((t82 == ((uint8_t)41ULL)));
                    if (t83) {
                        gm_l16 = ((uint8_t)1);
                    } else {
                        uint8_t t84 = gm_l6;
                        uint8_t t85 = ((t84 == ((uint8_t)93ULL)));
                        gm_l16 = t85;
                    }
                    uint8_t t86 = gm_l16;
                    if (t86) {
                        uint8_t t87 = gm_l6;
                        uint8_t t88 = ((t87 == ((uint8_t)41ULL)));
                        if (t88) {
                            gm_l17 = ((uint8_t)40ULL);
                        } else {
                            gm_l17 = ((uint8_t)91ULL);
                        }
                        uint8_t t89 = gm_l17;
                        gm_l7 = t89;
                        uint8_t t90 = gm_l6;
                        gm_l8 = t90;
                        gm_l9 = ((int64_t)0LL);
                        uint64_t t91 = gm_l5;
                        gm_l10 = t91;
                        {
                            for (;;) {
                                uint64_t t92 = gm_l10;
                                uint8_t t93 = (t92 > ((uint64_t)0ULL));
                                if (!t93) {
                                    goto gm_lb7;
                                }
                                {
                                    uint64_t t94 = gm_l10;
                                    uint64_t r95; bool o96 = __builtin_sub_overflow(t94, ((uint64_t)1ULL), &r95);
                                    if (o96) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 202, 17);
                                    uint64_t t97 = r95;
                                    gm_l10 = t97;
                                    void * t98 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                    uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                    uint64_t t100 = gm_l10;
                                    if ((uintptr_t)t100 >= (uintptr_t)(t99)) gm_trap_index((uintptr_t)t100, t99, gm_srcvksed7nkdegg, 203, 20);
                                    uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)t98) + (uintptr_t)t100 * 1));
                                    uint8_t t102 = gm_l8;
                                    uint8_t t103 = ((t101 == t102));
                                    if (t103) {
                                        int64_t t104 = gm_l9;
                                        int64_t r105; bool o106 = __builtin_add_overflow(t104, ((int64_t)1LL), &r105);
                                        if (o106) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 204, 21);
                                        int64_t t107 = r105;
                                        gm_l9 = t107;
                                    } else {
                                        void * t108 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                        uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                        uint64_t t110 = gm_l10;
                                        if ((uintptr_t)t110 >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)t110, t109, gm_srcvksed7nkdegg, 205, 27);
                                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)t110 * 1));
                                        uint8_t t112 = gm_l7;
                                        uint8_t t113 = ((t111 == t112));
                                        if (t113) {
                                            int64_t t114 = gm_l9;
                                            int64_t r115; bool o116 = __builtin_sub_overflow(t114, ((int64_t)1LL), &r115);
                                            if (o116) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 206, 21);
                                            int64_t t117 = r115;
                                            gm_l9 = t117;
                                            int64_t t118 = gm_l9;
                                            uint8_t t119 = ((t118 == ((int64_t)0LL)));
                                            if (t119) {
                                                goto gm_lb7;
                                            }
                                        }
                                    }
                                }
gm_lb9:;
                            }
                        }
gm_lb7:;
                        int64_t t120 = gm_l9;
                        uint8_t t121 = (!(t120 == ((int64_t)0LL)));
                        if (t121) {
                            gm_tc70a9dbbmgkg t122;
                            memset(&t122, 0, sizeof t122);
                            memmove(gm_out, ((uint8_t *)&t122), 24);
                            goto gm_fdone;
                        }
                        uint64_t t123 = gm_l10;
                        gm_l5 = t123;
                    } else {
                        goto gm_lb4;
                    }
                }
            }
gm_lb6:;
        }
    }
gm_lb4:;
    {
        for (;;) {
            uint64_t t124 = gm_l5;
            uint64_t t125 = gm_l4;
            uint8_t t126 = (t124 < t125);
            if (!t126) {
                goto gm_lb10;
            }
            void * t127 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t129 = gm_l5;
            if ((uintptr_t)t129 >= (uintptr_t)(t128)) gm_trap_index((uintptr_t)t129, t128, gm_srcvksed7nkdegg, 217, 23);
            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)t127) + (uintptr_t)t129 * 1));
            uint8_t t131 = ((t130 == ((uint8_t)46ULL)));
            if (!t131) {
                goto gm_lb10;
            }
            {
                uint64_t t132 = gm_l5;
                uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                if (o134) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 217, 37);
                uint64_t t135 = r133;
                gm_l5 = t135;
            }
gm_lb12:;
        }
    }
gm_lb10:;
    uint64_t t136 = gm_l5;
    uint64_t t137 = gm_l4;
    uint8_t t138 = (t136 < t137);
    if (t138) {
        uint64_t t139 = gm_l5;
        uint64_t t140 = gm_l4;
        gm_t2lqu8jsj511d t141;
        GM_ST(uint64_t, ((uint8_t *)&t141 + 0), t139);
        GM_ST(uint64_t, ((uint8_t *)&t141 + 8), t140);
        gm_tc70a9dbbmgkg t142;
        GM_ST(uint8_t, (uint8_t *)&t142 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t142 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t142 + 20, 0);
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t142), t141);
        ((uint8_t *)&t142)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t142), 24);
        goto gm_fdone;
    }
    gm_tc70a9dbbmgkg t143;
    memset(&t143, 0, sizeof t143);
    memmove(gm_out, ((uint8_t *)&t143), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tc70a9dbbmgkg gm_f_member_context_p58kq5l_c(gm_b16_8 a0, uint64_t a1) { gm_tc70a9dbbmgkg gm_r; gm_f_member_context_p58kq5l(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_members_8m2g0sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    struct gm_fr_members_8m2g0sl F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* lo */
    uint32_t gm_l3; /* hi */
    void * gm_l4; /* ix */
    gm_th2lqbo77pvsc gm_l5; /* found */
    gm_b16_8 gm_l6; /* t */
    gm_b16_8 gm_l7; /* inner */
    gm_b16_8 gm_l8; /* items */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 852u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l13;
    gm_b16_8 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 120);
    gm_l4 = t16;
    void * t17 = gm_l4;
    uint32_t t18 = gm_l1;
    uint32_t t19 = gm_l2;
    uint32_t t20 = gm_l3;
    gm_f_members_at_do67lp0((void *)&gm_l5, t17, t18, t19, t20);
    {
        {
            uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (t21) {
                goto gm_lb2;
            }
            gm_l13 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l13 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 169, 8);
    }
gm_lb1:;
    uint8_t t22 = gm_l13;
    if (t22) {
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, ((uint8_t *)t23));
        uint32_t t25 = gm_l1;
        gm_b16_8 t26 = gm_f_source_text_7ru912k(t24, t25);
        gm_l6 = t26;
        {
            {
                uint32_t t27 = gm_l2;
                uint64_t t28 = (uint64_t)t27;
                uint32_t t29 = gm_l3;
                uint64_t t30 = (uint64_t)t29;
                uint8_t t31 = (t28 <= t30);
                if (!t31) {
                    goto gm_lb5;
                }
                uint32_t t32 = gm_l3;
                uint64_t t33 = (uint64_t)t32;
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint8_t t35 = (t33 <= t34);
                if (!t35) {
                    goto gm_lb5;
                }
                gm_b16_8 t36 = gm_l6;
                uint32_t t37 = gm_l2;
                uint64_t t38 = (uint64_t)t37;
                uint8_t t39 = gm_f_is_char_boundary_h62fhaa(t36, t38);
                if (!t39) {
                    goto gm_lb5;
                }
                gm_b16_8 t40 = gm_l6;
                uint32_t t41 = gm_l3;
                uint64_t t42 = (uint64_t)t41;
                uint8_t t43 = gm_f_is_char_boundary_h62fhaa(t40, t42);
                if (!t43) {
                    goto gm_lb5;
                }
                void * t44 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint32_t t46 = gm_l2;
                uint64_t t47 = (uint64_t)t46;
                uint32_t t48 = gm_l3;
                uint64_t t49 = (uint64_t)t48;
                uint64_t t50 = (uintptr_t)t47;
                uint64_t t51 = (uintptr_t)t49;
                if (t50 > t51 || t51 > (uintptr_t)t45) gm_trap_slice(t50, t51, t45, gm_srcvksed7nkdegg, 173, 22);
                uint64_t t52 = ((uintptr_t)t49) - (uintptr_t)t47;
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t44 + (uintptr_t)t47 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52));
                gm_l7 = t53;
                {
                    {
                        gm_b16_8 t54 = gm_l7;
                        gm_b16_8 t55;
                        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_smooaouqmm8dj));
                        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(1));
                        uint8_t t56 = gm_f_starts_with_6vvt01e(t54, t55);
                        if (!t56) {
                            goto gm_lb7;
                        }
                        gm_b16_8 t57 = gm_l7;
                        gm_b16_8 t58;
                        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s1asl1e1tvb0k));
                        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(1));
                        uint8_t t59 = gm_f_ends_with_biriu5l(t57, t58);
                        if (!t59) {
                            goto gm_lb7;
                        }
                        void * t60 = gm_l4;
                        uint32_t t61 = gm_l1;
                        uint32_t t62 = gm_l2;
                        uint32_t r63; bool o64 = __builtin_add_overflow(t62, ((uint32_t)1ULL), &r63);
                        if (o64) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 174, 94);
                        uint32_t t65 = r63;
                        uint32_t t66 = gm_l3;
                        uint32_t r67; bool o68 = __builtin_sub_overflow(t66, ((uint32_t)1ULL), &r67);
                        if (o68) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 174, 102);
                        uint32_t t69 = r67;
                        gm_f_members_at_do67lp0((void *)&gm_l5, t60, t61, t65, t69);
                        goto gm_lb6;
                    }
gm_lb7:;
                }
gm_lb6:;
                goto gm_lb4;
            }
gm_lb5:;
        }
gm_lb4:;
    }
    uint8_t t70 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
    if (t70) {
        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
        gm_l14 = t71;
    } else {
        gm_tqetrgo0q24b5 t72;
        memset(&t72, 0, sizeof t72);
        memmove(gm_out, ((uint8_t *)&t72), 48);
        goto gm_fdone;
    }
    gm_b16_8 t73 = gm_l14;
    gm_l8 = t73;
    gm_trqfvmjtt4sij t74;
    gm_f_list_0202sdh_4((void *)&t74, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t74;
    gm_l11 = ((uint64_t)0ULL);
    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
    gm_l10 = t75;
    {
        for (;;) {
            uint64_t t76 = gm_l11;
            uint64_t t77 = gm_l10;
            uint8_t t78 = (t76 < t77);
            if (!t78) {
                goto gm_lb8;
            }
            {
                {
                    {
                        uint64_t t79 = gm_l11;
                        gm_l12 = t79;
                        void * t80 = (void *)((uint8_t *)&F.l9);
                        void * t81 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t83 = gm_l12;
                        void * t84 = (void *)(((uint8_t *)t81) + (uintptr_t)t83 * 64);
                        gm_th2lqbo77pvsc t85;
                        memset(&t85, 0, sizeof t85);
                        gm_t8vuock3jnv59 t86;
                        gm_f_item_json_ml3fbup((void *)&t86, (void *)NULL, cx, NULL, t84, &t85);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t87 = gm_f_push_0pgacsj_4(t80, &t86);
                        gm_l15 = t87;
                        uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcvksed7nkdegg, 179, 29);
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 179, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t89 = gm_l11;
            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
            uint64_t t92 = r90;
            gm_l11 = t92;
        }
    }
gm_lb8:;
    gm_trqfvmjtt4sij t93 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l9));
    gm_tqetrgo0q24b5 t94;
    GM_ST(uint8_t, (uint8_t *)&t94 + 41, 0);
    GM_ST(uint16_t, (uint8_t *)&t94 + 42, 0);
    GM_ST(uint32_t, (uint8_t *)&t94 + 44, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t94), t93);
    ((uint8_t *)&t94)[40] = 1;
    memmove(gm_out, ((uint8_t *)&t94), 48);
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
__attribute__((aligned(4))) void gm_f_names_ms1jmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_names_ms1jmv4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    void * gm_l3; /* ix */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* l */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* it */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* it */
    GM_STACK_CHECK(&F, 1047u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    void * t19 = (void *)(((uint8_t *)t18) + 120);
    gm_l3 = t19;
    gm_t4c7ipjj9s3fp t20;
    gm_f_hash_map_cuh4fs9((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t20;
    gm_trqfvmjtt4sij t21;
    gm_f_list_0202sdh_4((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t21;
    void * t22 = gm_l3;
    uint32_t t23 = gm_l1;
    uint32_t t24 = gm_l2;
    gm_f_locals_at_2n5h0d6((void *)&F.l6, (void *)NULL, cx, NULL, t22, t23, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_closure t26 = ((gm_closure){GM_PTR_TAG(gm_f_fn_oucukrb), NULL, cx});
    gm_f_stable_sort_by_qutfkbd(t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = ((uint64_t)0ULL);
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
    gm_l7 = t27;
    {
        for (;;) {
            uint64_t t28 = gm_l8;
            uint64_t t29 = gm_l7;
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t31 = gm_l8;
                        gm_l9 = t31;
                        void * t32 = GM_LD(void *, ((uint8_t *)&F.l6));
                        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                        uint64_t t34 = gm_l9;
                        void * t35 = (void *)(((uint8_t *)t32) + (uintptr_t)t34 * 88);
                        gm_l10 = t35;
                        void * t36 = (void *)((uint8_t *)&F.l4);
                        void * t37 = gm_l10;
                        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)t37));
                        uint8_t t39 = gm_f_contains_57n7jqd(t36, t38);
                        uint8_t t40 = (uint8_t)!t39;
                        if (t40) {
                            void * t41 = (void *)((uint8_t *)&F.l4);
                            void * t42 = gm_l10;
                            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)t42));
                            gm_tftb1odc739t5 t44 = gm_f_put_5c6qgui(t41, t43, ((uint8_t)1));
                            gm_l19 = t44;
                            uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcvksed7nkdegg, 144, 13);
                            void * t46 = (void *)((uint8_t *)&F.l5);
                            void * t47 = gm_l10;
                            void * t48 = (void *)((uint8_t *)t47);
                            gm_b16_8 t49;
                            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sicgmpca63jln));
                            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
                            gm_th2lqbo77pvsc t50;
                            GM_ST(uint8_t, (uint8_t *)&t50 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t50 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t50 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t50), t49);
                            ((uint8_t *)&t50)[16] = 1;
                            gm_t8vuock3jnv59 t51;
                            gm_f_item_json_ml3fbup((void *)&t51, (void *)NULL, cx, NULL, t48, &t50);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t52 = gm_f_push_0pgacsj_4(t46, &t51);
                            gm_l20 = t52;
                            uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcvksed7nkdegg, 145, 13);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 141, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t54 = gm_l8;
            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
            uint64_t t57 = r55;
            gm_l8 = t57;
        }
    }
gm_lb1:;
    gm_l12 = ((uint64_t)0ULL);
    void * t58 = gm_l3;
    uint64_t t59 = GM_LD(uint64_t, ((((uint8_t *)t58) + 328) + 8));
    gm_l11 = t59;
    {
        for (;;) {
            uint64_t t60 = gm_l12;
            uint64_t t61 = gm_l11;
            uint8_t t62 = (t60 < t61);
            if (!t62) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t63 = gm_l12;
                        gm_l13 = t63;
                        void * t64 = gm_l3;
                        void * t65 = GM_LD(void *, (((uint8_t *)t64) + 328));
                        uint64_t t66 = GM_LD(uint64_t, ((((uint8_t *)t64) + 328) + 8));
                        uint64_t t67 = gm_l13;
                        if ((uintptr_t)t67 >= (uintptr_t)(t66)) gm_trap_index((uintptr_t)t67, t66, gm_srcvksed7nkdegg, 149, 16);
                        void * t68 = (void *)(((uint8_t *)t65) + (uintptr_t)t67 * 64);
                        gm_l14 = t68;
                        void * t69 = (void *)((uint8_t *)&F.l4);
                        void * t70 = gm_l14;
                        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)t70));
                        uint8_t t72 = gm_f_contains_57n7jqd(t69, t71);
                        uint8_t t73 = (uint8_t)!t72;
                        if (t73) {
                            void * t74 = (void *)((uint8_t *)&F.l4);
                            void * t75 = gm_l14;
                            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)t75));
                            gm_tftb1odc739t5 t77 = gm_f_put_5c6qgui(t74, t76, ((uint8_t)1));
                            gm_l21 = t77;
                            uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                            if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcvksed7nkdegg, 151, 13);
                            void * t79 = (void *)((uint8_t *)&F.l5);
                            void * t80 = gm_l14;
                            gm_b16_8 t81;
                            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_svubtddeidu1d));
                            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(1));
                            gm_th2lqbo77pvsc t82;
                            GM_ST(uint8_t, (uint8_t *)&t82 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t82 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t82 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t82), t81);
                            ((uint8_t *)&t82)[16] = 1;
                            gm_t8vuock3jnv59 t83;
                            gm_f_item_json_ml3fbup((void *)&t83, (void *)NULL, cx, NULL, t80, &t82);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t84 = gm_f_push_0pgacsj_4(t79, &t83);
                            gm_l22 = t84;
                            uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                            if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcvksed7nkdegg, 152, 13);
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 148, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t86 = gm_l12;
            uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
            uint64_t t89 = r87;
            gm_l12 = t89;
        }
    }
gm_lb6:;
    gm_l16 = ((uint64_t)0ULL);
    void * t90 = gm_l3;
    uint64_t t91 = GM_LD(uint64_t, ((((uint8_t *)t90) + 344) + 8));
    gm_l15 = t91;
    {
        for (;;) {
            uint64_t t92 = gm_l16;
            uint64_t t93 = gm_l15;
            uint8_t t94 = (t92 < t93);
            if (!t94) {
                goto gm_lb11;
            }
            {
                {
                    {
                        uint64_t t95 = gm_l16;
                        gm_l17 = t95;
                        void * t96 = gm_l3;
                        void * t97 = GM_LD(void *, (((uint8_t *)t96) + 344));
                        uint64_t t98 = GM_LD(uint64_t, ((((uint8_t *)t96) + 344) + 8));
                        uint64_t t99 = gm_l17;
                        if ((uintptr_t)t99 >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)t99, t98, gm_srcvksed7nkdegg, 156, 16);
                        void * t100 = (void *)(((uint8_t *)t97) + (uintptr_t)t99 * 64);
                        gm_l18 = t100;
                        void * t101 = (void *)((uint8_t *)&F.l4);
                        void * t102 = gm_l18;
                        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)t102));
                        uint8_t t104 = gm_f_contains_57n7jqd(t101, t103);
                        uint8_t t105 = (uint8_t)!t104;
                        if (t105) {
                            void * t106 = (void *)((uint8_t *)&F.l4);
                            void * t107 = gm_l18;
                            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)t107));
                            gm_tftb1odc739t5 t109 = gm_f_put_5c6qgui(t106, t108, ((uint8_t)1));
                            gm_l23 = t109;
                            uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                            if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcvksed7nkdegg, 158, 13);
                            void * t111 = (void *)((uint8_t *)&F.l5);
                            void * t112 = gm_l18;
                            gm_b16_8 t113;
                            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_s2dssfe3261t2));
                            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(1));
                            gm_th2lqbo77pvsc t114;
                            GM_ST(uint8_t, (uint8_t *)&t114 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t114 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t114 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t114), t113);
                            ((uint8_t *)&t114)[16] = 1;
                            gm_t8vuock3jnv59 t115;
                            gm_f_item_json_ml3fbup((void *)&t115, (void *)NULL, cx, NULL, t112, &t114);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t116 = gm_f_push_0pgacsj_4(t111, &t115);
                            gm_l24 = t116;
                            uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                            if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcvksed7nkdegg, 159, 13);
                        }
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 155, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t118 = gm_l16;
            uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
            uint64_t t121 = r119;
            gm_l16 = t121;
        }
    }
gm_lb11:;
    memmove(gm_out, ((uint8_t *)&F.l5), 40);
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
extern __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
inline __attribute__((aligned(4))) void gm_f_references_pd7nt1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3) {
    struct gm_fr_references_pd7nt1t F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    uint8_t gm_l3; /* include_declaration */
    gm_tl874t2atnlh3 gm_l4; /* def */
    gm_t8vuock3jnv59 gm_l7; /* l */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_tl874t2atnlh3 gm_l10; /* s */
    gm_t8vuock3jnv59 gm_l11; /* l */
    GM_STACK_CHECK(&F, 581u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_toceffb697p0u gm_l12;
    gm_tl874t2atnlh3 gm_l13;
    gm_t3ph3kbiqt1ah gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_t3ph3kbiqt1ah gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = (void *)(((uint8_t *)t14) + 120);
    uint32_t t16 = gm_l1;
    uint32_t t17 = gm_l2;
    gm_toceffb697p0u t18 = gm_f_definition_at_9jj9uuu(t15, t16, t17);
    gm_l12 = t18;
    uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l12))[12] != 0);
    if (t19) {
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l12));
        gm_l13 = t20;
    } else {
        gm_b16_8 t21 = gm_f_empty_slice_dheabhi_4();
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t23 = gm_l13;
    gm_l4 = t23;
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    void * t26 = (void *)(((uint8_t *)t25) + 120);
    gm_tl874t2atnlh3 t27 = gm_l4;
    gm_f_uses_of_nalefva((void *)&F.l5, (void *)NULL, cx, NULL, t26, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t28;
    gm_f_list_0202sdh_4((void *)&t28, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t28;
    {
        {
            uint8_t t29 = gm_l3;
            if (!t29) {
                goto gm_lb2;
            }
            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
            uint8_t t32 = (t30 > t31);
            if (!t32) {
                goto gm_lb2;
            }
            {
                {
                    void * t33 = gm_l0;
                    gm_tl874t2atnlh3 t34 = gm_l4;
                    gm_f_location_i7gnofg((void *)&gm_l14, (void *)NULL, cx, NULL, t33, t34);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l14))[24] != 0);
                    if (!t35) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l14), 24);
                    void * t36 = (void *)((uint8_t *)&F.l6);
                    gm_tftb1odc739t5 t37 = gm_f_push_0pgacsj_4(t36, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l7)));
                    gm_l15 = t37;
                    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                    if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcvksed7nkdegg, 55, 37);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_l8 = t39;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t40 = gm_l9;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t42 = (t40 < t41);
            if (!t42) {
                goto gm_lb5;
            }
            {
                void * t43 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t45 = gm_l9;
                {
                    {
                        gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + (uintptr_t)t45 * 12));
                        gm_l10 = t46;
                        {
                            {
                                void * t47 = gm_l0;
                                gm_tl874t2atnlh3 t48 = gm_l10;
                                gm_f_location_i7gnofg((void *)&gm_l16, (void *)NULL, cx, NULL, t47, t48);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t49 = (uint8_t)((((uint8_t *)&gm_l16))[24] != 0);
                                if (!t49) {
                                    goto gm_lb11;
                                }
                                memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l16), 24);
                                void * t50 = (void *)((uint8_t *)&F.l6);
                                gm_tftb1odc739t5 t51 = gm_f_push_0pgacsj_4(t50, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l11)));
                                gm_l17 = t51;
                                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcvksed7nkdegg, 58, 35);
                                goto gm_lb10;
                            }
gm_lb11:;
                        }
gm_lb10:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 57, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t53 = gm_l9;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l9 = t56;
        }
    }
gm_lb5:;
    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
    gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t58);
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
__attribute__((aligned(4))) void gm_f_signature_help_16c2a8f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_signature_help_16c2a8f F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l3; /* t */
    uint64_t gm_l5; /* active */
    gm_b16_8 gm_l7; /* code */
    gm_b16_8 gm_l10; /* name */
    uint64_t gm_l11; /* s */
    uint64_t gm_l12; /* before_end */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* p */
    gm_b16_8 gm_l17; /* n */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_b16_8 gm_l21; /* first */
    uint64_t gm_l22; /* k */
    gm_b16_8 gm_l23; /* label */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l27; /* p */
    gm_b16_8 gm_l30; /* d */
    gm_tq47bkpmd2b9a gm_l33; /* <temporary array> */
    GM_STACK_CHECK(&F, 3128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9nnki79sf2e1 gm_l34;
    gm_tvin65ttqbr1d gm_l35;
    gm_teijtv56moffm gm_l36;
    gm_t8scu1osgcra5 gm_l37;
    gm_tec7dnv8fjfj8 gm_l38;
    gm_t6igi6646q59o gm_l39;
    gm_tf5srh4mp4b3i gm_l40;
    uint64_t gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tf5srh4mp4b3i gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tabb710d9mq2e gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tabb710d9mq2e gm_l50;
    {
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((uint8_t *)t31));
        uint32_t t33 = gm_l1;
        gm_b16_8 t34 = gm_f_source_text_7ru912k(t32, t33);
        gm_l3 = t34;
        gm_b16_8 t35 = gm_l3;
        uint32_t t36 = gm_l2;
        uint64_t t37 = (uint64_t)t36;
        gm_f_call_context_r6lbirq((void *)&gm_l34, (void *)NULL, cx, NULL, t35, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l34))[40] != 0);
        if (t38) {
            memmove(((uint8_t *)&gm_l35), ((uint8_t *)&gm_l34), 40);
        } else {
            memmove(gm_out, ((uint8_t *)gm_cro0h2panct), 24);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l35), 40);
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
        gm_l5 = t39;
        void * t40 = gm_l0;
        void * t41 = GM_LD(void *, ((uint8_t *)t40));
        void * t42 = (void *)(((uint8_t *)t41) + 120);
        uint32_t t43 = gm_l1;
        uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&F.l4));
        uint64_t r45; bool o46 = __builtin_sub_overflow(t44, ((uint64_t)1ULL), &r45);
        if (o46) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 326, 37);
        uint64_t t47 = r45;
        uint32_t t48 = (uint32_t)t47;
        gm_f_hover_at_19okc4g((void *)&gm_l36, t42, t43, t48);
        uint8_t t49 = (uint8_t)((((uint8_t *)&gm_l36))[56] != 0);
        if (t49) {
            memmove(((uint8_t *)&gm_l37), ((uint8_t *)&gm_l36), 56);
        } else {
            memmove(gm_out, ((uint8_t *)gm_cro0h2panct), 24);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l37), 56);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 16));
        gm_l7 = t50;
        gm_b16_8 t51 = gm_l7;
        gm_f_split_signature_3r31d7h((void *)&gm_l38, (void *)NULL, cx, NULL, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t52 = (uint8_t)((((uint8_t *)&gm_l38))[56] != 0);
        if (t52) {
            memmove(((uint8_t *)&gm_l39), ((uint8_t *)&gm_l38), 56);
        } else {
            memmove(gm_out, ((uint8_t *)gm_cro0h2panct), 24);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l8), ((uint8_t *)&gm_l39), 56);
        memmove(((uint8_t *)&F.l9), ((uint8_t *)&F.l8), 40);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(0));
        gm_l10 = t53;
        gm_b16_8 t54 = gm_l7;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s2urait735ofq));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(4));
        uint8_t t56 = gm_f_contains_gkki4kq(t54, t55);
        if (t56) {
            void * t57 = GM_LD(void *, ((uint8_t *)&gm_l7));
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            gm_b16_8 t59 = gm_l7;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s1b629p19p3qq));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
            gm_tf5srh4mp4b3i t61 = gm_f_find_nab29co(t59, t60);
            gm_l40 = t61;
            uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l40))[8] != 0);
            if (t62) {
                uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&gm_l40));
                gm_l41 = t63;
            } else {
                gm_l41 = ((uint64_t)0ULL);
            }
            uint64_t t64 = gm_l41;
            uint64_t t65 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t66 = (uintptr_t)t64;
            if (t65 > t66 || t66 > (uintptr_t)t58) gm_trap_slice(t65, t66, t58, gm_srcvksed7nkdegg, 332, 16);
            uint64_t t67 = ((uintptr_t)t64) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t57 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67));
            gm_l10 = t68;
        } else {
            uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)&F.l4));
            gm_l11 = t69;
            {
                for (;;) {
                    uint64_t t70 = gm_l11;
                    uint8_t t71 = (t70 > ((uint64_t)0ULL));
                    if (!t71) {
                        goto gm_lb1;
                    }
                    void * t72 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t74 = gm_l11;
                    uint64_t r75; bool o76 = __builtin_sub_overflow(t74, ((uint64_t)1ULL), &r75);
                    if (o76) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 336, 28);
                    uint64_t t77 = r75;
                    if ((uintptr_t)t77 >= (uintptr_t)(t73)) gm_trap_index((uintptr_t)t77, t73, gm_srcvksed7nkdegg, 336, 26);
                    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)t72) + (uintptr_t)t77 * 1));
                    uint8_t t79 = (t78 >= ((uint8_t)128ULL));
                    if (t79) {
                        gm_l42 = ((uint8_t)1);
                    } else {
                        void * t80 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t82 = gm_l11;
                        uint64_t r83; bool o84 = __builtin_sub_overflow(t82, ((uint64_t)1ULL), &r83);
                        if (o84) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 336, 69);
                        uint64_t t85 = r83;
                        if ((uintptr_t)t85 >= (uintptr_t)(t81)) gm_trap_index((uintptr_t)t85, t81, gm_srcvksed7nkdegg, 336, 67);
                        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t80) + (uintptr_t)t85 * 1));
                        uint8_t t87 = gm_f_is_ident_cont_317fpdb(t86);
                        gm_l42 = t87;
                    }
                    uint8_t t88 = gm_l42;
                    if (!t88) {
                        goto gm_lb1;
                    }
                    {
                        uint64_t t89 = gm_l11;
                        uint64_t r90; bool o91 = __builtin_sub_overflow(t89, ((uint64_t)1ULL), &r90);
                        if (o91) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 336, 80);
                        uint64_t t92 = r90;
                        gm_l11 = t92;
                    }
gm_lb3:;
                }
            }
gm_lb1:;
            void * t93 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t95 = gm_l11;
            uint64_t t96 = GM_LD(uint64_t, ((uint8_t *)&F.l4));
            uint64_t t97 = (uintptr_t)t95;
            uint64_t t98 = (uintptr_t)t96;
            if (t97 > t98 || t98 > (uintptr_t)t94) gm_trap_slice(t97, t98, t94, gm_srcvksed7nkdegg, 337, 16);
            uint64_t t99 = ((uintptr_t)t96) - (uintptr_t)t95;
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(((uint8_t *)t93 + (uintptr_t)t95 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(t99));
            gm_l10 = t100;
        }
    }
    {
        uint64_t t101 = GM_LD(uint64_t, ((uint8_t *)&F.l4));
        gm_l12 = t101;
        {
            for (;;) {
                uint64_t t102 = gm_l12;
                uint8_t t103 = (t102 > ((uint64_t)0ULL));
                if (!t103) {
                    goto gm_lb4;
                }
                void * t104 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t106 = gm_l12;
                uint64_t r107; bool o108 = __builtin_sub_overflow(t106, ((uint64_t)1ULL), &r107);
                if (o108) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 341, 33);
                uint64_t t109 = r107;
                if ((uintptr_t)t109 >= (uintptr_t)(t105)) gm_trap_index((uintptr_t)t109, t105, gm_srcvksed7nkdegg, 341, 31);
                uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t104) + (uintptr_t)t109 * 1));
                uint8_t t111 = (t110 >= ((uint8_t)128ULL));
                if (t111) {
                    gm_l43 = ((uint8_t)1);
                } else {
                    void * t112 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t114 = gm_l12;
                    uint64_t r115; bool o116 = __builtin_sub_overflow(t114, ((uint64_t)1ULL), &r115);
                    if (o116) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 341, 83);
                    uint64_t t117 = r115;
                    if ((uintptr_t)t117 >= (uintptr_t)(t113)) gm_trap_index((uintptr_t)t117, t113, gm_srcvksed7nkdegg, 341, 81);
                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)t112) + (uintptr_t)t117 * 1));
                    uint8_t t119 = gm_f_is_ident_cont_317fpdb(t118);
                    gm_l43 = t119;
                }
                uint8_t t120 = gm_l43;
                if (!t120) {
                    goto gm_lb4;
                }
                {
                    uint64_t t121 = gm_l12;
                    uint64_t r122; bool o123 = __builtin_sub_overflow(t121, ((uint64_t)1ULL), &r122);
                    if (o123) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 341, 103);
                    uint64_t t124 = r122;
                    gm_l12 = t124;
                }
gm_lb6:;
            }
        }
gm_lb4:;
        {
            {
                uint64_t t125 = gm_l12;
                uint8_t t126 = (t125 > ((uint64_t)0ULL));
                if (!t126) {
                    goto gm_lb8;
                }
                void * t127 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t129 = gm_l12;
                uint64_t r130; bool o131 = __builtin_sub_overflow(t129, ((uint64_t)1ULL), &r130);
                if (o131) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 342, 29);
                uint64_t t132 = r130;
                if ((uintptr_t)t132 >= (uintptr_t)(t128)) gm_trap_index((uintptr_t)t132, t128, gm_srcvksed7nkdegg, 342, 27);
                uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)t127) + (uintptr_t)t132 * 1));
                uint8_t t134 = ((t133 == ((uint8_t)46ULL)));
                if (!t134) {
                    goto gm_lb8;
                }
                gm_trqfvmjtt4sij t135;
                gm_f_list_epvducg((void *)&t135, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l13 = t135;
                gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                gm_l14 = t136;
                gm_l15 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t137 = gm_l15;
                        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint8_t t139 = (t137 < t138);
                        if (!t139) {
                            goto gm_lb9;
                        }
                        {
                            void * t140 = GM_LD(void *, ((uint8_t *)&gm_l14));
                            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            uint64_t t142 = gm_l15;
                            {
                                {
                                    gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)t140) + (uintptr_t)t142 * 16));
                                    gm_l16 = t143;
                                    gm_b16_8 t144 = gm_l16;
                                    gm_b16_8 t145;
                                    GM_ST(void *, (uint8_t *)&t145, (void *)(gm_s7b8bvi5kqaqt));
                                    GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(6));
                                    uint8_t t146 = gm_f_ends_with_biriu5l(t144, t145);
                                    if (t146) {
                                        gm_l44 = ((uint8_t)1);
                                    } else {
                                        gm_b16_8 t147 = gm_l16;
                                        gm_b16_8 t148;
                                        GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s2urait735ofq));
                                        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(4));
                                        uint8_t t149 = gm_f_contains_gkki4kq(t147, t148);
                                        gm_l44 = t149;
                                    }
                                    uint8_t t150 = gm_l44;
                                    uint8_t t151 = (uint8_t)!t150;
                                    if (t151) {
                                        void * t152 = (void *)((uint8_t *)&F.l13);
                                        gm_b16_8 t153 = gm_l16;
                                        gm_tftb1odc739t5 t154 = gm_f_push_3ef746b(t152, t153);
                                        gm_l45 = t154;
                                        uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                        if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcvksed7nkdegg, 345, 73);
                                    }
                                    goto gm_lb12;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 344, 9);
                            }
gm_lb12:;
                        }
gm_lb11:;
                        uint64_t t156 = gm_l15;
                        uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                        uint64_t t159 = r157;
                        gm_l15 = t159;
                    }
                }
gm_lb9:;
                memmove(((uint8_t *)&F.l9), ((uint8_t *)&F.l13), 40);
                uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                uint8_t t161 = (t160 > ((uint64_t)0ULL));
                if (t161) {
                    void * t162 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t163 = gm_f_remove_qvip58u(t162, ((uint64_t)0ULL));
                }
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
    }
    {
        {
            {
                uint8_t t164 = (uint8_t)(((((uint8_t *)&F.l4) + 16))[16] != 0);
                if (!t164) {
                    goto gm_lb15;
                }
                gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
                gm_l17 = t165;
                gm_l19 = ((uint64_t)0ULL);
                uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                gm_l18 = t166;
                {
                    for (;;) {
                        uint64_t t167 = gm_l19;
                        uint64_t t168 = gm_l18;
                        uint8_t t169 = (t167 < t168);
                        if (!t169) {
                            goto gm_lb16;
                        }
                        {
                            {
                                {
                                    uint64_t t170 = gm_l19;
                                    gm_l20 = t170;
                                    void * t171 = GM_LD(void *, ((uint8_t *)&F.l9));
                                    uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                    uint64_t t173 = gm_l20;
                                    if ((uintptr_t)t173 >= (uintptr_t)(t172)) gm_trap_index((uintptr_t)t173, t172, gm_srcvksed7nkdegg, 352, 22);
                                    gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)t171) + (uintptr_t)t173 * 16));
                                    gm_l21 = t174;
                                    {
                                        {
                                            gm_b16_8 t175 = gm_l21;
                                            gm_tf5srh4mp4b3i t176 = gm_f_index_of_hdeg351(t175, ((uint8_t)58ULL));
                                            gm_l46 = t176;
                                            uint8_t t177 = (uint8_t)((((uint8_t *)&gm_l46))[8] != 0);
                                            if (!t177) {
                                                goto gm_lb22;
                                            }
                                            uint64_t t178 = GM_LD(uint64_t, ((uint8_t *)&gm_l46));
                                            gm_l22 = t178;
                                            void * t179 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                            uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                            uint64_t t181 = gm_l22;
                                            uint64_t t182 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t183 = (uintptr_t)t181;
                                            if (t182 > t183 || t183 > (uintptr_t)t180) gm_trap_slice(t182, t183, t180, gm_srcvksed7nkdegg, 353, 51);
                                            uint64_t t184 = ((uintptr_t)t181) - (uintptr_t)((uint64_t)0ULL);
                                            gm_b16_8 t185;
                                            GM_ST(void *, (uint8_t *)&t185, (void *)(((uint8_t *)t179 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                            GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(t184));
                                            gm_l21 = t185;
                                            goto gm_lb21;
                                        }
gm_lb22:;
                                    }
gm_lb21:;
                                    gm_b16_8 t186 = gm_l21;
                                    gm_b16_8 t187 = gm_f_trim_fdrfe6i(t186);
                                    gm_b16_8 t188 = gm_l17;
                                    uint8_t t189 = (gm_eq0iqiubfm7e(((uint8_t *)&t187), ((uint8_t *)&t188)));
                                    if (t189) {
                                        uint64_t t190 = gm_l20;
                                        gm_l5 = t190;
                                        goto gm_lb16;
                                    }
                                    goto gm_lb19;
                                }
gm_lb20:;
                                gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 351, 9);
                            }
gm_lb19:;
                        }
gm_lb18:;
                        uint64_t t191 = gm_l19;
                        uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                        uint64_t t194 = r192;
                        gm_l19 = t194;
                    }
                }
gm_lb16:;
                goto gm_lb14;
            }
gm_lb15:;
        }
gm_lb14:;
        gm_b16_8 t195 = gm_l10;
        gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
        gm_b16_8 t198;
        GM_ST(void *, (uint8_t *)&t198, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l47, (void *)NULL, cx, NULL, t197, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
        if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srcvksed7nkdegg, 360, 43);
        gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
        gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 40));
        gm_tfeab279c16rr t202;
        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 0), t195);
        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 16), t200);
        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 32), t201);
        gm_f_format_9ug250p((void *)&gm_l48, (void *)NULL, cx, NULL, &t202);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
        if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srcvksed7nkdegg, 360, 14);
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
        gm_l23 = t204;
        gm_trqfvmjtt4sij t205;
        gm_f_list_0202sdh_4((void *)&t205, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l24 = t205;
        gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_l25 = t206;
        gm_l26 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t207 = gm_l26;
                uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                uint8_t t209 = (t207 < t208);
                if (!t209) {
                    goto gm_lb23;
                }
                {
                    void * t210 = GM_LD(void *, ((uint8_t *)&gm_l25));
                    uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                    uint64_t t212 = gm_l26;
                    {
                        {
                            gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)t210) + (uintptr_t)t212 * 16));
                            gm_l27 = t213;
                            gm_tin97tt4ud0s1 t214;
                            gm_f_obj_aoa7fl7((void *)&t214, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l28 = t214;
                            void * t215 = (void *)((uint8_t *)&F.l28);
                            gm_b16_8 t216;
                            GM_ST(void *, (uint8_t *)&t216, (void *)(gm_su137hja0ebcd));
                            GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(5));
                            gm_b16_8 t217 = gm_l27;
                            gm_t8vuock3jnv59 t218;
                            gm_f_text_ls49dp6((void *)&t218, t217);
                            gm_b0_1 t219 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t215, t216, &t218);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t220 = (void *)((uint8_t *)&F.l24);
                            void * t221 = (void *)((uint8_t *)&F.l28);
                            gm_t8vuock3jnv59 t222;
                            gm_f_value_pkuqal3((void *)&t222, t221);
                            gm_tftb1odc739t5 t223 = gm_f_push_0pgacsj_4(t220, &t222);
                            gm_l49 = t223;
                            uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcvksed7nkdegg, 365, 9);
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 362, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t225 = gm_l26;
                uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                uint64_t t228 = r226;
                gm_l26 = t228;
            }
        }
gm_lb23:;
        gm_tin97tt4ud0s1 t229;
        gm_f_obj_aoa7fl7((void *)&t229, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l29 = t229;
        void * t230 = (void *)((uint8_t *)&F.l29);
        gm_b16_8 t231;
        GM_ST(void *, (uint8_t *)&t231, (void *)(gm_su137hja0ebcd));
        GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(5));
        gm_b16_8 t232 = gm_l23;
        gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
        gm_t8vuock3jnv59 t234;
        gm_f_text_ls49dp6((void *)&t234, t233);
        gm_b0_1 t235 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t230, t231, &t234);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t236 = (void *)((uint8_t *)&F.l29);
        gm_b16_8 t237;
        GM_ST(void *, (uint8_t *)&t237, (void *)(gm_sd6a57g2jmtks));
        GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(10));
        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
        gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
        gm_t8vuock3jnv59 t240;
        gm_f_array_k32ana3((void *)&t240, (void *)NULL, cx, NULL, t239);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t241 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t236, t237, &t240);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t242 = (uint8_t)((((((uint8_t *)&F.l6) + 16) + 16))[16] != 0);
                if (!t242) {
                    goto gm_lb29;
                }
                gm_b16_8 t243 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 16) + 16));
                gm_l30 = t243;
                gm_tin97tt4ud0s1 t244;
                gm_f_obj_aoa7fl7((void *)&t244, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l31 = t244;
                void * t245 = (void *)((uint8_t *)&F.l31);
                gm_b16_8 t246;
                GM_ST(void *, (uint8_t *)&t246, (void *)(gm_s7j2u3ofu66j8));
                GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(4));
                gm_b16_8 t247;
                GM_ST(void *, (uint8_t *)&t247, (void *)(gm_sm06nsg0msdgd));
                GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(8));
                gm_t8vuock3jnv59 t248;
                gm_f_text_ls49dp6((void *)&t248, t247);
                gm_b0_1 t249 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t245, t246, &t248);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t250 = (void *)((uint8_t *)&F.l31);
                gm_b16_8 t251;
                GM_ST(void *, (uint8_t *)&t251, (void *)(gm_s5qkai093c1q0));
                GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(5));
                gm_b16_8 t252 = gm_l30;
                gm_f_dupe_g9iqpvn((void *)&gm_l50, (void *)NULL, cx, NULL, t252, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7bjnihrifk)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t253 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                if (!t253) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srcvksed7nkdegg, 373, 29);
                gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                gm_t8vuock3jnv59 t256;
                gm_f_text_ls49dp6((void *)&t256, t255);
                gm_b0_1 t257 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t250, t251, &t256);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t258 = (void *)((uint8_t *)&F.l29);
                gm_b16_8 t259;
                GM_ST(void *, (uint8_t *)&t259, (void *)(gm_s7unnudd0kkri));
                GM_ST(uintptr_t, (uint8_t *)&t259 + 8, (uintptr_t)(13));
                void * t260 = (void *)((uint8_t *)&F.l31);
                gm_t8vuock3jnv59 t261;
                gm_f_value_pkuqal3((void *)&t261, t260);
                gm_b0_1 t262 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t258, t259, &t261);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb28;
            }
gm_lb29:;
        }
gm_lb28:;
    }
    {
        gm_tin97tt4ud0s1 t263;
        gm_f_obj_aoa7fl7((void *)&t263, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l32 = t263;
        void * t264 = (void *)((uint8_t *)&F.l32);
        gm_b16_8 t265;
        GM_ST(void *, (uint8_t *)&t265, (void *)(gm_s22mk3qhbqci2));
        GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(10));
        void * t266 = (void *)((uint8_t *)&F.l29);
        gm_t8vuock3jnv59 t267;
        gm_f_value_pkuqal3((void *)&t267, t266);
        gm_tq47bkpmd2b9a t268;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t268 + 0), t267);
        gm_l33 = t268;
        void * t269 = (void *)((uint8_t *)&gm_l33);
        uint64_t t270 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t271 = (uintptr_t)((uint64_t)1ULL);
        if (t270 > t271 || t271 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t270, t271, ((uint64_t)1ULL), gm_srcvksed7nkdegg, 377, 31);
        uint64_t t272 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t273;
        GM_ST(void *, (uint8_t *)&t273, (void *)(((uint8_t *)t269 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(t272));
        gm_t8vuock3jnv59 t274;
        gm_f_array_k32ana3((void *)&t274, (void *)NULL, cx, NULL, t273);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t275 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t264, t265, &t274);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t276 = (void *)((uint8_t *)&F.l32);
        gm_b16_8 t277;
        GM_ST(void *, (uint8_t *)&t277, (void *)(gm_si1j2hanh22ll));
        GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(15));
        gm_t8vuock3jnv59 t278;
        gm_f_int_95caidg((void *)&t278, (void *)NULL, cx, NULL, ((int64_t)0LL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t279 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t276, t277, &t278);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t280 = (void *)((uint8_t *)&F.l32);
        gm_b16_8 t281;
        GM_ST(void *, (uint8_t *)&t281, (void *)(gm_schvua5hkahp9));
        GM_ST(uintptr_t, (uint8_t *)&t281 + 8, (uintptr_t)(15));
        uint64_t t282 = gm_l5;
        int64_t t283 = (int64_t)t282;
        gm_t8vuock3jnv59 t284;
        gm_f_int_95caidg((void *)&t284, (void *)NULL, cx, NULL, t283);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t285 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t280, t281, &t284);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t286 = (void *)((uint8_t *)&F.l32);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t286);
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
__attribute__((aligned(4))) void gm_f_split_signature_3r31d7h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_split_signature_3r31d7h F;
    gm_b16_8 gm_l0; /* sig */
    uint64_t gm_l1; /* at */
    uint64_t gm_l2; /* start */
    int64_t gm_l3; /* depth */
    uint64_t gm_l4; /* cur_start */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint8_t gm_l9; /* c */
    gm_b16_8 gm_l10; /* last */
    GM_STACK_CHECK(&F, 999u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l11;
    uint64_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_b16_8 t9 = gm_l0;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sum9vj65rg4jb));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(3));
    gm_tf5srh4mp4b3i t11 = gm_f_find_nab29co(t9, t10);
    gm_l11 = t11;
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
    if (t12) {
        uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
        gm_l12 = t13;
    } else {
        gm_tec7dnv8fjfj8 t14;
        memset(&t14, 0, sizeof t14);
        memmove(gm_out, ((uint8_t *)&t14), 64);
        goto gm_fdone;
    }
    uint64_t t15 = gm_l12;
    gm_l1 = t15;
    uint64_t t16 = gm_l1;
    uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)3ULL), &r17);
    if (o18) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 297, 14);
    uint64_t t19 = r17;
    gm_l2 = t19;
    gm_l3 = ((int64_t)0LL);
    uint64_t t20 = gm_l2;
    gm_l4 = t20;
    gm_trqfvmjtt4sij t21;
    gm_f_list_epvducg((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t21;
    uint64_t t22 = gm_l2;
    gm_l7 = t22;
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l6 = t23;
    {
        for (;;) {
            uint64_t t24 = gm_l7;
            uint64_t t25 = gm_l6;
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t27 = gm_l7;
                        gm_l8 = t27;
                        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t30 = gm_l8;
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)t30 * 1));
                        gm_l9 = t31;
                        uint8_t t32 = gm_l9;
                        uint8_t t33 = ((t32 == ((uint8_t)40ULL)));
                        if (t33) {
                            gm_l14 = ((uint8_t)1);
                        } else {
                            uint8_t t34 = gm_l9;
                            uint8_t t35 = ((t34 == ((uint8_t)91ULL)));
                            gm_l14 = t35;
                        }
                        uint8_t t36 = gm_l14;
                        if (t36) {
                            gm_l13 = ((uint8_t)1);
                        } else {
                            uint8_t t37 = gm_l9;
                            uint8_t t38 = ((t37 == ((uint8_t)123ULL)));
                            gm_l13 = t38;
                        }
                        uint8_t t39 = gm_l13;
                        if (t39) {
                            int64_t t40 = gm_l3;
                            int64_t r41; bool o42 = __builtin_add_overflow(t40, ((int64_t)1LL), &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 304, 13);
                            int64_t t43 = r41;
                            gm_l3 = t43;
                        } else {
                            uint8_t t44 = gm_l9;
                            uint8_t t45 = ((t44 == ((uint8_t)41ULL)));
                            if (t45) {
                                gm_l16 = ((uint8_t)1);
                            } else {
                                uint8_t t46 = gm_l9;
                                uint8_t t47 = ((t46 == ((uint8_t)93ULL)));
                                gm_l16 = t47;
                            }
                            uint8_t t48 = gm_l16;
                            if (t48) {
                                gm_l15 = ((uint8_t)1);
                            } else {
                                uint8_t t49 = gm_l9;
                                uint8_t t50 = ((t49 == ((uint8_t)125ULL)));
                                gm_l15 = t50;
                            }
                            uint8_t t51 = gm_l15;
                            if (t51) {
                                int64_t t52 = gm_l3;
                                uint8_t t53 = ((t52 == ((int64_t)0LL)));
                                if (t53) {
                                    void * t54 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t56 = gm_l4;
                                    uint64_t t57 = gm_l8;
                                    uint64_t t58 = (uintptr_t)t56;
                                    uint64_t t59 = (uintptr_t)t57;
                                    if (t58 > t59 || t59 > (uintptr_t)t55) gm_trap_slice(t58, t59, t55, gm_srcvksed7nkdegg, 307, 34);
                                    uint64_t t60 = ((uintptr_t)t57) - (uintptr_t)t56;
                                    gm_b16_8 t61;
                                    GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t54 + (uintptr_t)t56 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60));
                                    gm_b16_8 t62 = gm_f_trim_fdrfe6i(t61);
                                    gm_l10 = t62;
                                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                    uint8_t t64 = (t63 > ((uint64_t)0ULL));
                                    if (t64) {
                                        void * t65 = (void *)((uint8_t *)&F.l5);
                                        gm_b16_8 t66 = gm_l10;
                                        gm_tftb1odc739t5 t67 = gm_f_push_3ef746b(t65, t66);
                                        gm_l17 = t67;
                                        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                        if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcvksed7nkdegg, 308, 35);
                                    }
                                    void * t69 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t71 = gm_l8;
                                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                                    if (o73) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 309, 65);
                                    uint64_t t74 = r72;
                                    uint64_t t75 = (uintptr_t)t74;
                                    uint64_t t76 = (uintptr_t)t70;
                                    if (t75 > t76 || t76 > (uintptr_t)t70) gm_trap_slice(t75, t76, t70, gm_srcvksed7nkdegg, 309, 61);
                                    uint64_t t77 = ((uintptr_t)t70) - (uintptr_t)t74;
                                    gm_b16_8 t78;
                                    GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t69 + (uintptr_t)t74 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(t77));
                                    gm_trqfvmjtt4sij t79 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
                                    gm_t6igi6646q59o t80;
                                    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t80 + 0), t79);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 40), t78);
                                    gm_tec7dnv8fjfj8 t81;
                                    GM_ST(uint8_t, (uint8_t *)&t81 + 57, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t81 + 58, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t81 + 60, 0);
                                    GM_ST(gm_t6igi6646q59o, ((uint8_t *)&t81), t80);
                                    ((uint8_t *)&t81)[56] = 1;
                                    memmove(gm_out, ((uint8_t *)&t81), 64);
                                    goto gm_fdone;
                                }
                                int64_t t82 = gm_l3;
                                int64_t r83; bool o84 = __builtin_sub_overflow(t82, ((int64_t)1LL), &r83);
                                if (o84) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 311, 13);
                                int64_t t85 = r83;
                                gm_l3 = t85;
                            } else {
                                {
                                    {
                                        uint8_t t86 = gm_l9;
                                        uint8_t t87 = ((t86 == ((uint8_t)44ULL)));
                                        if (!t87) {
                                            goto gm_lb7;
                                        }
                                        int64_t t88 = gm_l3;
                                        uint8_t t89 = ((t88 == ((int64_t)0LL)));
                                        if (!t89) {
                                            goto gm_lb7;
                                        }
                                        void * t90 = (void *)((uint8_t *)&F.l5);
                                        void * t91 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                        uint64_t t93 = gm_l4;
                                        uint64_t t94 = gm_l8;
                                        uint64_t t95 = (uintptr_t)t93;
                                        uint64_t t96 = (uintptr_t)t94;
                                        if (t95 > t96 || t96 > (uintptr_t)t92) gm_trap_slice(t95, t96, t92, gm_srcvksed7nkdegg, 313, 34);
                                        uint64_t t97 = ((uintptr_t)t94) - (uintptr_t)t93;
                                        gm_b16_8 t98;
                                        GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t91 + (uintptr_t)t93 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(t97));
                                        gm_b16_8 t99 = gm_f_trim_fdrfe6i(t98);
                                        gm_tftb1odc739t5 t100 = gm_f_push_3ef746b(t90, t99);
                                        gm_l18 = t100;
                                        uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                        if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcvksed7nkdegg, 313, 13);
                                        uint64_t t102 = gm_l8;
                                        uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                                        if (o104) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 314, 25);
                                        uint64_t t105 = r103;
                                        gm_l4 = t105;
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                }
gm_lb6:;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 301, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t106 = gm_l7;
            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
            uint64_t t109 = r107;
            gm_l7 = t109;
        }
    }
gm_lb1:;
    gm_tec7dnv8fjfj8 t110;
    memset(&t110, 0, sizeof t110);
    memmove(gm_out, ((uint8_t *)&t110), 64);
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
__attribute__((aligned(4))) void gm_f_symbol_json_b065ihj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_symbol_json_b065ihj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* s */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 745u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t7;
    gm_f_list_0202sdh_4((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    void * t8 = gm_l1;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 64) + 8));
    gm_l3 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l4;
            uint64_t t11 = gm_l3;
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t13 = gm_l4;
                        gm_l5 = t13;
                        void * t14 = (void *)((uint8_t *)&F.l2);
                        void * t15 = gm_l0;
                        void * t16 = gm_l1;
                        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 64));
                        uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 64) + 8));
                        uint64_t t19 = gm_l5;
                        if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_srcvksed7nkdegg, 99, 65);
                        void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 80);
                        gm_t8vuock3jnv59 t21;
                        gm_f_symbol_json_b065ihj((void *)&t21, (void *)NULL, cx, NULL, t15, t20);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t22 = gm_f_push_0pgacsj_4(t14, &t21);
                        gm_l7 = t22;
                        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcvksed7nkdegg, 99, 34);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 99, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l4;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l4 = t27;
        }
    }
gm_lb1:;
    gm_tin97tt4ud0s1 t28;
    gm_f_obj_aoa7fl7((void *)&t28, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t28;
    void * t29 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sius9pki5u4e3));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(4));
    void * t31 = gm_l1;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)t31));
    gm_t8vuock3jnv59 t33;
    gm_f_text_ls49dp6((void *)&t33, t32);
    gm_b0_1 t34 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t29, t30, &t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t35 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sb6smklmjl60o));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(6));
    void * t37 = gm_l1;
    gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 24));
    gm_t8vuock3jnv59 t39;
    gm_f_text_ls49dp6((void *)&t39, t38);
    gm_b0_1 t40 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t35, t36, &t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t41 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s7j2u3ofu66j8));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
    void * t43 = gm_l1;
    gm_t9qkh0rsooc8d t44 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t43) + 16));
    int64_t t45 = gm_f_symbol_kind_g6njvq1(t44);
    gm_t8vuock3jnv59 t46;
    gm_f_int_95caidg((void *)&t46, (void *)NULL, cx, NULL, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t47 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t41, t42, &t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t48 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t49;
    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sl4ns6hobicht));
    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(5));
    void * t50 = gm_l0;
    void * t51 = GM_LD(void *, ((uint8_t *)t50));
    void * t52 = gm_l1;
    gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 40));
    void * t54 = gm_l0;
    gm_t9qkh0rsooc8d t55 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t54) + 8));
    gm_t8vuock3jnv59 t56;
    gm_f_range_demc0bt((void *)&t56, (void *)NULL, cx, NULL, t51, t53, t55);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t57 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t48, t49, &t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t58 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t59;
    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sttp5u65omp7t));
    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(14));
    void * t60 = gm_l0;
    void * t61 = GM_LD(void *, ((uint8_t *)t60));
    void * t62 = gm_l1;
    gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 52));
    void * t64 = gm_l0;
    gm_t9qkh0rsooc8d t65 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t64) + 8));
    gm_t8vuock3jnv59 t66;
    gm_f_range_demc0bt((void *)&t66, (void *)NULL, cx, NULL, t61, t63, t65);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t67 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t58, t59, &t66);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t68 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t69;
    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_srdnkonbfvqdq));
    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(8));
    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
    gm_t8vuock3jnv59 t72;
    gm_f_array_k32ana3((void *)&t72, (void *)NULL, cx, NULL, t71);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t73 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t68, t69, &t72);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t74 = (void *)((uint8_t *)&F.l6);
    gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t74);
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
inline __attribute__((aligned(4))) int64_t gm_f_symbol_kind_g6njvq1(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_symbol_kind_g6njvq1 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb2;
            }
            gm_l1 = ((int64_t)2LL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)13ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                goto gm_lb3;
            }
gm_lb4:;
            gm_l1 = ((int64_t)12LL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)2ULL));
                    if (!t33) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)12ULL));
                    if (!t35) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                goto gm_lb7;
            }
gm_lb8:;
            gm_l1 = ((int64_t)6LL);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)3ULL));
            if (!t37) {
                goto gm_lb11;
            }
            gm_l1 = ((int64_t)23LL);
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)4ULL));
            if (!t39) {
                goto gm_lb12;
            }
            gm_l1 = ((int64_t)10LL);
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)5ULL));
            if (!t41) {
                goto gm_lb13;
            }
            gm_l1 = ((int64_t)11LL);
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)6ULL));
            if (!t43) {
                goto gm_lb14;
            }
            gm_l1 = ((int64_t)19LL);
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)7ULL));
            if (!t45) {
                goto gm_lb15;
            }
            gm_l1 = ((int64_t)14LL);
            goto gm_lb1;
        }
gm_lb15:;
        {
            {
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)8ULL));
                    if (!t47) {
                        goto gm_lb18;
                    }
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)9ULL));
                    if (!t49) {
                        goto gm_lb19;
                    }
                    goto gm_lb17;
                }
gm_lb19:;
                goto gm_lb16;
            }
gm_lb17:;
            gm_l1 = ((int64_t)13LL);
            goto gm_lb1;
        }
gm_lb16:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)10ULL));
            if (!t51) {
                goto gm_lb20;
            }
            gm_l1 = ((int64_t)8LL);
            goto gm_lb1;
        }
gm_lb20:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)11ULL));
            if (!t53) {
                goto gm_lb21;
            }
            gm_l1 = ((int64_t)22LL);
            goto gm_lb1;
        }
gm_lb21:;
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)14ULL));
            if (!t55) {
                goto gm_lb22;
            }
            gm_l1 = ((int64_t)5LL);
            goto gm_lb1;
        }
gm_lb22:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)15ULL));
            if (!t57) {
                goto gm_lb23;
            }
            gm_l1 = ((int64_t)26LL);
            goto gm_lb1;
        }
gm_lb23:;
        {
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)16ULL));
            if (!t59) {
                goto gm_lb24;
            }
            gm_l1 = ((int64_t)13LL);
            goto gm_lb1;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 63, 42);
    }
gm_lb1:;
    int64_t t60 = gm_l1;
    F.ret = t60;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
