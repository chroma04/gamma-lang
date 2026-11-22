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
typedef struct { uint8_t f0; uint8_t p1[3]; int32_t f1; int32_t f2; int32_t f3; int32_t f4; int32_t f5; uint32_t f6; uint8_t p7[4]; int64_t f7; } gm_t01jnheg9lqvd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { unsigned __int128 f0; unsigned __int128 f1; } gm_t793vlgpa3b88;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t a[4]; } gm_tu703bl1jkggd;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t01jnheg9lqvd) == 40 && _Alignof(gm_t01jnheg9lqvd) == 8 && offsetof(gm_t01jnheg9lqvd, f0) == 0 && offsetof(gm_t01jnheg9lqvd, f1) == 4 && offsetof(gm_t01jnheg9lqvd, f2) == 8 && offsetof(gm_t01jnheg9lqvd, f3) == 12 && offsetof(gm_t01jnheg9lqvd, f4) == 16 && offsetof(gm_t01jnheg9lqvd, f5) == 20 && offsetof(gm_t01jnheg9lqvd, f6) == 24 && offsetof(gm_t01jnheg9lqvd, f7) == 32, "layout of `(u8, i32, i32, i32, i32, i32, u32, i64)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; double f1; } gm_t2mvr8fpgjkg6;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_t586m754bse2v;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t793vlgpa3b88) == 32 && _Alignof(gm_t793vlgpa3b88) == 16 && offsetof(gm_t793vlgpa3b88, f0) == 0 && offsetof(gm_t793vlgpa3b88, f1) == 16, "layout of `(lo: u128, hi: u128)`");
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_t89vrq05jgle5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; uint64_t f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_b16_8 f11; gm_b16_8 f12; gm_b16_8 f13; } gm_t8mpn05efspp6;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; __int128 f1; } gm_tctdkl637nfjf;
typedef struct { gm_t2lqu8jsj511d a[2]; } gm_te3hi449i91a8;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_tg1t0c5hkovn5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; uint64_t f8; } gm_tjg6nvivto1u9;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; uint64_t f1; int32_t f2; uint8_t pe[4]; } gm_tlk7da9ssbu7q;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; uint64_t f9; gm_b16_8 f10; gm_b16_8 f11; gm_b16_8 f12; gm_b16_8 f13; gm_b16_8 f14; gm_b16_8 f15; gm_b16_8 f16; } gm_toq7erhpb1d2e;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; uint64_t f6; gm_b16_8 f7; } gm_tpbrs0bkofuth;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; uint32_t f6; uint8_t p7[4]; gm_b16_8 f7; gm_b16_8 f8; uint32_t f9; uint8_t pe[4]; } gm_tt4j8kuecfafr;
_Static_assert(sizeof(gm_tu703bl1jkggd) == 32 && _Alignof(gm_tu703bl1jkggd) == 8 && offsetof(gm_tu703bl1jkggd, a) == 0, "layout of `[4]usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tu8mt0h8clq1l;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2mvr8fpgjkg6) == 24 && _Alignof(gm_t2mvr8fpgjkg6) == 8 && offsetof(gm_t2mvr8fpgjkg6, f0) == 0 && offsetof(gm_t2mvr8fpgjkg6, f1) == 16, "layout of `(string, f64)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t586m754bse2v) == 120 && _Alignof(gm_t586m754bse2v) == 8 && offsetof(gm_t586m754bse2v, f0) == 0 && offsetof(gm_t586m754bse2v, f1) == 16 && offsetof(gm_t586m754bse2v, f2) == 32 && offsetof(gm_t586m754bse2v, f3) == 40 && offsetof(gm_t586m754bse2v, f4) == 56 && offsetof(gm_t586m754bse2v, f5) == 72 && offsetof(gm_t586m754bse2v, f6) == 88 && offsetof(gm_t586m754bse2v, f7) == 104, "layout of `([]u8, string, u32, []u8, []u8, []u8, []u8, []u8)`");
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t89vrq05jgle5) == 72 && _Alignof(gm_t89vrq05jgle5) == 8 && offsetof(gm_t89vrq05jgle5, f0) == 0 && offsetof(gm_t89vrq05jgle5, f1) == 16 && offsetof(gm_t89vrq05jgle5, f2) == 32 && offsetof(gm_t89vrq05jgle5, f3) == 48 && offsetof(gm_t89vrq05jgle5, f4) == 56, "layout of `(string, string, string, u64, string)`");
_Static_assert(sizeof(gm_t8mpn05efspp6) == 208 && _Alignof(gm_t8mpn05efspp6) == 8 && offsetof(gm_t8mpn05efspp6, f0) == 0 && offsetof(gm_t8mpn05efspp6, f1) == 16 && offsetof(gm_t8mpn05efspp6, f2) == 32 && offsetof(gm_t8mpn05efspp6, f3) == 40 && offsetof(gm_t8mpn05efspp6, f4) == 56 && offsetof(gm_t8mpn05efspp6, f5) == 72 && offsetof(gm_t8mpn05efspp6, f6) == 88 && offsetof(gm_t8mpn05efspp6, f7) == 104 && offsetof(gm_t8mpn05efspp6, f8) == 112 && offsetof(gm_t8mpn05efspp6, f9) == 128 && offsetof(gm_t8mpn05efspp6, f10) == 144 && offsetof(gm_t8mpn05efspp6, f11) == 160 && offsetof(gm_t8mpn05efspp6, f12) == 176 && offsetof(gm_t8mpn05efspp6, f13) == 192, "layout of `([]u8, []u8, u64, string, string, []u8, []u8, u64, string, string, string, []u8, []u8, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t3877pk7lv748 f0; uint8_t f1; uint8_t pe[7]; } gm_t9geo0l9gngfn;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tctdkl637nfjf) == 32 && _Alignof(gm_tctdkl637nfjf) == 16 && offsetof(gm_tctdkl637nfjf, f0) == 0 && offsetof(gm_tctdkl637nfjf, f1) == 16, "layout of `(string, i128)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te3hi449i91a8) == 32 && _Alignof(gm_te3hi449i91a8) == 8 && offsetof(gm_te3hi449i91a8, a) == 0, "layout of `[2]BlobPart`");
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
typedef struct { gm_t7g88i52rrtm5 f0; uint8_t f1; uint8_t pe[3]; } gm_tflpan0ud4tim;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg1t0c5hkovn5) == 128 && _Alignof(gm_tg1t0c5hkovn5) == 8 && offsetof(gm_tg1t0c5hkovn5, f0) == 0 && offsetof(gm_tg1t0c5hkovn5, f1) == 16 && offsetof(gm_tg1t0c5hkovn5, f2) == 32 && offsetof(gm_tg1t0c5hkovn5, f3) == 48 && offsetof(gm_tg1t0c5hkovn5, f4) == 64 && offsetof(gm_tg1t0c5hkovn5, f5) == 80 && offsetof(gm_tg1t0c5hkovn5, f6) == 96 && offsetof(gm_tg1t0c5hkovn5, f7) == 112, "layout of `(string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
_Static_assert(sizeof(gm_tjg6nvivto1u9) == 128 && _Alignof(gm_tjg6nvivto1u9) == 8 && offsetof(gm_tjg6nvivto1u9, f0) == 0 && offsetof(gm_tjg6nvivto1u9, f1) == 16 && offsetof(gm_tjg6nvivto1u9, f2) == 24 && offsetof(gm_tjg6nvivto1u9, f3) == 40 && offsetof(gm_tjg6nvivto1u9, f4) == 56 && offsetof(gm_tjg6nvivto1u9, f5) == 72 && offsetof(gm_tjg6nvivto1u9, f6) == 88 && offsetof(gm_tjg6nvivto1u9, f7) == 104 && offsetof(gm_tjg6nvivto1u9, f8) == 120, "layout of `(string, u64, string, string, string, []u8, string, string, u64)`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlk7da9ssbu7q) == 32 && _Alignof(gm_tlk7da9ssbu7q) == 8 && offsetof(gm_tlk7da9ssbu7q, f0) == 0 && offsetof(gm_tlk7da9ssbu7q, f1) == 16 && offsetof(gm_tlk7da9ssbu7q, f2) == 24, "layout of `(string, u64, i32)`");
typedef struct { gm_tep2drsl9408b f0; gm_th2lqbo77pvsc f1; } gm_tnau36onunklf;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_toq7erhpb1d2e) == 256 && _Alignof(gm_toq7erhpb1d2e) == 8 && offsetof(gm_toq7erhpb1d2e, f0) == 0 && offsetof(gm_toq7erhpb1d2e, f1) == 16 && offsetof(gm_toq7erhpb1d2e, f2) == 32 && offsetof(gm_toq7erhpb1d2e, f3) == 40 && offsetof(gm_toq7erhpb1d2e, f4) == 56 && offsetof(gm_toq7erhpb1d2e, f5) == 72 && offsetof(gm_toq7erhpb1d2e, f6) == 88 && offsetof(gm_toq7erhpb1d2e, f7) == 104 && offsetof(gm_toq7erhpb1d2e, f8) == 120 && offsetof(gm_toq7erhpb1d2e, f9) == 136 && offsetof(gm_toq7erhpb1d2e, f10) == 144 && offsetof(gm_toq7erhpb1d2e, f11) == 160 && offsetof(gm_toq7erhpb1d2e, f12) == 176 && offsetof(gm_toq7erhpb1d2e, f13) == 192 && offsetof(gm_toq7erhpb1d2e, f14) == 208 && offsetof(gm_toq7erhpb1d2e, f15) == 224 && offsetof(gm_toq7erhpb1d2e, f16) == 240, "layout of `([]u8, []u8, u64, string, string, string, []u8, []u8, []u8, u64, string, string, string, []u8, []u8, []u8, []u8)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
_Static_assert(sizeof(gm_tpbrs0bkofuth) == 112 && _Alignof(gm_tpbrs0bkofuth) == 8 && offsetof(gm_tpbrs0bkofuth, f0) == 0 && offsetof(gm_tpbrs0bkofuth, f1) == 16 && offsetof(gm_tpbrs0bkofuth, f2) == 24 && offsetof(gm_tpbrs0bkofuth, f3) == 40 && offsetof(gm_tpbrs0bkofuth, f4) == 56 && offsetof(gm_tpbrs0bkofuth, f5) == 72 && offsetof(gm_tpbrs0bkofuth, f6) == 88 && offsetof(gm_tpbrs0bkofuth, f7) == 96, "layout of `(string, u64, string, string, string, string, u64, string)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tt4j8kuecfafr) == 144 && _Alignof(gm_tt4j8kuecfafr) == 8 && offsetof(gm_tt4j8kuecfafr, f0) == 0 && offsetof(gm_tt4j8kuecfafr, f1) == 16 && offsetof(gm_tt4j8kuecfafr, f2) == 32 && offsetof(gm_tt4j8kuecfafr, f3) == 48 && offsetof(gm_tt4j8kuecfafr, f4) == 64 && offsetof(gm_tt4j8kuecfafr, f5) == 80 && offsetof(gm_tt4j8kuecfafr, f6) == 96 && offsetof(gm_tt4j8kuecfafr, f7) == 104 && offsetof(gm_tt4j8kuecfafr, f8) == 120 && offsetof(gm_tt4j8kuecfafr, f9) == 136, "layout of `(string, string, string, string, string, string, u32, string, string, u32)`");
_Static_assert(sizeof(gm_tu8mt0h8clq1l) == 56 && _Alignof(gm_tu8mt0h8clq1l) == 8 && offsetof(gm_tu8mt0h8clq1l, f0) == 0 && offsetof(gm_tu8mt0h8clq1l, f1) == 16 && offsetof(gm_tu8mt0h8clq1l, f2) == 32 && offsetof(gm_tu8mt0h8clq1l, f3) == 40, "layout of `(string, string, u32, string)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9geo0l9gngfn) == 32 && _Alignof(gm_t9geo0l9gngfn) == 8 && offsetof(gm_t9geo0l9gngfn, f0) == 0 && offsetof(gm_t9geo0l9gngfn, f1) == 24, "layout of `?TempPiece`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; gm_tnau36onunklf v1; } f1; } gm_teegpca3bk79b;
_Static_assert(sizeof(gm_tflpan0ud4tim) == 28 && _Alignof(gm_tflpan0ud4tim) == 4 && offsetof(gm_tflpan0ud4tim, f0) == 0 && offsetof(gm_tflpan0ud4tim, f1) == 24, "layout of `?FmtSpec`");
_Static_assert(sizeof(gm_tnau36onunklf) == 48 && _Alignof(gm_tnau36onunklf) == 8 && offsetof(gm_tnau36onunklf, f0) == 0 && offsetof(gm_tnau36onunklf, f1) == 24, "layout of `(v: ValTy, direct: ?string)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_teegpca3bk79b) == 56 && _Alignof(gm_teegpca3bk79b) == 8 && offsetof(gm_teegpca3bk79b, f0) == 0 && offsetof(gm_teegpca3bk79b, f1) == 8, "layout of `WriteTo`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
struct gm_fr_addr_of_hiaqkn0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_adt_def_ni54he1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_bits_get_gmqvhi0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_bits_pack_ttermrr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_bits_set_eqvtegv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_blob_d5apnrc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_blob_over_qs5fusr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_bound_n4p0vpb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_outside_q01mr8i {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* covered */
    gm_trqfvmjtt4sij l17; /* stores */
    gm_b0_1 ret;
};
struct gm_fr_coerce_value_ducritf {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_emit_buf_mqofii1 {
    gm_frame h;
    gm_teegpca3bk79b l1; /* sink */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_emit_to_o7js99p {
    gm_frame h;
    gm_teegpca3bk79b l1; /* sink */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6b65ik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jjs3scl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_find_with_gcs5p4r {
    gm_frame h;
    uint32_t l1; /* label */
    gm_t710lsrq1m8gk l2; /* w */
    uint8_t ret;
};
struct gm_fr_float_lit_26lt2tp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gdv4o16 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_okfag60 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_0017fui {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0gjpujh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0lf0h5l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_12uv53h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1amnh72 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_29fhse4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2hfpta7 {
    gm_frame h;
    gm_tjg6nvivto1u9 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tjg6nvivto1u9 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2m0cdvi {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3vtl3te {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4c9sg76 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4g0prpe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4un19t8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5b908c8 {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_66ttjd0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6df13cd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7lru3c2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7peeku4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8933odu {
    gm_frame h;
    gm_t01jnheg9lqvd l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t01jnheg9lqvd l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8b7o2bp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8sjjbd9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bd99ob7 {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bt0erq9 {
    gm_frame h;
    gm_toq7erhpb1d2e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_toq7erhpb1d2e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c2otgb4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ccpi8au {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_clarrev {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dp1qh3k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_elsuh0a {
    gm_frame h;
    gm_tpbrs0bkofuth l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tpbrs0bkofuth l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fsfutor {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gjujk09 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h6b2ofo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_he684oe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ij8u0vj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ik8h77v {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tif3elqk01bdl l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j1qjusd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jb7bqji {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jq3k246 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k2g0ike {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mgf81l5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mh7gc0u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mhned9o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n0794r2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nptl5af {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p7hqke6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p7tc45u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qr5c6un {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rvm8ibl {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sa1940l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t9gsufu {
    gm_frame h;
    gm_tjftu5su6fh8e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tjftu5su6fh8e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tbdgr6h {
    gm_frame h;
    gm_t89vrq05jgle5 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t89vrq05jgle5 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tcr2jpk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tg9hp73 {
    gm_frame h;
    gm_tu8mt0h8clq1l l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tu8mt0h8clq1l l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tt4n370 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ucmo647 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vjen3ng {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_voj344o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vr86ufh {
    gm_frame h;
    gm_t8mpn05efspp6 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t8mpn05efspp6 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_global_addr_fili22i {
    gm_frame h;
    gm_tq7qjpkmqmc5s l4; /* gl */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_index_check_m2p0u1k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_lit_ai37456 {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* v */
    gm_b16_8 ret;
};
struct gm_fr_int_ty_7nma1b7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_temp_rrllgdr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_with_label_rskslsd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
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
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_3dc3bmf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_3dvovo0 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_46ha41j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_982sjai {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_agpuven {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_amf91ik {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* args */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_aoq4lf0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_cf5thm0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_f1jrb6n {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_fch1rvr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_g6msml8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_glh29gu {
    gm_frame h;
    gm_tj4tbafenpsjm l1; /* args */
    gm_tj4tbafenpsjm l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_gnj41u6 {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_h1q98vm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_hg1enfq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_jj5gp4g {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* args */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_jrb37qc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_k4dha91 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_lhoje7n {
    gm_frame h;
    gm_tq3m5qgvkpje8 l1; /* args */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_lot5c36 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_pu91mqj {
    gm_frame h;
    gm_tg1t0c5hkovn5 l1; /* args */
    gm_tg1t0c5hkovn5 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_q8b376p {
    gm_frame h;
    gm_tt4j8kuecfafr l1; /* args */
    gm_tt4j8kuecfafr l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_qch21ro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_qfq4ah7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_qjbq226 {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* args */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_qqgq3om {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_qqk2cu4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_qv8qu4r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_rulhdfs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_sgpievq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_sm7hlk3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_t20u0i3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_ubknr7g {
    gm_frame h;
    gm_t586m754bse2v l1; /* args */
    gm_t586m754bse2v l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_uc25pl5 {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* args */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_um0rj1a {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_uu8i6ue {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_vddfrtf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_26fbt7s {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d7fqe {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_0hebde7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_none_v2s8s0l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_res_riv19po {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_slice_sa50efe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_some_9aqq928 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_variant_srnk8ha {
    gm_frame h;
    gm_tckdobunaulee l7; /* variant */
    gm_trqfvmjtt4sij l9; /* parts */
    gm_b16_8 ret;
};
struct gm_fr_mask_epjl3to {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_opt_present_joptd3k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_type_plain_sanetd4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_p5tbf4u {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u0rp7b1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
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
struct gm_fr_regrow_gb34qsp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r9l0iqd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_res_is_err_11p8qlp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_spec_literal_lmkpbc1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_split_u128_vrmr6en {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    gm_t793vlgpa3b88 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_store_at7m90t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_target_ret_q7qjs0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_piece_572g74q {
    gm_frame h;
    gm_t9geo0l9gngfn ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_unwind_call_4k8ulav {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_uses_effect_7j7sn5a {
    gm_frame h;
    void * l0; /* prog */
    void * l2; /* effect_ */
    gm_tsth9qu1dv0g3 l4; /* callee */
    uint8_t ret;
};
struct gm_fr_view_addr_nj9md2j {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_write_table_values_s1u867r {
    gm_frame h;
    gm_teegpca3bk79b l2; /* sink */
    gm_trqfvmjtt4sij l7; /* ptrs */
    gm_trqfvmjtt4sij l8; /* descs */
    gm_trqfvmjtt4sij l9; /* strs */
    gm_tddtjfggdat0d l28; /* sig */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_write_values_lfh31jc {
    gm_frame h;
    gm_teegpca3bk79b l2; /* sink */
    gm_tddtjfggdat0d l16; /* sig */
    gm_t5o0tpv5jka2l ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq988v99hgkn(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqau1qc90fpr(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqd5a8dm2ae7(const uint8_t *a, const uint8_t *b);
inline bool gm_eqfavatugqhv(const uint8_t *a, const uint8_t *b);
inline bool gm_eqfji27tihfo(const uint8_t *a, const uint8_t *b);
inline bool gm_eqhcfklnokct(const uint8_t *a, const uint8_t *b);
inline bool gm_eqssuf05leel(const uint8_t *a, const uint8_t *b);
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_get_gmqvhi0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_pack_ttermrr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_raw_e0qg9mb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_set_eqvtegv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_value_u15lfan(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_d5apnrc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_over_qs5fusr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_bound_n4p0vpb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_clear_outside_q01mr8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_coerce_value_ducritf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tmn7n0n9raou5 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_emit_buf_mqofii1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_emit_to_o7js99p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6b65ik7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjs3scl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_find_with_gcs5p4r(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_lit_26lt2tp(void *env, gm_node *cx, gm_node *inst, uint16_t a0, double a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_gdv4o16(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_okfag60(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_fn_ptr_type_plain_bi8eu4l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_0017fui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0g25qf5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_0lf0h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_12uv53h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_13htpf7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_29fhse4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2har1kl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_2hfpta7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_2m0cdvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4c9sg76(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_4g0prpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4l7juhh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_4lbrh5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5b908c8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_66ttjd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_68dp3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_6df13cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7peeku4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8933odu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8b7o2bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8sjjbd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ag0n4rn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_bd99ob7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bdj7ou7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_boag3qs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_bt0erq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c2otgb4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ccpi8au(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_clarrev(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dp1qh3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_elsuh0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fja4j2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_flhcmdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_gjujk09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h349qsj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_he684oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ij8u0vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ik8h77v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_j1qjusd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_j9cgp0l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_jb7bqji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jq3k246(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_k2g0ike(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mgf81l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_mh7gc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mhned9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_n0794r2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_na0trrv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nptl5af(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_p7hqke6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_p7tc45u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qon8vgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rlukvu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rq0djtj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rvm8ibl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_sa1940l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_t2mrh76(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t9gsufu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_tbdgr6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_tcr2jpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_tg9hp73(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_tt4n370(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uanb37d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ucmo647(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vjen3ng(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_voj344o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vr86ufh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_global_addr_fili22i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_global_index_nhjk80t(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_global_tag_gs7s3p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_index_check_m2p0u1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_temp_rrllgdr(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_with_label_rskslsd(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_1mlocql(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3dc3bmf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3dvovo0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_46ha41j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4sol8m9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_63um0g9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6ivcvs8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6jk4j49(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7afcl2l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8uttu8h(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9379f24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_982sjai(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9fi0qm1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_agpuven(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_amf91ik(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ao39327(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_aoq4lf0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_cf5thm0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_d96evvt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_f1jrb6n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_fa323qa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_fch1rvr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_glh29gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gnj41u6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_h1q98vm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_hg1enfq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_hogigjq(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_j0q4pgj(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jj5gp4g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jrb37qc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_k4dha91(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_l87n6gd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_lhoje7n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_lot5c36(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_mde61j0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_mfcn16l(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ogkr5eg(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_onfij02(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_p1tolr4(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_pu91mqj(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_q8b376p(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qch21ro(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qfq4ah7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qjbq226(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqgq3om(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qu88946(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qv8qu4r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ro0flsf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rosnm2q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_rulhdfs(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_saljq9o(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_sgpievq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_slpvrc7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_sm7hlk3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_t20u0i3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ubknr7g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uc25pl5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_um0rj1a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_vddfrtf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) void gm_f_list_26fbt7s(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d7fqe(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_res_riv19po(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mask_epjl3to(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_present_joptd3k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p5tbf4u(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u0rp7b1(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gb34qsp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_is_err_11p8qlp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_literal_lmkpbc1(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t793vlgpa3b88 gm_f_split_u128_vrmr6en_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_store_at7m90t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_temp_piece_572g74q(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t9geo0l9gngfn gm_f_temp_piece_572g74q_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_uses_effect_7j7sn5a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_view_addr_nj9md2j(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_view_addr_nj9md2j_c(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_write_table_values_s1u867r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_write_values_lfh31jc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0s3f0v5p7674[27];
extern const uint8_t gm_s1ie3s345dm40[27];
extern const uint8_t gm_s1uq73uineubo[35];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s3svhagp75tqj[38];
extern const uint8_t gm_s3t3r41giacns[14];
extern const uint8_t gm_s43jqu37ghtoq[19];
extern const uint8_t gm_s4a6h3o2n8s33[94];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4jrltvkedvou[31];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4renf991vs7d[44];
extern const uint8_t gm_s5ie9tn1qnf6v[9];
extern const uint8_t gm_s5nt88lj0viqk[4];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s5s65385he1ib[24];
extern const uint8_t gm_s77n5hibks91d[30];
extern const uint8_t gm_s7kegmslujo7o[14];
extern const uint8_t gm_s84jtr743uu26[43];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s8j05biehb1ag[93];
extern const uint8_t gm_s8tfl6snglqci[19];
extern const uint8_t gm_s93ulqqrv8rm5[39];
extern const uint8_t gm_sa1d17thk38ms[10];
extern const uint8_t gm_sagfhrncf3ga2[11];
extern const uint8_t gm_sbeptfoja7ub2[37];
extern const uint8_t gm_sbvi33ihvufj1[20];
extern const uint8_t gm_sc48cegnkinfm[12];
extern const uint8_t gm_scfb66ogf0spa[19];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_scjbu6gm2hrhf[8];
extern const uint8_t gm_sddkt83bdvunc[57];
extern const uint8_t gm_sdgiuincq4rs9[34];
extern const uint8_t gm_sdkba1tit6s52[17];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_sfm11uv2bn7us[29];
extern const uint8_t gm_sfrlts7iu6kt3[23];
extern const uint8_t gm_sg3acic2chh26[34];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5n0glcljcci[33];
extern const uint8_t gm_sgal4nbdtbtvo[18];
extern const uint8_t gm_sgme8q1t414rd[19];
extern const uint8_t gm_sgmjl48sjjf69[22];
extern const uint8_t gm_sgovtffcl93pk[15];
extern const uint8_t gm_sh50kc23mi3vj[36];
extern const uint8_t gm_shsqur3mdh4ln[41];
extern const uint8_t gm_si5ao9la9pflk[19];
extern const uint8_t gm_si6va5n9qkjau[185];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sidk0bupbfuvu[11];
extern const uint8_t gm_sigjilgl161c6[5];
extern const uint8_t gm_silop0tio8u7e[105];
extern const uint8_t gm_sj6oj7rmit9sk[93];
extern const uint8_t gm_sj718thiqudru[10];
extern const uint8_t gm_sjg90l7q4kg8o[56];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sk7tgk6pi62vu[14];
extern const uint8_t gm_skmsgkj4jeo7n[6];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sktpp3fjerrpt[32];
extern const uint8_t gm_sl4ll6nrpr5rj[33];
extern const uint8_t gm_sl6fvbluu7spr[26];
extern const uint8_t gm_slcgg5l8i9236[50];
extern const uint8_t gm_slesj9fnpbent[24];
extern const uint8_t gm_sm9ij4qld7msj[25];
extern const uint8_t gm_smarqt2mi26ea[7];
extern const uint8_t gm_smiaoqkobhu81[54];
extern const uint8_t gm_smidq0067e6d6[7];
extern const uint8_t gm_smr1r7ugehv5r[19];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_sncti78voitis[76];
extern const uint8_t gm_snlpcspgan3ri[16];
extern const uint8_t gm_snm3509s12ig4[26];
extern const uint8_t gm_snpp5o7gj4ij7[10];
extern const uint8_t gm_so2hmicihnte8[25];
extern const uint8_t gm_so3t10be8g68d[10];
extern const uint8_t gm_so4pc0b7vpiib[106];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_spe8vb31u5qu0[5];
extern const uint8_t gm_spm6tgoorr19q[29];
extern const uint8_t gm_spnnqfjs7id3o[13];
extern const uint8_t gm_sq3oimi1i4re9[27];
extern const uint8_t gm_sq52gnep1cqbp[45];
extern const uint8_t gm_sq76oorqdc8ip[20];
extern const uint8_t gm_sqf3cuedcn484[21];
extern const uint8_t gm_sr3mniof9fusf[12];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srqlea3g58va7[25];
extern const uint8_t gm_ss27lm4pmhcij[45];
extern const uint8_t gm_ss9ss6njjac9i[45];
extern const uint8_t gm_sshlmlg0kjl8a[19];
extern const uint8_t gm_ssor6j0m3frue[14];
extern const uint8_t gm_st2e24ck2icsi[26];
extern const uint8_t gm_st3c9ev3jkdrf[32];
extern const uint8_t gm_statjrbfbnhar[11];
extern const uint8_t gm_staulpjprqbul[155];
extern const uint8_t gm_stbuagagnsakl[37];
extern const uint8_t gm_stfpae614bc1t[35];
extern const uint8_t gm_stol2i9gi9a35[18];
extern const uint8_t gm_stuj0mv5nl55t[4];
extern const uint8_t gm_suf3bl270p4gs[56];
extern const uint8_t gm_sugv53k5dm8d7[24];
extern const uint8_t gm_svp6nj851nh2i[29];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_tmpl gm_tpl0ke3r8fiia4r;
extern const gm_tmpl gm_tpl1buo4rn7srlb;
extern const gm_tmpl gm_tpl1c171lb88cdd;
extern const gm_tmpl gm_tpl2utldc2k4g4b;
extern const gm_tmpl gm_tpl3072uo9s1dtn;
extern const gm_tmpl gm_tpl3lpe5kp3g9ir;
extern const gm_tmpl gm_tpl3tcah2c39gjk;
extern const gm_tmpl gm_tpl409di1gnsfit;
extern const gm_tmpl gm_tpl4561ar79q0c0;
extern const gm_tmpl gm_tpl4c2mfb3cngak;
extern const gm_tmpl gm_tpl4upfck3qu2er;
extern const gm_tmpl gm_tpl5jffirbo3a3v;
extern const gm_tmpl gm_tpl64qrh372lslh;
extern const gm_tmpl gm_tpl6bjoajp2fh2n;
extern const gm_tmpl gm_tpl7bbfeces83t7;
extern const gm_tmpl gm_tpl7oj2vfshts60;
extern const gm_tmpl gm_tpl8g9lchslmbjf;
extern const gm_tmpl gm_tpl8geisveh2vug;
extern const gm_tmpl gm_tpl8urde81v7030;
extern const gm_tmpl gm_tpl98etevndg0cj;
extern const gm_tmpl gm_tpl9bim66fl3b37;
extern const gm_tmpl gm_tpl9ne0ajfec16n;
extern const gm_tmpl gm_tpl9q5j6bisf2fa;
extern const gm_tmpl gm_tpl9rm8d4b25q2c;
extern const gm_tmpl gm_tpla15qb3qedeuc;
extern const gm_tmpl gm_tpla1fscog446dv;
extern const gm_tmpl gm_tpla6sajpp07b9l;
extern const gm_tmpl gm_tpla7fg14q9gk8f;
extern const gm_tmpl gm_tplak7vr9q0b05e;
extern const gm_tmpl gm_tplastf5j9ge45b;
extern const gm_tmpl gm_tplb5ldvki5nntc;
extern const gm_tmpl gm_tplbaqni0b3abqt;
extern const gm_tmpl gm_tplbfdkthp45ai4;
extern const gm_tmpl gm_tplbgu1ah9e6d87;
extern const gm_tmpl gm_tplbo9rmalpf2vj;
extern const gm_tmpl gm_tplchb0p7o483ge;
extern const gm_tmpl gm_tpldq0sm30o8mph;
extern const gm_tmpl gm_tpldtm53npup7ha;
extern const gm_tmpl gm_tpldvh3e1jcen1u;
extern const gm_tmpl gm_tple41tg2t64d1i;
extern const gm_tmpl gm_tple6jpca094i2j;
extern const gm_tmpl gm_tplf3en286s8fa5;
extern const gm_tmpl gm_tplfr9bfp6t7vb7;
extern const gm_tmpl gm_tplgclcdp539fpt;
extern const gm_tmpl gm_tplgv9va17kt7ll;
extern const gm_tmpl gm_tplgvbr3vejj74c;
extern const gm_tmpl gm_tplhlt4sff0mrem;
extern const gm_tmpl gm_tplhmairn7oibac;
extern const gm_tmpl gm_tplibgopbmor3p6;
extern const gm_tmpl gm_tplicmhpq9e6sbc;
extern const gm_tmpl gm_tplit8jkrg5bt3p;
extern const gm_tmpl gm_tplj6m5ttjte39q;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplk5t5jhqqvnff;
extern const gm_tmpl gm_tplkmrsfbsle095;
extern const gm_tmpl gm_tpll6udkhd3c609;
extern const gm_tmpl gm_tpll9g0ell7ocpk;
extern const gm_tmpl gm_tplm02t0g7r3hdp;
extern const gm_tmpl gm_tplm3f7baid20me;
extern const gm_tmpl gm_tplmaqqdmni75bf;
extern const gm_tmpl gm_tplmemqaoor78ph;
extern const gm_tmpl gm_tpln4hlddpv31vi;
extern const gm_tmpl gm_tplneoolql7n1n7;
extern const gm_tmpl gm_tplo2fstigc8bcf;
extern const gm_tmpl gm_tplodcipjau6eqr;
extern const gm_tmpl gm_tplpog54do9oh2a;
extern const gm_tmpl gm_tplq3ra7ik9ej59;
extern const gm_tmpl gm_tplqar620s2vpg7;
extern const gm_tmpl gm_tplqbbl1qig0e0s;
extern const gm_tmpl gm_tplqhu7t9iljb1t;
extern const gm_tmpl gm_tplqt9e8rihgogv;
extern const gm_tmpl gm_tplr42r2lp0p4d1;
extern const gm_tmpl gm_tplrbicm1oan2em;
extern const gm_tmpl gm_tplrcurrnquacuf;
extern const gm_tmpl gm_tplrffp652tkgo2;
extern const gm_tmpl gm_tplrp7ukmgscavj;
extern const gm_tmpl gm_tplt6nq68u4co32;
extern const gm_tmpl gm_tpltfcdqmjl1tnb;
extern const gm_tmpl gm_tplu272587ugnvi;
extern const gm_tmpl gm_tplu49icuspir05;
extern const gm_tmpl gm_tplu7gq2cm8aanh;
extern const gm_tmpl gm_tplulq1qidn3nog;
extern const gm_tmpl gm_tplv0flrfdqimb1;
extern const gm_tmpl gm_tplvj9n0kinsqlj;
extern const gm_tmpl gm_tplvpjd91cp2l7g;
extern const gm_tmpl gm_tplvqa9kv7qt4oc;
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
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq988v99hgkn(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq0iqiubfm7e(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqau1qc90fpr(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq9m6mvtmgde(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eqfavatugqhv(a + 8, b + 8);
    return true;
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqd5a8dm2ae7(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eqfji27tihfo(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqfavatugqhv(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqssuf05leel(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eq988v99hgkn(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eq0iqiubfm7e(a + 8, b + 8);
    return true;
}
inline bool gm_eqfji27tihfo(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    for (uintptr_t i = 0; i < n; i++) if (!gm_eqau1qc90fpr(pa + i * 40, pb + i * 40)) return false;
    return true;
}
inline bool gm_eqhcfklnokct(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqvs1pd373b1(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eq9oqmmiu8qc(a + 8, b + 8);
    return true;
}
inline bool gm_eqssuf05leel(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq9m6mvtmgde(a + 0, b + 0)) return false;
    if (!gm_eq0iqiubfm7e(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eqb2nno7n67r(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_def_ni54he1 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    void * t6 = gm_f_adt_of_gej99vn(t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
    goto gm_fret;
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
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
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    F.l2 = ((uint8_t)0);
    void * t2 = gm_l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_d5apnrc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_blob_d5apnrc F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* n */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    void * t5 = gm_l0;
    gm_b16_8 t6 = gm_l2;
    gm_b16_8 t7 = gm_l2;
    gm_t6j6smj7qoctu t8;
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 16), t7);
    gm_b0_1 t9 = gm_f_linef_cf5thm0((void *)NULL, cx, NULL, t5, &t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t10 = gm_l2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_over_qs5fusr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_blob_over_qs5fusr F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* parts */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t4;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_b16_8 t7 = gm_l3;
    gm_b16_8 t8 = gm_l2;
    gm_b0_1 t9 = gm_f_clear_outside_q01mr8i((void *)NULL, cx, NULL, t5, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t10 = gm_l3;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_unwind_2hji4bl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 216));
    if (t3) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t4 = gm_l0;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_smr5ddngcjuao));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(18));
    gm_b0_1 t6 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = gm_l0;
    gm_b0_1 t8 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6b65ik7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6b65ik7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjs3scl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jjs3scl F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_find_with_gcs5p4r(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_with_gcs5p4r F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)59ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t4) + 8) + 8), 40);
            uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
            uint32_t t8 = F.l1;
            uint8_t t9 = ((t7 == t8));
            if (t9) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t10 = gm_l0;
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_gdv4o16, (void *)&F.h, cx});
    uint8_t t12 = gm_f_any_child_ffc8h1k(t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
        void * t5 = gm_l0;
        gm_closure t6 = t4;
        gm_node *ccx7 = t6.cx;
        void * t8 = t6.code;
        uint8_t t9;
        if (GM_PTR_TAGGED(t8)) {
            t9 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t8))(t5);
        } else {
            t9 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t8)(t6.env, ccx7, NULL, t5);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t9);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_gdv4o16(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_gdv4o16 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_find_with_gcs5p4r *)((&F.h)->up))->l1));
    uint8_t t4 = gm_f_find_with_gcs5p4r(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_okfag60(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_okfag60 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_uses_effect_7j7sn5a *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_uses_effect_7j7sn5a *)((&F.h)->up))->l2));
    uint8_t t5 = gm_f_uses_effect_7j7sn5a((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_0017fui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0017fui F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4jrltvkedvou));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldq0sm30o8mph), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0gjpujh F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl6fvbluu7spr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4upfck3qu2er), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_0lf0h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0lf0h5l F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smarqt2mi26ea));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm02t0g7r3hdp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_12uv53h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_12uv53h F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5nt88lj0viqk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbgu1ah9e6d87), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_1amnh72 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_29fhse4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_29fhse4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm9ij4qld7msj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla15qb3qedeuc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_2hfpta7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2hfpta7 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so4pc0b7vpiib));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(105));
    gm_tjg6nvivto1u9 t3 = GM_LD(gm_tjg6nvivto1u9, ((uint8_t *)a0));
    gm_f_format_t2mrh76((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqar620s2vpg7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_2m0cdvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2m0cdvi F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sddkt83bdvunc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_th2d9c7aju8hp t3 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
    gm_f_format_h349qsj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplmemqaoor78ph), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3vtl3te F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjtp8ip8t28uh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqt9e8rihgogv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4c9sg76(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4c9sg76 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smarqt2mi26ea));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm02t0g7r3hdp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4g0prpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4g0prpe F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so3t10be8g68d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpll6udkhd3c609), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4un19t8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdkba1tit6s52));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla6sajpp07b9l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5b908c8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5b908c8 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgme8q1t414rd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7bbfeces83t7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_66ttjd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_66ttjd0 F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tcc1rq4n1avb2 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa1d17thk38ms));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_j9cgp0l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj6m5ttjte39q), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6df13cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6df13cd F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssor6j0m3frue));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgv9va17kt7ll), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7lru3c2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stol2i9gi9a35));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvj9n0kinsqlj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7peeku4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7peeku4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbvi33ihvufj1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4c2mfb3cngak), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8933odu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8933odu F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ss9ss6njjac9i));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(44));
    gm_t01jnheg9lqvd t3 = GM_LD(gm_t01jnheg9lqvd, ((uint8_t *)a0));
    gm_f_format_uanb37d((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrcurrnquacuf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8b7o2bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8b7o2bp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slesj9fnpbent));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl98etevndg0cj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8sjjbd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8sjjbd9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgovtffcl93pk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7oj2vfshts60), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bd99ob7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bd99ob7 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh50kc23mi3vj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_th2d9c7aju8hp t3 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
    gm_f_format_bdj7ou7((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvqa9kv7qt4oc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_bt0erq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bt0erq9 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si6va5n9qkjau));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(184));
    gm_toq7erhpb1d2e t3 = GM_LD(gm_toq7erhpb1d2e, ((uint8_t *)a0));
    gm_f_format_4lbrh5r((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhmairn7oibac), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c2otgb4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c2otgb4 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snlpcspgan3ri));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(15));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplodcipjau6eqr), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ccpi8au(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ccpi8au F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl4ll6nrpr5rj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_flhcmdb((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple41tg2t64d1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_clarrev(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_clarrev F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t5hdnnftkup6m gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si5ao9la9pflk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
    gm_f_format_0g25qf5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplulq1qidn3nog), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dp1qh3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dp1qh3k F;
    gm_tgbgti82mf879 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ss27lm4pmhcij));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(44));
    gm_tgbgti82mf879 t3 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
    gm_f_format_fja4j2u((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrp7ukmgscavj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_elsuh0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_elsuh0a F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8j05biehb1ag));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(92));
    gm_tpbrs0bkofuth t3 = GM_LD(gm_tpbrs0bkofuth, ((uint8_t *)a0));
    gm_f_format_rq0djtj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbfdkthp45ai4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fsfutor F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdsb9longvn22));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu7gq2cm8aanh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gjujk09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_gjujk09 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sj718thiqudru));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8g9lchslmbjf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h6b2ofo F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stuj0mv5nl55t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf3en286s8fa5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_he684oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_he684oe F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scjbu6gm2hrhf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrbicm1oan2em), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ij8u0vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ij8u0vj F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stfpae614bc1t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplit8jkrg5bt3p), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ik8h77v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ik8h77v F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4renf991vs7d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(43));
    gm_tif3elqk01bdl t3 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
    gm_f_format_rlukvu8((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9ne0ajfec16n), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_j1qjusd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_j1qjusd F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spe8vb31u5qu0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3tcah2c39gjk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jb7bqji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jb7bqji F;
    gm_tctdkl637nfjf gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tctdkl637nfjf gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_statjrbfbnhar));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_tctdkl637nfjf t3 = GM_LD(gm_tctdkl637nfjf, ((uint8_t *)a0));
    gm_f_format_2har1kl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpog54do9oh2a), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jq3k246(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jq3k246 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smidq0067e6d6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3lpe5kp3g9ir), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_k2g0ike(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_k2g0ike F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tcc1rq4n1avb2 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snpp5o7gj4ij7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_j9cgp0l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu49icuspir05), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mgf81l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_mgf81l5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sigjilgl161c6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplak7vr9q0b05e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mh7gc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mh7gc0u F;
    gm_tommf4j9upomc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7kegmslujo7o));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(13));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_4l7juhh((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbaqni0b3abqt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mhned9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_mhned9o F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stbuagagnsakl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0ke3r8fiia4r), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n0794r2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n0794r2 F;
    gm_tommf4j9upomc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spnnqfjs7id3o));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_4l7juhh((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplgclcdp539fpt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nptl5af(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nptl5af F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sj6oj7rmit9sk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(92));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4561ar79q0c0), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_p7hqke6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_p7hqke6 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0s3f0v5p7674));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5jffirbo3a3v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p7tc45u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p7tc45u F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5s65385he1ib));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplicmhpq9e6sbc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_qr5c6un F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skmsgkj4jeo7n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk5t5jhqqvnff), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rvm8ibl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rvm8ibl F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh50kc23mi3vj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_th2d9c7aju8hp t3 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
    gm_f_format_qon8vgm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvqa9kv7qt4oc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sa1940l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_sa1940l F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t9gsufu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t9gsufu F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_st3c9ev3jkdrf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(31));
    gm_tjftu5su6fh8e t3 = GM_LD(gm_tjftu5su6fh8e, ((uint8_t *)a0));
    gm_f_format_boag3qs((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkmrsfbsle095), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tbdgr6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tbdgr6h F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgme8q1t414rd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t89vrq05jgle5 t3 = GM_LD(gm_t89vrq05jgle5, ((uint8_t *)a0));
    gm_f_format_13htpf7((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7bbfeces83t7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tcr2jpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tcr2jpk F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgmjl48sjjf69));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(21));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqhu7t9iljb1t), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tg9hp73(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tg9hp73 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slcgg5l8i9236));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(49));
    gm_tu8mt0h8clq1l t3 = GM_LD(gm_tu8mt0h8clq1l, ((uint8_t *)a0));
    gm_f_format_ag0n4rn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8urde81v7030), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tt4n370(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tt4n370 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sc48cegnkinfm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhlt4sff0mrem), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ucmo647(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ucmo647 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sshlmlg0kjl8a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2utldc2k4g4b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vjen3ng(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vjen3ng F;
    gm_t2mvr8fpgjkg6 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t2mvr8fpgjkg6 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sagfhrncf3ga2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t2mvr8fpgjkg6 t3 = GM_LD(gm_t2mvr8fpgjkg6, ((uint8_t *)a0));
    gm_f_format_na0trrv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldvh3e1jcen1u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_voj344o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_voj344o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3t3r41giacns));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(13));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvpjd91cp2l7g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vr86ufh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vr86ufh F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_staulpjprqbul));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(154));
    gm_t8mpn05efspp6 t3 = GM_LD(gm_t8mpn05efspp6, ((uint8_t *)a0));
    gm_f_format_68dp3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfr9bfp6t7vb7), t2);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 152));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 465, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 152), t7);
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 280));
    uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 466, 5);
    uint32_t t13 = r11;
    GM_ST(uint32_t, (((uint8_t *)t8) + 280), t13);
    void * t14 = gm_l0;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 152));
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_index_check_m2p0u1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_index_check_m2p0u1k F;
    void * gm_l0; /* fg */
    uint8_t gm_l1; /* checked */
    gm_b16_8 gm_l2; /* i */
    gm_b16_8 gm_l3; /* len */
    gm_b16_8 gm_l4; /* at */
    GM_STACK_CHECK(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t t2 = gm_l1;
    if (t2) {
        void * t3 = gm_l0;
        gm_b16_8 t4 = gm_l2;
        gm_b16_8 t5 = gm_l3;
        gm_b16_8 t6 = gm_l2;
        gm_b16_8 t7 = gm_l3;
        gm_b16_8 t8 = gm_l4;
        gm_tud7oovfvnf43 t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t5);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 32), t6);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 48), t7);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 64), t8);
        gm_b0_1 t10 = gm_f_linef_gnj41u6((void *)NULL, cx, NULL, t3, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ty_7nma1b7 F;
    void * gm_l0; /* ty */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    uint8_t t5 = gm_l1;
    uint16_t t6 = gm_l2;
    gm_t88rcucg1b7uk t7 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 104);
    gm_b16_8 t6 = gm_l1;
    gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
    gm_l2 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmok80ho8hh01, 449, 5);
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 104);
    gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
    gm_l3 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmok80ho8hh01, 450, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3dc3bmf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3dc3bmf F;
    void * gm_l0; /* fg */
    gm_t3877pk7lv748 gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suf3bl270p4gs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_b0_1 t4 = gm_f_linef_mde61j0((void *)NULL, cx, NULL, t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tplchb0p7o483ge), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3dvovo0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3dvovo0 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s77n5hibks91d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_b0_1 t4 = gm_f_linef_rosnm2q((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl64qrh372lslh), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_46ha41j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_46ha41j F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sidk0bupbfuvu));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl9rm8d4b25q2c), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_982sjai(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_982sjai F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbeptfoja7ub2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_b0_1 t4 = gm_f_linef_onfij02((void *)NULL, cx, NULL, t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpl1buo4rn7srlb), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_agpuven(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_agpuven F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s84jtr743uu26));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
    gm_b0_1 t4 = gm_f_linef_fa323qa((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpldtm53npup7ha), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_amf91ik(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_amf91ik F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st2e24ck2icsi));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_b0_1 t4 = gm_f_linef_4sol8m9((void *)NULL, cx, NULL, t2, &(*(gm_t0uuhaljsf410 *)((uint8_t *)a1)), ((void *)&gm_tpltfcdqmjl1tnb), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_aoq4lf0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_aoq4lf0 F;
    void * gm_l0; /* fg */
    gm_tob1snuii5bme gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tob1snuii5bme gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shsqur3mdh4ln));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_b0_1 t4 = gm_f_linef_63um0g9((void *)NULL, cx, NULL, t2, &(*(gm_tob1snuii5bme *)((uint8_t *)a1)), ((void *)&gm_tplt6nq68u4co32), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_cf5thm0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_cf5thm0 F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq3oimi1i4re9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplu272587ugnvi), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_f1jrb6n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_f1jrb6n F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scfb66ogf0spa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplb5ldvki5nntc), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_fch1rvr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_fch1rvr F;
    void * gm_l0; /* fg */
    gm_t3877pk7lv748 gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s93ulqqrv8rm5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_b0_1 t4 = gm_f_linef_ro0flsf((void *)NULL, cx, NULL, t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tplibgopbmor3p6), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_g6msml8 F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sidk0bupbfuvu));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl9rm8d4b25q2c), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_glh29gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_glh29gu F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srqlea3g58va7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_b0_1 t4 = gm_f_linef_j0q4pgj((void *)NULL, cx, NULL, t2, &(*(gm_tj4tbafenpsjm *)((uint8_t *)a1)), ((void *)&gm_tplneoolql7n1n7), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gnj41u6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_gnj41u6 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smiaoqkobhu81));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_b0_1 t4 = gm_f_linef_l87n6gd((void *)NULL, cx, NULL, t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tplo2fstigc8bcf), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_h1q98vm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_h1q98vm F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sq76oorqdc8ip));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(19));
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplq3ra7ik9ej59), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_hg1enfq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_hg1enfq F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_smr1r7ugehv5r));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(18));
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplv0flrfdqimb1), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jj5gp4g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_jj5gp4g F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg5n0glcljcci));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_b0_1 t4 = gm_f_linef_hogigjq((void *)NULL, cx, NULL, t2, &(*(gm_t0uuhaljsf410 *)((uint8_t *)a1)), ((void *)&gm_tplrffp652tkgo2), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jrb37qc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_jrb37qc F;
    void * gm_l0; /* fg */
    gm_tiilmfd16v3fc gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sktpp3fjerrpt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_b0_1 t4 = gm_f_linef_mfcn16l((void *)NULL, cx, NULL, t2, &(*(gm_tiilmfd16v3fc *)((uint8_t *)a1)), ((void *)&gm_tpl1c171lb88cdd), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_k4dha91(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_k4dha91 F;
    void * gm_l0; /* fg */
    gm_t3877pk7lv748 gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snm3509s12ig4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_b0_1 t4 = gm_f_linef_ro0flsf((void *)NULL, cx, NULL, t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tplmaqqdmni75bf), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_lhoje7n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_lhoje7n F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjg90l7q4kg8o));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_b0_1 t4 = gm_f_linef_1mlocql((void *)NULL, cx, NULL, t2, &(*(gm_tq3m5qgvkpje8 *)((uint8_t *)a1)), ((void *)&gm_tplr42r2lp0p4d1), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_lot5c36(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_lot5c36 F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqf3cuedcn484));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_b0_1 t4 = gm_f_linef_fa323qa((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl9bim66fl3b37), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_pu91mqj(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_pu91mqj F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sncti78voitis));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(75));
    gm_b0_1 t4 = gm_f_linef_ogkr5eg((void *)NULL, cx, NULL, t2, &(*(gm_tg1t0c5hkovn5 *)((uint8_t *)a1)), ((void *)&gm_tpl6bjoajp2fh2n), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_q8b376p(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_q8b376p F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_silop0tio8u7e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(104));
    gm_b0_1 t4 = gm_f_linef_6ivcvs8((void *)NULL, cx, NULL, t2, &(*(gm_tt4j8kuecfafr *)((uint8_t *)a1)), ((void *)&gm_tpl8geisveh2vug), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qch21ro(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_qch21ro F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sq76oorqdc8ip));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(19));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplq3ra7ik9ej59), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qfq4ah7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qfq4ah7 F;
    void * gm_l0; /* fg */
    gm_t5g5vgung6h6s gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t5g5vgung6h6s gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfm11uv2bn7us));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_b0_1 t4 = gm_f_linef_qu88946((void *)NULL, cx, NULL, t2, &(*(gm_t5g5vgung6h6s *)((uint8_t *)a1)), ((void *)&gm_tpln4hlddpv31vi), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qjbq226(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qjbq226 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdgiuincq4rs9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_b0_1 t4 = gm_f_linef_6jk4j49((void *)NULL, cx, NULL, t2, &(*(gm_t0uuhaljsf410 *)((uint8_t *)a1)), ((void *)&gm_tplbo9rmalpf2vj), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqgq3om(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qqgq3om F;
    void * gm_l0; /* fg */
    gm_tiilmfd16v3fc gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so2hmicihnte8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_b0_1 t4 = gm_f_linef_mfcn16l((void *)NULL, cx, NULL, t2, &(*(gm_tiilmfd16v3fc *)((uint8_t *)a1)), ((void *)&gm_tpl409di1gnsfit), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_qqk2cu4 F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_si731m8b0sr6s));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpla1fscog446dv), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qv8qu4r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qv8qu4r F;
    void * gm_l0; /* fg */
    gm_tiilmfd16v3fc gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svp6nj851nh2i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_b0_1 t4 = gm_f_linef_mfcn16l((void *)NULL, cx, NULL, t2, &(*(gm_tiilmfd16v3fc *)((uint8_t *)a1)), ((void *)&gm_tpla7fg14q9gk8f), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_rulhdfs(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_rulhdfs F;
    void * gm_l0; /* fg */
    gm_tiilmfd16v3fc gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3svhagp75tqj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_b0_1 t4 = gm_f_linef_mfcn16l((void *)NULL, cx, NULL, t2, &(*(gm_tiilmfd16v3fc *)((uint8_t *)a1)), ((void *)&gm_tplqbbl1qig0e0s), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_sgpievq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_sgpievq F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sgal4nbdtbtvo));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(17));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplastf5j9ge45b), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_sm7hlk3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_sm7hlk3 F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq52gnep1cqbp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplm3f7baid20me), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_t20u0i3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_t20u0i3 F;
    void * gm_l0; /* fg */
    gm_tlk7da9ssbu7q gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tlk7da9ssbu7q gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1ie3s345dm40));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_b0_1 t4 = gm_f_linef_saljq9o((void *)NULL, cx, NULL, t2, &(*(gm_tlk7da9ssbu7q *)((uint8_t *)a1)), ((void *)&gm_tpl3072uo9s1dtn), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ubknr7g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_ubknr7g F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4a6h3o2n8s33));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(93));
    gm_b0_1 t4 = gm_f_linef_9fi0qm1((void *)NULL, cx, NULL, t2, &(*(gm_t586m754bse2v *)((uint8_t *)a1)), ((void *)&gm_tpll9g0ell7ocpk), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uc25pl5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_uc25pl5 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1uq73uineubo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_b0_1 t4 = gm_f_linef_8uttu8h((void *)NULL, cx, NULL, t2, &(*(gm_t0uuhaljsf410 *)((uint8_t *)a1)), ((void *)&gm_tpl9q5j6bisf2fa), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_um0rj1a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_um0rj1a F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbeptfoja7ub2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_b0_1 t4 = gm_f_linef_p1tolr4((void *)NULL, cx, NULL, t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpl1buo4rn7srlb), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_uu8i6ue F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5ie9tn1qnf6v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tple6jpca094i2j), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_vddfrtf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1) {
    struct gm_fr_linef_vddfrtf F;
    void * gm_l0; /* fg */
    gm_tcc1rq4n1avb2 gm_l1; /* args */
    void * gm_l2; /* out */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tcc1rq4n1avb2 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sg3acic2chh26));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(33));
    gm_b0_1 t5 = gm_f_linef_d96evvt((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplgvbr3vejj74c), t4);
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
inline __attribute__((aligned(4))) void gm_f_list_26fbt7s(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_26fbt7s F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jjs3scl();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d7fqe(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d7fqe F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6b65ik7();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mask_epjl3to(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_mask_epjl3to F;
    uint32_t gm_l0; /* w */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint32_t)128ULL));
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sugv53k5dm8d7));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(23));
        F.ret = t4;
        goto gm_fret;
    }
    uint32_t t5 = gm_l0;
    gm_t88rcucg1b7uk t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t5);
    gm_f_format_mhned9o((void *)&gm_l1, (void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src0gtuh94b1fce, 770, 12);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 8) + 8));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_type_plain_sanetd4 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_fn_ptr_type_plain_bi8eu4l((void *)NULL, cx, NULL, t3, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p5tbf4u(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_p5tbf4u F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_6b65ik7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_gb34qsp(t8, ((uint64_t)1ULL), t10);
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
    gm_t2lqu8jsj511d t18 = gm_l1;
    GM_ST(gm_t2lqu8jsj511d, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u0rp7b1(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u0rp7b1 F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_jjs3scl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_r9l0iqd(t8, ((uint64_t)1ULL), t10);
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
    gm_t2lqu8jsj511d t18 = gm_l1;
    GM_ST(gm_t2lqu8jsj511d, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gb34qsp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gb34qsp F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r9l0iqd F;
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
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
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_b16_8 t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
    gm_t3877pk7lv748 t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), t4);
    gm_f_format_0gjpujh((void *)&gm_l2, (void *)NULL, cx, NULL, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src0gtuh94b1fce, 291, 72);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    gm_b16_8 t2 = gm_l0;
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    gm_f_format_7lru3c2((void *)&gm_l1, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src0gtuh94b1fce, 289, 60);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
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
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_u128_vrmr6en F;
    unsigned __int128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 t2 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    gm_l1 = t2;
    unsigned __int128 t3 = gm_l1;
    unsigned __int128 t4 = (unsigned __int128)(t3 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)));
    unsigned __int128 t5 = gm_l1;
    unsigned __int128 n6 = (unsigned __int128)((uint64_t)64ULL);
    if (n6 >= 128) gm_trap_shift((uint64_t)n6, "u128", gm_src0gtuh94b1fce, 751, 40);
    unsigned __int128 t7 = (unsigned __int128)(t5 >> (int)n6);
    gm_t793vlgpa3b88 t8;
    GM_ST(unsigned __int128, ((uint8_t *)&t8 + 0), t4);
    GM_ST(unsigned __int128, ((uint8_t *)&t8 + 16), t7);
    memmove(gm_out, (((uint8_t *)&t8) + 0), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_store_at7m90t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_store_at7m90t F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* addr */
    gm_b16_8 gm_l3; /* v */
    gm_b16_8 gm_l4; /* c */
    GM_STACK_CHECK(&F, 208u);
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
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t6;
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l4;
    gm_b16_8 t9 = gm_l2;
    gm_b16_8 t10 = gm_l3;
    gm_tfeab279c16rr t11;
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t9);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 32), t10);
    gm_b0_1 t12 = gm_f_linef_f1jrb6n((void *)NULL, cx, NULL, t7, &t11);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_target_ret_q7qjs0g F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* f */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tommf4j9upomc gm_l3;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_f_frame_up_g8486eu((void *)&gm_l3, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
    gm_l2 = t4;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    uint32_t t8 = gm_l2;
    void * t9 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 64));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_t6j6smj7qoctu t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t6);
    gm_t53at80pfdus3 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t8 + 8), t7);
    gm_t88rcucg1b7uk t9 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t8);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_t88rcucg1b7uk t6 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t7 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t2, t6);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_b16_8 t2 = gm_l1;
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    gm_f_format_fsfutor((void *)&gm_l2, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcd5arorrf8jkg, 38, 27);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_tl874t2atnlh3 t7 = gm_l0;
    gm_t08olq97rscn3 t8;
    GM_ST(uint32_t, (uint8_t *)&t8 + 28, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t8 + 16), t7);
    memmove(gm_out, ((uint8_t *)&t8), 32);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_unwind_call_4k8ulav F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* call */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 216));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        gm_tnpifv20irnph t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
        gm_b0_1 t7 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_l1;
        gm_tnpifv20irnph t10;
        GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
        gm_b0_1 t11 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t12 = gm_l0;
        gm_b0_1 t13 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_addr_of_hiaqkn0 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_b16_8 t4 = gm_l2;
    uint8_t t5 = gm_f_is_temp_rrllgdr(t4);
    if (t5) {
        gm_b16_8 t6 = gm_l2;
        gm_tnpifv20irnph t7;
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
        gm_f_format_4un19t8((void *)&gm_l5, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src0gtuh94b1fce, 37, 28);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        F.ret = t10;
        goto gm_fret;
    }
    {
        {
            gm_b16_8 t11 = gm_l2;
            gm_f_view_addr_nj9md2j((void *)&gm_l6, t11);
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_l3 = t13;
            gm_b16_8 t14 = gm_l3;
            F.ret = t14;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = gm_l0;
    gm_t88rcucg1b7uk t16 = gm_l1;
    gm_b16_8 t17 = gm_l2;
    gm_b16_8 t18 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t15, t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t18;
    gm_b16_8 t19 = gm_l4;
    gm_tnpifv20irnph t20;
    GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
    gm_f_format_4un19t8((void *)&gm_l7, (void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src0gtuh94b1fce, 40, 12);
    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
    F.ret = t23;
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
extern __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_bits_get_gmqvhi0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3) {
    struct gm_fr_bits_get_gmqvhi0 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    uint32_t gm_l3; /* index */
    gm_tcmkompbl09il gm_l4; /* bl */
    gm_tov4mtrfp5e7v gm_l5; /* f */
    gm_b16_8 gm_l6; /* a */
    gm_b16_8 gm_l7; /* uc */
    gm_b16_8 gm_l8; /* raw */
    gm_b16_8 gm_l9; /* raw2 */
    GM_STACK_CHECK(&F, 634u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t57euh612ligr gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_bits_layout_23rect9((void *)&gm_l10, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l10))[24] != 0);
    if (!t6) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 297, 11);
    memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l10), 24);
    void * t7 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)&gm_l4) + 8) + 8));
    uint32_t t9 = gm_l3;
    uint64_t t10 = (uint64_t)t9;
    if ((uintptr_t)t10 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t10, t8, gm_src0gtuh94b1fce, 298, 10);
    gm_tov4mtrfp5e7v t11 = GM_LD(gm_tov4mtrfp5e7v, (((uint8_t *)t7) + (uintptr_t)t10 * 12));
    gm_l5 = t11;
    void * t12 = gm_l0;
    gm_t88rcucg1b7uk t13 = gm_l1;
    gm_b16_8 t14 = gm_l2;
    gm_b16_8 t15 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t15;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    void * t18 = (void *)(((uint8_t *)t17) + 8);
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
    gm_b16_8 t20 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t20;
    gm_b16_8 t21 = gm_l7;
    gm_b16_8 t22 = gm_l6;
    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
    uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
    gm_b16_8 t25 = gm_f_mask_epjl3to((void *)NULL, cx, NULL, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tu8mt0h8clq1l t26;
    GM_ST(uint32_t, (uint8_t *)&t26 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 16), t22);
    GM_ST(uint32_t, ((uint8_t *)&t26 + 32), t23);
    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 40), t25);
    gm_f_format_tg9hp73((void *)&gm_l11, (void *)NULL, cx, NULL, &t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src0gtuh94b1fce, 301, 12);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
    gm_l8 = t28;
    void * t29 = gm_l0;
    void * t30 = gm_l0;
    void * t31 = GM_LD(void *, ((uint8_t *)t30));
    void * t32 = (void *)(((uint8_t *)t31) + 8);
    gm_t88rcucg1b7uk t33 = gm_f_int_ty_7nma1b7((void *)NULL, cx, NULL, t32, ((uint8_t)0), ((uint16_t)128ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t34 = gm_l8;
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
    gm_b16_8 t36 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t29, t33, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t36;
    void * t37 = gm_l0;
    gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t39 = gm_l9;
    gm_b16_8 t40 = gm_f_bits_value_u15lfan((void *)NULL, cx, NULL, t37, t38, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t40;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_pack_ttermrr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_bits_pack_ttermrr F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* tys */
    gm_b16_8 gm_l3; /* vals */
    gm_tcmkompbl09il gm_l4; /* bl */
    gm_b16_8 gm_l5; /* acc */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_tov4mtrfp5e7v gm_l9; /* f */
    gm_b16_8 gm_l10; /* raw */
    gm_b16_8 gm_l11; /* b */
    gm_b16_8 gm_l12; /* uc */
    GM_STACK_CHECK(&F, 850u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t57euh612ligr gm_l13;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_f_bits_layout_23rect9((void *)&gm_l13, (void *)NULL, cx, NULL, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
    if (!t11) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 322, 11);
    memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l13), 24);
    void * t12 = gm_l0;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = (void *)(((uint8_t *)t14) + 8);
    gm_t88rcucg1b7uk t16 = gm_f_int_ty_7nma1b7((void *)NULL, cx, NULL, t15, ((uint8_t)0), ((uint16_t)128ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sicgmpca63jln));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
    gm_b16_8 t18 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t12, t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t18;
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    gm_l6 = t19;
    {
        for (;;) {
            uint64_t t20 = gm_l7;
            uint64_t t21 = gm_l6;
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t23 = gm_l7;
                        gm_l8 = t23;
                        void * t24 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)&gm_l4) + 8) + 8));
                        uint64_t t26 = gm_l8;
                        if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_src0gtuh94b1fce, 325, 14);
                        gm_tov4mtrfp5e7v t27 = GM_LD(gm_tov4mtrfp5e7v, (((uint8_t *)t24) + (uintptr_t)t26 * 12));
                        gm_l9 = t27;
                        void * t28 = gm_l0;
                        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t31 = gm_l8;
                        if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_src0gtuh94b1fce, 326, 28);
                        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + (uintptr_t)t31 * 4));
                        void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t35 = gm_l8;
                        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t33) + (uintptr_t)t35 * 16));
                        gm_b16_8 t37 = gm_f_bits_raw_e0qg9mb((void *)NULL, cx, NULL, t28, t32, t36);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t37;
                        void * t38 = gm_l0;
                        gm_b16_8 t39 = gm_l5;
                        gm_b16_8 t40 = gm_l10;
                        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                        gm_b16_8 t42 = gm_f_mask_epjl3to((void *)NULL, cx, NULL, t41);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t43 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                        gm_tj4tbafenpsjm t44;
                        GM_ST(uint32_t, (uint8_t *)&t44 + 52, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t39);
                        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 16), t40);
                        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 32), t42);
                        GM_ST(uint32_t, ((uint8_t *)&t44 + 48), t43);
                        gm_b0_1 t45 = gm_f_linef_glh29gu((void *)NULL, cx, NULL, t38, &t44);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 324, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l7;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l7 = t49;
        }
    }
gm_lb1:;
    void * t50 = gm_l0;
    gm_t88rcucg1b7uk t51 = gm_l1;
    gm_b16_8 t52 = gm_f_blob_d5apnrc((void *)NULL, cx, NULL, t50, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t52;
    void * t53 = gm_l0;
    void * t54 = GM_LD(void *, ((uint8_t *)t53));
    void * t55 = (void *)(((uint8_t *)t54) + 8);
    gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
    gm_b16_8 t57 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t55, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t57;
    void * t58 = gm_l0;
    gm_b16_8 t59 = gm_l12;
    gm_b16_8 t60 = gm_l11;
    gm_b16_8 t61 = gm_l12;
    gm_b16_8 t62 = gm_l5;
    gm_t0uuhaljsf410 t63;
    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t59);
    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 16), t60);
    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 32), t61);
    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 48), t62);
    gm_b0_1 t64 = gm_f_linef_uc25pl5((void *)NULL, cx, NULL, t58, &t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t65 = gm_l11;
    F.ret = t65;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_bits_set_eqvtegv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_bits_set_eqvtegv F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* whole */
    uint32_t gm_l3; /* index */
    gm_t88rcucg1b7uk gm_l4; /* vt */
    gm_b16_8 gm_l5; /* v */
    gm_tcmkompbl09il gm_l6; /* bl */
    gm_tov4mtrfp5e7v gm_l7; /* f */
    gm_b16_8 gm_l8; /* a */
    gm_b16_8 gm_l9; /* uc */
    gm_b16_8 gm_l10; /* raw */
    gm_b16_8 gm_l11; /* m */
    gm_b16_8 gm_l12; /* b */
    GM_STACK_CHECK(&F, 785u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t57euh612ligr gm_l13;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_bits_layout_23rect9((void *)&gm_l13, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
    if (!t6) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 309, 11);
    memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l13), 24);
    void * t7 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)&gm_l6) + 8) + 8));
    uint32_t t9 = gm_l3;
    uint64_t t10 = (uint64_t)t9;
    if ((uintptr_t)t10 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t10, t8, gm_src0gtuh94b1fce, 310, 10);
    gm_tov4mtrfp5e7v t11 = GM_LD(gm_tov4mtrfp5e7v, (((uint8_t *)t7) + (uintptr_t)t10 * 12));
    gm_l7 = t11;
    void * t12 = gm_l0;
    gm_t88rcucg1b7uk t13 = gm_l1;
    gm_b16_8 t14 = gm_l2;
    gm_b16_8 t15 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t15;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    void * t18 = (void *)(((uint8_t *)t17) + 8);
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
    gm_b16_8 t20 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t20;
    void * t21 = gm_l0;
    gm_t88rcucg1b7uk t22 = gm_l4;
    gm_b16_8 t23 = gm_l5;
    gm_b16_8 t24 = gm_f_bits_raw_e0qg9mb((void *)NULL, cx, NULL, t21, t22, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t24;
    uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
    gm_b16_8 t26 = gm_f_mask_epjl3to((void *)NULL, cx, NULL, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t26;
    void * t27 = gm_l0;
    gm_t88rcucg1b7uk t28 = gm_l1;
    gm_b16_8 t29 = gm_f_blob_d5apnrc((void *)NULL, cx, NULL, t27, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t29;
    void * t30 = gm_l0;
    gm_b16_8 t31 = gm_l9;
    gm_b16_8 t32 = gm_l12;
    gm_b16_8 t33 = gm_l9;
    gm_b16_8 t34 = gm_l9;
    gm_b16_8 t35 = gm_l8;
    gm_b16_8 t36 = gm_l11;
    uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
    gm_b16_8 t38 = gm_l10;
    gm_b16_8 t39 = gm_l11;
    uint32_t t40 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
    gm_tt4j8kuecfafr t41;
    GM_ST(uint32_t, (uint8_t *)&t41 + 100, 0);
    GM_ST(uint32_t, (uint8_t *)&t41 + 140, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t31);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 16), t32);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 32), t33);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 48), t34);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 64), t35);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 80), t36);
    GM_ST(uint32_t, ((uint8_t *)&t41 + 96), t37);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 104), t38);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 120), t39);
    GM_ST(uint32_t, ((uint8_t *)&t41 + 136), t40);
    gm_b0_1 t42 = gm_f_linef_q8b376p((void *)NULL, cx, NULL, t30, &t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t43 = gm_l12;
    F.ret = t43;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_blob_d5apnrc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_blob_over_qs5fusr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_bound_n4p0vpb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_bound_n4p0vpb F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    GM_STACK_CHECK(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint64_t t6 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 8) + 8));
    uint8_t t10 = (t6 > t9);
    if (t10) {
        gm_b16_8 t11 = gm_l2;
        gm_b16_8 t12 = gm_l2;
        gm_t6j6smj7qoctu t13;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t11);
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t12);
        gm_f_format_nptl5af((void *)&gm_l3, (void *)NULL, cx, NULL, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_src0gtuh94b1fce, 658, 16);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        F.ret = t16;
        goto gm_fret;
    }
    gm_b16_8 t17 = gm_l2;
    gm_tnpifv20irnph t18;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
    gm_f_format_6df13cd((void *)&gm_l4, (void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src0gtuh94b1fce, 660, 12);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
    F.ret = t21;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_clear_outside_q01mr8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_clear_outside_q01mr8i F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* dest */
    gm_b16_8 gm_l3; /* parts */
    uint64_t gm_l4; /* size */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t2lqu8jsj511d gm_l10; /* p */
    uint64_t gm_l11; /* i */
    uint64_t gm_l12; /* end */
    uint64_t gm_l13; /* gaps */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint8_t gm_l16; /* c */
    uint64_t gm_l18; /* i */
    gm_tu703bl1jkggd gm_l19; /* sizes */
    uint64_t gm_l20; /* k */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* s */
    uint8_t gm_l24; /* all_free */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* j */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t2lqu8jsj511d gm_l30; /* st */
    GM_STACK_CHECK(&F, 1595u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    {
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, ((uint8_t *)t42));
        void * t44 = (void *)(((uint8_t *)t43) + 8);
        gm_t88rcucg1b7uk t45 = gm_l1;
        uint64_t t46 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t47 = (uint64_t)t46;
        gm_l4 = t47;
        gm_trqfvmjtt4sij t48;
        gm_f_list_s83jn2q((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t48;
        gm_l7 = ((uint64_t)0ULL);
        uint64_t t49 = gm_l4;
        gm_l6 = t49;
        {
            for (;;) {
                uint64_t t50 = gm_l7;
                uint64_t t51 = gm_l6;
                uint8_t t52 = (t50 < t51);
                if (!t52) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            void * t53 = (void *)((uint8_t *)&F.l5);
                            gm_tftb1odc739t5 t54 = gm_f_push_vd1290p(t53, ((uint8_t)0));
                            gm_l31 = t54;
                            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src0gtuh94b1fce, 131, 24);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 131, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t56 = gm_l7;
                uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                uint64_t t59 = r57;
                gm_l7 = t59;
            }
        }
gm_lb1:;
        gm_b16_8 t60 = gm_l3;
        gm_l8 = t60;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t61 = gm_l9;
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t63 = (t61 < t62);
                if (!t63) {
                    goto gm_lb6;
                }
                {
                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t66 = gm_l9;
                    {
                        {
                            gm_t2lqu8jsj511d t67 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)t64) + (uintptr_t)t66 * 16));
                            gm_l10 = t67;
                            uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                            uint64_t t69 = (uint64_t)t68;
                            gm_l11 = t69;
                            uint64_t t70 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t r72; bool o73 = __builtin_add_overflow(t70, t71, &r72);
                            if (o73) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 134, 17);
                            uint64_t t74 = r72;
                            uint64_t t75 = (uint64_t)t74;
                            gm_l12 = t75;
                            {
                                for (;;) {
                                    uint64_t t76 = gm_l11;
                                    uint64_t t77 = gm_l12;
                                    uint8_t t78 = (t76 < t77);
                                    if (!t78) {
                                        goto gm_lb11;
                                    }
                                    uint64_t t79 = gm_l11;
                                    uint64_t t80 = gm_l4;
                                    uint8_t t81 = (t79 < t80);
                                    if (!t81) {
                                        goto gm_lb11;
                                    }
                                    {
                                        void * t82 = GM_LD(void *, ((uint8_t *)&F.l5));
                                        uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                        uint64_t t84 = gm_l11;
                                        if ((uintptr_t)t84 >= (uintptr_t)(t83)) gm_trap_index((uintptr_t)t84, t83, gm_src0gtuh94b1fce, 136, 13);
                                        GM_ST(uint8_t, (((uint8_t *)t82) + (uintptr_t)t84 * 1), ((uint8_t)1));
                                        uint64_t t85 = gm_l11;
                                        uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                                        if (o87) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 137, 13);
                                        uint64_t t88 = r86;
                                        gm_l11 = t88;
                                    }
gm_lb13:;
                                }
                            }
gm_lb11:;
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 132, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t89 = gm_l9;
                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                uint64_t t92 = r90;
                gm_l9 = t92;
            }
        }
gm_lb6:;
    }
    {
        gm_l13 = ((uint64_t)0ULL);
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_l14 = t93;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t94 = gm_l15;
                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t96 = (t94 < t95);
                if (!t96) {
                    goto gm_lb14;
                }
                {
                    void * t97 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t99 = gm_l15;
                    {
                        {
                            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)t99 * 1));
                            gm_l16 = t100;
                            uint8_t t101 = gm_l16;
                            uint8_t t102 = (uint8_t)!t101;
                            if (t102) {
                                uint64_t t103 = gm_l13;
                                uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                                if (o105) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 142, 20);
                                uint64_t t106 = r104;
                                gm_l13 = t106;
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 141, 5);
                    }
gm_lb17:;
                }
gm_lb16:;
                uint64_t t107 = gm_l15;
                uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                uint64_t t110 = r108;
                gm_l15 = t110;
            }
        }
gm_lb14:;
        uint64_t t111 = gm_l13;
        uint8_t t112 = (t111 > ((uint64_t)32ULL));
        if (t112) {
            void * t113 = gm_l0;
            gm_b16_8 t114 = gm_l2;
            gm_b16_8 t115 = gm_l2;
            gm_t6j6smj7qoctu t116;
            GM_ST(gm_b16_8, ((uint8_t *)&t116 + 0), t114);
            GM_ST(gm_b16_8, ((uint8_t *)&t116 + 16), t115);
            gm_b0_1 t117 = gm_f_linef_cf5thm0((void *)NULL, cx, NULL, t113, &t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t118;
        gm_f_list_v5d7fqe((void *)&t118, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t118;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t119 = gm_l18;
                uint64_t t120 = gm_l4;
                uint8_t t121 = (t119 < t120);
                if (!t121) {
                    goto gm_lb19;
                }
                {
                    void * t122 = GM_LD(void *, ((uint8_t *)&F.l5));
                    uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                    uint64_t t124 = gm_l18;
                    if ((uintptr_t)t124 >= (uintptr_t)(t123)) gm_trap_index((uintptr_t)t124, t123, gm_src0gtuh94b1fce, 151, 12);
                    uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)t122) + (uintptr_t)t124 * 1));
                    if (t125) {
                        uint64_t t126 = gm_l18;
                        uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                        if (o128) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 152, 13);
                        uint64_t t129 = r127;
                        gm_l18 = t129;
                        goto gm_lb21;
                    }
                    gm_tu703bl1jkggd t130;
                    GM_ST(uint64_t, ((uint8_t *)&t130 + 0), ((uint64_t)8ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t130 + 8), ((uint64_t)4ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t130 + 16), ((uint64_t)2ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t130 + 24), ((uint64_t)1ULL));
                    gm_l19 = t130;
                    gm_l20 = ((uint64_t)1ULL);
                    void * t131 = (void *)((uint8_t *)&gm_l19);
                    uint64_t t132 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t133 = (uintptr_t)((uint64_t)4ULL);
                    if (t132 > t133 || t133 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t132, t133, ((uint64_t)4ULL), gm_src0gtuh94b1fce, 158, 9);
                    uint64_t t134 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t135;
                    GM_ST(void *, (uint8_t *)&t135, (void *)(((uint8_t *)t131 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(t134));
                    gm_l21 = t135;
                    gm_l22 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t136 = gm_l22;
                            uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                            uint8_t t138 = (t136 < t137);
                            if (!t138) {
                                goto gm_lb22;
                            }
                            {
                                void * t139 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                uint64_t t141 = gm_l22;
                                {
                                    {
                                        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t139) + (uintptr_t)t141 * 8));
                                        gm_l23 = t142;
                                        {
                                            {
                                                uint64_t t143 = gm_l18;
                                                uint64_t t144 = gm_l23;
                                                if (t144 == 0) gm_trap("division by zero", gm_src0gtuh94b1fce, 159, 16);
                                                uint64_t t145 = (uint64_t)(t143 % t144);
                                                uint8_t t146 = ((t145 == ((uint64_t)0ULL)));
                                                if (!t146) {
                                                    goto gm_lb28;
                                                }
                                                uint64_t t147 = gm_l18;
                                                uint64_t t148 = gm_l23;
                                                uint64_t r149; bool o150 = __builtin_add_overflow(t147, t148, &r149);
                                                if (o150) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 159, 31);
                                                uint64_t t151 = r149;
                                                uint64_t t152 = gm_l4;
                                                uint8_t t153 = (t151 <= t152);
                                                if (!t153) {
                                                    goto gm_lb28;
                                                }
                                                gm_l24 = ((uint8_t)1);
                                                uint64_t t154 = gm_l18;
                                                gm_l26 = t154;
                                                uint64_t t155 = gm_l18;
                                                uint64_t t156 = gm_l23;
                                                uint64_t r157; bool o158 = __builtin_add_overflow(t155, t156, &r157);
                                                if (o158) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 161, 29);
                                                uint64_t t159 = r157;
                                                gm_l25 = t159;
                                                {
                                                    for (;;) {
                                                        uint64_t t160 = gm_l26;
                                                        uint64_t t161 = gm_l25;
                                                        uint8_t t162 = (t160 < t161);
                                                        if (!t162) {
                                                            goto gm_lb29;
                                                        }
                                                        {
                                                            {
                                                                {
                                                                    uint64_t t163 = gm_l26;
                                                                    gm_l27 = t163;
                                                                    void * t164 = GM_LD(void *, ((uint8_t *)&F.l5));
                                                                    uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                    uint64_t t166 = gm_l27;
                                                                    if ((uintptr_t)t166 >= (uintptr_t)(t165)) gm_trap_index((uintptr_t)t166, t165, gm_src0gtuh94b1fce, 162, 24);
                                                                    uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)t164) + (uintptr_t)t166 * 1));
                                                                    if (t167) {
                                                                        gm_l24 = ((uint8_t)0);
                                                                    }
                                                                    goto gm_lb32;
                                                                }
gm_lb33:;
                                                                gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 161, 17);
                                                            }
gm_lb32:;
                                                        }
gm_lb31:;
                                                        uint64_t t168 = gm_l26;
                                                        uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
                                                        uint64_t t171 = r169;
                                                        gm_l26 = t171;
                                                    }
                                                }
gm_lb29:;
                                                uint8_t t172 = gm_l24;
                                                if (t172) {
                                                    uint64_t t173 = gm_l23;
                                                    gm_l20 = t173;
                                                    goto gm_lb22;
                                                }
                                                goto gm_lb27;
                                            }
gm_lb28:;
                                        }
gm_lb27:;
                                        goto gm_lb25;
                                    }
gm_lb26:;
                                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 158, 9);
                                }
gm_lb25:;
                            }
gm_lb24:;
                            uint64_t t174 = gm_l22;
                            uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                            uint64_t t177 = r175;
                            gm_l22 = t177;
                        }
                    }
gm_lb22:;
                    void * t178 = (void *)((uint8_t *)&F.l17);
                    uint64_t t179 = gm_l18;
                    uint64_t t180 = gm_l20;
                    gm_t2lqu8jsj511d t181;
                    GM_ST(uint64_t, ((uint8_t *)&t181 + 0), t179);
                    GM_ST(uint64_t, ((uint8_t *)&t181 + 8), t180);
                    gm_tftb1odc739t5 t182 = gm_f_push_p5tbf4u(t178, t181);
                    gm_l32 = t182;
                    uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t183) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src0gtuh94b1fce, 170, 9);
                    uint64_t t184 = gm_l18;
                    uint64_t t185 = gm_l20;
                    uint64_t r186; bool o187 = __builtin_add_overflow(t184, t185, &r186);
                    if (o187) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 171, 9);
                    uint64_t t188 = r186;
                    gm_l18 = t188;
                }
gm_lb21:;
            }
        }
gm_lb19:;
    }
    {
        {
            {
                void * t189 = gm_l0;
                void * t190 = GM_LD(void *, ((uint8_t *)t189));
                uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)t190) + 1136) + 1));
                if (!t191) {
                    goto gm_lb35;
                }
                uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                uint8_t t193 = (t192 > ((uint64_t)1ULL));
                if (!t193) {
                    goto gm_lb35;
                }
                uint64_t t194 = gm_l4;
                uint8_t t195 = (t194 <= ((uint64_t)64ULL));
                if (!t195) {
                    goto gm_lb35;
                }
                void * t196 = gm_l0;
                void * t197 = GM_LD(void *, ((uint8_t *)t196));
                uint8_t t198 = GM_LD(uint8_t, (((((uint8_t *)t197) + 8) + 8) + 56));
                uint8_t t199 = (uint8_t)!t198;
                if (!t199) {
                    goto gm_lb35;
                }
                void * t200 = gm_l0;
                gm_b16_8 t201 = gm_l2;
                gm_b16_8 t202 = gm_l2;
                gm_t6j6smj7qoctu t203;
                GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t201);
                GM_ST(gm_b16_8, ((uint8_t *)&t203 + 16), t202);
                gm_b0_1 t204 = gm_f_linef_cf5thm0((void *)NULL, cx, NULL, t200, &t203);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb35:;
        }
gm_lb34:;
        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
        gm_l28 = t205;
        gm_l29 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t206 = gm_l29;
                uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                uint8_t t208 = (t206 < t207);
                if (!t208) {
                    goto gm_lb36;
                }
                {
                    void * t209 = GM_LD(void *, ((uint8_t *)&gm_l28));
                    uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                    uint64_t t211 = gm_l29;
                    {
                        {
                            gm_t2lqu8jsj511d t212 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)t209) + (uintptr_t)t211 * 16));
                            gm_l30 = t212;
                            void * t213 = gm_l0;
                            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                            uint64_t r215; bool o216 = __builtin_mul_overflow(t214, ((uint64_t)8ULL), &r215);
                            if (o216) gm_trap("integer overflow in `*`", gm_src0gtuh94b1fce, 177, 83);
                            uint64_t t217 = r215;
                            gm_b16_8 t218 = gm_l2;
                            uint64_t t219 = GM_LD(uint64_t, ((uint8_t *)&gm_l30));
                            gm_tob1snuii5bme t220;
                            GM_ST(uint64_t, ((uint8_t *)&t220 + 0), t217);
                            GM_ST(gm_b16_8, ((uint8_t *)&t220 + 8), t218);
                            GM_ST(uint64_t, ((uint8_t *)&t220 + 24), t219);
                            gm_b0_1 t221 = gm_f_linef_aoq4lf0((void *)NULL, cx, NULL, t213, &t220);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb39;
                        }
gm_lb40:;
                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 177, 5);
                    }
gm_lb39:;
                }
gm_lb38:;
                uint64_t t222 = gm_l29;
                uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                uint64_t t225 = r223;
                gm_l29 = t225;
            }
        }
gm_lb36:;
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
__attribute__((aligned(4))) void gm_f_coerce_value_ducritf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tmn7n0n9raou5 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_coerce_value_ducritf F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tmn7n0n9raou5 gm_l3; /* kind */
    gm_t88rcucg1b7uk gm_l4; /* xt */
    gm_b16_8 gm_l5; /* v */
    gm_b16_8 gm_l6; /* c */
    uint32_t gm_l7; /* variant */
    gm_b16_8 gm_l8; /* a */
    gm_t88rcucg1b7uk gm_l9; /* e1 */
    gm_t88rcucg1b7uk gm_l10; /* ok */
    gm_t88rcucg1b7uk gm_l11; /* e2 */
    gm_b16_8 gm_l12; /* res */
    gm_b16_8 gm_l13; /* bad */
    uint64_t gm_l14; /* off */
    gm_b16_8 gm_l15; /* ev */
    gm_b16_8 gm_l16; /* wrapped */
    gm_b16_8 gm_l17; /* r */
    gm_b16_8 gm_l18; /* ov */
    gm_b16_8 gm_l19; /* r2 */
    gm_b16_8 gm_l20; /* a */
    gm_t88rcucg1b7uk gm_l21; /* pointee */
    uint64_t gm_l22; /* len */
    gm_b16_8 gm_l23; /* code */
    GM_STACK_CHECK(&F, 3964u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tabb710d9mq2e gm_l24;
    gm_t53at80pfdus3 gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_b16_8 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    uint8_t gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_t53at80pfdus3 gm_l32;
    gm_t53at80pfdus3 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_b16_8 gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    {
        {
            {
                {
                    {
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                        if (!t34) {
                            goto gm_lb4;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                        if (!t36) {
                            goto gm_lb5;
                        }
                        goto gm_lb3;
                    }
gm_lb5:;
                    goto gm_lb2;
                }
gm_lb3:;
                void * t37 = gm_l0;
                void * t38 = GM_LD(void *, ((uint8_t *)t37));
                void * t39 = (void *)(((uint8_t *)t38) + 8);
                gm_t88rcucg1b7uk t40 = gm_l2;
                gm_b16_8 t41 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t41;
                void * t42 = gm_l0;
                gm_t88rcucg1b7uk t43 = gm_l2;
                gm_b16_8 t44 = gm_l6;
                gm_b16_8 t45 = gm_l5;
                gm_t6j6smj7qoctu t46;
                GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t44);
                GM_ST(gm_b16_8, ((uint8_t *)&t46 + 16), t45);
                gm_f_format_3vtl3te((void *)&gm_l24, (void *)NULL, cx, NULL, &t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src0gtuh94b1fce, 670, 30);
                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                gm_b16_8 t50 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t42, t43, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t51 + 32, 0);
                ((uint8_t *)&t51)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t51 + 8), t50);
                memmove(gm_out, ((uint8_t *)&t51), 40);
                goto gm_fdone;
            }
gm_lb2:;
            {
                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t53 = (uint8_t)(t52 == ((uint8_t)2ULL));
                if (!t53) {
                    goto gm_lb6;
                }
                void * t54 = gm_l0;
                gm_t88rcucg1b7uk t55 = gm_l2;
                gm_b16_8 t56 = gm_l5;
                gm_b16_8 t57 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t54, t55, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t58;
                GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t58 + 32, 0);
                ((uint8_t *)&t58)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
                memmove(gm_out, ((uint8_t *)&t58), 40);
                goto gm_fdone;
            }
gm_lb6:;
            {
                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t60 = (uint8_t)(t59 == ((uint8_t)3ULL));
                if (!t60) {
                    goto gm_lb7;
                }
                void * t61 = gm_l0;
                gm_t88rcucg1b7uk t62 = gm_l2;
                gm_t88rcucg1b7uk t63 = gm_l4;
                gm_b16_8 t64 = gm_l5;
                gm_b16_8 t65 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t61, t62, ((uint8_t)0), t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t66;
                GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t66 + 32, 0);
                ((uint8_t *)&t66)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
                memmove(gm_out, ((uint8_t *)&t66), 40);
                goto gm_fdone;
            }
gm_lb7:;
            {
                uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t68 = (uint8_t)(t67 == ((uint8_t)4ULL));
                if (!t68) {
                    goto gm_lb8;
                }
                uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                gm_l7 = t69;
                void * t70 = gm_l0;
                gm_t88rcucg1b7uk t71 = gm_l4;
                gm_b16_8 t72 = gm_l5;
                gm_b16_8 t73 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t70, t71, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t73;
                {
                    {
                        void * t74 = gm_l0;
                        void * t75 = GM_LD(void *, ((uint8_t *)t74));
                        void * t76 = (void *)(((uint8_t *)t75) + 8);
                        gm_t88rcucg1b7uk t77 = gm_l4;
                        gm_f_kind_ghvav22((void *)&gm_l25, t76, GM_CONV(gm_csguankko4lmhd, t77));
                        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                        uint8_t t79 = (uint8_t)(t78 == ((uint8_t)14ULL));
                        if (!t79) {
                            goto gm_lb10;
                        }
                        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 8));
                        gm_l9 = t80;
                        gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
                        gm_l10 = t81;
                        {
                            {
                                void * t82 = gm_l0;
                                void * t83 = GM_LD(void *, ((uint8_t *)t82));
                                void * t84 = (void *)(((uint8_t *)t83) + 8);
                                gm_t88rcucg1b7uk t85 = gm_l2;
                                gm_f_kind_ghvav22((void *)&gm_l26, t84, GM_CONV(gm_csguankko4lmhd, t85));
                                uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                                uint8_t t87 = (uint8_t)(t86 == ((uint8_t)14ULL));
                                if (!t87) {
                                    goto gm_lb12;
                                }
                                gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l26) + 8));
                                gm_l11 = t88;
                                void * t89 = gm_l0;
                                gm_t88rcucg1b7uk t90 = gm_l2;
                                gm_b16_8 t91 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t89, t90);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l12 = t91;
                                void * t92 = gm_l0;
                                gm_t88rcucg1b7uk t93 = gm_l4;
                                gm_b16_8 t94 = gm_l8;
                                gm_b16_8 t95 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t92, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l13 = t95;
                                void * t96 = gm_l0;
                                void * t97 = GM_LD(void *, ((uint8_t *)t96));
                                void * t98 = (void *)(((uint8_t *)t97) + 8);
                                gm_t88rcucg1b7uk t99 = gm_l4;
                                gm_b16_8 t100 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t98, t99);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l27 = t100;
                                void * t101 = GM_LD(void *, ((uint8_t *)&gm_l27));
                                uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t102)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t102, gm_src0gtuh94b1fce, 680, 20);
                                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t101) + (uintptr_t)((uint64_t)1ULL) * 8));
                                gm_l14 = t103;
                                void * t104 = gm_l0;
                                gm_b16_8 t105 = gm_l13;
                                gm_tnpifv20irnph t106;
                                GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t105);
                                gm_b0_1 t107 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t104, t106);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t108 = gm_l0;
                                void * t109 = gm_l0;
                                uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)t109) + 144));
                                uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                                if (o112) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 682, 13);
                                uint64_t t113 = r111;
                                GM_ST(uint64_t, (((uint8_t *)t108) + 144), t113);
                                void * t114 = gm_l0;
                                gm_t88rcucg1b7uk t115 = gm_l9;
                                gm_b16_8 t116 = gm_l8;
                                uint64_t t117 = gm_l14;
                                gm_t3877pk7lv748 t118;
                                GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t116);
                                GM_ST(uint64_t, ((uint8_t *)&t118 + 16), t117);
                                gm_f_format_gjujk09((void *)&gm_l28, (void *)NULL, cx, NULL, &t118);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src0gtuh94b1fce, 683, 31);
                                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                gm_b16_8 t122 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t114, t115, t121);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l15 = t122;
                                void * t123 = gm_l0;
                                gm_t88rcucg1b7uk t124 = gm_l11;
                                uint32_t t125 = gm_l7;
                                uint64_t t126 = (uint64_t)t125;
                                gm_b16_8 t127 = gm_l15;
                                gm_th2lqbo77pvsc t128;
                                GM_ST(uint8_t, (uint8_t *)&t128 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t128 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t128 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t128), t127);
                                ((uint8_t *)&t128)[16] = 1;
                                gm_b16_8 t129 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t123, t124, t126, &t128);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l16 = t129;
                                void * t130 = gm_l0;
                                gm_t88rcucg1b7uk t131 = gm_l2;
                                gm_t88rcucg1b7uk t132 = gm_l11;
                                gm_b16_8 t133 = gm_l16;
                                gm_b16_8 t134 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t130, t131, ((uint8_t)1), t132, t133);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l17 = t134;
                                void * t135 = gm_l0;
                                gm_b16_8 t136 = gm_l12;
                                gm_b16_8 t137 = gm_l17;
                                gm_t6j6smj7qoctu t138;
                                GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t136);
                                GM_ST(gm_b16_8, ((uint8_t *)&t138 + 16), t137);
                                gm_b0_1 t139 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t135, &t138);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t140 = gm_l0;
                                void * t141 = gm_l0;
                                uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t141) + 144));
                                uint64_t r143; bool o144 = __builtin_sub_overflow(t142, ((uint64_t)1ULL), &r143);
                                if (o144) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 687, 13);
                                uint64_t t145 = r143;
                                GM_ST(uint64_t, (((uint8_t *)t140) + 144), t145);
                                void * t146 = gm_l0;
                                gm_b16_8 t147;
                                GM_ST(void *, (uint8_t *)&t147, (void *)(gm_s02uuedsru1qu));
                                GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(8));
                                gm_b0_1 t148 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t146, t147);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t149 = gm_l0;
                                void * t150 = gm_l0;
                                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)t150) + 144));
                                uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                                if (o153) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 689, 13);
                                uint64_t t154 = r152;
                                GM_ST(uint64_t, (((uint8_t *)t149) + 144), t154);
                                void * t155 = gm_l0;
                                gm_t88rcucg1b7uk t156 = gm_l10;
                                gm_b16_8 t157 = gm_l8;
                                uint64_t t158 = gm_l14;
                                gm_t3877pk7lv748 t159;
                                GM_ST(gm_b16_8, ((uint8_t *)&t159 + 0), t157);
                                GM_ST(uint64_t, ((uint8_t *)&t159 + 16), t158);
                                gm_f_format_gjujk09((void *)&gm_l29, (void *)NULL, cx, NULL, &t159);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_src0gtuh94b1fce, 690, 31);
                                gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                                gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                                gm_b16_8 t163 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t155, t156, t162);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l18 = t163;
                                void * t164 = gm_l0;
                                gm_t88rcucg1b7uk t165 = gm_l2;
                                gm_t88rcucg1b7uk t166 = gm_l10;
                                gm_b16_8 t167 = gm_l18;
                                gm_b16_8 t168 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t164, t165, ((uint8_t)0), t166, t167);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l19 = t168;
                                void * t169 = gm_l0;
                                gm_b16_8 t170 = gm_l12;
                                gm_b16_8 t171 = gm_l19;
                                gm_t6j6smj7qoctu t172;
                                GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t170);
                                GM_ST(gm_b16_8, ((uint8_t *)&t172 + 16), t171);
                                gm_b0_1 t173 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t169, &t172);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t174 = gm_l0;
                                void * t175 = gm_l0;
                                uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)t175) + 144));
                                uint64_t r177; bool o178 = __builtin_sub_overflow(t176, ((uint64_t)1ULL), &r177);
                                if (o178) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 693, 13);
                                uint64_t t179 = r177;
                                GM_ST(uint64_t, (((uint8_t *)t174) + 144), t179);
                                void * t180 = gm_l0;
                                gm_b16_8 t181;
                                GM_ST(void *, (uint8_t *)&t181, (void *)(gm_s2cuflhqkkuaq));
                                GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(1));
                                gm_b0_1 t182 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t180, t181);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t183 = gm_l12;
                                gm_t5o0tpv5jka2l t184;
                                GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t184 + 24, 0);
                                GM_ST(uint64_t, (uint8_t *)&t184 + 32, 0);
                                ((uint8_t *)&t184)[0] = 0;
                                GM_ST(gm_b16_8, ((uint8_t *)&t184 + 8), t183);
                                memmove(gm_out, ((uint8_t *)&t184), 40);
                                goto gm_fdone;
                            }
gm_lb12:;
                            gm_tl874t2atnlh3 t185 = gm_l1;
                            gm_b16_8 t186;
                            GM_ST(void *, (uint8_t *)&t186, (void *)(gm_s43jqu37ghtoq));
                            GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(18));
                            gm_t08olq97rscn3 t187;
                            gm_f_unsupported_531q51r((void *)&t187, (void *)NULL, cx, NULL, t185, t186);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t5o0tpv5jka2l t188;
                            GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                            ((uint8_t *)&t188)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t188 + 8), t187);
                            memmove(gm_out, ((uint8_t *)&t188), 40);
                            goto gm_fdone;
                        }
gm_lb11:;
                    }
gm_lb10:;
                    gm_tl874t2atnlh3 t189 = gm_l1;
                    gm_b16_8 t190;
                    GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s43jqu37ghtoq));
                    GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(18));
                    gm_t08olq97rscn3 t191;
                    gm_f_unsupported_531q51r((void *)&t191, (void *)NULL, cx, NULL, t189, t190);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t192;
                    GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                    ((uint8_t *)&t192)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t192 + 8), t191);
                    memmove(gm_out, ((uint8_t *)&t192), 40);
                    goto gm_fdone;
                }
gm_lb9:;
            }
gm_lb8:;
        }
        {
            {
                uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t194 = (uint8_t)(t193 == ((uint8_t)5ULL));
                if (!t194) {
                    goto gm_lb13;
                }
                void * t195 = gm_l0;
                gm_t88rcucg1b7uk t196 = gm_l2;
                gm_b16_8 t197 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t195, t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t198;
                GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t198 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t198 + 32, 0);
                ((uint8_t *)&t198)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t198 + 8), t197);
                memmove(gm_out, ((uint8_t *)&t198), 40);
                goto gm_fdone;
            }
gm_lb13:;
            {
                uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t200 = (uint8_t)(t199 == ((uint8_t)10ULL));
                if (!t200) {
                    goto gm_lb14;
                }
                void * t201 = gm_l0;
                gm_t88rcucg1b7uk t202 = gm_l4;
                uint8_t t203 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t201, t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t203) {
                    void * t204 = gm_l0;
                    gm_t88rcucg1b7uk t205 = gm_l2;
                    uint8_t t206 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t204, t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t207 = (uint8_t)!t206;
                    gm_l30 = t207;
                } else {
                    gm_l30 = ((uint8_t)0);
                }
                uint8_t t208 = gm_l30;
                if (!t208) {
                    goto gm_lb14;
                }
                void * t209 = gm_l0;
                gm_t88rcucg1b7uk t210 = gm_l2;
                gm_b16_8 t211 = gm_l5;
                gm_tnpifv20irnph t212;
                GM_ST(gm_b16_8, ((uint8_t *)&t212 + 0), t211);
                gm_f_format_8sjjbd9((void *)&gm_l31, (void *)NULL, cx, NULL, t212);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t213 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                if (!t213) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src0gtuh94b1fce, 699, 95);
                gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
                gm_b16_8 t216 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t209, t210, t215);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t217;
                GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t217 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t217 + 32, 0);
                ((uint8_t *)&t217)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t217 + 8), t216);
                memmove(gm_out, ((uint8_t *)&t217), 40);
                goto gm_fdone;
            }
gm_lb14:;
            {
                {
                    {
                        uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t219 = (uint8_t)(t218 == ((uint8_t)6ULL));
                        if (!t219) {
                            goto gm_lb17;
                        }
                        goto gm_lb16;
                    }
gm_lb17:;
                    {
                        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t221 = (uint8_t)(t220 == ((uint8_t)10ULL));
                        if (!t221) {
                            goto gm_lb18;
                        }
                        goto gm_lb16;
                    }
gm_lb18:;
                    {
                        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t223 = (uint8_t)(t222 == ((uint8_t)8ULL));
                        if (!t223) {
                            goto gm_lb19;
                        }
                        goto gm_lb16;
                    }
gm_lb19:;
                    {
                        uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t225 = (uint8_t)(t224 == ((uint8_t)11ULL));
                        if (!t225) {
                            goto gm_lb20;
                        }
                        goto gm_lb16;
                    }
gm_lb20:;
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    {
                        void * t226 = gm_l0;
                        void * t227 = GM_LD(void *, ((uint8_t *)t226));
                        void * t228 = (void *)(((uint8_t *)t227) + 8);
                        gm_t88rcucg1b7uk t229 = gm_l2;
                        gm_t5v8n6qrn4rh4 t230;
                        gm_f_repr_53quqki((void *)&t230, (void *)NULL, cx, NULL, t228, t229);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t231 = gm_l0;
                        void * t232 = GM_LD(void *, ((uint8_t *)t231));
                        void * t233 = (void *)(((uint8_t *)t232) + 8);
                        gm_t88rcucg1b7uk t234 = gm_l4;
                        gm_t5v8n6qrn4rh4 t235;
                        gm_f_repr_53quqki((void *)&t235, (void *)NULL, cx, NULL, t233, t234);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t236 = (gm_eqhcfklnokct(((uint8_t *)&t230), ((uint8_t *)&t235)));
                        if (!t236) {
                            goto gm_lb22;
                        }
                        void * t237 = gm_l0;
                        void * t238 = GM_LD(void *, ((uint8_t *)t237));
                        void * t239 = (void *)(((uint8_t *)t238) + 8);
                        gm_t88rcucg1b7uk t240 = gm_l2;
                        gm_t5v8n6qrn4rh4 t241;
                        gm_f_repr_53quqki((void *)&t241, (void *)NULL, cx, NULL, t239, t240);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&t241) + 0));
                        uint8_t t243 = (uint8_t)(!(t242 == ((uint8_t)6ULL)));
                        if (!t243) {
                            goto gm_lb22;
                        }
                        gm_b16_8 t244 = gm_l5;
                        gm_t5o0tpv5jka2l t245;
                        GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t245 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t245 + 32, 0);
                        ((uint8_t *)&t245)[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&t245 + 8), t244);
                        gm_t5v8n6qrn4rh4 t246;
                        GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t246 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t246 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)6ULL));
                        memmove(gm_out, ((uint8_t *)&t245), 40);
                        goto gm_fdone;
                    }
gm_lb22:;
                }
gm_lb21:;
                void * t247 = gm_l0;
                gm_t88rcucg1b7uk t248 = gm_l4;
                gm_b16_8 t249 = gm_l5;
                gm_b16_8 t250 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t247, t248, t249);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t250;
                void * t251 = gm_l0;
                gm_t88rcucg1b7uk t252 = gm_l2;
                gm_b16_8 t253 = gm_l20;
                gm_b16_8 t254 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t251, t252, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t255;
                GM_ST(uint8_t, (uint8_t *)&t255 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t255 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t255 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t255 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t255 + 32, 0);
                ((uint8_t *)&t255)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t255 + 8), t254);
                memmove(gm_out, ((uint8_t *)&t255), 40);
                goto gm_fdone;
            }
gm_lb15:;
        }
        {
            {
                uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t257 = (uint8_t)(t256 == ((uint8_t)7ULL));
                if (!t257) {
                    goto gm_lb23;
                }
                {
                    {
                        void * t258 = gm_l0;
                        void * t259 = GM_LD(void *, ((uint8_t *)t258));
                        void * t260 = (void *)(((uint8_t *)t259) + 8);
                        gm_t88rcucg1b7uk t261 = gm_l4;
                        gm_f_kind_ghvav22((void *)&gm_l32, t260, GM_CONV(gm_csguankko4lmhd, t261));
                        uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                        uint8_t t263 = (uint8_t)(t262 == ((uint8_t)8ULL));
                        if (!t263) {
                            goto gm_lb25;
                        }
                        gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l32) + 8) + 4));
                        gm_l21 = t264;
                        {
                            {
                                void * t265 = gm_l0;
                                void * t266 = GM_LD(void *, ((uint8_t *)t265));
                                void * t267 = (void *)(((uint8_t *)t266) + 8);
                                gm_t88rcucg1b7uk t268 = gm_l21;
                                gm_f_kind_ghvav22((void *)&gm_l33, t267, GM_CONV(gm_csguankko4lmhd, t268));
                                uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                                uint8_t t270 = (uint8_t)(t269 == ((uint8_t)11ULL));
                                if (!t270) {
                                    goto gm_lb27;
                                }
                                uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                gm_l22 = t271;
                                void * t272 = gm_l0;
                                gm_t88rcucg1b7uk t273 = gm_l2;
                                gm_b16_8 t274 = gm_l5;
                                uint64_t t275 = gm_l22;
                                gm_t4ce0jd3tdro4 t276;
                                GM_ST(uint64_t, ((uint8_t *)&t276 + 0), t275);
                                gm_f_format_1amnh72((void *)&gm_l34, (void *)NULL, cx, NULL, t276);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t277 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                if (!t277) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src0gtuh94b1fce, 708, 40);
                                gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
                                gm_b16_8 t280 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t272, t273, t274, t279);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t5o0tpv5jka2l t281;
                                GM_ST(uint8_t, (uint8_t *)&t281 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t281 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t281 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t281 + 24, 0);
                                GM_ST(uint64_t, (uint8_t *)&t281 + 32, 0);
                                ((uint8_t *)&t281)[0] = 0;
                                GM_ST(gm_b16_8, ((uint8_t *)&t281 + 8), t280);
                                memmove(gm_out, ((uint8_t *)&t281), 40);
                                goto gm_fdone;
                            }
gm_lb27:;
                            gm_tl874t2atnlh3 t282 = gm_l1;
                            gm_b16_8 t283;
                            GM_ST(void *, (uint8_t *)&t283, (void *)(gm_spm6tgoorr19q));
                            GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(28));
                            gm_t08olq97rscn3 t284;
                            gm_f_unsupported_531q51r((void *)&t284, (void *)NULL, cx, NULL, t282, t283);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t5o0tpv5jka2l t285;
                            GM_ST(uint8_t, (uint8_t *)&t285 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t285 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t285 + 4, 0);
                            ((uint8_t *)&t285)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t285 + 8), t284);
                            memmove(gm_out, ((uint8_t *)&t285), 40);
                            goto gm_fdone;
                        }
gm_lb26:;
                    }
gm_lb25:;
                    gm_tl874t2atnlh3 t286 = gm_l1;
                    gm_b16_8 t287;
                    GM_ST(void *, (uint8_t *)&t287, (void *)(gm_spm6tgoorr19q));
                    GM_ST(uintptr_t, (uint8_t *)&t287 + 8, (uintptr_t)(28));
                    gm_t08olq97rscn3 t288;
                    gm_f_unsupported_531q51r((void *)&t288, (void *)NULL, cx, NULL, t286, t287);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t289;
                    GM_ST(uint8_t, (uint8_t *)&t289 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t289 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t289 + 4, 0);
                    ((uint8_t *)&t289)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t289 + 8), t288);
                    memmove(gm_out, ((uint8_t *)&t289), 40);
                    goto gm_fdone;
                }
gm_lb24:;
            }
gm_lb23:;
            {
                uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t291 = (uint8_t)(t290 == ((uint8_t)9ULL));
                if (!t291) {
                    goto gm_lb28;
                }
                {
                    {
                        void * t292 = gm_l0;
                        void * t293 = GM_LD(void *, ((uint8_t *)t292));
                        void * t294 = (void *)(((uint8_t *)t293) + 8);
                        gm_t88rcucg1b7uk t295 = gm_l4;
                        gm_t5v8n6qrn4rh4 t296;
                        gm_f_repr_53quqki((void *)&t296, (void *)NULL, cx, NULL, t294, t295);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&t296) + 0));
                        uint8_t t298 = (uint8_t)((t297 == ((uint8_t)3ULL)));
                        if (!t298) {
                            goto gm_lb30;
                        }
                        void * t299 = gm_l0;
                        void * t300 = GM_LD(void *, ((uint8_t *)t299));
                        void * t301 = (void *)(((uint8_t *)t300) + 8);
                        gm_t88rcucg1b7uk t302 = gm_l2;
                        gm_t5v8n6qrn4rh4 t303;
                        GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t303 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t303 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t303 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t303 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)3ULL));
                        gm_t5v8n6qrn4rh4 t304;
                        gm_f_repr_53quqki((void *)&t304, (void *)NULL, cx, NULL, t301, t302);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&t304) + 0));
                        uint8_t t306 = (uint8_t)((t305 == ((uint8_t)4ULL)));
                        if (!t306) {
                            goto gm_lb30;
                        }
                        void * t307 = gm_l0;
                        gm_t88rcucg1b7uk t308 = gm_l4;
                        gm_t5v8n6qrn4rh4 t309;
                        GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t309 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t309 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)4ULL));
                        uint8_t t310 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t307, t308);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t310) {
                            gm_b16_8 t311 = gm_l5;
                            gm_tnpifv20irnph t312;
                            GM_ST(gm_b16_8, ((uint8_t *)&t312 + 0), t311);
                            gm_f_format_8sjjbd9((void *)&gm_l36, (void *)NULL, cx, NULL, t312);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t313 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                            if (!t313) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src0gtuh94b1fce, 712, 59);
                            gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                            gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                            gm_l35 = t315;
                        } else {
                            gm_b16_8 t316 = gm_l5;
                            gm_l35 = t316;
                        }
                        gm_b16_8 t317 = gm_l35;
                        gm_l23 = t317;
                        void * t318 = gm_l0;
                        gm_t88rcucg1b7uk t319 = gm_l2;
                        gm_b16_8 t320 = gm_l23;
                        void * t321 = gm_l0;
                        gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)t321) + 200));
                        gm_t6j6smj7qoctu t323;
                        GM_ST(gm_b16_8, ((uint8_t *)&t323 + 0), t320);
                        GM_ST(gm_b16_8, ((uint8_t *)&t323 + 16), t322);
                        gm_f_format_0017fui((void *)&gm_l37, (void *)NULL, cx, NULL, &t323);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t324 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                        if (!t324) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src0gtuh94b1fce, 713, 34);
                        gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                        gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&t325));
                        gm_b16_8 t327 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t318, t319, t326);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t5o0tpv5jka2l t328;
                        GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t328 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t328 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t328 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t328 + 32, 0);
                        ((uint8_t *)&t328)[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&t328 + 8), t327);
                        memmove(gm_out, ((uint8_t *)&t328), 40);
                        goto gm_fdone;
                    }
gm_lb30:;
                }
gm_lb29:;
                gm_b16_8 t329 = gm_l5;
                gm_t5o0tpv5jka2l t330;
                GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t330 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t330 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t330 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t330 + 32, 0);
                ((uint8_t *)&t330)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t330 + 8), t329);
                memmove(gm_out, ((uint8_t *)&t330), 40);
                goto gm_fdone;
            }
gm_lb28:;
        }
        {
            {
                uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                uint8_t t332 = (uint8_t)(t331 == ((uint8_t)12ULL));
                if (!t332) {
                    goto gm_lb31;
                }
                gm_tl874t2atnlh3 t333 = gm_l1;
                gm_b16_8 t334;
                GM_ST(void *, (uint8_t *)&t334, (void *)(gm_sfrlts7iu6kt3));
                GM_ST(uintptr_t, (uint8_t *)&t334 + 8, (uintptr_t)(22));
                gm_t08olq97rscn3 t335;
                gm_f_unsupported_531q51r((void *)&t335, (void *)NULL, cx, NULL, t333, t334);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t336;
                GM_ST(uint8_t, (uint8_t *)&t336 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t336 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t336 + 4, 0);
                ((uint8_t *)&t336)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t336 + 8), t335);
                memmove(gm_out, ((uint8_t *)&t336), 40);
                goto gm_fdone;
            }
gm_lb31:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 667, 5);
        }
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
inline __attribute__((aligned(4))) void gm_f_emit_buf_mqofii1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_emit_buf_mqofii1 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l2; /* b */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* sl */
    GM_STACK_CHECK(&F, 695u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tqgtd5gtgj65s gm_l7;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_b0_1 t5 = gm_f_linef_qch21ro((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = gm_l0;
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 144));
    uint64_t r9; bool o10 = __builtin_add_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 601, 5);
    uint64_t t11 = r9;
    GM_ST(uint64_t, (((uint8_t *)t6) + 144), t11);
    void * t12 = gm_l0;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = (void *)(((uint8_t *)t14) + 8);
    gm_t88rcucg1b7uk t16 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t17 = gm_l2;
    gm_tnpifv20irnph t18;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
    gm_f_format_he684oe((void *)&gm_l5, (void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src0gtuh94b1fce, 602, 46);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
    gm_b16_8 t22 = gm_l2;
    gm_tnpifv20irnph t23;
    GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
    gm_f_format_0lf0h5l((void *)&gm_l6, (void *)NULL, cx, NULL, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src0gtuh94b1fce, 602, 73);
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    gm_b16_8 t27 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t12, t16, t21, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t27;
    void * t28 = gm_l0;
    gm_b16_8 t29 = gm_l4;
    gm_tl874t2atnlh3 t30 = gm_l3;
    gm_teegpca3bk79b t31 = GM_LD(gm_teegpca3bk79b, ((uint8_t *)a1));
    gm_f_emit_to_o7js99p((void *)&gm_l7, (void *)NULL, cx, NULL, t28, &t31, t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t32) {
    } else {
        gm_t08olq97rscn3 t33 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l7) + 8));
        gm_tqgtd5gtgj65s t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        ((uint8_t *)&t34)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t34 + 8), t33);
        memmove(gm_out, ((uint8_t *)&t34), 40);
        goto gm_fdone;
    }
    void * t35 = gm_l0;
    void * t36 = gm_l0;
    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 144));
    uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
    if (o39) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 604, 5);
    uint64_t t40 = r38;
    GM_ST(uint64_t, (((uint8_t *)t35) + 144), t40);
    void * t41 = gm_l0;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s2cuflhqkkuaq));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
    gm_b0_1 t43 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t44 = gm_l0;
    gm_b16_8 t45 = gm_l2;
    gm_tnpifv20irnph t46;
    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
    gm_b0_1 t47 = gm_f_linef_sgpievq((void *)NULL, cx, NULL, t44, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tqgtd5gtgj65s t48;
    memset(&t48, 0, sizeof t48);
    ((uint8_t *)&t48)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t48 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t48), 40);
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
inline __attribute__((aligned(4))) void gm_f_emit_to_o7js99p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_emit_to_o7js99p F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l2; /* slice */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l4; /* unit_ty */
    gm_tpmdr2hmdhsqr gm_l5; /* vals */
    gm_tnqk2qt7j42o9 gm_l6; /* tys */
    gm_tep2drsl9408b gm_l7; /* cv */
    gm_tiilmfd16v3fc gm_l8; /* effect_ */
    uint32_t gm_l9; /* op */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t5o0tpv5jka2l gm_l10;
    gm_t5o0tpv5jka2l gm_l11;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t8;
    gm_b16_8 t9 = gm_l2;
    gm_tpmdr2hmdhsqr t10;
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
    gm_l5 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_t88rcucg1b7uk t14 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnqk2qt7j42o9 t15;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 0), t14);
    gm_l6 = t15;
    {
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
            if (!t17) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l7), (((uint8_t *)a1) + 8), 24);
            void * t18 = gm_l0;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 16));
            void * t21 = (void *)((uint8_t *)&gm_l5);
            uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t23 = (uintptr_t)((uint64_t)1ULL);
            if (t22 > t23 || t23 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t22, t23, ((uint64_t)1ULL), gm_src0gtuh94b1fce, 616, 50);
            uint64_t t24 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            void * t27 = (void *)((uint8_t *)&gm_l6);
            uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t29 = (uintptr_t)((uint64_t)1ULL);
            if (t28 > t29 || t29 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t28, t29, ((uint64_t)1ULL), gm_src0gtuh94b1fce, 616, 60);
            uint64_t t30 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t27 + (uintptr_t)((uint64_t)0ULL) * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_t88rcucg1b7uk t33 = gm_l4;
            gm_f_call_closure_mha9p1e((void *)&gm_l10, (void *)NULL, cx, NULL, t18, t19, t20, t26, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (t34) {
            } else {
                gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l10) + 8));
                gm_tqgtd5gtgj65s t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                ((uint8_t *)&t36)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t36 + 8), t35);
                memmove(gm_out, ((uint8_t *)&t36), 40);
                goto gm_fdone;
            }
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)0ULL));
            if (!t39) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l8), (((uint8_t *)a1) + 8), 24);
            uint32_t t40 = GM_LD(uint32_t, ((((uint8_t *)a1) + 8) + 24));
            gm_l9 = t40;
            void * t41 = gm_l0;
            gm_tl874t2atnlh3 t42 = gm_l3;
            void * t43 = (void *)((uint8_t *)&gm_l8);
            uint32_t t44 = gm_l9;
            void * t45 = (void *)((uint8_t *)&gm_l5);
            uint64_t t46 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t47 = (uintptr_t)((uint64_t)1ULL);
            if (t46 > t47 || t47 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t46, t47, ((uint64_t)1ULL), gm_src0gtuh94b1fce, 619, 52);
            uint64_t t48 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t45 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48));
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            void * t51 = (void *)((uint8_t *)&gm_l6);
            uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t53 = (uintptr_t)((uint64_t)1ULL);
            if (t52 > t53 || t53 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t52, t53, ((uint64_t)1ULL), gm_src0gtuh94b1fce, 619, 62);
            uint64_t t54 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t51 + (uintptr_t)((uint64_t)0ULL) * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
            gm_t88rcucg1b7uk t57 = gm_l4;
            gm_f_perform_t77ghfg((void *)&gm_l11, (void *)NULL, cx, NULL, t41, t42, t43, t44, t50, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (t58) {
            } else {
                gm_t08olq97rscn3 t59 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l11) + 8));
                gm_tqgtd5gtgj65s t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                ((uint8_t *)&t60)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t60 + 8), t59);
                memmove(gm_out, ((uint8_t *)&t60), 40);
                goto gm_fdone;
            }
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 614, 5);
    }
gm_lb1:;
    gm_tqgtd5gtgj65s t62;
    memset(&t62, 0, sizeof t62);
    ((uint8_t *)&t62)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t62 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t62), 40);
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
extern __attribute__((aligned(4))) uint8_t gm_f_find_with_gcs5p4r(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_float_lit_26lt2tp(void *env, gm_node *cx, gm_node *inst, uint16_t a0, double a1) {
    struct gm_fr_float_lit_26lt2tp F;
    uint16_t gm_l0; /* bits */
    double gm_l1; /* x */
    gm_b16_8 gm_l2; /* c */
    GM_STACK_CHECK(&F, 534u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    {
        {
            uint16_t t6 = gm_l0;
            uint8_t t7 = ((t6 == ((uint16_t)16ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_scj0sm2kpivk4));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(8));
            gm_l3 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint16_t t9 = gm_l0;
            uint8_t t10 = ((t9 == ((uint16_t)32ULL)));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s4cess8gum6i7));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
            gm_l3 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s866ofni9sb8u));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(6));
            gm_l3 = t12;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 756, 10);
    }
gm_lb1:;
    gm_b16_8 t13 = gm_l3;
    gm_l2 = t13;
    double t14 = gm_l1;
    uint8_t t15 = (uint8_t)__builtin_isnan(t14);
    if (t15) {
        gm_b16_8 t16 = gm_l2;
        gm_tnpifv20irnph t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
        gm_f_format_8b7o2bp((void *)&gm_l4, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src0gtuh94b1fce, 761, 28);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        F.ret = t20;
        goto gm_fret;
    }
    double t21 = gm_l1;
    uint8_t t22 = (uint8_t)__builtin_isinf(t21);
    if (t22) {
        gm_b16_8 t23 = gm_l2;
        double t24 = gm_l1;
        uint8_t t25 = (t24 < ((double)0e0));
        if (t25) {
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(1));
            gm_l5 = t26;
        } else {
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(0));
            gm_l5 = t27;
        }
        gm_b16_8 t28 = gm_l5;
        gm_t6j6smj7qoctu t29;
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t23);
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 16), t28);
        gm_f_format_p7tc45u((void *)&gm_l6, (void *)NULL, cx, NULL, &t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src0gtuh94b1fce, 762, 28);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        F.ret = t32;
        goto gm_fret;
    }
    gm_b16_8 t33 = gm_l2;
    double t34 = gm_l1;
    gm_t2mvr8fpgjkg6 t35;
    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t33);
    GM_ST(double, ((uint8_t *)&t35 + 16), t34);
    gm_f_format_vjen3ng((void *)&gm_l7, (void *)NULL, cx, NULL, &t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src0gtuh94b1fce, 764, 12);
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_gdv4o16(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_okfag60(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_global_addr_fili22i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_global_addr_fili22i F;
    void * gm_l0; /* g */
    uint64_t gm_l1; /* decl */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l5; /* t */
    GM_STACK_CHECK(&F, 575u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l6;
    uint64_t gm_l7;
    uint8_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = gm_l1;
    gm_tf5srh4mp4b3i t10 = gm_f_global_index_nhjk80t(t8, t9);
    gm_l6 = t10;
    uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
    if (t11) {
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l7 = t12;
    } else {
        gm_tl874t2atnlh3 t13 = gm_l2;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sk7tgk6pi62vu));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(13));
        gm_t08olq97rscn3 t15;
        gm_f_unsupported_531q51r((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        ((uint8_t *)&t16)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t16 + 8), t15);
        memmove(gm_out, ((uint8_t *)&t16), 40);
        goto gm_fdone;
    }
    uint64_t t17 = gm_l7;
    gm_l3 = t17;
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, ((uint8_t *)t18));
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1568));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1568) + 8));
    uint64_t t23 = gm_l3;
    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_src0gtuh94b1fce, 640, 11);
    memmove(((uint8_t *)&F.l4), (((uint8_t *)t21) + (uintptr_t)t23 * 104), 104);
    void * t24 = gm_l0;
    uint64_t t25 = gm_l3;
    gm_b16_8 t26 = gm_f_global_tag_gs7s3p6((void *)NULL, cx, NULL, t24, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t26;
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
            if (!t27) {
                goto gm_lb2;
            }
            {
                {
                    uint8_t t28 = (uint8_t)(((((uint8_t *)&F.l4) + 56))[16] != 0);
                    if (t28) {
                        goto gm_lb4;
                    }
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l8 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 642, 28);
            }
gm_lb3:;
            uint8_t t29 = gm_l8;
            if (!t29) {
                goto gm_lb2;
            }
            gm_b16_8 t30 = gm_l5;
            gm_tnpifv20irnph t31;
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
            gm_f_format_4g0prpe((void *)&gm_l9, (void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src0gtuh94b1fce, 642, 58);
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_t5o0tpv5jka2l t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 32, 0);
            ((uint8_t *)&t35)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t35 + 8), t34);
            memmove(gm_out, ((uint8_t *)&t35), 40);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t36 = gm_l5;
    gm_tnpifv20irnph t37;
    GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
    gm_f_format_7peeku4((void *)&gm_l10, (void *)NULL, cx, NULL, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src0gtuh94b1fce, 643, 12);
    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
    gm_t5o0tpv5jka2l t41;
    GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t41 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t41 + 32, 0);
    ((uint8_t *)&t41)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 8), t40);
    memmove(gm_out, ((uint8_t *)&t41), 40);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_index_check_m2p0u1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_int_lit_ai37456 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint16_t gm_l3; /* f */
    uint8_t gm_l4; /* signed */
    uint64_t gm_l5; /* storage */
    gm_b16_8 gm_l6; /* c */
    unsigned __int128 gm_l7; /* lo */
    unsigned __int128 gm_l8; /* hi */
    __int128 gm_l9; /* x */
    unsigned __int128 gm_l10; /* lo */
    GM_STACK_CHECK(&F, 1371u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_t5v8n6qrn4rh4 gm_l11;
    gm_t793vlgpa3b88 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_ta24rd13gupia gm_l14;
    __int128 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_b16_8 gm_l19;
    gm_t793vlgpa3b88 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_f_repr_53quqki((void *)&gm_l11, (void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)1ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint16_t t14 = GM_LD(uint16_t, (((uint8_t *)&gm_l11) + 8));
            gm_l3 = t14;
            uint16_t t15 = gm_l3;
            gm_tngtuiuskn938 t16;
            gm_f_from_int_ofjuoqh((void *)&t16, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            double t17 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t18 = gm_f_float_lit_26lt2tp((void *)NULL, cx, NULL, t15, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t18;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb3;
            }
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 8));
            gm_l4 = t21;
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)&gm_l11) + 8) + 8));
            gm_l5 = t22;
            uint8_t t23 = gm_l4;
            uint64_t t24 = gm_l5;
            gm_b16_8 t25 = gm_f_int_cty_s62af0i(t23, t24);
            gm_l6 = t25;
            gm_tbci5ospk5nnn t26 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
            gm_f_split_u128_vrmr6en((void *)&gm_l12, &t26);
            unsigned __int128 t27 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l12));
            gm_l7 = t27;
            unsigned __int128 t28 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l12) + 16));
            gm_l8 = t28;
            uint64_t t29 = gm_l5;
            uint8_t t30 = ((t29 == ((uint64_t)16ULL)));
            if (t30) {
                gm_b16_8 t31 = gm_l6;
                unsigned __int128 t32 = gm_l8;
                uint64_t t33 = (uint64_t)t32;
                unsigned __int128 t34 = gm_l7;
                uint64_t t35 = (uint64_t)t34;
                gm_tgbgti82mf879 t36;
                GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t31);
                GM_ST(uint64_t, ((uint8_t *)&t36 + 16), t33);
                GM_ST(uint64_t, ((uint8_t *)&t36 + 24), t35);
                gm_f_format_dp1qh3k((void *)&gm_l13, (void *)NULL, cx, NULL, &t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src0gtuh94b1fce, 262, 24);
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                F.ret = t39;
                goto gm_fret;
            } else {
                uint8_t t40 = gm_l4;
                if (t40) {
                    gm_tbci5ospk5nnn t41 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
                    gm_f_to_i128_o7ud1ue((void *)&gm_l14, &t41);
                    uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                    if (t42) {
                        __int128 t43 = GM_LD(__int128, ((uint8_t *)&gm_l14));
                        gm_l15 = t43;
                    } else {
                        gm_l15 = ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                    }
                    __int128 t44 = gm_l15;
                    gm_l9 = t44;
                    __int128 t45 = gm_l9;
                    uint8_t t46 = ((t45 == ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 9223372036854775808ULL))));
                    if (t46) {
                        gm_b16_8 t47 = gm_l6;
                        gm_tnpifv20irnph t48;
                        GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                        gm_f_format_ij8u0vj((void *)&gm_l16, (void *)NULL, cx, NULL, t48);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                        if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src0gtuh94b1fce, 265, 55);
                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                        F.ret = t51;
                        goto gm_fret;
                    }
                    gm_b16_8 t52 = gm_l6;
                    __int128 t53 = gm_l9;
                    gm_tctdkl637nfjf t54;
                    GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t52);
                    GM_ST(__int128, ((uint8_t *)&t54 + 16), t53);
                    gm_f_format_jb7bqji((void *)&gm_l17, (void *)NULL, cx, NULL, &t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src0gtuh94b1fce, 266, 24);
                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                    F.ret = t57;
                    goto gm_fret;
                } else {
                    gm_b16_8 t58 = gm_l6;
                    unsigned __int128 t59 = gm_l7;
                    uint64_t t60 = (uint64_t)t59;
                    gm_t3877pk7lv748 t61;
                    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t58);
                    GM_ST(uint64_t, ((uint8_t *)&t61 + 16), t60);
                    gm_f_format_tt4n370((void *)&gm_l18, (void *)NULL, cx, NULL, &t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src0gtuh94b1fce, 268, 24);
                    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                    F.ret = t64;
                    goto gm_fret;
                }
            }
        }
gm_lb3:;
        {
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)2ULL));
            if (!t66) {
                goto gm_lb4;
            }
            uint8_t t67 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)));
            if (t67) {
                gm_b16_8 t68;
                GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sicgmpca63jln));
                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
                gm_l19 = t68;
            } else {
                gm_b16_8 t69;
                GM_ST(void *, (uint8_t *)&t69, (void *)(gm_svubtddeidu1d));
                GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(1));
                gm_l19 = t69;
            }
            gm_b16_8 t70 = gm_l19;
            F.ret = t70;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)3ULL));
            if (!t72) {
                goto gm_lb5;
            }
            gm_tbci5ospk5nnn t73 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
            gm_f_split_u128_vrmr6en((void *)&gm_l20, &t73);
            unsigned __int128 t74 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l20));
            gm_l10 = t74;
            unsigned __int128 t75 = gm_l10;
            uint64_t t76 = (uint64_t)t75;
            gm_t4ce0jd3tdro4 t77;
            GM_ST(uint64_t, ((uint8_t *)&t77 + 0), t76);
            gm_f_format_p7hqke6((void *)&gm_l21, (void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src0gtuh94b1fce, 274, 20);
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            F.ret = t80;
            goto gm_fret;
        }
gm_lb5:;
        {
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(1));
            F.ret = t81;
            goto gm_fret;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 256, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_temp_rrllgdr(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_temp_rrllgdr F;
    gm_b16_8 gm_l0; /* v */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t8 = (t7 < ((uint64_t)2ULL));
    if (t8) {
        gm_l4 = ((uint8_t)1);
    } else {
        void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t10, gm_src0gtuh94b1fce, 27, 21);
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t12 = (!(t11 == ((uint8_t)116ULL)));
        gm_l4 = t12;
    }
    uint8_t t13 = gm_l4;
    if (t13) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t16 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t17 = (uintptr_t)t15;
    if (t16 > t17 || t17 > (uintptr_t)t15) gm_trap_slice(t16, t17, t15, gm_src0gtuh94b1fce, 28, 14);
    uint64_t t18 = ((uintptr_t)t15) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t14 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18));
    gm_l1 = t19;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l2;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t25 = gm_l2;
                {
                    {
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t23) + (uintptr_t)t25 * 1));
                        gm_l3 = t26;
                        uint8_t t27 = gm_l3;
                        uint8_t t28 = (t27 < ((uint8_t)48ULL));
                        if (t28) {
                            gm_l5 = ((uint8_t)1);
                        } else {
                            uint8_t t29 = gm_l3;
                            uint8_t t30 = (t29 > ((uint8_t)57ULL));
                            gm_l5 = t30;
                        }
                        uint8_t t31 = gm_l5;
                        if (t31) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 28, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t32 = gm_l2;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            uint64_t t35 = r33;
            gm_l2 = t35;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_with_label_rskslsd(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_is_with_label_rskslsd F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* label */
    uint32_t gm_l3; /* f */
    void * gm_l4; /* b */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tommf4j9upomc gm_l5;
    void * t4 = gm_l0;
    uint32_t t5 = gm_l1;
    gm_f_frame_up_g8486eu((void *)&gm_l5, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 16));
    gm_l3 = t6;
    {
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((uint8_t *)t7));
            void * t9 = GM_LD(void *, ((uint8_t *)t8));
            uint32_t t10 = gm_l3;
            void * t11 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = (uint8_t)(GM_LD(void *, (((uint8_t *)t11) + 152)) != 0);
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = GM_LD(void *, (((uint8_t *)t11) + 152));
            gm_l4 = t13;
            void * t14 = gm_l4;
            uint32_t t15 = gm_l2;
            uint8_t t16 = gm_f_find_with_gcs5p4r(t14, t15);
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_load_0hebde7 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* a */
    gm_b16_8 gm_l3; /* c */
    gm_t3877pk7lv748 gm_l4; /* p */
    gm_b16_8 gm_l5; /* ld */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* v */
    GM_STACK_CHECK(&F, 786u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9geo0l9gngfn gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tqcc6dmlne11b gm_l11;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_b16_8 t10 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_t88rcucg1b7uk t14 = gm_l1;
    uint8_t t15 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t15) {
        {
            {
                gm_b16_8 t16 = gm_l2;
                gm_f_temp_piece_572g74q((void *)&gm_l8, t16);
                uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l8))[24] != 0);
                if (!t17) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l8), 24);
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
                void * t19 = gm_l0;
                void * t20 = GM_LD(void *, ((uint8_t *)t19));
                void * t21 = (void *)(((uint8_t *)t20) + 8);
                gm_t88rcucg1b7uk t22 = gm_l1;
                uint64_t t23 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t23 == 0) gm_trap("division by zero", gm_src0gtuh94b1fce, 94, 16);
                uint64_t t24 = (uint64_t)(t18 % t23);
                uint8_t t25 = ((t24 == ((uint64_t)0ULL)));
                if (t25) {
                    gm_b16_8 t26 = gm_l3;
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
                    gm_tftksr4op114e t29;
                    GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t26);
                    GM_ST(gm_b16_8, ((uint8_t *)&t29 + 16), t27);
                    GM_ST(uint64_t, ((uint8_t *)&t29 + 32), t28);
                    gm_f_format_ccpi8au((void *)&gm_l9, (void *)NULL, cx, NULL, &t29);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                    if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src0gtuh94b1fce, 94, 55);
                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                    F.ret = t32;
                    goto gm_fret;
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    gm_b16_8 t33 = gm_l3;
    gm_b16_8 t34 = gm_l2;
    gm_t6j6smj7qoctu t35;
    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t33);
    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 16), t34);
    gm_f_format_voj344o((void *)&gm_l10, (void *)NULL, cx, NULL, &t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src0gtuh94b1fce, 97, 11);
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_l5 = t37;
    {
        {
            void * t38 = gm_l0;
            gm_t88rcucg1b7uk t39 = gm_l1;
            gm_f_int_info_2tcdf1n((void *)&gm_l11, (void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
            if (!t40) {
                goto gm_lb4;
            }
            gm_tuglhjjmj2g58 t41 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l11));
            gm_l6 = t41;
            gm_tuglhjjmj2g58 t42 = gm_l6;
            gm_b16_8 t43 = gm_l5;
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
            gm_b16_8 t45 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t42, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t45;
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l1;
            gm_b16_8 t48 = gm_l7;
            gm_b16_8 t49 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t46, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t49;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t50 = gm_l0;
    gm_t88rcucg1b7uk t51 = gm_l1;
    gm_b16_8 t52 = gm_l5;
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
    gm_b16_8 t54 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t50, t51, t53);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t54;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_none_v2s8s0l F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_t5v8n6qrn4rh4 t6;
    gm_f_repr_53quqki((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)3ULL)));
    gm_t5v8n6qrn4rh4 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)3ULL));
    if (t8) {
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sr3mniof9fusf));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(11));
        F.ret = t10;
        goto gm_fret;
    }
    void * t11 = gm_l0;
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_b16_8 t13 = gm_f_blob_d5apnrc((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t13;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_res_riv19po(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_make_res_riv19po F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* is_err */
    gm_t88rcucg1b7uk gm_l3; /* payload_ty */
    gm_b16_8 gm_l4; /* payload */
    gm_b16_8 gm_l5; /* offs */
    gm_te3hi449i91a8 gm_l6; /* parts */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 667u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    int32_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t6;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t8, gm_src0gtuh94b1fce, 223, 40);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)((uint64_t)0ULL) * 8));
    gm_t2lqu8jsj511d t10;
    GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t9);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 8), ((uint64_t)1ULL));
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t12, gm_src0gtuh94b1fce, 223, 72);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t11) + (uintptr_t)((uint64_t)1ULL) * 8));
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_t88rcucg1b7uk t17 = gm_l3;
    uint64_t t18 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t2lqu8jsj511d t19;
    GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t13);
    GM_ST(uint64_t, ((uint8_t *)&t19 + 8), t18);
    gm_te3hi449i91a8 t20;
    GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t20 + 0), t10);
    GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t20 + 16), t19);
    gm_l6 = t20;
    void * t21 = gm_l0;
    gm_t88rcucg1b7uk t22 = gm_l1;
    void * t23 = (void *)((uint8_t *)&gm_l6);
    uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t25 = (uintptr_t)((uint64_t)2ULL);
    if (t24 > t25 || t25 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t24, t25, ((uint64_t)2ULL), gm_src0gtuh94b1fce, 224, 26);
    uint64_t t26 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t23 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    gm_b16_8 t29 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t21, t22, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t29;
    void * t30 = gm_l0;
    gm_b16_8 t31 = gm_l7;
    void * t32 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t33, gm_src0gtuh94b1fce, 225, 47);
    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t32) + (uintptr_t)((uint64_t)0ULL) * 8));
    uint8_t t35 = gm_l2;
    if (t35) {
        gm_l8 = ((int32_t)1LL);
    } else {
        gm_l8 = ((int32_t)0LL);
    }
    int32_t t36 = gm_l8;
    gm_tlk7da9ssbu7q t37;
    GM_ST(uint32_t, (uint8_t *)&t37 + 28, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t31);
    GM_ST(uint64_t, ((uint8_t *)&t37 + 16), t34);
    GM_ST(int32_t, ((uint8_t *)&t37 + 24), t36);
    gm_b0_1 t38 = gm_f_linef_t20u0i3((void *)NULL, cx, NULL, t30, &t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t39 = gm_l0;
    gm_t88rcucg1b7uk t40 = gm_l3;
    gm_b16_8 t41 = gm_l7;
    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t43, gm_src0gtuh94b1fce, 226, 65);
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t42) + (uintptr_t)((uint64_t)1ULL) * 8));
    gm_t3877pk7lv748 t45;
    GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t41);
    GM_ST(uint64_t, ((uint8_t *)&t45 + 16), t44);
    gm_f_format_tcr2jpk((void *)&gm_l9, (void *)NULL, cx, NULL, &t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src0gtuh94b1fce, 226, 26);
    gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
    gm_b16_8 t49 = gm_l4;
    gm_b0_1 t50 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t39, t40, t48, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t51 = gm_l7;
    F.ret = t51;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_make_slice_sa50efe F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* ptr */
    gm_b16_8 gm_l3; /* len */
    gm_b16_8 gm_l4; /* b */
    uint64_t gm_l5; /* p */
    GM_STACK_CHECK(&F, 320u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = gm_f_ptr_bytes_eocneng(t5);
    gm_l5 = t6;
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l4;
    gm_b16_8 t9 = gm_l2;
    gm_t6j6smj7qoctu t10;
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 16), t9);
    gm_b0_1 t11 = gm_f_linef_sm7hlk3((void *)NULL, cx, NULL, t7, &t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = gm_l0;
    gm_b16_8 t13 = gm_l4;
    uint64_t t14 = gm_l5;
    gm_b16_8 t15 = gm_l3;
    gm_tq3m5qgvkpje8 t16;
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t13);
    GM_ST(uint64_t, ((uint8_t *)&t16 + 16), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 24), t15);
    gm_b0_1 t17 = gm_f_linef_lhoje7n((void *)NULL, cx, NULL, t12, &t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t18 = gm_l4;
    F.ret = t18;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_make_some_9aqq928 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* payload */
    gm_t88rcucg1b7uk gm_l3; /* inner */
    uint64_t gm_l4; /* off */
    gm_te3hi449i91a8 gm_l5; /* parts */
    gm_b16_8 gm_l6; /* b */
    GM_STACK_CHECK(&F, 777u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l7;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_t5v8n6qrn4rh4 t8;
    gm_f_repr_53quqki((void *)&t8, (void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
    uint8_t t10 = (uint8_t)((t9 == ((uint8_t)3ULL)));
    gm_t5v8n6qrn4rh4 t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)3ULL));
    if (t10) {
        gm_b16_8 t12 = gm_l2;
        F.ret = t12;
        goto gm_fret;
    }
    {
        {
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, ((uint8_t *)t13));
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            gm_t88rcucg1b7uk t16 = gm_l1;
            gm_f_kind_ghvav22((void *)&gm_l7, t15, GM_CONV(gm_csguankko4lmhd, t16));
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)13ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
            gm_l3 = t19;
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, ((uint8_t *)t20));
            void * t22 = (void *)(((uint8_t *)t21) + 8);
            gm_t88rcucg1b7uk t23 = gm_l1;
            gm_b16_8 t24 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t24;
            void * t25 = GM_LD(void *, ((uint8_t *)&gm_l8));
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t26, gm_src0gtuh94b1fce, 202, 12);
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t25) + (uintptr_t)((uint64_t)1ULL) * 8));
            gm_l4 = t27;
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, ((uint8_t *)t28));
            void * t30 = (void *)(((uint8_t *)t29) + 8);
            gm_t88rcucg1b7uk t31 = gm_l3;
            uint64_t t32 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2lqu8jsj511d t33;
            GM_ST(uint64_t, ((uint8_t *)&t33 + 0), ((uint64_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t33 + 8), t32);
            uint64_t t34 = gm_l4;
            gm_t2lqu8jsj511d t35;
            GM_ST(uint64_t, ((uint8_t *)&t35 + 0), t34);
            GM_ST(uint64_t, ((uint8_t *)&t35 + 8), ((uint64_t)1ULL));
            gm_te3hi449i91a8 t36;
            GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t36 + 0), t33);
            GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t36 + 16), t35);
            gm_l5 = t36;
            void * t37 = gm_l0;
            gm_t88rcucg1b7uk t38 = gm_l1;
            void * t39 = (void *)((uint8_t *)&gm_l5);
            uint64_t t40 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t41 = (uintptr_t)((uint64_t)2ULL);
            if (t40 > t41 || t41 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t40, t41, ((uint64_t)2ULL), gm_src0gtuh94b1fce, 204, 26);
            uint64_t t42 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t39 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42));
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
            gm_b16_8 t45 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t37, t38, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t45;
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l3;
            gm_b16_8 t48 = gm_l6;
            gm_tnpifv20irnph t49;
            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
            gm_f_format_4un19t8((void *)&gm_l9, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src0gtuh94b1fce, 205, 21);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_b16_8 t53 = gm_l2;
            gm_b0_1 t54 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t46, t47, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t55 = gm_l0;
            gm_b16_8 t56 = gm_l6;
            uint64_t t57 = gm_l4;
            gm_t3877pk7lv748 t58;
            GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t56);
            GM_ST(uint64_t, ((uint8_t *)&t58 + 16), t57);
            gm_b0_1 t59 = gm_f_linef_k4dha91((void *)NULL, cx, NULL, t55, &t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t60 = gm_l6;
            F.ret = t60;
            goto gm_fret;
        }
gm_lb2:;
        gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 201, 46);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3) {
    struct gm_fr_make_variant_srnk8ha F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* v */
    gm_th2lqbo77pvsc gm_l3; /* payload */
    void * gm_l4; /* def */
    gm_t88rcucg1b7uk gm_l5; /* tag */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l8; /* offs */
    gm_t88rcucg1b7uk gm_l10; /* pt */
    gm_b16_8 gm_l11; /* b */
    gm_b16_8 gm_l12; /* tag_val */
    gm_b16_8 gm_l13; /* p */
    gm_t88rcucg1b7uk gm_l14; /* pt */
    GM_STACK_CHECK(&F, 944u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_tftb1odc739t5 gm_l15;
    uint8_t gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    void * t13 = gm_l0;
    gm_t88rcucg1b7uk t14 = gm_l1;
    void * t15 = gm_f_adt_def_ni54he1(t13, GM_CONV(gm_csguankko4lmhd, t14));
    gm_l4 = t15;
    {
        {
            void * t16 = gm_l4;
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t16) + 24) + 8));
            gm_l5 = t19;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((((uint8_t *)t16) + 24) + 8) + 8));
            gm_l6 = t20;
            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l6));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t t23 = gm_l2;
            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_src0gtuh94b1fce, 234, 16);
            memmove(((uint8_t *)&F.l7), (((uint8_t *)t21) + (uintptr_t)t23 * 72), 72);
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, ((uint8_t *)t24));
            void * t26 = (void *)(((uint8_t *)t25) + 8);
            gm_t88rcucg1b7uk t27 = gm_l1;
            gm_b16_8 t28 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t28;
            gm_trqfvmjtt4sij t29;
            gm_f_list_26fbt7s((void *)&t29, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l9 = t29;
            void * t30 = (void *)((uint8_t *)&F.l9);
            void * t31 = GM_LD(void *, ((uint8_t *)&gm_l8));
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t32, gm_src0gtuh94b1fce, 237, 30);
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t31) + (uintptr_t)((uint64_t)0ULL) * 8));
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, ((uint8_t *)t34));
            void * t36 = (void *)(((uint8_t *)t35) + 8);
            gm_t88rcucg1b7uk t37 = gm_l5;
            uint64_t t38 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2lqu8jsj511d t39;
            GM_ST(uint64_t, ((uint8_t *)&t39 + 0), t33);
            GM_ST(uint64_t, ((uint8_t *)&t39 + 8), t38);
            gm_tftb1odc739t5 t40 = gm_f_push_u0rp7b1(t30, GM_CONV(gm_csc7lc6jlkqkuq, t39));
            gm_l15 = t40;
            uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src0gtuh94b1fce, 237, 5);
            {
                {
                    uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                    if (t42) {
                        goto gm_lb4;
                    }
                    gm_l16 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 238, 8);
            }
gm_lb3:;
            uint8_t t43 = gm_l16;
            if (t43) {
                {
                    {
                        uint8_t t44 = (uint8_t)(((((uint8_t *)&F.l7) + 16))[4] != 0);
                        if (!t44) {
                            goto gm_lb7;
                        }
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                        gm_l10 = t45;
                        void * t46 = (void *)((uint8_t *)&F.l9);
                        void * t47 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t48, gm_src0gtuh94b1fce, 239, 61);
                        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t47) + (uintptr_t)((uint64_t)1ULL) * 8));
                        void * t50 = gm_l0;
                        void * t51 = GM_LD(void *, ((uint8_t *)t50));
                        void * t52 = (void *)(((uint8_t *)t51) + 8);
                        gm_t88rcucg1b7uk t53 = gm_l10;
                        uint64_t t54 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t52, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t2lqu8jsj511d t55;
                        GM_ST(uint64_t, ((uint8_t *)&t55 + 0), t49);
                        GM_ST(uint64_t, ((uint8_t *)&t55 + 8), t54);
                        gm_tftb1odc739t5 t56 = gm_f_push_u0rp7b1(t46, GM_CONV(gm_csc7lc6jlkqkuq, t55));
                        gm_l17 = t56;
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src0gtuh94b1fce, 239, 36);
                        goto gm_lb6;
                    }
gm_lb7:;
                }
gm_lb6:;
            }
            void * t58 = gm_l0;
            gm_t88rcucg1b7uk t59 = gm_l1;
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            gm_b16_8 t62 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t58, t59, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t62;
            void * t63 = gm_l0;
            gm_t88rcucg1b7uk t64 = gm_l5;
            gm_b16_8 t65 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t63, t64, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l7) + 24)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t65;
            void * t66 = gm_l0;
            gm_t88rcucg1b7uk t67 = gm_l5;
            gm_b16_8 t68 = gm_l11;
            void * t69 = GM_LD(void *, ((uint8_t *)&gm_l8));
            uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t70, gm_src0gtuh94b1fce, 243, 58);
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 8));
            gm_t3877pk7lv748 t72;
            GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t68);
            GM_ST(uint64_t, ((uint8_t *)&t72 + 16), t71);
            gm_f_format_tcr2jpk((void *)&gm_l18, (void *)NULL, cx, NULL, &t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src0gtuh94b1fce, 243, 19);
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            gm_b16_8 t76 = gm_l12;
            gm_b0_1 t77 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t66, t67, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                    if (!t78) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_l13 = t79;
                    {
                        {
                            uint8_t t80 = (uint8_t)(((((uint8_t *)&F.l7) + 16))[4] != 0);
                            if (!t80) {
                                goto gm_lb11;
                            }
                            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                            gm_l14 = t81;
                            void * t82 = gm_l0;
                            gm_t88rcucg1b7uk t83 = gm_l14;
                            gm_b16_8 t84 = gm_l11;
                            void * t85 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t86)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t86, gm_src0gtuh94b1fce, 245, 88);
                            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t85) + (uintptr_t)((uint64_t)1ULL) * 8));
                            gm_t3877pk7lv748 t88;
                            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t84);
                            GM_ST(uint64_t, ((uint8_t *)&t88 + 16), t87);
                            gm_f_format_tcr2jpk((void *)&gm_l19, (void *)NULL, cx, NULL, &t88);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src0gtuh94b1fce, 245, 49);
                            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                            gm_b16_8 t92 = gm_l13;
                            gm_b0_1 t93 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t82, t83, t91, t92);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                    }
gm_lb10:;
                    goto gm_lb8;
                }
gm_lb9:;
            }
gm_lb8:;
            gm_b16_8 t94 = gm_l11;
            F.ret = t94;
            goto gm_fret;
        }
gm_lb2:;
        gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 233, 44);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_mask_epjl3to(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_opt_present_joptd3k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_opt_present_joptd3k F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* addr */
    uint64_t gm_l3; /* off */
    GM_STACK_CHECK(&F, 424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l4;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_t5v8n6qrn4rh4 t6;
    gm_f_repr_53quqki((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)3ULL)));
    gm_t5v8n6qrn4rh4 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)3ULL));
    if (t8) {
        gm_b16_8 t10 = gm_l2;
        gm_tnpifv20irnph t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
        gm_f_format_29fhse4((void *)&gm_l4, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src0gtuh94b1fce, 194, 41);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        F.ret = t14;
        goto gm_fret;
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_t88rcucg1b7uk t18 = gm_l1;
    gm_b16_8 t19 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t19;
    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t21, gm_src0gtuh94b1fce, 195, 12);
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + (uintptr_t)((uint64_t)1ULL) * 8));
    gm_l3 = t22;
    gm_b16_8 t23 = gm_l2;
    uint64_t t24 = gm_l3;
    gm_t3877pk7lv748 t25;
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t23);
    GM_ST(uint64_t, ((uint8_t *)&t25 + 16), t24);
    gm_f_format_c2otgb4((void *)&gm_l6, (void *)NULL, cx, NULL, &t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src0gtuh94b1fce, 196, 12);
    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    F.ret = t28;
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
extern __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_res_is_err_11p8qlp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_11p8qlp F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* addr */
    uint64_t gm_l3; /* off */
    GM_STACK_CHECK(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l4;
    gm_tabb710d9mq2e gm_l5;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t6;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t8, gm_src0gtuh94b1fce, 217, 12);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)((uint64_t)0ULL) * 8));
    gm_l3 = t9;
    gm_b16_8 t10 = gm_l2;
    uint64_t t11 = gm_l3;
    gm_t3877pk7lv748 t12;
    GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t10);
    GM_ST(uint64_t, ((uint8_t *)&t12 + 16), t11);
    gm_f_format_c2otgb4((void *)&gm_l5, (void *)NULL, cx, NULL, &t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src0gtuh94b1fce, 218, 12);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_spec_literal_lmkpbc1(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_spec_literal_lmkpbc1 F;
    gm_t7g88i52rrtm5 gm_l0; /* s */
    int32_t gm_l1; /* align */
    int32_t gm_l2; /* kind */
    int64_t gm_l3; /* precision */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK(&F, 276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    int32_t gm_l5;
    int32_t gm_l6;
    int32_t gm_l7;
    int32_t gm_l8;
    int32_t gm_l9;
    gm_tabb710d9mq2e gm_l10;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_l5 = ((int32_t)0LL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb3;
            }
            gm_l5 = ((int32_t)1LL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)2ULL));
            if (!t22) {
                goto gm_lb4;
            }
            gm_l5 = ((int32_t)2LL);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)3ULL));
            if (!t24) {
                goto gm_lb5;
            }
            gm_l5 = ((int32_t)3LL);
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 775, 14);
    }
gm_lb1:;
    int32_t t25 = gm_l5;
    gm_l1 = t25;
    {
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb7;
            }
            gm_l6 = ((int32_t)0LL);
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
            if (!t29) {
                goto gm_lb8;
            }
            gm_l6 = ((int32_t)1LL);
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
            if (!t31) {
                goto gm_lb9;
            }
            gm_l6 = ((int32_t)2LL);
            goto gm_lb6;
        }
gm_lb9:;
        {
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)3ULL));
            if (!t33) {
                goto gm_lb10;
            }
            gm_l6 = ((int32_t)3LL);
            goto gm_lb6;
        }
gm_lb10:;
        {
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)4ULL));
            if (!t35) {
                goto gm_lb11;
            }
            gm_l6 = ((int32_t)4LL);
            goto gm_lb6;
        }
gm_lb11:;
        {
            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)5ULL));
            if (!t37) {
                goto gm_lb12;
            }
            gm_l6 = ((int32_t)5LL);
            goto gm_lb6;
        }
gm_lb12:;
        {
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)6ULL));
            if (!t39) {
                goto gm_lb13;
            }
            gm_l6 = ((int32_t)6LL);
            goto gm_lb6;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 781, 13);
    }
gm_lb6:;
    int32_t t40 = gm_l6;
    gm_l2 = t40;
    gm_l3 = ((int64_t)-1LL);
    {
        {
            uint8_t t41 = (uint8_t)(((((uint8_t *)&gm_l0) + 12))[4] != 0);
            if (!t41) {
                goto gm_lb15;
            }
            uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 12));
            gm_l4 = t42;
            uint32_t t43 = gm_l4;
            int64_t t44 = (int64_t)t43;
            gm_l3 = t44;
            goto gm_lb14;
        }
gm_lb15:;
    }
gm_lb14:;
    uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&gm_l0));
    int32_t t46 = gm_l1;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 2));
    if (t47) {
        gm_l7 = ((int32_t)1LL);
    } else {
        gm_l7 = ((int32_t)0LL);
    }
    int32_t t48 = gm_l7;
    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 3));
    if (t49) {
        gm_l8 = ((int32_t)1LL);
    } else {
        gm_l8 = ((int32_t)0LL);
    }
    int32_t t50 = gm_l8;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 4));
    if (t51) {
        gm_l9 = ((int32_t)1LL);
    } else {
        gm_l9 = ((int32_t)0LL);
    }
    int32_t t52 = gm_l9;
    int32_t t53 = gm_l2;
    uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    int64_t t55 = gm_l3;
    gm_t01jnheg9lqvd t56;
    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t56 + 28, 0);
    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), t45);
    GM_ST(int32_t, ((uint8_t *)&t56 + 4), t46);
    GM_ST(int32_t, ((uint8_t *)&t56 + 8), t48);
    GM_ST(int32_t, ((uint8_t *)&t56 + 12), t50);
    GM_ST(int32_t, ((uint8_t *)&t56 + 16), t52);
    GM_ST(int32_t, ((uint8_t *)&t56 + 20), t53);
    GM_ST(uint32_t, ((uint8_t *)&t56 + 24), t54);
    GM_ST(int64_t, ((uint8_t *)&t56 + 32), t55);
    gm_f_format_8933odu((void *)&gm_l10, (void *)NULL, cx, NULL, &t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src0gtuh94b1fce, 792, 12);
    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
    F.ret = t59;
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
extern __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t793vlgpa3b88 gm_f_split_u128_vrmr6en_c(gm_tbci5ospk5nnn a0) { gm_t793vlgpa3b88 gm_r; gm_f_split_u128_vrmr6en(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_store_at7m90t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_temp_piece_572g74q(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_temp_piece_572g74q F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* base */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* inner */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* k */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    gm_t3877pk7lv748 gm_l9; /* p */
    GM_STACK_CHECK(&F, 1273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    gm_t9geo0l9gngfn gm_l14;
    gm_t3877pk7lv748 gm_l15;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s5pra70i8o734));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(13));
    gm_l1 = t12;
    {
        {
            gm_b16_8 t13 = gm_l0;
            gm_b16_8 t14 = gm_l1;
            uint8_t t15 = gm_f_starts_with_6vvt01e(t13, t14);
            if (!t15) {
                goto gm_lb2;
            }
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t18 = (t16 > t17);
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)1ULL), &r22);
            if (o23) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 54, 55);
            uint64_t t24 = r22;
            if ((uintptr_t)t24 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t24, t20, gm_src0gtuh94b1fce, 54, 53);
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)t24 * 1));
            uint8_t t26 = ((t25 == ((uint8_t)41ULL)));
            if (!t26) {
                goto gm_lb2;
            }
            void * t27 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t r31; bool o32 = __builtin_sub_overflow(t30, ((uint64_t)1ULL), &r31);
            if (o32) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 55, 26);
            uint64_t t33 = r31;
            uint64_t t34 = (uintptr_t)t29;
            uint64_t t35 = (uintptr_t)t33;
            if (t34 > t35 || t35 > (uintptr_t)t28) gm_trap_slice(t34, t35, t28, gm_src0gtuh94b1fce, 55, 14);
            uint64_t t36 = ((uintptr_t)t33) - (uintptr_t)t29;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t27 + (uintptr_t)t29 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36));
            gm_l2 = t37;
            gm_b16_8 t38 = gm_l2;
            uint8_t t39 = gm_f_is_temp_rrllgdr(t38);
            if (t39) {
                gm_b16_8 t40 = gm_l2;
                gm_t3877pk7lv748 t41;
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                GM_ST(uint64_t, ((uint8_t *)&t41 + 16), ((uint64_t)0ULL));
                gm_t9geo0l9gngfn t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t42 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t42 + 28, 0);
                GM_ST(gm_t3877pk7lv748, ((uint8_t *)&t42), t41);
                ((uint8_t *)&t42)[24] = 1;
                memmove(gm_out, ((uint8_t *)&t42), 32);
                goto gm_fdone;
            }
            gm_t9geo0l9gngfn t43;
            memset(&t43, 0, sizeof t43);
            memmove(gm_out, ((uint8_t *)&t43), 32);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t45 = (t44 < ((uint64_t)2ULL));
    if (t45) {
        gm_l11 = ((uint8_t)1);
    } else {
        void * t46 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t47, gm_src0gtuh94b1fce, 60, 21);
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t49 = (!(t48 == ((uint8_t)40ULL)));
        gm_l11 = t49;
    }
    uint8_t t50 = gm_l11;
    if (t50) {
        gm_l10 = ((uint8_t)1);
    } else {
        void * t51 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t r54; bool o55 = __builtin_sub_overflow(t53, ((uint64_t)1ULL), &r54);
        if (o55) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 60, 42);
        uint64_t t56 = r54;
        if ((uintptr_t)t56 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t56, t52, gm_src0gtuh94b1fce, 60, 40);
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t51) + (uintptr_t)t56 * 1));
        uint8_t t58 = (!(t57 == ((uint8_t)41ULL)));
        gm_l10 = t58;
    }
    uint8_t t59 = gm_l10;
    if (t59) {
        gm_t9geo0l9gngfn t60;
        memset(&t60, 0, sizeof t60);
        memmove(gm_out, ((uint8_t *)&t60), 32);
        goto gm_fdone;
    }
    void * t61 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r64; bool o65 = __builtin_sub_overflow(t63, ((uint64_t)1ULL), &r64);
    if (o65) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 61, 19);
    uint64_t t66 = r64;
    uint64_t t67 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t68 = (uintptr_t)t66;
    if (t67 > t68 || t68 > (uintptr_t)t62) gm_trap_slice(t67, t68, t62, gm_src0gtuh94b1fce, 61, 14);
    uint64_t t69 = ((uintptr_t)t66) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t61 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69));
    gm_l3 = t70;
    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    gm_l4 = t71;
    {
        for (;;) {
            uint64_t t72 = gm_l4;
            uint8_t t73 = (t72 > ((uint64_t)0ULL));
            if (!t73) {
                goto gm_lb3;
            }
            void * t74 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t76 = gm_l4;
            uint64_t r77; bool o78 = __builtin_sub_overflow(t76, ((uint64_t)1ULL), &r77);
            if (o78) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 63, 27);
            uint64_t t79 = r77;
            if ((uintptr_t)t79 >= (uintptr_t)(t75)) gm_trap_index((uintptr_t)t79, t75, gm_src0gtuh94b1fce, 63, 21);
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)t74) + (uintptr_t)t79 * 1));
            uint8_t t81 = (t80 >= ((uint8_t)48ULL));
            if (!t81) {
                goto gm_lb3;
            }
            void * t82 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t84 = gm_l4;
            uint64_t r85; bool o86 = __builtin_sub_overflow(t84, ((uint64_t)1ULL), &r85);
            if (o86) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 63, 51);
            uint64_t t87 = r85;
            if ((uintptr_t)t87 >= (uintptr_t)(t83)) gm_trap_index((uintptr_t)t87, t83, gm_src0gtuh94b1fce, 63, 45);
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)t82) + (uintptr_t)t87 * 1));
            uint8_t t89 = (t88 <= ((uint8_t)57ULL));
            if (!t89) {
                goto gm_lb3;
            }
            {
                uint64_t t90 = gm_l4;
                uint64_t r91; bool o92 = __builtin_sub_overflow(t90, ((uint64_t)1ULL), &r91);
                if (o92) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 63, 67);
                uint64_t t93 = r91;
                gm_l4 = t93;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    uint64_t t94 = gm_l4;
    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t96 = ((t94 == t95));
    if (t96) {
        gm_l13 = ((uint8_t)1);
    } else {
        uint64_t t97 = gm_l4;
        uint8_t t98 = (t97 < ((uint64_t)3ULL));
        gm_l13 = t98;
    }
    uint8_t t99 = gm_l13;
    if (t99) {
        gm_l12 = ((uint8_t)1);
    } else {
        void * t100 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t102 = gm_l4;
        uint64_t r103; bool o104 = __builtin_sub_overflow(t102, ((uint64_t)3ULL), &r103);
        if (o104) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 64, 46);
        uint64_t t105 = r103;
        uint64_t t106 = gm_l4;
        uint64_t t107 = (uintptr_t)t105;
        uint64_t t108 = (uintptr_t)t106;
        if (t107 > t108 || t108 > (uintptr_t)t101) gm_trap_slice(t107, t108, t101, gm_src0gtuh94b1fce, 64, 40);
        uint64_t t109 = ((uintptr_t)t106) - (uintptr_t)t105;
        gm_b16_8 t110;
        GM_ST(void *, (uint8_t *)&t110, (void *)(((uint8_t *)t100 + (uintptr_t)t105 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(t109));
        gm_b16_8 t111;
        GM_ST(void *, (uint8_t *)&t111, (void *)(gm_s0m0qaokk51g0));
        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(3));
        uint8_t t112 = (gm_eq0iqiubfm7e(((uint8_t *)&t110), ((uint8_t *)&t111)));
        uint8_t t113 = (uint8_t)!t112;
        gm_l12 = t113;
    }
    uint8_t t114 = gm_l12;
    if (t114) {
        gm_t9geo0l9gngfn t115;
        memset(&t115, 0, sizeof t115);
        memmove(gm_out, ((uint8_t *)&t115), 32);
        goto gm_fdone;
    }
    gm_l5 = ((uint64_t)0ULL);
    void * t116 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t118 = gm_l4;
    uint64_t t119 = (uintptr_t)t118;
    uint64_t t120 = (uintptr_t)t117;
    if (t119 > t120 || t120 > (uintptr_t)t117) gm_trap_slice(t119, t120, t117, gm_src0gtuh94b1fce, 66, 14);
    uint64_t t121 = ((uintptr_t)t117) - (uintptr_t)t118;
    gm_b16_8 t122;
    GM_ST(void *, (uint8_t *)&t122, (void *)(((uint8_t *)t116 + (uintptr_t)t118 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(t121));
    gm_l6 = t122;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t123 = gm_l7;
            uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t125 = (t123 < t124);
            if (!t125) {
                goto gm_lb6;
            }
            {
                void * t126 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t128 = gm_l7;
                {
                    {
                        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)t126) + (uintptr_t)t128 * 1));
                        gm_l8 = t129;
                        uint64_t t130 = gm_l5;
                        uint64_t r131; bool o132 = __builtin_mul_overflow(t130, ((uint64_t)10ULL), &r131);
                        if (o132) gm_trap("integer overflow in `*`", gm_src0gtuh94b1fce, 66, 31);
                        uint64_t t133 = r131;
                        uint8_t t134 = gm_l8;
                        uint8_t r135; bool o136 = __builtin_sub_overflow(t134, ((uint8_t)48ULL), &r135);
                        if (o136) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 66, 44);
                        uint8_t t137 = r135;
                        uint64_t t138 = (uint64_t)t137;
                        uint64_t r139; bool o140 = __builtin_add_overflow(t133, t138, &r139);
                        if (o140) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 66, 31);
                        uint64_t t141 = r139;
                        gm_l5 = t141;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 66, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t142 = gm_l7;
            uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
            uint64_t t145 = r143;
            gm_l7 = t145;
        }
    }
gm_lb6:;
    void * t146 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t148 = gm_l4;
    uint64_t r149; bool o150 = __builtin_sub_overflow(t148, ((uint64_t)3ULL), &r149);
    if (o150) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 67, 30);
    uint64_t t151 = r149;
    uint64_t t152 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t153 = (uintptr_t)t151;
    if (t152 > t153 || t153 > (uintptr_t)t147) gm_trap_slice(t152, t153, t147, gm_src0gtuh94b1fce, 67, 21);
    uint64_t t154 = ((uintptr_t)t151) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t155;
    GM_ST(void *, (uint8_t *)&t155, (void *)(((uint8_t *)t146 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(t154));
    gm_f_temp_piece_572g74q((void *)&gm_l14, t155);
    uint8_t t156 = (uint8_t)((((uint8_t *)&gm_l14))[24] != 0);
    if (t156) {
        memmove(((uint8_t *)&gm_l15), ((uint8_t *)&gm_l14), 24);
    } else {
        gm_t9geo0l9gngfn t157;
        memset(&t157, 0, sizeof t157);
        memmove(gm_out, ((uint8_t *)&t157), 32);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l15), 24);
    gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 16));
    uint64_t t160 = gm_l5;
    uint64_t r161; bool o162 = __builtin_add_overflow(t159, t160, &r161);
    if (o162) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 68, 41);
    uint64_t t163 = r161;
    gm_t3877pk7lv748 t164;
    GM_ST(gm_b16_8, ((uint8_t *)&t164 + 0), t158);
    GM_ST(uint64_t, ((uint8_t *)&t164 + 16), t163);
    gm_t9geo0l9gngfn t165;
    GM_ST(uint8_t, (uint8_t *)&t165 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t165 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t165 + 28, 0);
    GM_ST(gm_t3877pk7lv748, ((uint8_t *)&t165), t164);
    ((uint8_t *)&t165)[24] = 1;
    memmove(gm_out, ((uint8_t *)&t165), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t9geo0l9gngfn gm_f_temp_piece_572g74q_c(gm_b16_8 a0) { gm_t9geo0l9gngfn gm_r; gm_f_temp_piece_572g74q(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_uses_effect_7j7sn5a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_uses_effect_7j7sn5a F;
    void * gm_l1; /* e */
    uint8_t gm_l3; /* found */
    gm_tiilmfd16v3fc gm_l5; /* x */
    uint32_t gm_l6; /* f */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* x */
    GM_STACK_CHECK(&F, 350u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    uint8_t gm_l10;
    gm_l3 = ((uint8_t)0);
    {
        {
            void * t13 = gm_l1;
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)34ULL));
            if (!t15) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l4), ((((uint8_t *)t13) + 8) + 8), 40);
            {
                {
                    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
                    if (!t17) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l5), (((uint8_t *)&F.l4) + 8), 24);
                    void * t18 = F.l2;
                    gm_tiilmfd16v3fc t19 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l5));
                    gm_tiilmfd16v3fc t20 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t18));
                    uint8_t t21 = (gm_eqd5a8dm2ae7(((uint8_t *)&t19), ((uint8_t *)&t20)));
                    gm_l3 = t21;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                    if (!t23) {
                        goto gm_lb5;
                    }
                    uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
                    gm_l6 = t24;
                    void * t25 = F.l0;
                    uint32_t t26 = gm_l6;
                    void * t27 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t25, t26);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 72));
                    gm_l7 = t28;
                    gm_l8 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t30 = gm_l8;
                            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint8_t t32 = (t30 < t31);
                            if (!t32) {
                                goto gm_lb6;
                            }
                            {
                                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint64_t t35 = gm_l8;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l9), (((uint8_t *)t33) + (uintptr_t)t35 * 24), 24);
                                        void * t36 = F.l2;
                                        gm_tiilmfd16v3fc t37 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l9));
                                        gm_tiilmfd16v3fc t38 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t36));
                                        uint8_t t39 = (gm_eqd5a8dm2ae7(((uint8_t *)&t37), ((uint8_t *)&t38)));
                                        if (t39) {
                                            gm_l3 = ((uint8_t)1);
                                        }
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 730, 17);
                                }
gm_lb9:;
                            }
gm_lb8:;
                            uint64_t t40 = gm_l8;
                            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                            uint64_t t43 = r41;
                            gm_l8 = t43;
                        }
                    }
gm_lb6:;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)2ULL));
                    if (!t45) {
                        goto gm_lb11;
                    }
                    goto gm_lb3;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 727, 9);
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t46 = gm_l3;
    if (t46) {
        gm_l10 = ((uint8_t)1);
    } else {
        void * t47 = gm_l1;
        gm_closure t48 = ((gm_closure){(void *)gm_f_fn_okfag60, (void *)&F.h, cx});
        uint8_t t49 = gm_f_any_child_ffc8h1k(t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t49;
    }
    uint8_t t50 = gm_l10;
    F.ret = t50;
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
__attribute__((aligned(4))) void gm_f_view_addr_nj9md2j(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_addr_nj9md2j F;
    gm_b16_8 gm_l0; /* v */
    gm_b16_8 gm_l1; /* mark */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* a */
    GM_STACK_CHECK(&F, 545u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_t9geo0l9gngfn gm_l5;
    gm_b16_8 t8 = gm_l0;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_somhu4vltiil1));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(3));
    uint8_t t10 = gm_f_starts_with_6vvt01e(t8, t9);
    uint8_t t11 = (uint8_t)!t10;
    if (t11) {
        gm_th2lqbo77pvsc t12;
        memset(&t12, 0, sizeof t12);
        memmove(gm_out, ((uint8_t *)&t12), 24);
        goto gm_fdone;
    }
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s8tfl6snglqci));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(18));
    gm_l1 = t13;
    gm_l2 = ((uint64_t)3ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l2;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t r16; bool o17 = __builtin_add_overflow(t14, t15, &r16);
            if (o17) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 77, 11);
            uint64_t t18 = r16;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t20 = (t18 <= t19);
            if (!t20) {
                goto gm_lb1;
            }
            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t23 = gm_l2;
            uint64_t t24 = gm_l2;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t r26; bool o27 = __builtin_add_overflow(t24, t25, &r26);
            if (o27) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 77, 47);
            uint64_t t28 = r26;
            uint64_t t29 = (uintptr_t)t23;
            uint64_t t30 = (uintptr_t)t28;
            if (t29 > t30 || t30 > (uintptr_t)t22) gm_trap_slice(t29, t30, t22, gm_src0gtuh94b1fce, 77, 42);
            uint64_t t31 = ((uintptr_t)t28) - (uintptr_t)t23;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t21 + (uintptr_t)t23 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31));
            gm_b16_8 t33 = gm_l1;
            uint8_t t34 = (gm_eq0iqiubfm7e(((uint8_t *)&t32), ((uint8_t *)&t33)));
            uint8_t t35 = (uint8_t)!t34;
            if (!t35) {
                goto gm_lb1;
            }
            {
                uint64_t t36 = gm_l2;
                uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                if (o38) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 77, 72);
                uint64_t t39 = r37;
                gm_l2 = t39;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t40 = gm_l2;
    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
    if (o43) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 78, 8);
    uint64_t t44 = r42;
    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t46 = (t44 > t45);
    if (t46) {
        gm_th2lqbo77pvsc t47;
        memset(&t47, 0, sizeof t47);
        memmove(gm_out, ((uint8_t *)&t47), 24);
        goto gm_fdone;
    }
    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t50 = gm_l2;
    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)3ULL), &r51);
    if (o52) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 79, 12);
    uint64_t t53 = r51;
    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r55; bool o56 = __builtin_sub_overflow(t54, ((uint64_t)1ULL), &r55);
    if (o56) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 79, 19);
    uint64_t t57 = r55;
    uint64_t t58 = (uintptr_t)t53;
    uint64_t t59 = (uintptr_t)t57;
    if (t58 > t59 || t59 > (uintptr_t)t49) gm_trap_slice(t58, t59, t49, gm_src0gtuh94b1fce, 79, 10);
    uint64_t t60 = ((uintptr_t)t57) - (uintptr_t)t53;
    gm_b16_8 t61;
    GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t48 + (uintptr_t)t53 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60));
    gm_l3 = t61;
    gm_b16_8 t62 = gm_l3;
    gm_f_temp_piece_572g74q((void *)&gm_l5, t62);
    {
        {
            uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l5))[24] != 0);
            if (t63) {
                goto gm_lb5;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb4;
        }
gm_lb5:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb4;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 80, 8);
    }
gm_lb4:;
    uint8_t t64 = gm_l4;
    if (t64) {
        gm_th2lqbo77pvsc t65;
        memset(&t65, 0, sizeof t65);
        memmove(gm_out, ((uint8_t *)&t65), 24);
        goto gm_fdone;
    }
    gm_b16_8 t66 = gm_l3;
    gm_th2lqbo77pvsc t67;
    GM_ST(uint8_t, (uint8_t *)&t67 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t67 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t67 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t67), t66);
    ((uint8_t *)&t67)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t67), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_view_addr_nj9md2j_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_view_addr_nj9md2j(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_write_table_values_s1u867r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_write_table_values_s1u867r F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l3; /* tv */
    gm_b16_8 gm_l4; /* tys */
    gm_b16_8 gm_l5; /* vals */
    gm_t88rcucg1b7uk gm_l6; /* str_ty */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint32_t gm_l13; /* k */
    gm_b16_8 gm_l14; /* t */
    gm_b16_8 gm_l15; /* piece */
    gm_b16_8 gm_l16; /* v */
    gm_b16_8 gm_l17; /* d */
    gm_b16_8 gm_l18; /* sp */
    gm_b16_8 gm_l19; /* plain */
    uint64_t gm_l20; /* p */
    gm_th2lqbo77pvsc gm_l21; /* stub */
    gm_tiilmfd16v3fc gm_l22; /* effect_ */
    uint32_t gm_l23; /* op */
    uint32_t gm_l24; /* id */
    gm_tnqk2qt7j42o9 gm_l25; /* stub_tys */
    gm_tep2drsl9408b gm_l26; /* cv */
    gm_th2lqbo77pvsc gm_l27; /* direct */
    gm_b16_8 gm_l29; /* c */
    gm_b16_8 gm_l30; /* ccx */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_tiilmfd16v3fc gm_l33; /* eff */
    uint32_t gm_l34; /* id */
    gm_b16_8 gm_l35; /* n */
    gm_b16_8 gm_l36; /* fs */
    gm_b16_8 gm_l37; /* ff */
    gm_b16_8 gm_l38; /* dn */
    gm_b16_8 gm_l39; /* sl */
    gm_b16_8 gm_l40; /* dn */
    gm_b16_8 gm_l41; /* core */
    gm_b16_8 gm_l42; /* ps */
    gm_b16_8 gm_l43; /* pf */
    gm_b16_8 gm_l44; /* at */
    gm_b16_8 gm_l45; /* b */
    gm_b16_8 gm_l46; /* sl */
    gm_b16_8 gm_l47; /* b */
    GM_STACK_CHECK(&F, 8755u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tabb710d9mq2e gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_b16_8 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tabb710d9mq2e gm_l53;
    gm_tabb710d9mq2e gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tabb710d9mq2e gm_l59;
    gm_tabb710d9mq2e gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_t53at80pfdus3 gm_l62;
    gm_tabb710d9mq2e gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tabb710d9mq2e gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_tabb710d9mq2e gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_tqgtd5gtgj65s gm_l72;
    gm_tabb710d9mq2e gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_tabb710d9mq2e gm_l75;
    gm_tqgtd5gtgj65s gm_l76;
    gm_tabb710d9mq2e gm_l77;
    gm_tqgtd5gtgj65s gm_l78;
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, ((uint8_t *)t24));
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        gm_t88rcucg1b7uk t27 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t27;
        gm_trqfvmjtt4sij t28;
        gm_f_list_epvducg((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t28;
        gm_trqfvmjtt4sij t29;
        gm_f_list_epvducg((void *)&t29, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t29;
        gm_trqfvmjtt4sij t30;
        gm_f_list_epvducg((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t30;
        gm_l11 = ((uint64_t)0ULL);
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        gm_l10 = t31;
        {
            for (;;) {
                uint64_t t32 = gm_l11;
                uint64_t t33 = gm_l10;
                uint8_t t34 = (t32 < t33);
                if (!t34) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            uint64_t t35 = gm_l11;
                            gm_l12 = t35;
                            void * t36 = (void *)((uint8_t *)&F.l7);
                            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint64_t t39 = gm_l12;
                            if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_src0gtuh94b1fce, 475, 52);
                            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t37) + (uintptr_t)t39 * 16));
                            gm_tnpifv20irnph t41;
                            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                            gm_f_format_ucmo647((void *)&gm_l48, (void *)NULL, cx, NULL, t41);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_src0gtuh94b1fce, 475, 19);
                            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                            gm_tftb1odc739t5 t45 = gm_f_push_3ef746b(t36, t44);
                            gm_l49 = t45;
                            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_src0gtuh94b1fce, 475, 9);
                            void * t47 = (void *)((uint8_t *)&F.l8);
                            void * t48 = gm_l0;
                            void * t49 = GM_LD(void *, ((uint8_t *)t48));
                            void * t50 = (void *)(((uint8_t *)t49) + 8);
                            void * t51 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint64_t t53 = gm_l12;
                            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t51) + (uintptr_t)t53 * 4));
                            gm_b16_8 t55 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t50, t54);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t56 = gm_f_push_3ef746b(t47, t55);
                            gm_l50 = t56;
                            uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_src0gtuh94b1fce, 476, 9);
                            void * t58 = (void *)((uint8_t *)&F.l9);
                            void * t59 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint64_t t61 = gm_l12;
                            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t59) + (uintptr_t)t61 * 4));
                            gm_t88rcucg1b7uk t63 = gm_l6;
                            uint8_t t64 = (gm_eq9m6mvtmgde(((uint8_t *)&t62), ((uint8_t *)&t63)));
                            if (t64) {
                                gm_b16_8 t65;
                                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_svubtddeidu1d));
                                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
                                gm_l51 = t65;
                            } else {
                                gm_b16_8 t66;
                                GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sicgmpca63jln));
                                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(1));
                                gm_l51 = t66;
                            }
                            gm_b16_8 t67 = gm_l51;
                            gm_tftb1odc739t5 t68 = gm_f_push_3ef746b(t58, t67);
                            gm_l52 = t68;
                            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_src0gtuh94b1fce, 477, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 474, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t70 = gm_l11;
                uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                uint64_t t73 = r71;
                gm_l11 = t73;
            }
        }
gm_lb1:;
    }
    {
        void * t74 = gm_l0;
        uint32_t t75 = gm_f_fresh_5kokjmg(t74);
        gm_l13 = t75;
        uint32_t t76 = gm_l13;
        gm_t88rcucg1b7uk t77;
        GM_ST(uint32_t, ((uint8_t *)&t77 + 0), t76);
        gm_f_format_j1qjusd((void *)&gm_l53, (void *)NULL, cx, NULL, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
        if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_src0gtuh94b1fce, 480, 10);
        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
        gm_l14 = t79;
        void * t80 = gm_l0;
        gm_b16_8 t81 = gm_l14;
        gm_b16_8 t82 = gm_l3;
        gm_t6j6smj7qoctu t83;
        GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t81);
        GM_ST(gm_b16_8, ((uint8_t *)&t83 + 16), t82);
        gm_b0_1 t84 = gm_f_linef_agpuven((void *)NULL, cx, NULL, t80, &t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint8_t t86 = (t85 > ((uint64_t)0ULL));
        if (t86) {
            void * t87 = gm_l0;
            uint32_t t88 = gm_l13;
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l54, (void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
            if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_src0gtuh94b1fce, 483, 53);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
            gm_tiilmfd16v3fc t94;
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t94 + 0), t88);
            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 8), t93);
            gm_b0_1 t95 = gm_f_linef_qv8qu4r((void *)NULL, cx, NULL, t87, &t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t96 = gm_l0;
            uint32_t t97 = gm_l13;
            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l55, (void *)NULL, cx, NULL, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
            if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_src0gtuh94b1fce, 484, 56);
            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
            gm_tiilmfd16v3fc t103;
            GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t103 + 0), t97);
            GM_ST(gm_b16_8, ((uint8_t *)&t103 + 8), t102);
            gm_b0_1 t104 = gm_f_linef_jrb37qc((void *)NULL, cx, NULL, t96, &t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t105 = gm_l0;
            uint32_t t106 = gm_l13;
            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l56, (void *)NULL, cx, NULL, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
            if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_src0gtuh94b1fce, 485, 62);
            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
            gm_tiilmfd16v3fc t112;
            GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t112 + 0), t106);
            GM_ST(gm_b16_8, ((uint8_t *)&t112 + 8), t111);
            gm_b0_1 t113 = gm_f_linef_rulhdfs((void *)NULL, cx, NULL, t105, &t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b16_8 t114 = gm_l14;
        uint32_t t115 = gm_l13;
        gm_tommf4j9upomc t116;
        GM_ST(uint32_t, (uint8_t *)&t116 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t116 + 0), t114);
        GM_ST(uint32_t, ((uint8_t *)&t116 + 16), t115);
        gm_f_format_mh7gc0u((void *)&gm_l57, (void *)NULL, cx, NULL, &t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_src0gtuh94b1fce, 487, 14);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
        gm_l15 = t118;
    }
    {
        uint32_t t119 = gm_l13;
        uint32_t t120 = gm_l13;
        gm_tcc1rq4n1avb2 t121;
        GM_ST(uint32_t, ((uint8_t *)&t121 + 0), t119);
        GM_ST(uint32_t, ((uint8_t *)&t121 + 4), t120);
        gm_f_format_66ttjd0((void *)&gm_l58, (void *)NULL, cx, NULL, t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
        if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_src0gtuh94b1fce, 489, 10);
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
        gm_l16 = t123;
        uint32_t t124 = gm_l13;
        uint32_t t125 = gm_l13;
        gm_tcc1rq4n1avb2 t126;
        GM_ST(uint32_t, ((uint8_t *)&t126 + 0), t124);
        GM_ST(uint32_t, ((uint8_t *)&t126 + 4), t125);
        gm_f_format_k2g0ike((void *)&gm_l59, (void *)NULL, cx, NULL, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
        if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 8, gm_src0gtuh94b1fce, 490, 10);
        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
        gm_l17 = t128;
        gm_b16_8 t129 = gm_l14;
        uint32_t t130 = gm_l13;
        gm_tommf4j9upomc t131;
        GM_ST(uint32_t, (uint8_t *)&t131 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t131 + 0), t129);
        GM_ST(uint32_t, ((uint8_t *)&t131 + 16), t130);
        gm_f_format_n0794r2((void *)&gm_l60, (void *)NULL, cx, NULL, &t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
        if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_src0gtuh94b1fce, 491, 11);
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
        gm_l18 = t133;
        gm_b16_8 t134 = gm_l18;
        uint32_t t135 = gm_l13;
        uint32_t t136 = gm_l13;
        gm_t5hdnnftkup6m t137;
        GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t134);
        GM_ST(uint32_t, ((uint8_t *)&t137 + 16), t135);
        GM_ST(uint32_t, ((uint8_t *)&t137 + 20), t136);
        gm_f_format_clarrev((void *)&gm_l61, (void *)NULL, cx, NULL, &t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
        if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_src0gtuh94b1fce, 492, 14);
        gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
        gm_l19 = t139;
        void * t140 = gm_l0;
        uint64_t t141 = gm_f_ptr_bytes_eocneng(t140);
        gm_l20 = t141;
        gm_th2lqbo77pvsc t142;
        memset(&t142, 0, sizeof t142);
        gm_l21 = t142;
        {
            {
                uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                uint8_t t144 = (uint8_t)(t143 == ((uint8_t)0ULL));
                if (!t144) {
                    goto gm_lb7;
                }
                memmove(((uint8_t *)&gm_l22), (((uint8_t *)a2) + 8), 24);
                uint32_t t145 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 24));
                gm_l23 = t145;
                void * t146 = gm_l0;
                void * t147 = GM_LD(void *, ((uint8_t *)t146));
                void * t148 = (void *)((uint8_t *)&gm_l22);
                uint32_t t149 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t147, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t149;
                gm_t88rcucg1b7uk t150 = gm_l6;
                gm_tnqk2qt7j42o9 t151;
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t151 + 0), t150);
                gm_l25 = t151;
                void * t152 = gm_l0;
                void * t153 = (void *)((uint8_t *)&gm_l22);
                uint32_t t154 = gm_l24;
                uint32_t t155 = gm_l23;
                void * t156 = (void *)((uint8_t *)&gm_l25);
                uint64_t t157 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t158 = (uintptr_t)((uint64_t)1ULL);
                if (t157 > t158 || t158 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t157, t158, ((uint64_t)1ULL), gm_src0gtuh94b1fce, 498, 46);
                uint64_t t159 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t160;
                GM_ST(void *, (uint8_t *)&t160, (void *)(((uint8_t *)t156 + (uintptr_t)((uint64_t)0ULL) * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(t159));
                gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
                void * t162 = gm_l0;
                void * t163 = GM_LD(void *, ((uint8_t *)t162));
                void * t164 = (void *)(((uint8_t *)t163) + 8);
                gm_t88rcucg1b7uk t165 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t164);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_out_stub_ils4a69((void *)&gm_l21, (void *)NULL, cx, NULL, t152, t153, t154, t155, t161, t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
    }
    {
        {
            {
                {
                    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t167 = (uint8_t)(t166 == ((uint8_t)1ULL));
                    if (!t167) {
                        goto gm_lb9;
                    }
                    memmove(((uint8_t *)&gm_l26), (((uint8_t *)a2) + 8), 24);
                    memmove(((uint8_t *)&gm_l27), ((((uint8_t *)a2) + 8) + 24), 24);
                    void * t168 = gm_l0;
                    void * t169 = GM_LD(void *, ((uint8_t *)t168));
                    void * t170 = (void *)(((uint8_t *)t169) + 8);
                    gm_t88rcucg1b7uk t171 = gm_l6;
                    uint8_t t172 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t173 = (uint8_t)!t172;
                    if (!t173) {
                        goto gm_lb9;
                    }
                    {
                        {
                            {
                                {
                                    void * t174 = gm_l0;
                                    void * t175 = GM_LD(void *, ((uint8_t *)t174));
                                    void * t176 = (void *)(((uint8_t *)t175) + 8);
                                    gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l26) + 16));
                                    gm_f_kind_ghvav22((void *)&gm_l62, t176, GM_CONV(gm_csguankko4lmhd, t177));
                                    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                                    uint8_t t179 = (uint8_t)(t178 == ((uint8_t)15ULL));
                                    if (!t179) {
                                        goto gm_lb11;
                                    }
                                    memmove(((uint8_t *)&F.l28), (((uint8_t *)&gm_l62) + 8), 48);
                                    void * t180 = gm_l0;
                                    gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l26) + 16));
                                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
                                    gm_b16_8 t183 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t180, t181, t182);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l29 = t183;
                                    void * t184 = gm_l0;
                                    uint32_t t185 = gm_f_fresh_5kokjmg(t184);
                                    gm_t88rcucg1b7uk t186;
                                    GM_ST(uint32_t, ((uint8_t *)&t186 + 0), t185);
                                    gm_f_format_qr5c6un((void *)&gm_l63, (void *)NULL, cx, NULL, t186);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t187 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                    if (!t187) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_src0gtuh94b1fce, 503, 16);
                                    gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                                    gm_l30 = t188;
                                    void * t189 = gm_l0;
                                    gm_b16_8 t190 = gm_l30;
                                    gm_b16_8 t191 = gm_l29;
                                    gm_t6j6smj7qoctu t192;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t192 + 0), t190);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t192 + 16), t191);
                                    gm_b0_1 t193 = gm_f_linef_lot5c36((void *)NULL, cx, NULL, t189, &t192);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 24));
                                    gm_l31 = t194;
                                    gm_l32 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t195 = gm_l32;
                                            uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                            uint8_t t197 = (t195 < t196);
                                            if (!t197) {
                                                goto gm_lb12;
                                            }
                                            {
                                                void * t198 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                                uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                                uint64_t t200 = gm_l32;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&gm_l33), (((uint8_t *)t198) + (uintptr_t)t200 * 24), 24);
                                                        void * t201 = gm_l0;
                                                        void * t202 = GM_LD(void *, ((uint8_t *)t201));
                                                        void * t203 = (void *)((uint8_t *)&gm_l33);
                                                        uint32_t t204 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t202, t203);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l34 = t204;
                                                        void * t205 = gm_l0;
                                                        uint32_t t206 = gm_f_fresh_5kokjmg(t205);
                                                        gm_t88rcucg1b7uk t207;
                                                        GM_ST(uint32_t, ((uint8_t *)&t207 + 0), t206);
                                                        gm_f_format_mgf81l5((void *)&gm_l64, (void *)NULL, cx, NULL, t207);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t208 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                                        if (!t208) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src0gtuh94b1fce, 507, 18);
                                                        gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                                        gm_l35 = t209;
                                                        void * t210 = gm_l0;
                                                        gm_b16_8 t211 = gm_l35;
                                                        void * t212 = gm_l0;
                                                        gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)t212) + 200));
                                                        uint32_t t214 = gm_l34;
                                                        gm_b16_8 t215 = gm_l35;
                                                        gm_b16_8 t216 = gm_l35;
                                                        gm_b16_8 t217 = gm_l30;
                                                        gm_b16_8 t218 = gm_l30;
                                                        gm_b16_8 t219 = gm_l35;
                                                        gm_t586m754bse2v t220;
                                                        GM_ST(uint32_t, (uint8_t *)&t220 + 36, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 0), t211);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 16), t213);
                                                        GM_ST(uint32_t, ((uint8_t *)&t220 + 32), t214);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 40), t215);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 56), t216);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 72), t217);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 88), t218);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 104), t219);
                                                        gm_b0_1 t221 = gm_f_linef_ubknr7g((void *)NULL, cx, NULL, t210, &t220);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb15;
                                                    }
gm_lb16:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 505, 9);
                                                }
gm_lb15:;
                                            }
gm_lb14:;
                                            uint64_t t222 = gm_l32;
                                            uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                                            uint64_t t225 = r223;
                                            gm_l32 = t225;
                                        }
                                    }
gm_lb12:;
                                }
                                {
                                    void * t226 = gm_l0;
                                    gm_t88rcucg1b7uk t227 = gm_l6;
                                    gm_b16_8 t228 = gm_f_fn_sink_stub_5ipff9h((void *)NULL, cx, NULL, t226, t227);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l36 = t228;
                                    void * t229 = gm_l0;
                                    gm_b16_8 t230 = gm_l36;
                                    gm_b16_8 t231 = gm_f_fn_format_stub_53r45gt((void *)NULL, cx, NULL, t229, t230);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l37 = t231;
                                    void * t232 = gm_l0;
                                    uint32_t t233 = gm_l13;
                                    uint32_t t234 = gm_l13;
                                    gm_tcc1rq4n1avb2 t235;
                                    GM_ST(uint32_t, ((uint8_t *)&t235 + 0), t233);
                                    GM_ST(uint32_t, ((uint8_t *)&t235 + 4), t234);
                                    gm_b0_1 t236 = gm_f_linef_vddfrtf((void *)NULL, cx, NULL, t232, t235);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t237 = gm_l0;
                                    void * t238 = gm_l0;
                                    uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)t238) + 144));
                                    uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                                    if (o241) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 513, 9);
                                    uint64_t t242 = r240;
                                    GM_ST(uint64_t, (((uint8_t *)t237) + 144), t242);
                                    {
                                        {
                                            uint8_t t243 = (uint8_t)((((uint8_t *)&gm_l27))[16] != 0);
                                            if (!t243) {
                                                goto gm_lb18;
                                            }
                                            gm_b16_8 t244 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                            gm_l38 = t244;
                                            void * t245 = gm_l0;
                                            gm_b16_8 t246 = gm_l15;
                                            gm_tnpifv20irnph t247;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t246);
                                            gm_b0_1 t248 = gm_f_linef_h1q98vm((void *)NULL, cx, NULL, t245, t247);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t249 = gm_l0;
                                            void * t250 = gm_l0;
                                            uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)t250) + 144));
                                            uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                                            if (o253) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 516, 13);
                                            uint64_t t254 = r252;
                                            GM_ST(uint64_t, (((uint8_t *)t249) + 144), t254);
                                            void * t255 = gm_l0;
                                            gm_t88rcucg1b7uk t256 = gm_l6;
                                            gm_b16_8 t257 = gm_l15;
                                            gm_tnpifv20irnph t258;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t258 + 0), t257);
                                            gm_f_format_jq3k246((void *)&gm_l65, (void *)NULL, cx, NULL, t258);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t259 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                            if (!t259) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_src0gtuh94b1fce, 517, 41);
                                            gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                            gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&t260));
                                            gm_b16_8 t262 = gm_l15;
                                            gm_tnpifv20irnph t263;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t263 + 0), t262);
                                            gm_f_format_4c9sg76((void *)&gm_l66, (void *)NULL, cx, NULL, t263);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                            if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_src0gtuh94b1fce, 517, 71);
                                            gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                                            gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                                            gm_b16_8 t267 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t255, t256, t261, t266);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l39 = t267;
                                            void * t268 = gm_l0;
                                            gm_b16_8 t269 = gm_l38;
                                            gm_b16_8 t270 = gm_l29;
                                            gm_b16_8 t271 = gm_l30;
                                            gm_b16_8 t272 = gm_l39;
                                            gm_t0uuhaljsf410 t273;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t273 + 0), t269);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t273 + 16), t270);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t273 + 32), t271);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t273 + 48), t272);
                                            gm_b0_1 t274 = gm_f_linef_amf91ik((void *)NULL, cx, NULL, t268, &t273);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t275 = gm_l0;
                                            gm_b0_1 t276 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t275);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t277 = gm_l0;
                                            void * t278 = gm_l0;
                                            uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)t278) + 144));
                                            uint64_t r280; bool o281 = __builtin_sub_overflow(t279, ((uint64_t)1ULL), &r280);
                                            if (o281) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 520, 13);
                                            uint64_t t282 = r280;
                                            GM_ST(uint64_t, (((uint8_t *)t277) + 144), t282);
                                            void * t283 = gm_l0;
                                            gm_b16_8 t284;
                                            GM_ST(void *, (uint8_t *)&t284, (void *)(gm_s2cuflhqkkuaq));
                                            GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(1));
                                            gm_b0_1 t285 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t283, t284);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        void * t286 = gm_l0;
                                        gm_b16_8 t287 = gm_l15;
                                        gm_b16_8 t288 = gm_l36;
                                        gm_b16_8 t289 = gm_l29;
                                        gm_b16_8 t290 = gm_l29;
                                        gm_b16_8 t291 = gm_l30;
                                        gm_b16_8 t292 = gm_l15;
                                        gm_b16_8 t293 = gm_l15;
                                        gm_th2d9c7aju8hp t294;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 0), t287);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 16), t288);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 32), t289);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 48), t290);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 64), t291);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 80), t292);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 96), t293);
                                        gm_f_format_2m0cdvi((void *)&gm_l67, (void *)NULL, cx, NULL, &t294);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t295 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                        if (!t295) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src0gtuh94b1fce, 523, 28);
                                        gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                        gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&t296));
                                        gm_b0_1 t298 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t286, t297);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
gm_lb17:;
                                }
                                {
                                    void * t299 = gm_l0;
                                    uint32_t t300 = gm_l13;
                                    gm_b16_8 t301 = gm_l14;
                                    gm_tiilmfd16v3fc t302;
                                    GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t302 + 0), t300);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t302 + 8), t301);
                                    gm_b0_1 t303 = gm_f_linef_qqgq3om((void *)NULL, cx, NULL, t299, &t302);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t304 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                    uint8_t t305 = (t304 > ((uint64_t)0ULL));
                                    if (t305) {
                                        void * t306 = gm_l0;
                                        uint32_t t307 = gm_l13;
                                        gm_b16_8 t308 = gm_l14;
                                        uint32_t t309 = gm_l13;
                                        gm_t5g5vgung6h6s t310;
                                        GM_ST(uint32_t, (uint8_t *)&t310 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t310 + 28, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t310 + 0), t307);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t310 + 8), t308);
                                        GM_ST(uint32_t, ((uint8_t *)&t310 + 24), t309);
                                        gm_b0_1 t311 = gm_f_linef_qfq4ah7((void *)NULL, cx, NULL, t306, &t310);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        {
                                            {
                                                uint8_t t312 = (uint8_t)((((uint8_t *)&gm_l27))[16] != 0);
                                                if (!t312) {
                                                    goto gm_lb20;
                                                }
                                                gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                gm_l40 = t313;
                                                void * t314 = gm_l0;
                                                gm_b16_8 t315 = gm_l19;
                                                gm_tnpifv20irnph t316;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t316 + 0), t315);
                                                gm_b0_1 t317 = gm_f_linef_46ha41j((void *)NULL, cx, NULL, t314, t316);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t318 = gm_l0;
                                                void * t319 = gm_l0;
                                                uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)t319) + 144));
                                                uint64_t r321; bool o322 = __builtin_add_overflow(t320, ((uint64_t)1ULL), &r321);
                                                if (o322) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 530, 17);
                                                uint64_t t323 = r321;
                                                GM_ST(uint64_t, (((uint8_t *)t318) + 144), t323);
                                                void * t324 = gm_l0;
                                                gm_b16_8 t325 = gm_l16;
                                                uint64_t t326 = gm_l20;
                                                gm_t3877pk7lv748 t327;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t327 + 0), t325);
                                                GM_ST(uint64_t, ((uint8_t *)&t327 + 16), t326);
                                                gm_b0_1 t328 = gm_f_linef_3dc3bmf((void *)NULL, cx, NULL, t324, &t327);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t329 = gm_l0;
                                                void * t330 = gm_l0;
                                                uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)t330) + 144));
                                                uint64_t r332; bool o333 = __builtin_add_overflow(t331, ((uint64_t)1ULL), &r332);
                                                if (o333) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 532, 17);
                                                uint64_t t334 = r332;
                                                GM_ST(uint64_t, (((uint8_t *)t329) + 144), t334);
                                                void * t335 = gm_l0;
                                                gm_b16_8 t336 = gm_l40;
                                                gm_b16_8 t337 = gm_l29;
                                                gm_b16_8 t338 = gm_l30;
                                                void * t339 = gm_l0;
                                                void * t340 = GM_LD(void *, ((uint8_t *)t339));
                                                void * t341 = (void *)(((uint8_t *)t340) + 8);
                                                gm_t88rcucg1b7uk t342 = gm_l6;
                                                gm_b16_8 t343 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t341, t342);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_b16_8 t344 = gm_l16;
                                                gm_tud7oovfvnf43 t345;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t345 + 0), t336);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t345 + 16), t337);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t345 + 32), t338);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t345 + 48), t343);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t345 + 64), t344);
                                                gm_b0_1 t346 = gm_f_linef_982sjai((void *)NULL, cx, NULL, t335, &t345);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t347 = gm_l0;
                                                gm_b0_1 t348 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t347);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t349 = gm_l0;
                                                void * t350 = gm_l0;
                                                uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)t350) + 144));
                                                uint64_t r352; bool o353 = __builtin_sub_overflow(t351, ((uint64_t)1ULL), &r352);
                                                if (o353) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 535, 17);
                                                uint64_t t354 = r352;
                                                GM_ST(uint64_t, (((uint8_t *)t349) + 144), t354);
                                                void * t355 = gm_l0;
                                                gm_b16_8 t356;
                                                GM_ST(void *, (uint8_t *)&t356, (void *)(gm_s2cuflhqkkuaq));
                                                GM_ST(uintptr_t, (uint8_t *)&t356 + 8, (uintptr_t)(1));
                                                gm_b0_1 t357 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t355, t356);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t358 = gm_l0;
                                                void * t359 = gm_l0;
                                                uint64_t t360 = GM_LD(uint64_t, (((uint8_t *)t359) + 144));
                                                uint64_t r361; bool o362 = __builtin_sub_overflow(t360, ((uint64_t)1ULL), &r361);
                                                if (o362) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 537, 17);
                                                uint64_t t363 = r361;
                                                GM_ST(uint64_t, (((uint8_t *)t358) + 144), t363);
                                                void * t364 = gm_l0;
                                                gm_b16_8 t365;
                                                GM_ST(void *, (uint8_t *)&t365, (void *)(gm_s02uuedsru1qu));
                                                GM_ST(uintptr_t, (uint8_t *)&t365 + 8, (uintptr_t)(8));
                                                gm_b0_1 t366 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t364, t365);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t367 = gm_l0;
                                                void * t368 = gm_l0;
                                                uint64_t t369 = GM_LD(uint64_t, (((uint8_t *)t368) + 144));
                                                uint64_t r370; bool o371 = __builtin_add_overflow(t369, ((uint64_t)1ULL), &r370);
                                                if (o371) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 539, 17);
                                                uint64_t t372 = r370;
                                                GM_ST(uint64_t, (((uint8_t *)t367) + 144), t372);
                                                void * t373 = gm_l0;
                                                gm_b16_8 t374 = gm_l37;
                                                gm_b16_8 t375 = gm_l29;
                                                gm_b16_8 t376 = gm_l29;
                                                gm_b16_8 t377 = gm_l30;
                                                gm_b16_8 t378 = gm_l17;
                                                gm_b16_8 t379 = gm_l16;
                                                gm_b16_8 t380 = gm_l18;
                                                gm_th2d9c7aju8hp t381;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t381 + 0), t374);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t381 + 16), t375);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t381 + 32), t376);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t381 + 48), t377);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t381 + 64), t378);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t381 + 80), t379);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t381 + 96), t380);
                                                gm_f_format_bd99ob7((void *)&gm_l68, (void *)NULL, cx, NULL, &t381);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t382 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                                if (!t382) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_src0gtuh94b1fce, 540, 32);
                                                gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                                                gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)&t383));
                                                gm_b0_1 t385 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t373, t384);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t386 = gm_l0;
                                                void * t387 = gm_l0;
                                                uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)t387) + 144));
                                                uint64_t r389; bool o390 = __builtin_sub_overflow(t388, ((uint64_t)1ULL), &r389);
                                                if (o390) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 541, 17);
                                                uint64_t t391 = r389;
                                                GM_ST(uint64_t, (((uint8_t *)t386) + 144), t391);
                                                void * t392 = gm_l0;
                                                gm_b16_8 t393;
                                                GM_ST(void *, (uint8_t *)&t393, (void *)(gm_s2cuflhqkkuaq));
                                                GM_ST(uintptr_t, (uint8_t *)&t393 + 8, (uintptr_t)(1));
                                                gm_b0_1 t394 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t392, t393);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                goto gm_lb19;
                                            }
gm_lb20:;
                                            void * t395 = gm_l0;
                                            gm_b16_8 t396 = gm_l19;
                                            gm_b16_8 t397 = gm_l16;
                                            uint64_t t398 = gm_l20;
                                            gm_b16_8 t399 = gm_l36;
                                            gm_b16_8 t400 = gm_l29;
                                            gm_b16_8 t401 = gm_l29;
                                            gm_b16_8 t402 = gm_l30;
                                            gm_b16_8 t403 = gm_l16;
                                            gm_b16_8 t404 = gm_l16;
                                            uint64_t t405 = gm_l20;
                                            gm_b16_8 t406 = gm_l37;
                                            gm_b16_8 t407 = gm_l29;
                                            gm_b16_8 t408 = gm_l29;
                                            gm_b16_8 t409 = gm_l30;
                                            gm_b16_8 t410 = gm_l17;
                                            gm_b16_8 t411 = gm_l16;
                                            gm_b16_8 t412 = gm_l18;
                                            gm_toq7erhpb1d2e t413;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 0), t396);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 16), t397);
                                            GM_ST(uint64_t, ((uint8_t *)&t413 + 32), t398);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 40), t399);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 56), t400);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 72), t401);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 88), t402);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 104), t403);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 120), t404);
                                            GM_ST(uint64_t, ((uint8_t *)&t413 + 136), t405);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 144), t406);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 160), t407);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 176), t408);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 192), t409);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 208), t410);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 224), t411);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t413 + 240), t412);
                                            gm_f_format_bt0erq9((void *)&gm_l69, (void *)NULL, cx, NULL, &t413);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t414 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                            if (!t414) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_src0gtuh94b1fce, 544, 32);
                                            gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                                            gm_b16_8 t416 = GM_LD(gm_b16_8, ((uint8_t *)&t415));
                                            gm_b0_1 t417 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t395, t416);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
gm_lb19:;
                                    }
                                }
                                {
                                    void * t418 = gm_l0;
                                    void * t419 = gm_l0;
                                    uint64_t t420 = GM_LD(uint64_t, (((uint8_t *)t419) + 144));
                                    uint64_t r421; bool o422 = __builtin_sub_overflow(t420, ((uint64_t)1ULL), &r421);
                                    if (o422) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 547, 9);
                                    uint64_t t423 = r421;
                                    GM_ST(uint64_t, (((uint8_t *)t418) + 144), t423);
                                    void * t424 = gm_l0;
                                    gm_b16_8 t425;
                                    GM_ST(void *, (uint8_t *)&t425, (void *)(gm_s2cuflhqkkuaq));
                                    GM_ST(uintptr_t, (uint8_t *)&t425 + 8, (uintptr_t)(1));
                                    gm_b0_1 t426 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t424, t425);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t427 = gm_l0;
                                    gm_b16_8 t428 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t427);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t5o0tpv5jka2l t429;
                                    GM_ST(uint8_t, (uint8_t *)&t429 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t429 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t429 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t429 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t429 + 32, 0);
                                    ((uint8_t *)&t429)[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t429 + 8), t428);
                                    memmove(gm_out, ((uint8_t *)&t429), 40);
                                    goto gm_fdone;
                                }
                            }
gm_lb11:;
                        }
                        {
                            gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 501, 46);
                        }
                    }
gm_lb10:;
                }
            }
gm_lb9:;
        }
gm_lb8:;
    }
    {
        void * t430 = gm_l0;
        uint32_t t431 = gm_l13;
        uint32_t t432 = gm_l13;
        gm_tcc1rq4n1avb2 t433;
        GM_ST(uint32_t, ((uint8_t *)&t433 + 0), t431);
        GM_ST(uint32_t, ((uint8_t *)&t433 + 4), t432);
        gm_b0_1 t434 = gm_f_linef_vddfrtf((void *)NULL, cx, NULL, t430, t433);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t435 = gm_l0;
        void * t436 = gm_l0;
        uint64_t t437 = GM_LD(uint64_t, (((uint8_t *)t436) + 144));
        uint64_t r438; bool o439 = __builtin_add_overflow(t437, ((uint64_t)1ULL), &r438);
        if (o439) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 552, 5);
        uint64_t t440 = r438;
        GM_ST(uint64_t, (((uint8_t *)t435) + 144), t440);
        {
            {
                uint8_t t441 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
                if (!t441) {
                    goto gm_lb22;
                }
                gm_b16_8 t442 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                gm_l41 = t442;
                void * t443 = gm_l0;
                gm_b16_8 t444 = gm_l41;
                gm_t88rcucg1b7uk t445 = gm_l6;
                gm_b16_8 t446 = gm_f_piece_stub_m8mb7rc((void *)NULL, cx, NULL, t443, t444, ((uint8_t)115ULL), t445);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l42 = t446;
                void * t447 = gm_l0;
                gm_b16_8 t448 = gm_l41;
                gm_t88rcucg1b7uk t449 = gm_l6;
                gm_b16_8 t450 = gm_f_piece_stub_m8mb7rc((void *)NULL, cx, NULL, t447, t448, ((uint8_t)102ULL), t449);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l43 = t450;
                void * t451 = gm_l0;
                void * t452 = GM_LD(void *, ((uint8_t *)t451));
                gm_tl874t2atnlh3 t453 = gm_l1;
                gm_b16_8 t454 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t452, t453);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l44 = t454;
                void * t455 = gm_l0;
                gm_b16_8 t456 = gm_l15;
                gm_b16_8 t457 = gm_l42;
                void * t458 = gm_l0;
                gm_b16_8 t459 = GM_LD(gm_b16_8, (((uint8_t *)t458) + 200));
                gm_b16_8 t460 = gm_l15;
                gm_b16_8 t461 = gm_l15;
                gm_b16_8 t462 = gm_l44;
                gm_tif3elqk01bdl t463;
                GM_ST(gm_b16_8, ((uint8_t *)&t463 + 0), t456);
                GM_ST(gm_b16_8, ((uint8_t *)&t463 + 16), t457);
                GM_ST(gm_b16_8, ((uint8_t *)&t463 + 32), t459);
                GM_ST(gm_b16_8, ((uint8_t *)&t463 + 48), t460);
                GM_ST(gm_b16_8, ((uint8_t *)&t463 + 64), t461);
                GM_ST(gm_b16_8, ((uint8_t *)&t463 + 80), t462);
                gm_f_format_ik8h77v((void *)&gm_l70, (void *)NULL, cx, NULL, &t463);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t464 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                if (!t464) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 8, gm_src0gtuh94b1fce, 557, 24);
                gm_b16_8 t465 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                gm_b16_8 t466 = GM_LD(gm_b16_8, ((uint8_t *)&t465));
                gm_b0_1 t467 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t455, t466);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t468 = gm_l0;
                uint32_t t469 = gm_l13;
                gm_b16_8 t470 = gm_l14;
                gm_tiilmfd16v3fc t471;
                GM_ST(uint32_t, (uint8_t *)&t471 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t471 + 0), t469);
                GM_ST(gm_b16_8, ((uint8_t *)&t471 + 8), t470);
                gm_b0_1 t472 = gm_f_linef_qqgq3om((void *)NULL, cx, NULL, t468, &t471);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t473 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t474 = (t473 > ((uint64_t)0ULL));
                if (t474) {
                    void * t475 = gm_l0;
                    uint32_t t476 = gm_l13;
                    gm_b16_8 t477 = gm_l14;
                    uint32_t t478 = gm_l13;
                    gm_t5g5vgung6h6s t479;
                    GM_ST(uint32_t, (uint8_t *)&t479 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t479 + 28, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t479 + 0), t476);
                    GM_ST(gm_b16_8, ((uint8_t *)&t479 + 8), t477);
                    GM_ST(uint32_t, ((uint8_t *)&t479 + 24), t478);
                    gm_b0_1 t480 = gm_f_linef_qfq4ah7((void *)NULL, cx, NULL, t475, &t479);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t481 = gm_l0;
                    gm_b16_8 t482 = gm_l18;
                    gm_tnpifv20irnph t483;
                    GM_ST(gm_b16_8, ((uint8_t *)&t483 + 0), t482);
                    gm_b0_1 t484 = gm_f_linef_46ha41j((void *)NULL, cx, NULL, t481, t483);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t485 = gm_l0;
                    void * t486 = gm_l0;
                    uint64_t t487 = GM_LD(uint64_t, (((uint8_t *)t486) + 144));
                    uint64_t r488; bool o489 = __builtin_add_overflow(t487, ((uint64_t)1ULL), &r488);
                    if (o489) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 562, 13);
                    uint64_t t490 = r488;
                    GM_ST(uint64_t, (((uint8_t *)t485) + 144), t490);
                    void * t491 = gm_l0;
                    uint32_t t492 = gm_f_fresh_5kokjmg(t491);
                    gm_t88rcucg1b7uk t493;
                    GM_ST(uint32_t, ((uint8_t *)&t493 + 0), t492);
                    gm_f_format_12uv53h((void *)&gm_l71, (void *)NULL, cx, NULL, t493);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t494 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                    if (!t494) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_src0gtuh94b1fce, 563, 18);
                    gm_b16_8 t495 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                    gm_l45 = t495;
                    void * t496 = gm_l0;
                    gm_b16_8 t497 = gm_l45;
                    gm_tnpifv20irnph t498;
                    GM_ST(gm_b16_8, ((uint8_t *)&t498 + 0), t497);
                    gm_b0_1 t499 = gm_f_linef_hg1enfq((void *)NULL, cx, NULL, t496, t498);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t500 = gm_l0;
                    gm_b16_8 t501 = gm_l45;
                    gm_b16_8 t502 = gm_l17;
                    gm_b16_8 t503 = gm_l16;
                    gm_b16_8 t504 = gm_l18;
                    gm_t0uuhaljsf410 t505;
                    GM_ST(gm_b16_8, ((uint8_t *)&t505 + 0), t501);
                    GM_ST(gm_b16_8, ((uint8_t *)&t505 + 16), t502);
                    GM_ST(gm_b16_8, ((uint8_t *)&t505 + 32), t503);
                    GM_ST(gm_b16_8, ((uint8_t *)&t505 + 48), t504);
                    gm_b0_1 t506 = gm_f_linef_jj5gp4g((void *)NULL, cx, NULL, t500, &t505);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t507 = gm_l0;
                    gm_b16_8 t508 = gm_l45;
                    gm_b16_8 t509 = GM_LD(gm_b16_8, ((uint8_t *)&t508));
                    gm_tl874t2atnlh3 t510 = gm_l1;
                    gm_teegpca3bk79b t511 = GM_LD(gm_teegpca3bk79b, ((uint8_t *)a2));
                    gm_f_emit_buf_mqofii1((void *)&gm_l72, (void *)NULL, cx, NULL, t507, &t511, t509, t510);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t512 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                    if (t512) {
                    } else {
                        gm_t08olq97rscn3 t513 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l72) + 8));
                        gm_t5o0tpv5jka2l t514;
                        GM_ST(uint8_t, (uint8_t *)&t514 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t514 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t514 + 4, 0);
                        ((uint8_t *)&t514)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t514 + 8), t513);
                        memmove(gm_out, ((uint8_t *)&t514), 40);
                        goto gm_fdone;
                    }
                    void * t515 = gm_l0;
                    void * t516 = gm_l0;
                    uint64_t t517 = GM_LD(uint64_t, (((uint8_t *)t516) + 144));
                    uint64_t r518; bool o519 = __builtin_sub_overflow(t517, ((uint64_t)1ULL), &r518);
                    if (o519) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 567, 13);
                    uint64_t t520 = r518;
                    GM_ST(uint64_t, (((uint8_t *)t515) + 144), t520);
                    void * t521 = gm_l0;
                    gm_b16_8 t522;
                    GM_ST(void *, (uint8_t *)&t522, (void *)(gm_s02uuedsru1qu));
                    GM_ST(uintptr_t, (uint8_t *)&t522 + 8, (uintptr_t)(8));
                    gm_b0_1 t523 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t521, t522);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t524 = gm_l0;
                    void * t525 = gm_l0;
                    uint64_t t526 = GM_LD(uint64_t, (((uint8_t *)t525) + 144));
                    uint64_t r527; bool o528 = __builtin_add_overflow(t526, ((uint64_t)1ULL), &r527);
                    if (o528) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 569, 13);
                    uint64_t t529 = r527;
                    GM_ST(uint64_t, (((uint8_t *)t524) + 144), t529);
                    void * t530 = gm_l0;
                    gm_b16_8 t531 = gm_l19;
                    gm_b16_8 t532 = gm_l16;
                    uint64_t t533 = gm_l20;
                    gm_b16_8 t534 = gm_l42;
                    void * t535 = gm_l0;
                    gm_b16_8 t536 = GM_LD(gm_b16_8, (((uint8_t *)t535) + 200));
                    gm_b16_8 t537 = gm_l16;
                    gm_b16_8 t538 = gm_l16;
                    uint64_t t539 = gm_l20;
                    gm_b16_8 t540 = gm_l44;
                    gm_b16_8 t541 = gm_l43;
                    void * t542 = gm_l0;
                    gm_b16_8 t543 = GM_LD(gm_b16_8, (((uint8_t *)t542) + 200));
                    gm_b16_8 t544 = gm_l17;
                    gm_b16_8 t545 = gm_l16;
                    gm_b16_8 t546 = gm_l44;
                    gm_t8mpn05efspp6 t547;
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 0), t531);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 16), t532);
                    GM_ST(uint64_t, ((uint8_t *)&t547 + 32), t533);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 40), t534);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 56), t536);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 72), t537);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 88), t538);
                    GM_ST(uint64_t, ((uint8_t *)&t547 + 104), t539);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 112), t540);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 128), t541);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 144), t543);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 160), t544);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 176), t545);
                    GM_ST(gm_b16_8, ((uint8_t *)&t547 + 192), t546);
                    gm_f_format_vr86ufh((void *)&gm_l73, (void *)NULL, cx, NULL, &t547);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t548 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                    if (!t548) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 8, gm_src0gtuh94b1fce, 570, 28);
                    gm_b16_8 t549 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
                    gm_b16_8 t550 = GM_LD(gm_b16_8, ((uint8_t *)&t549));
                    gm_b0_1 t551 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t530, t550);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t552 = gm_l0;
                    void * t553 = gm_l0;
                    uint64_t t554 = GM_LD(uint64_t, (((uint8_t *)t553) + 144));
                    uint64_t r555; bool o556 = __builtin_sub_overflow(t554, ((uint64_t)1ULL), &r555);
                    if (o556) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 571, 13);
                    uint64_t t557 = r555;
                    GM_ST(uint64_t, (((uint8_t *)t552) + 144), t557);
                    void * t558 = gm_l0;
                    gm_b16_8 t559;
                    GM_ST(void *, (uint8_t *)&t559, (void *)(gm_s2cuflhqkkuaq));
                    GM_ST(uintptr_t, (uint8_t *)&t559 + 8, (uintptr_t)(1));
                    gm_b0_1 t560 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t558, t559);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t561 = gm_l0;
                void * t562 = gm_l0;
                uint64_t t563 = GM_LD(uint64_t, (((uint8_t *)t562) + 144));
                uint64_t r564; bool o565 = __builtin_sub_overflow(t563, ((uint64_t)1ULL), &r564);
                if (o565) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 574, 9);
                uint64_t t566 = r564;
                GM_ST(uint64_t, (((uint8_t *)t561) + 144), t566);
                void * t567 = gm_l0;
                gm_b16_8 t568;
                GM_ST(void *, (uint8_t *)&t568, (void *)(gm_s2cuflhqkkuaq));
                GM_ST(uintptr_t, (uint8_t *)&t568 + 8, (uintptr_t)(1));
                gm_b0_1 t569 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t567, t568);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t570 = gm_l0;
                gm_b16_8 t571 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t570);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t572;
                GM_ST(uint8_t, (uint8_t *)&t572 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t572 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t572 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t572 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t572 + 32, 0);
                ((uint8_t *)&t572)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t572 + 8), t571);
                memmove(gm_out, ((uint8_t *)&t572), 40);
                goto gm_fdone;
            }
gm_lb22:;
        }
gm_lb21:;
    }
    {
        void * t573 = gm_l0;
        gm_b16_8 t574 = gm_l15;
        gm_tnpifv20irnph t575;
        GM_ST(gm_b16_8, ((uint8_t *)&t575 + 0), t574);
        gm_b0_1 t576 = gm_f_linef_h1q98vm((void *)NULL, cx, NULL, t573, t575);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t577 = gm_l0;
        void * t578 = gm_l0;
        uint64_t t579 = GM_LD(uint64_t, (((uint8_t *)t578) + 144));
        uint64_t r580; bool o581 = __builtin_add_overflow(t579, ((uint64_t)1ULL), &r580);
        if (o581) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 579, 5);
        uint64_t t582 = r580;
        GM_ST(uint64_t, (((uint8_t *)t577) + 144), t582);
        void * t583 = gm_l0;
        gm_t88rcucg1b7uk t584 = gm_l6;
        gm_b16_8 t585 = gm_l15;
        gm_tnpifv20irnph t586;
        GM_ST(gm_b16_8, ((uint8_t *)&t586 + 0), t585);
        gm_f_format_jq3k246((void *)&gm_l74, (void *)NULL, cx, NULL, t586);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t587 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
        if (!t587) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_src0gtuh94b1fce, 580, 33);
        gm_b16_8 t588 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
        gm_b16_8 t589 = GM_LD(gm_b16_8, ((uint8_t *)&t588));
        gm_b16_8 t590 = gm_l15;
        gm_tnpifv20irnph t591;
        GM_ST(gm_b16_8, ((uint8_t *)&t591 + 0), t590);
        gm_f_format_4c9sg76((void *)&gm_l75, (void *)NULL, cx, NULL, t591);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t592 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
        if (!t592) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 8, gm_src0gtuh94b1fce, 580, 63);
        gm_b16_8 t593 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
        gm_b16_8 t594 = GM_LD(gm_b16_8, ((uint8_t *)&t593));
        gm_b16_8 t595 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t583, t584, t589, t594);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l46 = t595;
        void * t596 = gm_l0;
        gm_b16_8 t597 = gm_l46;
        gm_tl874t2atnlh3 t598 = gm_l1;
        gm_teegpca3bk79b t599 = GM_LD(gm_teegpca3bk79b, ((uint8_t *)a2));
        gm_f_emit_to_o7js99p((void *)&gm_l76, (void *)NULL, cx, NULL, t596, &t599, t597, t598);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t600 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
        if (t600) {
        } else {
            gm_t08olq97rscn3 t601 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l76) + 8));
            gm_t5o0tpv5jka2l t602;
            GM_ST(uint8_t, (uint8_t *)&t602 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t602 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t602 + 4, 0);
            ((uint8_t *)&t602)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t602 + 8), t601);
            memmove(gm_out, ((uint8_t *)&t602), 40);
            goto gm_fdone;
        }
        void * t603 = gm_l0;
        void * t604 = gm_l0;
        uint64_t t605 = GM_LD(uint64_t, (((uint8_t *)t604) + 144));
        uint64_t r606; bool o607 = __builtin_sub_overflow(t605, ((uint64_t)1ULL), &r606);
        if (o607) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 582, 5);
        uint64_t t608 = r606;
        GM_ST(uint64_t, (((uint8_t *)t603) + 144), t608);
        void * t609 = gm_l0;
        gm_b16_8 t610;
        GM_ST(void *, (uint8_t *)&t610, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t610 + 8, (uintptr_t)(1));
        gm_b0_1 t611 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t609, t610);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t612 = gm_l0;
        uint32_t t613 = gm_l13;
        gm_b16_8 t614 = gm_l14;
        gm_tiilmfd16v3fc t615;
        GM_ST(uint32_t, (uint8_t *)&t615 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t615 + 0), t613);
        GM_ST(gm_b16_8, ((uint8_t *)&t615 + 8), t614);
        gm_b0_1 t616 = gm_f_linef_qqgq3om((void *)NULL, cx, NULL, t612, &t615);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t617 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint8_t t618 = (t617 > ((uint64_t)0ULL));
        if (t618) {
            void * t619 = gm_l0;
            uint32_t t620 = gm_f_fresh_5kokjmg(t619);
            gm_t88rcucg1b7uk t621;
            GM_ST(uint32_t, ((uint8_t *)&t621 + 0), t620);
            gm_f_format_12uv53h((void *)&gm_l77, (void *)NULL, cx, NULL, t621);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t622 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
            if (!t622) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_src0gtuh94b1fce, 586, 14);
            gm_b16_8 t623 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
            gm_l47 = t623;
            void * t624 = gm_l0;
            gm_b16_8 t625 = gm_l47;
            gm_tnpifv20irnph t626;
            GM_ST(gm_b16_8, ((uint8_t *)&t626 + 0), t625);
            gm_b0_1 t627 = gm_f_linef_hg1enfq((void *)NULL, cx, NULL, t624, t626);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t628 = gm_l0;
            uint32_t t629 = gm_l13;
            gm_b16_8 t630 = gm_l14;
            uint32_t t631 = gm_l13;
            gm_t5g5vgung6h6s t632;
            GM_ST(uint32_t, (uint8_t *)&t632 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t632 + 28, 0);
            GM_ST(uint32_t, ((uint8_t *)&t632 + 0), t629);
            GM_ST(gm_b16_8, ((uint8_t *)&t632 + 8), t630);
            GM_ST(uint32_t, ((uint8_t *)&t632 + 24), t631);
            gm_b0_1 t633 = gm_f_linef_qfq4ah7((void *)NULL, cx, NULL, t628, &t632);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t634 = gm_l0;
            gm_b16_8 t635 = gm_l18;
            gm_b16_8 t636 = gm_l47;
            gm_b16_8 t637 = gm_l17;
            gm_b16_8 t638 = gm_l16;
            gm_b16_8 t639 = gm_l18;
            gm_b16_8 t640 = gm_l47;
            gm_b16_8 t641 = gm_l17;
            gm_b16_8 t642 = gm_l16;
            gm_tg1t0c5hkovn5 t643;
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 0), t635);
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 16), t636);
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 32), t637);
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 48), t638);
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 64), t639);
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 80), t640);
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 96), t641);
            GM_ST(gm_b16_8, ((uint8_t *)&t643 + 112), t642);
            gm_b0_1 t644 = gm_f_linef_pu91mqj((void *)NULL, cx, NULL, t634, &t643);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t645 = gm_l0;
            gm_b16_8 t646 = gm_l47;
            gm_b16_8 t647 = GM_LD(gm_b16_8, ((uint8_t *)&t646));
            gm_tl874t2atnlh3 t648 = gm_l1;
            gm_teegpca3bk79b t649 = GM_LD(gm_teegpca3bk79b, ((uint8_t *)a2));
            gm_f_emit_buf_mqofii1((void *)&gm_l78, (void *)NULL, cx, NULL, t645, &t649, t647, t648);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t650 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
            if (t650) {
            } else {
                gm_t08olq97rscn3 t651 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l78) + 8));
                gm_t5o0tpv5jka2l t652;
                GM_ST(uint8_t, (uint8_t *)&t652 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t652 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t652 + 4, 0);
                ((uint8_t *)&t652)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t652 + 8), t651);
                memmove(gm_out, ((uint8_t *)&t652), 40);
                goto gm_fdone;
            }
        }
    }
    {
        void * t653 = gm_l0;
        void * t654 = gm_l0;
        uint64_t t655 = GM_LD(uint64_t, (((uint8_t *)t654) + 144));
        uint64_t r656; bool o657 = __builtin_sub_overflow(t655, ((uint64_t)1ULL), &r656);
        if (o657) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 592, 5);
        uint64_t t658 = r656;
        GM_ST(uint64_t, (((uint8_t *)t653) + 144), t658);
        void * t659 = gm_l0;
        gm_b16_8 t660;
        GM_ST(void *, (uint8_t *)&t660, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t660 + 8, (uintptr_t)(1));
        gm_b0_1 t661 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t659, t660);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t662 = gm_l0;
        gm_b16_8 t663 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t662);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t664;
        GM_ST(uint8_t, (uint8_t *)&t664 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t664 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t664 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t664 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t664 + 32, 0);
        ((uint8_t *)&t664)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t664 + 8), t663);
        memmove(gm_out, ((uint8_t *)&t664), 40);
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
__attribute__((aligned(4))) void gm_f_write_values_lfh31jc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_write_values_lfh31jc F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l3; /* pieces */
    gm_b16_8 gm_l4; /* specs */
    gm_b16_8 gm_l5; /* tys */
    gm_b16_8 gm_l6; /* vals */
    gm_t88rcucg1b7uk gm_l7; /* str_ty */
    gm_t88rcucg1b7uk gm_l8; /* unit_ty */
    gm_th2lqbo77pvsc gm_l9; /* stub */
    gm_tiilmfd16v3fc gm_l10; /* effect_ */
    uint32_t gm_l11; /* op */
    uint32_t gm_l12; /* id */
    gm_tnqk2qt7j42o9 gm_l13; /* stub_tys */
    gm_tep2drsl9408b gm_l14; /* cv */
    gm_th2lqbo77pvsc gm_l15; /* direct */
    gm_b16_8 gm_l17; /* c */
    gm_b16_8 gm_l18; /* ccx */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tiilmfd16v3fc gm_l21; /* eff */
    uint32_t gm_l22; /* id */
    gm_b16_8 gm_l23; /* n */
    gm_b16_8 gm_l24; /* fs */
    gm_b16_8 gm_l25; /* ff */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    gm_b16_8 gm_l29; /* d */
    gm_b16_8 gm_l30; /* sl */
    gm_b16_8 gm_l31; /* spec */
    gm_t7g88i52rrtm5 gm_l32; /* sp */
    gm_b16_8 gm_l33; /* a */
    uint64_t gm_l34; /* p */
    uint64_t gm_l35; /* p */
    gm_b16_8 gm_l36; /* core */
    gm_b16_8 gm_l37; /* ps */
    gm_b16_8 gm_l38; /* pf */
    gm_b16_8 gm_l39; /* at */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    gm_b16_8 gm_l43; /* s */
    gm_b16_8 gm_l44; /* d */
    gm_tflpan0ud4tim gm_l45; /* custom */
    gm_t7g88i52rrtm5 gm_l46; /* sp */
    gm_t7g88i52rrtm5 gm_l47; /* sp */
    gm_b16_8 gm_l48; /* b */
    gm_b16_8 gm_l49; /* c */
    gm_b16_8 gm_l50; /* a */
    uint64_t gm_l51; /* p */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    uint64_t gm_l54; /* i */
    gm_b16_8 gm_l55; /* s */
    gm_b16_8 gm_l56; /* sl */
    gm_b16_8 gm_l57; /* d */
    gm_b16_8 gm_l58; /* b */
    gm_tflpan0ud4tim gm_l59; /* custom */
    gm_t7g88i52rrtm5 gm_l60; /* sp */
    gm_t7g88i52rrtm5 gm_l61; /* sp */
    gm_b16_8 gm_l62; /* c */
    GM_STACK_CHECK(&F, 7821u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_t53at80pfdus3 gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tflpan0ud4tim gm_l68;
    gm_tflpan0ud4tim gm_l69;
    gm_tabb710d9mq2e gm_l70;
    uint8_t gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_tabb710d9mq2e gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_tflpan0ud4tim gm_l75;
    gm_tflpan0ud4tim gm_l76;
    gm_tabb710d9mq2e gm_l77;
    gm_tqgtd5gtgj65s gm_l78;
    gm_tabb710d9mq2e gm_l79;
    gm_tabb710d9mq2e gm_l80;
    gm_tabb710d9mq2e gm_l81;
    gm_tqgtd5gtgj65s gm_l82;
    gm_tabb710d9mq2e gm_l83;
    gm_tflpan0ud4tim gm_l84;
    gm_tflpan0ud4tim gm_l85;
    gm_tqgtd5gtgj65s gm_l86;
    {
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, ((uint8_t *)t50));
        void * t52 = (void *)(((uint8_t *)t51) + 8);
        gm_t88rcucg1b7uk t53 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t53;
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, ((uint8_t *)t54));
        void * t56 = (void *)(((uint8_t *)t55) + 8);
        gm_t88rcucg1b7uk t57 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t57;
        gm_th2lqbo77pvsc t58;
        memset(&t58, 0, sizeof t58);
        gm_l9 = t58;
        {
            {
                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                uint8_t t60 = (uint8_t)(t59 == ((uint8_t)0ULL));
                if (!t60) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l10), (((uint8_t *)a2) + 8), 24);
                uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 24));
                gm_l11 = t61;
                void * t62 = gm_l0;
                void * t63 = GM_LD(void *, ((uint8_t *)t62));
                void * t64 = (void *)((uint8_t *)&gm_l10);
                uint32_t t65 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t65;
                gm_t88rcucg1b7uk t66 = gm_l7;
                gm_tnqk2qt7j42o9 t67;
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t67 + 0), t66);
                gm_l13 = t67;
                void * t68 = gm_l0;
                void * t69 = (void *)((uint8_t *)&gm_l10);
                uint32_t t70 = gm_l12;
                uint32_t t71 = gm_l11;
                void * t72 = (void *)((uint8_t *)&gm_l13);
                uint64_t t73 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t74 = (uintptr_t)((uint64_t)1ULL);
                if (t73 > t74 || t74 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t73, t74, ((uint64_t)1ULL), gm_src0gtuh94b1fce, 355, 50);
                uint64_t t75 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t76;
                GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t72 + (uintptr_t)((uint64_t)0ULL) * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(t75));
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                gm_t88rcucg1b7uk t78 = gm_l8;
                gm_f_out_stub_ils4a69((void *)&gm_l9, (void *)NULL, cx, NULL, t68, t69, t70, t71, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                uint8_t t80 = (uint8_t)(t79 == ((uint8_t)1ULL));
                if (!t80) {
                    goto gm_lb3;
                }
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 351, 5);
        }
gm_lb1:;
        {
            {
                {
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)1ULL));
                    if (!t82) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&gm_l14), (((uint8_t *)a2) + 8), 24);
                    memmove(((uint8_t *)&gm_l15), ((((uint8_t *)a2) + 8) + 24), 24);
                    void * t83 = gm_l0;
                    void * t84 = GM_LD(void *, ((uint8_t *)t83));
                    void * t85 = (void *)(((uint8_t *)t84) + 8);
                    gm_t88rcucg1b7uk t86 = gm_l7;
                    uint8_t t87 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t85, t86);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t88 = (uint8_t)!t87;
                    if (!t88) {
                        goto gm_lb5;
                    }
                    {
                        {
                            {
                                {
                                    void * t89 = gm_l0;
                                    void * t90 = GM_LD(void *, ((uint8_t *)t89));
                                    void * t91 = (void *)(((uint8_t *)t90) + 8);
                                    gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 16));
                                    gm_f_kind_ghvav22((void *)&gm_l63, t91, GM_CONV(gm_csguankko4lmhd, t92));
                                    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l63) + 0));
                                    uint8_t t94 = (uint8_t)(t93 == ((uint8_t)15ULL));
                                    if (!t94) {
                                        goto gm_lb7;
                                    }
                                    memmove(((uint8_t *)&F.l16), (((uint8_t *)&gm_l63) + 8), 48);
                                    void * t95 = gm_l0;
                                    gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 16));
                                    gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                                    gm_b16_8 t98 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t95, t96, t97);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l17 = t98;
                                    void * t99 = gm_l0;
                                    uint32_t t100 = gm_f_fresh_5kokjmg(t99);
                                    gm_t88rcucg1b7uk t101;
                                    GM_ST(uint32_t, ((uint8_t *)&t101 + 0), t100);
                                    gm_f_format_qr5c6un((void *)&gm_l64, (void *)NULL, cx, NULL, t101);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                    if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src0gtuh94b1fce, 364, 16);
                                    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                    gm_l18 = t103;
                                    void * t104 = gm_l0;
                                    gm_b16_8 t105 = gm_l18;
                                    gm_b16_8 t106 = gm_l17;
                                    gm_t6j6smj7qoctu t107;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t105);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t107 + 16), t106);
                                    gm_b0_1 t108 = gm_f_linef_lot5c36((void *)NULL, cx, NULL, t104, &t107);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 24));
                                    gm_l19 = t109;
                                    gm_l20 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t110 = gm_l20;
                                            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                            uint8_t t112 = (t110 < t111);
                                            if (!t112) {
                                                goto gm_lb8;
                                            }
                                            {
                                                void * t113 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                                uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                uint64_t t115 = gm_l20;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&gm_l21), (((uint8_t *)t113) + (uintptr_t)t115 * 24), 24);
                                                        void * t116 = gm_l0;
                                                        void * t117 = GM_LD(void *, ((uint8_t *)t116));
                                                        void * t118 = (void *)((uint8_t *)&gm_l21);
                                                        uint32_t t119 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t117, t118);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l22 = t119;
                                                        void * t120 = gm_l0;
                                                        uint32_t t121 = gm_f_fresh_5kokjmg(t120);
                                                        gm_t88rcucg1b7uk t122;
                                                        GM_ST(uint32_t, ((uint8_t *)&t122 + 0), t121);
                                                        gm_f_format_mgf81l5((void *)&gm_l65, (void *)NULL, cx, NULL, t122);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t123 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                                        if (!t123) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_src0gtuh94b1fce, 368, 18);
                                                        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                                        gm_l23 = t124;
                                                        void * t125 = gm_l0;
                                                        gm_b16_8 t126 = gm_l23;
                                                        void * t127 = gm_l0;
                                                        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)t127) + 200));
                                                        uint32_t t129 = gm_l22;
                                                        gm_b16_8 t130 = gm_l23;
                                                        gm_b16_8 t131 = gm_l23;
                                                        gm_b16_8 t132 = gm_l18;
                                                        gm_b16_8 t133 = gm_l18;
                                                        gm_b16_8 t134 = gm_l23;
                                                        gm_t586m754bse2v t135;
                                                        GM_ST(uint32_t, (uint8_t *)&t135 + 36, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 0), t126);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 16), t128);
                                                        GM_ST(uint32_t, ((uint8_t *)&t135 + 32), t129);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 40), t130);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 56), t131);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 72), t132);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 88), t133);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 104), t134);
                                                        gm_b0_1 t136 = gm_f_linef_ubknr7g((void *)NULL, cx, NULL, t125, &t135);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb11;
                                                    }
gm_lb12:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 366, 9);
                                                }
gm_lb11:;
                                            }
gm_lb10:;
                                            uint64_t t137 = gm_l20;
                                            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                                            uint64_t t140 = r138;
                                            gm_l20 = t140;
                                        }
                                    }
gm_lb8:;
                                }
                                {
                                    void * t141 = gm_l0;
                                    gm_t88rcucg1b7uk t142 = gm_l7;
                                    gm_b16_8 t143 = gm_f_fn_sink_stub_5ipff9h((void *)NULL, cx, NULL, t141, t142);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l24 = t143;
                                    void * t144 = gm_l0;
                                    gm_b16_8 t145 = gm_l24;
                                    gm_b16_8 t146 = gm_f_fn_format_stub_53r45gt((void *)NULL, cx, NULL, t144, t145);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l25 = t146;
                                    gm_l27 = ((uint64_t)0ULL);
                                    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    gm_l26 = t147;
                                    {
                                        for (;;) {
                                            uint64_t t148 = gm_l27;
                                            uint64_t t149 = gm_l26;
                                            uint8_t t150 = (t148 < t149);
                                            if (!t150) {
                                                goto gm_lb13;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t151 = gm_l27;
                                                        gm_l28 = t151;
                                                        void * t152 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                        uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                        uint64_t t154 = gm_l28;
                                                        uint64_t t155 = GM_LD(uint64_t, ((((uint8_t *)t152) + (uintptr_t)t154 * 16) + 8));
                                                        uint8_t t156 = (t155 > ((uint64_t)0ULL));
                                                        if (t156) {
                                                            {
                                                                {
                                                                    uint8_t t157 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                                                                    if (!t157) {
                                                                        goto gm_lb19;
                                                                    }
                                                                    gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                                                                    gm_l29 = t158;
                                                                    void * t159 = gm_l0;
                                                                    gm_t88rcucg1b7uk t160 = gm_l7;
                                                                    void * t161 = gm_l0;
                                                                    void * t162 = GM_LD(void *, ((uint8_t *)t161));
                                                                    void * t163 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                                    uint64_t t165 = gm_l28;
                                                                    gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)t163) + (uintptr_t)t165 * 16));
                                                                    gm_b16_8 t167 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t162, t166);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    void * t168 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                                    uint64_t t170 = gm_l28;
                                                                    uint64_t t171 = GM_LD(uint64_t, ((((uint8_t *)t168) + (uintptr_t)t170 * 16) + 8));
                                                                    gm_t4ce0jd3tdro4 t172;
                                                                    GM_ST(uint64_t, ((uint8_t *)&t172 + 0), t171);
                                                                    gm_f_format_sa1940l((void *)&gm_l66, (void *)NULL, cx, NULL, t172);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                                                    if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_src0gtuh94b1fce, 376, 79);
                                                                    gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                                                                    gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                                                                    gm_b16_8 t176 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t159, t160, t167, t175);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l30 = t176;
                                                                    void * t177 = gm_l0;
                                                                    gm_b16_8 t178 = gm_l29;
                                                                    gm_b16_8 t179 = gm_l17;
                                                                    gm_b16_8 t180 = gm_l18;
                                                                    gm_b16_8 t181 = gm_l30;
                                                                    gm_t0uuhaljsf410 t182;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t182 + 0), t178);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t182 + 16), t179);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t182 + 32), t180);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t182 + 48), t181);
                                                                    gm_b0_1 t183 = gm_f_linef_amf91ik((void *)NULL, cx, NULL, t177, &t182);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    void * t184 = gm_l0;
                                                                    gm_b0_1 t185 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t184);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    goto gm_lb18;
                                                                }
gm_lb19:;
                                                                void * t186 = gm_l0;
                                                                gm_b16_8 t187 = gm_l24;
                                                                gm_b16_8 t188 = gm_l17;
                                                                gm_b16_8 t189 = gm_l17;
                                                                gm_b16_8 t190 = gm_l18;
                                                                void * t191 = gm_l0;
                                                                void * t192 = GM_LD(void *, ((uint8_t *)t191));
                                                                void * t193 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                                uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                                uint64_t t195 = gm_l28;
                                                                gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)t193) + (uintptr_t)t195 * 16));
                                                                gm_b16_8 t197 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t192, t196);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                void * t198 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                                uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                                uint64_t t200 = gm_l28;
                                                                uint64_t t201 = GM_LD(uint64_t, ((((uint8_t *)t198) + (uintptr_t)t200 * 16) + 8));
                                                                gm_tjftu5su6fh8e t202;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t202 + 0), t187);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t202 + 16), t188);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t202 + 32), t189);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t202 + 48), t190);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t202 + 64), t197);
                                                                GM_ST(uint64_t, ((uint8_t *)&t202 + 80), t201);
                                                                gm_f_format_t9gsufu((void *)&gm_l67, (void *)NULL, cx, NULL, &t202);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                                                if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src0gtuh94b1fce, 380, 36);
                                                                gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                                                gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                                                                gm_b0_1 t206 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t186, t205);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
gm_lb18:;
                                                        }
                                                        uint64_t t207 = gm_l28;
                                                        uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                                        uint8_t t209 = (t207 < t208);
                                                        if (t209) {
                                                            gm_b16_8 t210;
                                                            GM_ST(void *, (uint8_t *)&t210, (void *)(gm_s04qus0luoga5));
                                                            GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(4));
                                                            gm_l31 = t210;
                                                            {
                                                                {
                                                                    void * t211 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                                    uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                                    uint64_t t213 = gm_l28;
                                                                    uint8_t t214 = (t213 < t212);
                                                                    if (t214) {
                                                                        gm_t7g88i52rrtm5 t215 = GM_LD(gm_t7g88i52rrtm5, (((uint8_t *)t211) + (uintptr_t)t213 * 24));
                                                                        gm_tflpan0ud4tim t216;
                                                                        GM_ST(uint8_t, (uint8_t *)&t216 + 25, 0);
                                                                        GM_ST(uint16_t, (uint8_t *)&t216 + 26, 0);
                                                                        GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t216), t215);
                                                                        ((uint8_t *)&t216)[24] = 1;
                                                                        gm_l68 = t216;
                                                                    } else {
                                                                        gm_tflpan0ud4tim t217;
                                                                        memset(&t217, 0, sizeof t217);
                                                                        gm_l68 = t217;
                                                                    }
                                                                    memmove(((uint8_t *)&gm_l69), ((uint8_t *)&gm_l68), 28);
                                                                    uint8_t t218 = (uint8_t)((((uint8_t *)&gm_l69))[24] != 0);
                                                                    if (!t218) {
                                                                        goto gm_lb21;
                                                                    }
                                                                    memmove(((uint8_t *)&gm_l32), ((uint8_t *)&gm_l69), 24);
                                                                    void * t219 = (void *)((uint8_t *)&gm_l32);
                                                                    uint8_t t220 = gm_f_is_default_spec_cdd8jng(t219);
                                                                    uint8_t t221 = (uint8_t)!t220;
                                                                    if (t221) {
                                                                        gm_b16_8 t222 = gm_f_spec_literal_lmkpbc1((void *)NULL, cx, NULL, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l32)));
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_tnpifv20irnph t223;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t223 + 0), t222);
                                                                        gm_f_format_h6b2ofo((void *)&gm_l70, (void *)NULL, cx, NULL, t223);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                                                        if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 8, gm_src0gtuh94b1fce, 386, 62);
                                                                        gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                                                        gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                                                                        gm_l31 = t226;
                                                                    }
                                                                    goto gm_lb20;
                                                                }
gm_lb21:;
                                                            }
gm_lb20:;
                                                            void * t227 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                            uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                            uint64_t t229 = gm_l28;
                                                            if ((uintptr_t)t229 >= (uintptr_t)(t228)) gm_trap_index((uintptr_t)t229, t228, gm_src0gtuh94b1fce, 388, 22);
                                                            gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)t227) + (uintptr_t)t229 * 16));
                                                            gm_l33 = t230;
                                                            {
                                                                {
                                                                    gm_b16_8 t231 = gm_l31;
                                                                    gm_b16_8 t232;
                                                                    GM_ST(void *, (uint8_t *)&t232, (void *)(gm_s04qus0luoga5));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(4));
                                                                    uint8_t t233 = (gm_eq0iqiubfm7e(((uint8_t *)&t231), ((uint8_t *)&t232)));
                                                                    if (!t233) {
                                                                        goto gm_lb23;
                                                                    }
                                                                    void * t234 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                                                    uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                                                    uint64_t t236 = gm_l28;
                                                                    gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t234) + (uintptr_t)t236 * 4));
                                                                    gm_t88rcucg1b7uk t238 = gm_l7;
                                                                    uint8_t t239 = (gm_eq9m6mvtmgde(((uint8_t *)&t237), ((uint8_t *)&t238)));
                                                                    if (!t239) {
                                                                        goto gm_lb23;
                                                                    }
                                                                    {
                                                                        {
                                                                            uint8_t t240 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                                                                            if (t240) {
                                                                                goto gm_lb25;
                                                                            }
                                                                            gm_l71 = ((uint8_t)0);
                                                                            goto gm_lb24;
                                                                        }
gm_lb25:;
                                                                        {
                                                                            gm_l71 = ((uint8_t)1);
                                                                            goto gm_lb24;
                                                                        }
gm_lb26:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 389, 60);
                                                                    }
gm_lb24:;
                                                                    uint8_t t241 = gm_l71;
                                                                    if (!t241) {
                                                                        goto gm_lb23;
                                                                    }
                                                                    void * t242 = gm_l0;
                                                                    uint64_t t243 = gm_f_ptr_bytes_eocneng(t242);
                                                                    gm_l34 = t243;
                                                                    void * t244 = gm_l0;
                                                                    gm_b16_8 t245 = gm_l33;
                                                                    uint64_t t246 = gm_l34;
                                                                    gm_t3877pk7lv748 t247;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t245);
                                                                    GM_ST(uint64_t, ((uint8_t *)&t247 + 16), t246);
                                                                    gm_b0_1 t248 = gm_f_linef_fch1rvr((void *)NULL, cx, NULL, t244, &t247);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    void * t249 = gm_l0;
                                                                    void * t250 = gm_l0;
                                                                    uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)t250) + 144));
                                                                    uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                                                                    if (o253) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 393, 21);
                                                                    uint64_t t254 = r252;
                                                                    GM_ST(uint64_t, (((uint8_t *)t249) + 144), t254);
                                                                    void * t255 = gm_l0;
                                                                    uint8_t t256 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                                                                    if (!t256) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 394, 70);
                                                                    gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                                                                    gm_b16_8 t258 = gm_l17;
                                                                    gm_b16_8 t259 = gm_l18;
                                                                    void * t260 = gm_l0;
                                                                    void * t261 = GM_LD(void *, ((uint8_t *)t260));
                                                                    void * t262 = (void *)(((uint8_t *)t261) + 8);
                                                                    gm_t88rcucg1b7uk t263 = gm_l7;
                                                                    gm_b16_8 t264 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t262, t263);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_b16_8 t265 = gm_l33;
                                                                    gm_tud7oovfvnf43 t266;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t266 + 0), t257);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t266 + 16), t258);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t266 + 32), t259);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t266 + 48), t264);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t266 + 64), t265);
                                                                    gm_b0_1 t267 = gm_f_linef_um0rj1a((void *)NULL, cx, NULL, t255, &t266);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    void * t268 = gm_l0;
                                                                    gm_b0_1 t269 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t268);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    void * t270 = gm_l0;
                                                                    void * t271 = gm_l0;
                                                                    uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)t271) + 144));
                                                                    uint64_t r273; bool o274 = __builtin_sub_overflow(t272, ((uint64_t)1ULL), &r273);
                                                                    if (o274) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 396, 21);
                                                                    uint64_t t275 = r273;
                                                                    GM_ST(uint64_t, (((uint8_t *)t270) + 144), t275);
                                                                    void * t276 = gm_l0;
                                                                    gm_b16_8 t277;
                                                                    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_s2cuflhqkkuaq));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(1));
                                                                    gm_b0_1 t278 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t276, t277);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    goto gm_lb22;
                                                                }
gm_lb23:;
                                                                {
                                                                    {
                                                                        gm_b16_8 t279 = gm_l31;
                                                                        gm_b16_8 t280;
                                                                        GM_ST(void *, (uint8_t *)&t280, (void *)(gm_s04qus0luoga5));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(4));
                                                                        uint8_t t281 = (gm_eq0iqiubfm7e(((uint8_t *)&t279), ((uint8_t *)&t280)));
                                                                        if (!t281) {
                                                                            goto gm_lb28;
                                                                        }
                                                                        void * t282 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                                                        uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                                                        uint64_t t284 = gm_l28;
                                                                        gm_t88rcucg1b7uk t285 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t282) + (uintptr_t)t284 * 4));
                                                                        gm_t88rcucg1b7uk t286 = gm_l7;
                                                                        uint8_t t287 = (gm_eq9m6mvtmgde(((uint8_t *)&t285), ((uint8_t *)&t286)));
                                                                        if (!t287) {
                                                                            goto gm_lb28;
                                                                        }
                                                                        void * t288 = gm_l0;
                                                                        uint64_t t289 = gm_f_ptr_bytes_eocneng(t288);
                                                                        gm_l35 = t289;
                                                                        void * t290 = gm_l0;
                                                                        gm_b16_8 t291 = gm_l33;
                                                                        uint64_t t292 = gm_l35;
                                                                        gm_b16_8 t293 = gm_l24;
                                                                        gm_b16_8 t294 = gm_l17;
                                                                        gm_b16_8 t295 = gm_l17;
                                                                        gm_b16_8 t296 = gm_l18;
                                                                        gm_b16_8 t297 = gm_l33;
                                                                        gm_b16_8 t298 = gm_l33;
                                                                        uint64_t t299 = gm_l35;
                                                                        gm_tjg6nvivto1u9 t300;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 0), t291);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t300 + 16), t292);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 24), t293);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 40), t294);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 56), t295);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 72), t296);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 88), t297);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 104), t298);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t300 + 120), t299);
                                                                        gm_f_format_2hfpta7((void *)&gm_l72, (void *)NULL, cx, NULL, &t300);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t301 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                                                        if (!t301) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_src0gtuh94b1fce, 401, 36);
                                                                        gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                                                                        gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                                                                        gm_b0_1 t304 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t290, t303);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb27;
                                                                    }
gm_lb28:;
                                                                    void * t305 = gm_l0;
                                                                    gm_b16_8 t306 = gm_l25;
                                                                    gm_b16_8 t307 = gm_l17;
                                                                    gm_b16_8 t308 = gm_l17;
                                                                    gm_b16_8 t309 = gm_l18;
                                                                    void * t310 = gm_l0;
                                                                    void * t311 = GM_LD(void *, ((uint8_t *)t310));
                                                                    void * t312 = (void *)(((uint8_t *)t311) + 8);
                                                                    void * t313 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                                                    uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                                                    uint64_t t315 = gm_l28;
                                                                    gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t313) + (uintptr_t)t315 * 4));
                                                                    gm_b16_8 t317 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t312, t316);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_b16_8 t318 = gm_l33;
                                                                    gm_b16_8 t319 = gm_l31;
                                                                    gm_th2d9c7aju8hp t320;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 0), t306);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 16), t307);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 32), t308);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 48), t309);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 64), t317);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 80), t318);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 96), t319);
                                                                    gm_f_format_rvm8ibl((void *)&gm_l73, (void *)NULL, cx, NULL, &t320);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t321 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                                                                    if (!t321) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 8, gm_src0gtuh94b1fce, 403, 36);
                                                                    gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
                                                                    gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&t322));
                                                                    gm_b0_1 t324 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t305, t323);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
gm_lb27:;
                                                            }
gm_lb22:;
                                                        }
                                                        goto gm_lb16;
                                                    }
gm_lb17:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 373, 9);
                                                }
gm_lb16:;
                                            }
gm_lb15:;
                                            uint64_t t325 = gm_l27;
                                            uint64_t r326; bool o327 = __builtin_add_overflow(t325, ((uint64_t)1ULL), &r326);
                                            uint64_t t328 = r326;
                                            gm_l27 = t328;
                                        }
                                    }
gm_lb13:;
                                }
                                {
                                    void * t329 = gm_l0;
                                    gm_b16_8 t330 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t329);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t5o0tpv5jka2l t331;
                                    GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t331 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t331 + 32, 0);
                                    ((uint8_t *)&t331)[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t331 + 8), t330);
                                    memmove(gm_out, ((uint8_t *)&t331), 40);
                                    goto gm_fdone;
                                }
                            }
gm_lb7:;
                        }
                        {
                            gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 362, 46);
                        }
                    }
gm_lb6:;
                }
            }
gm_lb5:;
        }
gm_lb4:;
    }
    {
        {
            {
                uint8_t t332 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
                if (!t332) {
                    goto gm_lb30;
                }
                gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                gm_l36 = t333;
                void * t334 = gm_l0;
                gm_b16_8 t335 = gm_l36;
                gm_t88rcucg1b7uk t336 = gm_l7;
                gm_b16_8 t337 = gm_f_piece_stub_m8mb7rc((void *)NULL, cx, NULL, t334, t335, ((uint8_t)115ULL), t336);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t337;
                void * t338 = gm_l0;
                gm_b16_8 t339 = gm_l36;
                gm_t88rcucg1b7uk t340 = gm_l7;
                gm_b16_8 t341 = gm_f_piece_stub_m8mb7rc((void *)NULL, cx, NULL, t338, t339, ((uint8_t)102ULL), t340);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l38 = t341;
                void * t342 = gm_l0;
                void * t343 = GM_LD(void *, ((uint8_t *)t342));
                gm_tl874t2atnlh3 t344 = gm_l1;
                gm_b16_8 t345 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t343, t344);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l39 = t345;
                gm_l41 = ((uint64_t)0ULL);
                uint64_t t346 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                gm_l40 = t346;
                {
                    for (;;) {
                        uint64_t t347 = gm_l41;
                        uint64_t t348 = gm_l40;
                        uint8_t t349 = (t347 < t348);
                        if (!t349) {
                            goto gm_lb31;
                        }
                        {
                            {
                                {
                                    uint64_t t350 = gm_l41;
                                    gm_l42 = t350;
                                    void * t351 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t353 = gm_l42;
                                    uint64_t t354 = GM_LD(uint64_t, ((((uint8_t *)t351) + (uintptr_t)t353 * 16) + 8));
                                    uint8_t t355 = (t354 > ((uint64_t)0ULL));
                                    if (t355) {
                                        void * t356 = gm_l0;
                                        void * t357 = GM_LD(void *, ((uint8_t *)t356));
                                        void * t358 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t359 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t360 = gm_l42;
                                        gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)t358) + (uintptr_t)t360 * 16));
                                        gm_b16_8 t362 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t357, t361);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l43 = t362;
                                        void * t363 = gm_l0;
                                        gm_b16_8 t364 = gm_l37;
                                        void * t365 = gm_l0;
                                        gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)t365) + 200));
                                        gm_b16_8 t367 = gm_l43;
                                        void * t368 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t369 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t370 = gm_l42;
                                        uint64_t t371 = GM_LD(uint64_t, ((((uint8_t *)t368) + (uintptr_t)t370 * 16) + 8));
                                        gm_b16_8 t372 = gm_l39;
                                        gm_t89vrq05jgle5 t373;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t373 + 0), t364);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t373 + 16), t366);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t373 + 32), t367);
                                        GM_ST(uint64_t, ((uint8_t *)&t373 + 48), t371);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t373 + 56), t372);
                                        gm_f_format_tbdgr6h((void *)&gm_l74, (void *)NULL, cx, NULL, &t373);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t374 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                        if (!t374) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_src0gtuh94b1fce, 416, 32);
                                        gm_b16_8 t375 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
                                        gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&t375));
                                        gm_b0_1 t377 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t363, t376);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint64_t t378 = gm_l42;
                                    uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint8_t t380 = (t378 < t379);
                                    if (t380) {
                                        void * t381 = gm_l0;
                                        void * t382 = GM_LD(void *, ((uint8_t *)t381));
                                        void * t383 = (void *)(((uint8_t *)t382) + 8);
                                        void * t384 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                        uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                        uint64_t t386 = gm_l42;
                                        gm_t88rcucg1b7uk t387 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t384) + (uintptr_t)t386 * 4));
                                        gm_b16_8 t388 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t383, t387);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l44 = t388;
                                        gm_tflpan0ud4tim t389;
                                        memset(&t389, 0, sizeof t389);
                                        gm_l45 = t389;
                                        {
                                            {
                                                void * t390 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                uint64_t t391 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                uint64_t t392 = gm_l42;
                                                uint8_t t393 = (t392 < t391);
                                                if (t393) {
                                                    gm_t7g88i52rrtm5 t394 = GM_LD(gm_t7g88i52rrtm5, (((uint8_t *)t390) + (uintptr_t)t392 * 24));
                                                    gm_tflpan0ud4tim t395;
                                                    GM_ST(uint8_t, (uint8_t *)&t395 + 25, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t395 + 26, 0);
                                                    GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t395), t394);
                                                    ((uint8_t *)&t395)[24] = 1;
                                                    gm_l75 = t395;
                                                } else {
                                                    gm_tflpan0ud4tim t396;
                                                    memset(&t396, 0, sizeof t396);
                                                    gm_l75 = t396;
                                                }
                                                memmove(((uint8_t *)&gm_l76), ((uint8_t *)&gm_l75), 28);
                                                uint8_t t397 = (uint8_t)((((uint8_t *)&gm_l76))[24] != 0);
                                                if (!t397) {
                                                    goto gm_lb37;
                                                }
                                                memmove(((uint8_t *)&gm_l46), ((uint8_t *)&gm_l76), 24);
                                                void * t398 = (void *)((uint8_t *)&gm_l46);
                                                uint8_t t399 = gm_f_is_default_spec_cdd8jng(t398);
                                                uint8_t t400 = (uint8_t)!t399;
                                                if (t400) {
                                                    gm_t7g88i52rrtm5 t401 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)&gm_l46));
                                                    gm_tflpan0ud4tim t402;
                                                    GM_ST(uint8_t, (uint8_t *)&t402 + 25, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t402 + 26, 0);
                                                    GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t402), t401);
                                                    ((uint8_t *)&t402)[24] = 1;
                                                    gm_l45 = t402;
                                                }
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                        }
gm_lb36:;
                                        {
                                            {
                                                uint8_t t403 = (uint8_t)((((uint8_t *)&gm_l45))[24] != 0);
                                                if (!t403) {
                                                    goto gm_lb39;
                                                }
                                                memmove(((uint8_t *)&gm_l47), ((uint8_t *)&gm_l45), 24);
                                                void * t404 = gm_l0;
                                                uint32_t t405 = gm_f_fresh_5kokjmg(t404);
                                                gm_t88rcucg1b7uk t406;
                                                GM_ST(uint32_t, ((uint8_t *)&t406 + 0), t405);
                                                gm_f_format_12uv53h((void *)&gm_l77, (void *)NULL, cx, NULL, t406);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t407 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                                if (!t407) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_src0gtuh94b1fce, 425, 26);
                                                gm_b16_8 t408 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
                                                gm_l48 = t408;
                                                gm_b16_8 t409 = gm_f_spec_literal_lmkpbc1((void *)NULL, cx, NULL, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l47)));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l49 = t409;
                                                void * t410 = gm_l0;
                                                gm_b16_8 t411 = gm_l48;
                                                gm_tnpifv20irnph t412;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t412 + 0), t411);
                                                gm_b0_1 t413 = gm_f_linef_hg1enfq((void *)NULL, cx, NULL, t410, t412);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t414 = gm_l0;
                                                gm_b16_8 t415 = gm_l48;
                                                gm_b16_8 t416 = gm_l44;
                                                void * t417 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t418 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                uint64_t t419 = gm_l42;
                                                if ((uintptr_t)t419 >= (uintptr_t)(t418)) gm_trap_index((uintptr_t)t419, t418, gm_src0gtuh94b1fce, 428, 73);
                                                gm_b16_8 t420 = GM_LD(gm_b16_8, (((uint8_t *)t417) + (uintptr_t)t419 * 16));
                                                gm_b16_8 t421 = gm_l49;
                                                gm_t0uuhaljsf410 t422;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t422 + 0), t415);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t422 + 16), t416);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t422 + 32), t420);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t422 + 48), t421);
                                                gm_b0_1 t423 = gm_f_linef_qjbq226((void *)NULL, cx, NULL, t414, &t422);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t424 = gm_l0;
                                                gm_b16_8 t425 = gm_l48;
                                                gm_b16_8 t426 = GM_LD(gm_b16_8, ((uint8_t *)&t425));
                                                gm_tl874t2atnlh3 t427 = gm_l1;
                                                gm_teegpca3bk79b t428 = GM_LD(gm_teegpca3bk79b, ((uint8_t *)a2));
                                                gm_f_emit_buf_mqofii1((void *)&gm_l78, (void *)NULL, cx, NULL, t424, &t428, t426, t427);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t429 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                if (t429) {
                                                } else {
                                                    gm_t08olq97rscn3 t430 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l78) + 8));
                                                    gm_t5o0tpv5jka2l t431;
                                                    GM_ST(uint8_t, (uint8_t *)&t431 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t431 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t431 + 4, 0);
                                                    ((uint8_t *)&t431)[0] = 1;
                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t431 + 8), t430);
                                                    memmove(gm_out, ((uint8_t *)&t431), 40);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb38;
                                            }
gm_lb39:;
                                            void * t432 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                            uint64_t t433 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                            uint64_t t434 = gm_l42;
                                            gm_t88rcucg1b7uk t435 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t432) + (uintptr_t)t434 * 4));
                                            gm_t88rcucg1b7uk t436 = gm_l7;
                                            uint8_t t437 = (gm_eq9m6mvtmgde(((uint8_t *)&t435), ((uint8_t *)&t436)));
                                            if (t437) {
                                                void * t438 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t439 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                uint64_t t440 = gm_l42;
                                                if ((uintptr_t)t440 >= (uintptr_t)(t439)) gm_trap_index((uintptr_t)t440, t439, gm_src0gtuh94b1fce, 432, 26);
                                                gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)t438) + (uintptr_t)t440 * 16));
                                                gm_l50 = t441;
                                                void * t442 = gm_l0;
                                                uint64_t t443 = gm_f_ptr_bytes_eocneng(t442);
                                                gm_l51 = t443;
                                                void * t444 = gm_l0;
                                                gm_b16_8 t445 = gm_l50;
                                                uint64_t t446 = gm_l51;
                                                gm_b16_8 t447 = gm_l37;
                                                void * t448 = gm_l0;
                                                gm_b16_8 t449 = GM_LD(gm_b16_8, (((uint8_t *)t448) + 200));
                                                gm_b16_8 t450 = gm_l50;
                                                gm_b16_8 t451 = gm_l50;
                                                uint64_t t452 = gm_l51;
                                                gm_b16_8 t453 = gm_l39;
                                                gm_tpbrs0bkofuth t454;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t454 + 0), t445);
                                                GM_ST(uint64_t, ((uint8_t *)&t454 + 16), t446);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t454 + 24), t447);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t454 + 40), t449);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t454 + 56), t450);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t454 + 72), t451);
                                                GM_ST(uint64_t, ((uint8_t *)&t454 + 88), t452);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t454 + 96), t453);
                                                gm_f_format_elsuh0a((void *)&gm_l79, (void *)NULL, cx, NULL, &t454);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t455 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                                if (!t455) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 8, gm_src0gtuh94b1fce, 434, 36);
                                                gm_b16_8 t456 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l79) + 8));
                                                gm_b16_8 t457 = GM_LD(gm_b16_8, ((uint8_t *)&t456));
                                                gm_b0_1 t458 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t444, t457);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            } else {
                                                void * t459 = gm_l0;
                                                gm_b16_8 t460 = gm_l38;
                                                void * t461 = gm_l0;
                                                gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)t461) + 200));
                                                gm_b16_8 t463 = gm_l44;
                                                void * t464 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t465 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                uint64_t t466 = gm_l42;
                                                if ((uintptr_t)t466 >= (uintptr_t)(t465)) gm_trap_index((uintptr_t)t466, t465, gm_src0gtuh94b1fce, 436, 83);
                                                gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)t464) + (uintptr_t)t466 * 16));
                                                gm_b16_8 t468 = gm_l39;
                                                gm_tud7oovfvnf43 t469;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t469 + 0), t460);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t469 + 16), t462);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t469 + 32), t463);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t469 + 48), t467);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t469 + 64), t468);
                                                gm_f_format_5b908c8((void *)&gm_l80, (void *)NULL, cx, NULL, &t469);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t470 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                                if (!t470) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 8, gm_src0gtuh94b1fce, 436, 36);
                                                gm_b16_8 t471 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l80) + 8));
                                                gm_b16_8 t472 = GM_LD(gm_b16_8, ((uint8_t *)&t471));
                                                gm_b0_1 t473 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t459, t472);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                        }
gm_lb38:;
                                    }
                                    goto gm_lb34;
                                }
gm_lb35:;
                                gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 413, 9);
                            }
gm_lb34:;
                        }
gm_lb33:;
                        uint64_t t474 = gm_l41;
                        uint64_t r475; bool o476 = __builtin_add_overflow(t474, ((uint64_t)1ULL), &r475);
                        uint64_t t477 = r475;
                        gm_l41 = t477;
                    }
                }
gm_lb31:;
                void * t478 = gm_l0;
                gm_b16_8 t479 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t478);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t480;
                GM_ST(uint8_t, (uint8_t *)&t480 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t480 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t480 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t480 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t480 + 32, 0);
                ((uint8_t *)&t480)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t480 + 8), t479);
                memmove(gm_out, ((uint8_t *)&t480), 40);
                goto gm_fdone;
            }
gm_lb30:;
        }
gm_lb29:;
    }
    {
        gm_l53 = ((uint64_t)0ULL);
        uint64_t t481 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_l52 = t481;
        {
            for (;;) {
                uint64_t t482 = gm_l53;
                uint64_t t483 = gm_l52;
                uint8_t t484 = (t482 < t483);
                if (!t484) {
                    goto gm_lb40;
                }
                {
                    {
                        {
                            uint64_t t485 = gm_l53;
                            gm_l54 = t485;
                            void * t486 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t487 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t488 = gm_l54;
                            uint64_t t489 = GM_LD(uint64_t, ((((uint8_t *)t486) + (uintptr_t)t488 * 16) + 8));
                            uint8_t t490 = (t489 > ((uint64_t)0ULL));
                            if (t490) {
                                void * t491 = gm_l0;
                                void * t492 = GM_LD(void *, ((uint8_t *)t491));
                                void * t493 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t494 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t495 = gm_l54;
                                gm_b16_8 t496 = GM_LD(gm_b16_8, (((uint8_t *)t493) + (uintptr_t)t495 * 16));
                                gm_b16_8 t497 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t492, t496);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l55 = t497;
                                void * t498 = gm_l0;
                                gm_t88rcucg1b7uk t499 = gm_l7;
                                gm_b16_8 t500 = gm_l55;
                                void * t501 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t502 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t503 = gm_l54;
                                uint64_t t504 = GM_LD(uint64_t, ((((uint8_t *)t501) + (uintptr_t)t503 * 16) + 8));
                                gm_t4ce0jd3tdro4 t505;
                                GM_ST(uint64_t, ((uint8_t *)&t505 + 0), t504);
                                gm_f_format_sa1940l((void *)&gm_l81, (void *)NULL, cx, NULL, t505);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t506 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                if (!t506) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_src0gtuh94b1fce, 445, 44);
                                gm_b16_8 t507 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                                gm_b16_8 t508 = GM_LD(gm_b16_8, ((uint8_t *)&t507));
                                gm_b16_8 t509 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t498, t499, t500, t508);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l56 = t509;
                                void * t510 = gm_l0;
                                gm_b16_8 t511 = gm_l56;
                                gm_tl874t2atnlh3 t512 = gm_l1;
                                gm_teegpca3bk79b t513 = GM_LD(gm_teegpca3bk79b, ((uint8_t *)a2));
                                gm_f_emit_to_o7js99p((void *)&gm_l82, (void *)NULL, cx, NULL, t510, &t513, t511, t512);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t514 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                                if (t514) {
                                } else {
                                    gm_t08olq97rscn3 t515 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l82) + 8));
                                    gm_t5o0tpv5jka2l t516;
                                    GM_ST(uint8_t, (uint8_t *)&t516 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t516 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t516 + 4, 0);
                                    ((uint8_t *)&t516)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t516 + 8), t515);
                                    memmove(gm_out, ((uint8_t *)&t516), 40);
                                    goto gm_fdone;
                                }
                            }
                            uint64_t t517 = gm_l54;
                            uint64_t t518 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint8_t t519 = (t517 < t518);
                            if (t519) {
                                void * t520 = gm_l0;
                                void * t521 = GM_LD(void *, ((uint8_t *)t520));
                                void * t522 = (void *)(((uint8_t *)t521) + 8);
                                void * t523 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                uint64_t t524 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint64_t t525 = gm_l54;
                                gm_t88rcucg1b7uk t526 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t523) + (uintptr_t)t525 * 4));
                                gm_b16_8 t527 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t522, t526);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l57 = t527;
                                void * t528 = gm_l0;
                                uint32_t t529 = gm_f_fresh_5kokjmg(t528);
                                gm_t88rcucg1b7uk t530;
                                GM_ST(uint32_t, ((uint8_t *)&t530 + 0), t529);
                                gm_f_format_12uv53h((void *)&gm_l83, (void *)NULL, cx, NULL, t530);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t531 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                                if (!t531) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 8, gm_src0gtuh94b1fce, 450, 18);
                                gm_b16_8 t532 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
                                gm_l58 = t532;
                                void * t533 = gm_l0;
                                gm_b16_8 t534 = gm_l58;
                                gm_tnpifv20irnph t535;
                                GM_ST(gm_b16_8, ((uint8_t *)&t535 + 0), t534);
                                gm_b0_1 t536 = gm_f_linef_hg1enfq((void *)NULL, cx, NULL, t533, t535);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tflpan0ud4tim t537;
                                memset(&t537, 0, sizeof t537);
                                gm_l59 = t537;
                                {
                                    {
                                        void * t538 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                        uint64_t t539 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                        uint64_t t540 = gm_l54;
                                        uint8_t t541 = (t540 < t539);
                                        if (t541) {
                                            gm_t7g88i52rrtm5 t542 = GM_LD(gm_t7g88i52rrtm5, (((uint8_t *)t538) + (uintptr_t)t540 * 24));
                                            gm_tflpan0ud4tim t543;
                                            GM_ST(uint8_t, (uint8_t *)&t543 + 25, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t543 + 26, 0);
                                            GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t543), t542);
                                            ((uint8_t *)&t543)[24] = 1;
                                            gm_l84 = t543;
                                        } else {
                                            gm_tflpan0ud4tim t544;
                                            memset(&t544, 0, sizeof t544);
                                            gm_l84 = t544;
                                        }
                                        memmove(((uint8_t *)&gm_l85), ((uint8_t *)&gm_l84), 28);
                                        uint8_t t545 = (uint8_t)((((uint8_t *)&gm_l85))[24] != 0);
                                        if (!t545) {
                                            goto gm_lb46;
                                        }
                                        memmove(((uint8_t *)&gm_l60), ((uint8_t *)&gm_l85), 24);
                                        void * t546 = (void *)((uint8_t *)&gm_l60);
                                        uint8_t t547 = gm_f_is_default_spec_cdd8jng(t546);
                                        uint8_t t548 = (uint8_t)!t547;
                                        if (t548) {
                                            gm_t7g88i52rrtm5 t549 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)&gm_l60));
                                            gm_tflpan0ud4tim t550;
                                            GM_ST(uint8_t, (uint8_t *)&t550 + 25, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t550 + 26, 0);
                                            GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t550), t549);
                                            ((uint8_t *)&t550)[24] = 1;
                                            gm_l59 = t550;
                                        }
                                        goto gm_lb45;
                                    }
gm_lb46:;
                                }
gm_lb45:;
                                {
                                    {
                                        uint8_t t551 = (uint8_t)((((uint8_t *)&gm_l59))[24] != 0);
                                        if (!t551) {
                                            goto gm_lb48;
                                        }
                                        memmove(((uint8_t *)&gm_l61), ((uint8_t *)&gm_l59), 24);
                                        gm_b16_8 t552 = gm_f_spec_literal_lmkpbc1((void *)NULL, cx, NULL, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l61)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l62 = t552;
                                        void * t553 = gm_l0;
                                        gm_b16_8 t554 = gm_l58;
                                        gm_b16_8 t555 = gm_l57;
                                        void * t556 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        uint64_t t557 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t558 = gm_l54;
                                        if ((uintptr_t)t558 >= (uintptr_t)(t557)) gm_trap_index((uintptr_t)t558, t557, gm_src0gtuh94b1fce, 458, 69);
                                        gm_b16_8 t559 = GM_LD(gm_b16_8, (((uint8_t *)t556) + (uintptr_t)t558 * 16));
                                        gm_b16_8 t560 = gm_l62;
                                        gm_t0uuhaljsf410 t561;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t561 + 0), t554);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t561 + 16), t555);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t561 + 32), t559);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t561 + 48), t560);
                                        gm_b0_1 t562 = gm_f_linef_qjbq226((void *)NULL, cx, NULL, t553, &t561);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb47;
                                    }
gm_lb48:;
                                    void * t563 = gm_l0;
                                    gm_b16_8 t564 = gm_l58;
                                    gm_b16_8 t565 = gm_l57;
                                    void * t566 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t567 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t568 = gm_l54;
                                    if ((uintptr_t)t568 >= (uintptr_t)(t567)) gm_trap_index((uintptr_t)t568, t567, gm_src0gtuh94b1fce, 460, 65);
                                    gm_b16_8 t569 = GM_LD(gm_b16_8, (((uint8_t *)t566) + (uintptr_t)t568 * 16));
                                    gm_tfeab279c16rr t570;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t570 + 0), t564);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t570 + 16), t565);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t570 + 32), t569);
                                    gm_b0_1 t571 = gm_f_linef_3dvovo0((void *)NULL, cx, NULL, t563, &t570);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
gm_lb47:;
                                void * t572 = gm_l0;
                                gm_b16_8 t573 = gm_l58;
                                gm_b16_8 t574 = GM_LD(gm_b16_8, ((uint8_t *)&t573));
                                gm_tl874t2atnlh3 t575 = gm_l1;
                                gm_teegpca3bk79b t576 = GM_LD(gm_teegpca3bk79b, ((uint8_t *)a2));
                                gm_f_emit_buf_mqofii1((void *)&gm_l86, (void *)NULL, cx, NULL, t572, &t576, t574, t575);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t577 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                                if (t577) {
                                } else {
                                    gm_t08olq97rscn3 t578 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l86) + 8));
                                    gm_t5o0tpv5jka2l t579;
                                    GM_ST(uint8_t, (uint8_t *)&t579 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t579 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t579 + 4, 0);
                                    ((uint8_t *)&t579)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t579 + 8), t578);
                                    memmove(gm_out, ((uint8_t *)&t579), 40);
                                    goto gm_fdone;
                                }
                            }
                            goto gm_lb43;
                        }
gm_lb44:;
                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 442, 5);
                    }
gm_lb43:;
                }
gm_lb42:;
                uint64_t t580 = gm_l53;
                uint64_t r581; bool o582 = __builtin_add_overflow(t580, ((uint64_t)1ULL), &r581);
                uint64_t t583 = r581;
                gm_l53 = t583;
            }
        }
gm_lb40:;
    }
    {
        void * t584 = gm_l0;
        gm_b16_8 t585 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t584);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t586;
        GM_ST(uint8_t, (uint8_t *)&t586 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t586 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t586 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t586 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t586 + 32, 0);
        ((uint8_t *)&t586)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t586 + 8), t585);
        memmove(gm_out, ((uint8_t *)&t586), 40);
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
