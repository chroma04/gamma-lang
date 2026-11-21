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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; unsigned __int128 f1; } gm_tl5jbgvjjt15g;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; } gm_tpfol4ocdvoij;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tfbnciphf3261 v1; gm_tl5jbgvjjt15g v2; gm_t88rcucg1b7uk v3; gm_t88rcucg1b7uk v4; } f1; } gm_t7b7esi3n9r90;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl5jbgvjjt15g) == 32 && _Alignof(gm_tl5jbgvjjt15g) == 16 && offsetof(gm_tl5jbgvjjt15g, f0) == 0 && offsetof(gm_tl5jbgvjjt15g, f1) == 16, "layout of `(ty: Ty, bits: u128)`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpfol4ocdvoij) == 32 && _Alignof(gm_tpfol4ocdvoij) == 8 && offsetof(gm_tpfol4ocdvoij, f0) == 0 && offsetof(gm_tpfol4ocdvoij, f1) == 16 && offsetof(gm_tpfol4ocdvoij, f2) == 28, "layout of `(name: string, span: Span, ty: Ty)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; uint64_t v2; gm_tpfol4ocdvoij v3; uint32_t v4; } f1; } gm_tu78tssgij60g;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_t7b7esi3n9r90 f0; uint64_t f1; uint8_t pe[8]; } gm_t592joge1j4uv;
_Static_assert(sizeof(gm_t7b7esi3n9r90) == 48 && _Alignof(gm_t7b7esi3n9r90) == 16 && offsetof(gm_t7b7esi3n9r90, f0) == 0 && offsetof(gm_t7b7esi3n9r90, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t419mo45g7j0a v0; gm_t7b7esi3n9r90 v1; } f1; } gm_t9i04n6j2d1gi;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; } gm_tf4lk4crue8vt;
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t pe[15]; } gm_tf8kfhcnval78;
typedef struct { gm_tu78tssgij60g f0; gm_b16_8 f1; } gm_tlg23kb3oj23s;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_t7b7esi3n9r90 v1; gm_t419mo45g7j0a v2; } f1; } gm_tllthq21h2f0k;
typedef struct { gm_b16_8 f0; gm_t7b7esi3n9r90 f1; } gm_tmvmieab1acso;
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; } gm_tthvfhsvm2g0s;
_Static_assert(sizeof(gm_tu78tssgij60g) == 40 && _Alignof(gm_tu78tssgij60g) == 8 && offsetof(gm_tu78tssgij60g, f0) == 0 && offsetof(gm_tu78tssgij60g, f1) == 8, "layout of `Base`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t592joge1j4uv) == 64 && _Alignof(gm_t592joge1j4uv) == 16 && offsetof(gm_t592joge1j4uv, f0) == 0 && offsetof(gm_t592joge1j4uv, f1) == 48, "layout of `(index: Value, size: u64)`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t6j9ipvj0d6k5;
_Static_assert(sizeof(gm_t9i04n6j2d1gi) == 64 && _Alignof(gm_t9i04n6j2d1gi) == 16 && offsetof(gm_t9i04n6j2d1gi, f0) == 0 && offsetof(gm_t9i04n6j2d1gi, f1) == 16, "layout of `Sink`");
_Static_assert(sizeof(gm_tf4lk4crue8vt) == 64 && _Alignof(gm_tf4lk4crue8vt) == 16 && offsetof(gm_tf4lk4crue8vt, f0) == 0 && offsetof(gm_tf4lk4crue8vt, f1) == 16, "layout of `(dst: u32, v: Value)`");
_Static_assert(sizeof(gm_tf8kfhcnval78) == 64 && _Alignof(gm_tf8kfhcnval78) == 16 && offsetof(gm_tf8kfhcnval78, f0) == 0 && offsetof(gm_tf8kfhcnval78, f1) == 48, "layout of `?Value`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tb31k5l794t7p v0; gm_t592joge1j4uv v1; gm_t592joge1j4uv v2; gm_tlcm4lvu3g1a8 v3; } f1; } gm_tfqmtuprvunod;
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; gm_tf8kfhcnval78 f2; uint8_t f3; uint8_t pe[15]; } gm_thvv7nb91atip;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tf4lk4crue8vt v1; gm_tmvmieab1acso v2; } f1; } gm_tkgci6o4gktbf;
_Static_assert(sizeof(gm_tlg23kb3oj23s) == 56 && _Alignof(gm_tlg23kb3oj23s) == 8 && offsetof(gm_tlg23kb3oj23s, f0) == 0 && offsetof(gm_tlg23kb3oj23s, f1) == 40, "layout of `Place`");
_Static_assert(sizeof(gm_tllthq21h2f0k) == 64 && _Alignof(gm_tllthq21h2f0k) == 16 && offsetof(gm_tllthq21h2f0k, f0) == 0 && offsetof(gm_tllthq21h2f0k, f1) == 16, "layout of `Callee`");
_Static_assert(sizeof(gm_tmvmieab1acso) == 64 && _Alignof(gm_tmvmieab1acso) == 16 && offsetof(gm_tmvmieab1acso, f0) == 0 && offsetof(gm_tmvmieab1acso, f1) == 16, "layout of `(decls: []u32, v: Value)`");
_Static_assert(sizeof(gm_tthvfhsvm2g0s) == 96 && _Alignof(gm_tthvfhsvm2g0s) == 16 && offsetof(gm_tthvfhsvm2g0s, f0) == 0 && offsetof(gm_tthvfhsvm2g0s, f1) == 48, "layout of `(index: Value, len: Value)`");
_Static_assert(sizeof(gm_t6j9ipvj0d6k5) == 112 && _Alignof(gm_t6j9ipvj0d6k5) == 16 && offsetof(gm_t6j9ipvj0d6k5, f0) == 0 && offsetof(gm_t6j9ipvj0d6k5, f1) == 48 && offsetof(gm_t6j9ipvj0d6k5, f2) == 104, "layout of `(present: Value, place: Place, result: bool)`");
_Static_assert(sizeof(gm_tfqmtuprvunod) == 80 && _Alignof(gm_tfqmtuprvunod) == 16 && offsetof(gm_tfqmtuprvunod, f0) == 0 && offsetof(gm_tfqmtuprvunod, f1) == 16, "layout of `Proj`");
_Static_assert(sizeof(gm_thvv7nb91atip) == 176 && _Alignof(gm_thvv7nb91atip) == 16 && offsetof(gm_thvv7nb91atip, f0) == 0 && offsetof(gm_thvv7nb91atip, f1) == 48 && offsetof(gm_thvv7nb91atip, f2) == 96 && offsetof(gm_thvv7nb91atip, f3) == 160, "layout of `(lo: Value, hi: Value, len: ?Value, inclusive: bool)`");
_Static_assert(sizeof(gm_tkgci6o4gktbf) == 80 && _Alignof(gm_tkgci6o4gktbf) == 16 && offsetof(gm_tkgci6o4gktbf, f0) == 0 && offsetof(gm_tkgci6o4gktbf, f1) == 16, "layout of `Handler`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tthvfhsvm2g0s v0; gm_thvv7nb91atip v1; gm_t6j9ipvj0d6k5 v2; } f1; } gm_tkj0hg83g38nr;
_Static_assert(sizeof(gm_tkj0hg83g38nr) == 192 && _Alignof(gm_tkj0hg83g38nr) == 16 && offsetof(gm_tkj0hg83g38nr, f0) == 0 && offsetof(gm_tkj0hg83g38nr, f1) == 16, "layout of `Check`");
struct gm_fr_ancestor_o28bfm6 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_dst_of_c0rv8d0 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_field_type_ph14klv {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_for_each_place_ufnhgjt {
    gm_frame h;
    gm_tlg23kb3oj23s l2; /* p */
    gm_tlg23kb3oj23s l3; /* p */
    gm_tlg23kb3oj23s l4; /* p */
    gm_tlg23kb3oj23s l5; /* p */
    gm_tlg23kb3oj23s l6; /* p */
    gm_tkj0hg83g38nr l7; /* c */
    gm_tlg23kb3oj23s l8; /* p */
    gm_b0_1 ret;
};
struct gm_fr_for_each_value_j9v56m8 {
    gm_frame h;
    gm_closure l1; /* f */
    gm_t7b7esi3n9r90 l3; /* v */
    gm_t7b7esi3n9r90 l4; /* v */
    gm_t7b7esi3n9r90 l5; /* v */
    gm_t7b7esi3n9r90 l6; /* v */
    gm_t7b7esi3n9r90 l7; /* v */
    gm_t7b7esi3n9r90 l8; /* v */
    gm_t7b7esi3n9r90 l9; /* v */
    gm_tlg23kb3oj23s l10; /* p */
    gm_tlg23kb3oj23s l11; /* p */
    gm_tlg23kb3oj23s l12; /* p */
    gm_tlg23kb3oj23s l13; /* p */
    gm_tlg23kb3oj23s l14; /* p */
    gm_t7b7esi3n9r90 l15; /* v */
    gm_t7b7esi3n9r90 l16; /* a */
    gm_t7b7esi3n9r90 l17; /* b */
    gm_t7b7esi3n9r90 l18; /* a */
    gm_t7b7esi3n9r90 l19; /* b */
    gm_t7b7esi3n9r90 l20; /* a */
    gm_t7b7esi3n9r90 l21; /* b */
    gm_t7b7esi3n9r90 l22; /* a */
    gm_t7b7esi3n9r90 l23; /* b */
    gm_t7b7esi3n9r90 l24; /* a */
    gm_t7b7esi3n9r90 l25; /* b */
    gm_t7b7esi3n9r90 l30; /* v */
    gm_tllthq21h2f0k l31; /* c */
    gm_t7b7esi3n9r90 l33; /* v */
    gm_t7b7esi3n9r90 l36; /* v */
    gm_tf8kfhcnval78 l37; /* v */
    gm_tf8kfhcnval78 l38; /* v */
    gm_tf8kfhcnval78 l39; /* v */
    gm_tf8kfhcnval78 l40; /* v */
    gm_t7b7esi3n9r90 l41; /* x */
    gm_tkj0hg83g38nr l42; /* c */
    gm_t7b7esi3n9r90 l43; /* a */
    gm_t7b7esi3n9r90 l44; /* b */
    gm_t7b7esi3n9r90 l45; /* lo */
    gm_t7b7esi3n9r90 l46; /* hi */
    gm_tf8kfhcnval78 l47; /* len */
    gm_t7b7esi3n9r90 l48; /* x */
    gm_t7b7esi3n9r90 l49; /* v */
    gm_tlg23kb3oj23s l50; /* p */
    gm_t7b7esi3n9r90 l51; /* a */
    gm_t7b7esi3n9r90 l52; /* b */
    gm_t7b7esi3n9r90 l53; /* c */
    gm_t9i04n6j2d1gi l54; /* sink */
    gm_tf8kfhcnval78 l56; /* table */
    gm_t7b7esi3n9r90 l57; /* v */
    gm_t7b7esi3n9r90 l60; /* v */
    gm_t7b7esi3n9r90 l61; /* x */
    gm_t7b7esi3n9r90 l62; /* c */
    gm_t7b7esi3n9r90 l63; /* c */
    gm_tf8kfhcnval78 l64; /* v */
    gm_t7b7esi3n9r90 l65; /* x */
    gm_tf8kfhcnval78 l66; /* on */
    gm_t7b7esi3n9r90 l67; /* x */
    gm_tkgci6o4gktbf l68; /* h */
    gm_t7b7esi3n9r90 l69; /* v */
    gm_t7b7esi3n9r90 l70; /* v */
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_place_ao9jtnb {
    gm_frame h;
    gm_tfqmtuprvunod l3; /* x */
    gm_t7b7esi3n9r90 l4; /* i */
    gm_t7b7esi3n9r90 l5; /* i */
    gm_b0_1 ret;
};
struct gm_fr_place_type_5jm3hnm {
    gm_frame h;
    gm_tq7qjpkmqmc5s l12; /* g */
    gm_tfqmtuprvunod l18; /* x */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_pointee_5ui0dk7 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_value_type_p69quvj {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_t88rcucg1b7uk ret;
};
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_ancestor_o28bfm6(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_dst_of_c0rv8d0(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_field_type_ph14klv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_for_each_place_ufnhgjt(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_value_j9v56m8(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_ao9jtnb(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_place_type_5jm3hnm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_pointee_5ui0dk7(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_value_type_p69quvj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_value_type_p69quvj_c(void * a0, gm_t7b7esi3n9r90 a1);
extern const char gm_src0ih77f43mhf2[];
extern const char gm_srch081ittgol24[];
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_ancestor_o28bfm6(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ancestor_o28bfm6 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* levels */
    uint32_t gm_l3; /* at */
    uint32_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t gm_l6;
    uint32_t t7 = gm_l1;
    gm_l3 = t7;
    gm_l5 = ((uint32_t)0ULL);
    uint32_t t8 = gm_l2;
    gm_l4 = t8;
    {
        for (;;) {
            uint32_t t9 = gm_l5;
            uint32_t t10 = gm_l4;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint32_t t12 = gm_l3;
                        uint64_t t13 = (uint64_t)t12;
                        void * t14 = gm_l0;
                        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t14) + 384) + 8));
                        uint8_t t16 = (t13 >= t15);
                        if (t16) {
                            gm_t9805qtonbsag t17;
                            memset(&t17, 0, sizeof t17);
                            F.ret = t17;
                            goto gm_fret;
                        }
                        void * t18 = gm_l0;
                        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 384));
                        uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t18) + 384) + 8));
                        uint32_t t21 = gm_l3;
                        uint64_t t22 = (uint64_t)t21;
                        if ((uintptr_t)t22 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t22, t20, gm_srch081ittgol24, 441, 14);
                        uint8_t t23 = (uint8_t)((((((uint8_t *)t19) + (uintptr_t)t22 * 64) + 48))[4] != 0);
                        if (t23) {
                            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t19) + (uintptr_t)t22 * 64) + 48));
                            gm_l6 = t24;
                        } else {
                            gm_t9805qtonbsag t25;
                            memset(&t25, 0, sizeof t25);
                            F.ret = t25;
                            goto gm_fret;
                        }
                        uint32_t t26 = gm_l6;
                        gm_l3 = t26;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 439, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint32_t t27 = gm_l5;
            uint32_t r28; bool o29 = __builtin_add_overflow(t27, ((uint32_t)1ULL), &r28);
            uint32_t t30 = r28;
            gm_l5 = t30;
        }
    }
gm_lb1:;
    uint32_t t31 = gm_l3;
    gm_t9805qtonbsag t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t32), t31);
    ((uint8_t *)&t32)[4] = 1;
    F.ret = t32;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_dst_of_c0rv8d0(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dst_of_c0rv8d0 F;
    void * gm_l0; /* op */
    uint32_t gm_l1; /* d */
    uint32_t gm_l2; /* d */
    uint32_t gm_l3; /* d */
    uint32_t gm_l4; /* d */
    uint32_t gm_l5; /* d */
    uint32_t gm_l6; /* d */
    uint32_t gm_l7; /* d */
    uint32_t gm_l8; /* d */
    uint32_t gm_l9; /* d */
    uint32_t gm_l10; /* d */
    uint32_t gm_l11; /* d */
    uint32_t gm_l12; /* d */
    uint32_t gm_l13; /* d */
    uint32_t gm_l14; /* d */
    uint32_t gm_l15; /* d */
    uint32_t gm_l16; /* d */
    uint32_t gm_l17; /* d */
    uint32_t gm_l18; /* d */
    uint32_t gm_l19; /* d */
    uint32_t gm_l20; /* d */
    uint32_t gm_l21; /* d */
    uint32_t gm_l22; /* d */
    gm_t9805qtonbsag gm_l23; /* d */
    gm_t9805qtonbsag gm_l24; /* d */
    gm_t9805qtonbsag gm_l25; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9805qtonbsag gm_l26;
    void * t41 = gm_l0;
    {
        {
            {
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)0ULL));
                    if (!t43) {
                        goto gm_lb4;
                    }
                    uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l1 = t44;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)1ULL));
                    if (!t46) {
                        goto gm_lb5;
                    }
                    uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l1 = t47;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)2ULL));
                    if (!t49) {
                        goto gm_lb6;
                    }
                    uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l1 = t50;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)3ULL));
                    if (!t52) {
                        goto gm_lb7;
                    }
                    uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l1 = t53;
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)4ULL));
                    if (!t55) {
                        goto gm_lb8;
                    }
                    uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l1 = t56;
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)5ULL));
                    if (!t58) {
                        goto gm_lb9;
                    }
                    uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l1 = t59;
                    goto gm_lb3;
                }
gm_lb9:;
                goto gm_lb2;
            }
gm_lb3:;
            uint32_t t60 = gm_l1;
            gm_t9805qtonbsag t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t61), t60);
            ((uint8_t *)&t61)[4] = 1;
            gm_l26 = t61;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)6ULL));
                    if (!t63) {
                        goto gm_lb12;
                    }
                    uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l7 = t64;
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)9ULL));
                    if (!t66) {
                        goto gm_lb13;
                    }
                    uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l7 = t67;
                    goto gm_lb11;
                }
gm_lb13:;
                {
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)14ULL));
                    if (!t69) {
                        goto gm_lb14;
                    }
                    uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l7 = t70;
                    goto gm_lb11;
                }
gm_lb14:;
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)15ULL));
                    if (!t72) {
                        goto gm_lb15;
                    }
                    uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l7 = t73;
                    goto gm_lb11;
                }
gm_lb15:;
                goto gm_lb10;
            }
gm_lb11:;
            uint32_t t74 = gm_l7;
            gm_t9805qtonbsag t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t75), t74);
            ((uint8_t *)&t75)[4] = 1;
            gm_l26 = t75;
            goto gm_lb1;
        }
gm_lb10:;
        {
            {
                {
                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)10ULL));
                    if (!t77) {
                        goto gm_lb18;
                    }
                    uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l11 = t78;
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)11ULL));
                    if (!t80) {
                        goto gm_lb19;
                    }
                    uint32_t t81 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l11 = t81;
                    goto gm_lb17;
                }
gm_lb19:;
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)12ULL));
                    if (!t83) {
                        goto gm_lb20;
                    }
                    uint32_t t84 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l11 = t84;
                    goto gm_lb17;
                }
gm_lb20:;
                goto gm_lb16;
            }
gm_lb17:;
            uint32_t t85 = gm_l11;
            gm_t9805qtonbsag t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t86), t85);
            ((uint8_t *)&t86)[4] = 1;
            gm_l26 = t86;
            goto gm_lb1;
        }
gm_lb16:;
        {
            {
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)16ULL));
                    if (!t88) {
                        goto gm_lb23;
                    }
                    uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l14 = t89;
                    goto gm_lb22;
                }
gm_lb23:;
                {
                    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)17ULL));
                    if (!t91) {
                        goto gm_lb24;
                    }
                    uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l14 = t92;
                    goto gm_lb22;
                }
gm_lb24:;
                {
                    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t94 = (uint8_t)(t93 == ((uint8_t)18ULL));
                    if (!t94) {
                        goto gm_lb25;
                    }
                    uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l14 = t95;
                    goto gm_lb22;
                }
gm_lb25:;
                {
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)23ULL));
                    if (!t97) {
                        goto gm_lb26;
                    }
                    uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l14 = t98;
                    goto gm_lb22;
                }
gm_lb26:;
                {
                    uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t100 = (uint8_t)(t99 == ((uint8_t)24ULL));
                    if (!t100) {
                        goto gm_lb27;
                    }
                    uint32_t t101 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l14 = t101;
                    goto gm_lb22;
                }
gm_lb27:;
                goto gm_lb21;
            }
gm_lb22:;
            uint32_t t102 = gm_l14;
            gm_t9805qtonbsag t103;
            GM_ST(uint8_t, (uint8_t *)&t103 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t103 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t103), t102);
            ((uint8_t *)&t103)[4] = 1;
            gm_l26 = t103;
            goto gm_lb1;
        }
gm_lb21:;
        {
            {
                {
                    uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t105 = (uint8_t)(t104 == ((uint8_t)19ULL));
                    if (!t105) {
                        goto gm_lb30;
                    }
                    uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l19 = t106;
                    goto gm_lb29;
                }
gm_lb30:;
                {
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t108 = (uint8_t)(t107 == ((uint8_t)20ULL));
                    if (!t108) {
                        goto gm_lb31;
                    }
                    uint32_t t109 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l19 = t109;
                    goto gm_lb29;
                }
gm_lb31:;
                {
                    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)21ULL));
                    if (!t111) {
                        goto gm_lb32;
                    }
                    uint32_t t112 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l19 = t112;
                    goto gm_lb29;
                }
gm_lb32:;
                {
                    uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t114 = (uint8_t)(t113 == ((uint8_t)22ULL));
                    if (!t114) {
                        goto gm_lb33;
                    }
                    uint32_t t115 = GM_LD(uint32_t, (((uint8_t *)t41) + 16));
                    gm_l19 = t115;
                    goto gm_lb29;
                }
gm_lb33:;
                goto gm_lb28;
            }
gm_lb29:;
            uint32_t t116 = gm_l19;
            gm_t9805qtonbsag t117;
            GM_ST(uint8_t, (uint8_t *)&t117 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t117 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t117), t116);
            ((uint8_t *)&t117)[4] = 1;
            gm_l26 = t117;
            goto gm_lb1;
        }
gm_lb28:;
        {
            {
                {
                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)7ULL));
                    if (!t119) {
                        goto gm_lb36;
                    }
                    gm_t9805qtonbsag t120 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t41) + 16));
                    gm_l23 = t120;
                    goto gm_lb35;
                }
gm_lb36:;
                {
                    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)8ULL));
                    if (!t122) {
                        goto gm_lb37;
                    }
                    gm_t9805qtonbsag t123 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t41) + 16));
                    gm_l23 = t123;
                    goto gm_lb35;
                }
gm_lb37:;
                {
                    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
                    uint8_t t125 = (uint8_t)(t124 == ((uint8_t)44ULL));
                    if (!t125) {
                        goto gm_lb38;
                    }
                    gm_t9805qtonbsag t126 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t41) + 16));
                    gm_l23 = t126;
                    goto gm_lb35;
                }
gm_lb38:;
                goto gm_lb34;
            }
gm_lb35:;
            gm_t9805qtonbsag t127 = gm_l23;
            gm_l26 = t127;
            goto gm_lb1;
        }
gm_lb34:;
        {
            gm_t9805qtonbsag t128;
            memset(&t128, 0, sizeof t128);
            gm_l26 = t128;
            goto gm_lb1;
        }
gm_lb39:;
        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 305, 44);
    }
gm_lb1:;
    gm_t9805qtonbsag t129 = gm_l26;
    F.ret = t129;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_field_type_ph14klv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2) {
    struct gm_fr_field_type_ph14klv F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* elems */
    uint8_t gm_l4; /* is_const */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    gm_b16_8 gm_l6; /* fields */
    GM_STACK_CHECK(&F, 664u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l7;
    gm_tkn42u7na3t81 gm_l8;
    gm_tkn42u7na3t81 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_b16_8 gm_l11;
    gm_th2lqbo77pvsc gm_l12;
    gm_b16_8 gm_l13;
    gm_tkn42u7na3t81 gm_l14;
    void * t6 = gm_l0;
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l7, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)12ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_l3 = t11;
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t15 = (t13 < t14);
            if (t15) {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint32_t t18 = gm_l2;
                uint64_t t19 = (uint64_t)t18;
                if ((uintptr_t)t19 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t19, t17, gm_srch081ittgol24, 456, 62);
                gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + (uintptr_t)t19 * 4));
                gm_tkn42u7na3t81 t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t21), t20);
                ((uint8_t *)&t21)[4] = 1;
                gm_l8 = t21;
            } else {
                gm_tkn42u7na3t81 t22;
                memset(&t22, 0, sizeof t22);
                gm_l8 = t22;
            }
            gm_tkn42u7na3t81 t23 = gm_l8;
            F.ret = t23;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)10ULL));
            if (!t25) {
                goto gm_lb3;
            }
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
            gm_l4 = t26;
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l7) + 8) + 4));
            gm_l5 = t27;
            uint32_t t28 = gm_l2;
            uint8_t t29 = ((t28 == ((uint32_t)0ULL)));
            if (t29) {
                void * t30 = gm_l0;
                void * t31 = (void *)(((uint8_t *)t30) + 8);
                uint8_t t32 = gm_l4;
                gm_t88rcucg1b7uk t33 = gm_l5;
                gm_tbngattl1fo51 t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), t32);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34 + 4), t33);
                gm_t53at80pfdus3 t35;
                memset(&t35, 0, sizeof t35);
                GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)9ULL));
                GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t35 + 8), t34);
                gm_t88rcucg1b7uk t36 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t31, &t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37), t36);
                ((uint8_t *)&t37)[4] = 1;
                gm_l9 = t37;
            } else {
                void * t38 = gm_l0;
                void * t39 = (void *)(((uint8_t *)t38) + 8);
                gm_t88rcucg1b7uk t40 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41), t40);
                ((uint8_t *)&t41)[4] = 1;
                gm_l9 = t41;
            }
            gm_tkn42u7na3t81 t42 = gm_l9;
            F.ret = t42;
            goto gm_fret;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 455, 5);
    }
gm_lb1:;
    void * t43 = gm_l0;
    gm_t88rcucg1b7uk t44 = gm_l1;
    gm_f_struct_fields_chqu3it((void *)&gm_l10, t43, GM_CONV(gm_csguankko4lmhd, t44));
    uint8_t t45 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
    if (t45) {
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
        gm_l11 = t46;
    } else {
        void * t47 = gm_l0;
        gm_t88rcucg1b7uk t48 = gm_l1;
        gm_f_c_union_fields_btdvc8b((void *)&gm_l12, t47, GM_CONV(gm_csguankko4lmhd, t48));
        uint8_t t49 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
        if (t49) {
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
            gm_l13 = t50;
        } else {
            gm_tkn42u7na3t81 t51;
            memset(&t51, 0, sizeof t51);
            F.ret = t51;
            goto gm_fret;
        }
        gm_b16_8 t52 = gm_l13;
        gm_l11 = t52;
    }
    gm_b16_8 t53 = gm_l11;
    gm_l6 = t53;
    uint32_t t54 = gm_l2;
    uint64_t t55 = (uint64_t)t54;
    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
    uint8_t t57 = (t55 < t56);
    if (t57) {
        void * t58 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        uint32_t t60 = gm_l2;
        uint64_t t61 = (uint64_t)t60;
        if ((uintptr_t)t61 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t61, t59, gm_srch081ittgol24, 461, 39);
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t58) + (uintptr_t)t61 * 40) + 16));
        gm_tkn42u7na3t81 t63;
        GM_ST(uint8_t, (uint8_t *)&t63 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t63 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t63), t62);
        ((uint8_t *)&t63)[4] = 1;
        gm_l14 = t63;
    } else {
        gm_tkn42u7na3t81 t64;
        memset(&t64, 0, sizeof t64);
        gm_l14 = t64;
    }
    gm_tkn42u7na3t81 t65 = gm_l14;
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
inline __attribute__((aligned(4))) void gm_f_for_each_place_ufnhgjt(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_place_ufnhgjt F;
    void * gm_l0; /* op */
    gm_closure gm_l1; /* f */
    GM_STACK_CHECK(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t14 = gm_l0;
    {
        {
            {
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
                    if (!t16) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l2), ((((uint8_t *)t14) + 16) + 8), 56);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t14) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
                    if (!t18) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&F.l2), ((((uint8_t *)t14) + 16) + 8), 56);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t14) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)23ULL));
                    if (!t20) {
                        goto gm_lb6;
                    }
                    memmove(((uint8_t *)&F.l2), ((((uint8_t *)t14) + 16) + 8), 56);
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t14) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)24ULL));
                    if (!t22) {
                        goto gm_lb7;
                    }
                    memmove(((uint8_t *)&F.l2), ((((uint8_t *)t14) + 16) + 8), 56);
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t14) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)25ULL));
                    if (!t24) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&F.l2), (((uint8_t *)t14) + 16), 56);
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_closure t25 = gm_l1;
            void * t26 = (void *)((uint8_t *)&F.l2);
            gm_closure t27 = t25;
            gm_node *ccx28 = t27.cx;
            void * t29 = t27.code;
            gm_b0_1 t30;
            if (GM_PTR_TAGGED(t29)) {
                ((void (*)(void *))GM_PTR_UNTAG(t29))(t26);
            } else {
                t30 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t29)(t27.env, ccx28, NULL, t26);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t14) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)26ULL));
            if (!t32) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&F.l7), (((uint8_t *)t14) + 16), 192);
            {
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)2ULL));
                    if (!t34) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&F.l8), ((((uint8_t *)&F.l7) + 16) + 48), 56);
                    gm_closure t35 = gm_l1;
                    void * t36 = (void *)((uint8_t *)&F.l8);
                    gm_closure t37 = t35;
                    gm_node *ccx38 = t37.cx;
                    void * t39 = t37.code;
                    gm_b0_1 t40;
                    if (GM_PTR_TAGGED(t39)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t39))(t36);
                    } else {
                        t40 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t39)(t37.env, ccx38, NULL, t36);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            goto gm_lb1;
        }
gm_lb9:;
        {
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 383, 5);
    }
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_for_each_value_j9v56m8(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_value_j9v56m8 F;
    void * gm_l0; /* op */
    gm_closure gm_l2; /* place */
    gm_b16_8 gm_l26; /* args */
    gm_b16_8 gm_l27; /* args */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_b16_8 gm_l32; /* args */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_b16_8 gm_l55; /* args */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    GM_STACK_CHECK(&F, 2104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        gm_closure t87 = ((gm_closure){(void *)gm_f_place_ao9jtnb, (void *)&F.h, cx});
        gm_l2 = t87;
        void * t88 = gm_l0;
        {
            {
                {
                    {
                        {
                            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)0ULL));
                            if (!t90) {
                                goto gm_lb4;
                            }
                            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t88) + 16) + 16), 48);
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)4ULL));
                            if (!t92) {
                                goto gm_lb5;
                            }
                            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t88) + 16) + 16), 48);
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t94 = (uint8_t)(t93 == ((uint8_t)6ULL));
                            if (!t94) {
                                goto gm_lb6;
                            }
                            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t88) + 16) + 16), 48);
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)11ULL));
                            if (!t96) {
                                goto gm_lb7;
                            }
                            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t88) + 16) + 16), 48);
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t98 = (uint8_t)(t97 == ((uint8_t)34ULL));
                            if (!t98) {
                                goto gm_lb8;
                            }
                            memmove(((uint8_t *)&F.l3), (((uint8_t *)t88) + 16), 48);
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)21ULL));
                            if (!t100) {
                                goto gm_lb9;
                            }
                            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t88) + 16) + 16), 48);
                            goto gm_lb3;
                        }
gm_lb9:;
                        {
                            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)41ULL));
                            if (!t102) {
                                goto gm_lb10;
                            }
                            memmove(((uint8_t *)&F.l3), (((uint8_t *)t88) + 16), 48);
                            goto gm_lb3;
                        }
gm_lb10:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    gm_closure t103 = F.l1;
                    gm_closure t104 = t103;
                    gm_node *ccx105 = t104.cx;
                    void * t106 = t104.code;
                    gm_b0_1 t107;
                    if (GM_PTR_TAGGED(t106)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t106))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l3)));
                    } else {
                        t107 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t106)(t104.env, ccx105, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l3)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    {
                        {
                            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)1ULL));
                            if (!t109) {
                                goto gm_lb13;
                            }
                            memmove(((uint8_t *)&F.l10), ((((uint8_t *)t88) + 16) + 8), 56);
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)2ULL));
                            if (!t111) {
                                goto gm_lb14;
                            }
                            memmove(((uint8_t *)&F.l10), ((((uint8_t *)t88) + 16) + 8), 56);
                            goto gm_lb12;
                        }
gm_lb14:;
                        {
                            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t113 = (uint8_t)(t112 == ((uint8_t)23ULL));
                            if (!t113) {
                                goto gm_lb15;
                            }
                            memmove(((uint8_t *)&F.l10), ((((uint8_t *)t88) + 16) + 8), 56);
                            goto gm_lb12;
                        }
gm_lb15:;
                        {
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)24ULL));
                            if (!t115) {
                                goto gm_lb16;
                            }
                            memmove(((uint8_t *)&F.l10), ((((uint8_t *)t88) + 16) + 8), 56);
                            goto gm_lb12;
                        }
gm_lb16:;
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_closure t116 = gm_l2;
                    void * t117 = (void *)((uint8_t *)&F.l10);
                    gm_closure t118 = t116;
                    gm_node *ccx119 = t118.cx;
                    void * t120 = t118.code;
                    gm_b0_1 t121;
                    if (GM_PTR_TAGGED(t120)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t120))(t117);
                    } else {
                        t121 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t120)(t118.env, ccx119, NULL, t117);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t123 = (uint8_t)(t122 == ((uint8_t)25ULL));
                    if (!t123) {
                        goto gm_lb17;
                    }
                    memmove(((uint8_t *)&F.l14), (((uint8_t *)t88) + 16), 56);
                    memmove(((uint8_t *)&F.l15), ((((uint8_t *)t88) + 16) + 64), 48);
                    gm_closure t124 = gm_l2;
                    void * t125 = (void *)((uint8_t *)&F.l14);
                    gm_closure t126 = t124;
                    gm_node *ccx127 = t126.cx;
                    void * t128 = t126.code;
                    gm_b0_1 t129;
                    if (GM_PTR_TAGGED(t128)) {
                        ((void (*)(void *))GM_PTR_UNTAG(t128))(t125);
                    } else {
                        t129 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t128)(t126.env, ccx127, NULL, t125);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t130 = F.l1;
                    gm_closure t131 = t130;
                    gm_node *ccx132 = t131.cx;
                    void * t133 = t131.code;
                    gm_b0_1 t134;
                    if (GM_PTR_TAGGED(t133)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t133))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l15)));
                    } else {
                        t134 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t133)(t131.env, ccx132, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l15)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb17:;
                {
                    {
                        {
                            uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t136 = (uint8_t)(t135 == ((uint8_t)3ULL));
                            if (!t136) {
                                goto gm_lb20;
                            }
                            memmove(((uint8_t *)&F.l16), ((((uint8_t *)t88) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l17), ((((uint8_t *)t88) + 16) + 64), 48);
                            goto gm_lb19;
                        }
gm_lb20:;
                        {
                            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t138 = (uint8_t)(t137 == ((uint8_t)5ULL));
                            if (!t138) {
                                goto gm_lb21;
                            }
                            memmove(((uint8_t *)&F.l16), ((((uint8_t *)t88) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l17), ((((uint8_t *)t88) + 16) + 64), 48);
                            goto gm_lb19;
                        }
gm_lb21:;
                        {
                            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t140 = (uint8_t)(t139 == ((uint8_t)20ULL));
                            if (!t140) {
                                goto gm_lb22;
                            }
                            memmove(((uint8_t *)&F.l16), ((((uint8_t *)t88) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l17), ((((uint8_t *)t88) + 16) + 64), 48);
                            goto gm_lb19;
                        }
gm_lb22:;
                        {
                            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t142 = (uint8_t)(t141 == ((uint8_t)22ULL));
                            if (!t142) {
                                goto gm_lb23;
                            }
                            memmove(((uint8_t *)&F.l16), ((((uint8_t *)t88) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l17), ((((uint8_t *)t88) + 16) + 80), 48);
                            goto gm_lb19;
                        }
gm_lb23:;
                        {
                            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t144 = (uint8_t)(t143 == ((uint8_t)40ULL));
                            if (!t144) {
                                goto gm_lb24;
                            }
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)t88) + 16), 48);
                            memmove(((uint8_t *)&F.l17), ((((uint8_t *)t88) + 16) + 48), 48);
                            goto gm_lb19;
                        }
gm_lb24:;
                        goto gm_lb18;
                    }
gm_lb19:;
                    gm_closure t145 = F.l1;
                    gm_closure t146 = t145;
                    gm_node *ccx147 = t146.cx;
                    void * t148 = t146.code;
                    gm_b0_1 t149;
                    if (GM_PTR_TAGGED(t148)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t148))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l16)));
                    } else {
                        t149 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t148)(t146.env, ccx147, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l16)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t150 = F.l1;
                    gm_closure t151 = t150;
                    gm_node *ccx152 = t151.cx;
                    void * t153 = t151.code;
                    gm_b0_1 t154;
                    if (GM_PTR_TAGGED(t153)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t153))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l17)));
                    } else {
                        t154 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t153)(t151.env, ccx152, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l17)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb18:;
            }
            {
                {
                    {
                        {
                            uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t156 = (uint8_t)(t155 == ((uint8_t)7ULL));
                            if (!t156) {
                                goto gm_lb27;
                            }
                            gm_b16_8 t157 = GM_LD(gm_b16_8, ((((uint8_t *)t88) + 16) + 24));
                            gm_l26 = t157;
                            goto gm_lb26;
                        }
gm_lb27:;
                        {
                            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t159 = (uint8_t)(t158 == ((uint8_t)9ULL));
                            if (!t159) {
                                goto gm_lb28;
                            }
                            gm_b16_8 t160 = GM_LD(gm_b16_8, ((((uint8_t *)t88) + 16) + 8));
                            gm_l26 = t160;
                            goto gm_lb26;
                        }
gm_lb28:;
                        goto gm_lb25;
                    }
gm_lb26:;
                    gm_b16_8 t161 = gm_l26;
                    gm_l28 = t161;
                    gm_l29 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t163 = gm_l29;
                            uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                            uint8_t t165 = (t163 < t164);
                            if (!t165) {
                                goto gm_lb29;
                            }
                            {
                                void * t166 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                uint64_t t168 = gm_l29;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l30), (((uint8_t *)t166) + (uintptr_t)t168 * 48), 48);
                                        gm_closure t169 = F.l1;
                                        gm_closure t170 = t169;
                                        gm_node *ccx171 = t170.cx;
                                        void * t172 = t170.code;
                                        gm_b0_1 t173;
                                        if (GM_PTR_TAGGED(t172)) {
                                            ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t172))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l30)));
                                        } else {
                                            t173 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t172)(t170.env, ccx171, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l30)));
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_ll162;
                                        }
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 338, 54);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t174 = gm_l29;
                            uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                            uint64_t t177 = r175;
                            gm_l29 = t177;
                        }
                    }
                    goto gm_ls178;
gm_ll162:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb29; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb31; }
                    }
                    goto gm_lad;
gm_ls178:;
gm_lb29:;
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t180 = (uint8_t)(t179 == ((uint8_t)8ULL));
                    if (!t180) {
                        goto gm_lb34;
                    }
                    memmove(((uint8_t *)&F.l31), ((((uint8_t *)t88) + 16) + 16), 64);
                    gm_b16_8 t181 = GM_LD(gm_b16_8, ((((uint8_t *)t88) + 16) + 80));
                    gm_l32 = t181;
                    {
                        {
                            uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&F.l31) + 0));
                            uint8_t t183 = (uint8_t)(t182 == ((uint8_t)1ULL));
                            if (!t183) {
                                goto gm_lb36;
                            }
                            memmove(((uint8_t *)&F.l33), (((uint8_t *)&F.l31) + 16), 48);
                            gm_closure t184 = F.l1;
                            gm_closure t185 = t184;
                            gm_node *ccx186 = t185.cx;
                            void * t187 = t185.code;
                            gm_b0_1 t188;
                            if (GM_PTR_TAGGED(t187)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t187))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l33)));
                            } else {
                                t188 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t187)(t185.env, ccx186, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l33)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb35;
                        }
gm_lb36:;
                    }
gm_lb35:;
                    gm_b16_8 t189 = gm_l32;
                    gm_l34 = t189;
                    gm_l35 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t191 = gm_l35;
                            uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                            uint8_t t193 = (t191 < t192);
                            if (!t193) {
                                goto gm_lb37;
                            }
                            {
                                void * t194 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                uint64_t t196 = gm_l35;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l36), (((uint8_t *)t194) + (uintptr_t)t196 * 48), 48);
                                        gm_closure t197 = F.l1;
                                        gm_closure t198 = t197;
                                        gm_node *ccx199 = t198.cx;
                                        void * t200 = t198.code;
                                        gm_b0_1 t201;
                                        if (GM_PTR_TAGGED(t200)) {
                                            ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t200))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l36)));
                                        } else {
                                            t201 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t200)(t198.env, ccx199, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l36)));
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        goto gm_lb40;
                                    }
gm_lb41:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 341, 13);
                                }
gm_lb40:;
                            }
gm_lb39:;
                            uint64_t t202 = gm_l35;
                            uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                            uint64_t t205 = r203;
                            gm_l35 = t205;
                        }
                    }
                    goto gm_ls206;
gm_ll190:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb37; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb39; }
                    }
                    goto gm_lad;
gm_ls206:;
gm_lb37:;
                    goto gm_lb1;
                }
gm_lb34:;
            }
            {
                {
                    {
                        {
                            uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t208 = (uint8_t)(t207 == ((uint8_t)14ULL));
                            if (!t208) {
                                goto gm_lb44;
                            }
                            memmove(((uint8_t *)&F.l37), ((((uint8_t *)t88) + 16) + 16), 64);
                            goto gm_lb43;
                        }
gm_lb44:;
                        {
                            uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t210 = (uint8_t)(t209 == ((uint8_t)15ULL));
                            if (!t210) {
                                goto gm_lb45;
                            }
                            memmove(((uint8_t *)&F.l37), ((((uint8_t *)t88) + 16) + 16), 64);
                            goto gm_lb43;
                        }
gm_lb45:;
                        {
                            uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t212 = (uint8_t)(t211 == ((uint8_t)35ULL));
                            if (!t212) {
                                goto gm_lb46;
                            }
                            memmove(((uint8_t *)&F.l37), (((uint8_t *)t88) + 16), 64);
                            goto gm_lb43;
                        }
gm_lb46:;
                        {
                            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t214 = (uint8_t)(t213 == ((uint8_t)39ULL));
                            if (!t214) {
                                goto gm_lb47;
                            }
                            memmove(((uint8_t *)&F.l37), ((((uint8_t *)t88) + 16) + 16), 64);
                            goto gm_lb43;
                        }
gm_lb47:;
                        goto gm_lb42;
                    }
gm_lb43:;
                    {
                        {
                            uint8_t t215 = (uint8_t)((((uint8_t *)&F.l37))[48] != 0);
                            if (!t215) {
                                goto gm_lb49;
                            }
                            memmove(((uint8_t *)&F.l41), ((uint8_t *)&F.l37), 48);
                            gm_closure t216 = F.l1;
                            gm_closure t217 = t216;
                            gm_node *ccx218 = t217.cx;
                            void * t219 = t217.code;
                            gm_b0_1 t220;
                            if (GM_PTR_TAGGED(t219)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t219))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l41)));
                            } else {
                                t220 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t219)(t217.env, ccx218, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l41)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb48;
                        }
gm_lb49:;
                    }
gm_lb48:;
                    goto gm_lb1;
                }
gm_lb42:;
                {
                    uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t222 = (uint8_t)(t221 == ((uint8_t)26ULL));
                    if (!t222) {
                        goto gm_lb50;
                    }
                    memmove(((uint8_t *)&F.l42), (((uint8_t *)t88) + 16), 192);
                    {
                        {
                            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&F.l42) + 0));
                            uint8_t t224 = (uint8_t)(t223 == ((uint8_t)0ULL));
                            if (!t224) {
                                goto gm_lb52;
                            }
                            memmove(((uint8_t *)&F.l43), (((uint8_t *)&F.l42) + 16), 48);
                            memmove(((uint8_t *)&F.l44), ((((uint8_t *)&F.l42) + 16) + 48), 48);
                            gm_closure t225 = F.l1;
                            gm_closure t226 = t225;
                            gm_node *ccx227 = t226.cx;
                            void * t228 = t226.code;
                            gm_b0_1 t229;
                            if (GM_PTR_TAGGED(t228)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t228))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l43)));
                            } else {
                                t229 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t228)(t226.env, ccx227, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l43)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_closure t230 = F.l1;
                            gm_closure t231 = t230;
                            gm_node *ccx232 = t231.cx;
                            void * t233 = t231.code;
                            gm_b0_1 t234;
                            if (GM_PTR_TAGGED(t233)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t233))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l44)));
                            } else {
                                t234 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t233)(t231.env, ccx232, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l44)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb51;
                        }
gm_lb52:;
                        {
                            uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&F.l42) + 0));
                            uint8_t t236 = (uint8_t)(t235 == ((uint8_t)1ULL));
                            if (!t236) {
                                goto gm_lb53;
                            }
                            memmove(((uint8_t *)&F.l45), (((uint8_t *)&F.l42) + 16), 48);
                            memmove(((uint8_t *)&F.l46), ((((uint8_t *)&F.l42) + 16) + 48), 48);
                            memmove(((uint8_t *)&F.l47), ((((uint8_t *)&F.l42) + 16) + 96), 64);
                            gm_closure t237 = F.l1;
                            gm_closure t238 = t237;
                            gm_node *ccx239 = t238.cx;
                            void * t240 = t238.code;
                            gm_b0_1 t241;
                            if (GM_PTR_TAGGED(t240)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t240))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l45)));
                            } else {
                                t241 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t240)(t238.env, ccx239, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l45)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_closure t242 = F.l1;
                            gm_closure t243 = t242;
                            gm_node *ccx244 = t243.cx;
                            void * t245 = t243.code;
                            gm_b0_1 t246;
                            if (GM_PTR_TAGGED(t245)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t245))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l46)));
                            } else {
                                t246 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t245)(t243.env, ccx244, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l46)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t247 = (uint8_t)((((uint8_t *)&F.l47))[48] != 0);
                                    if (!t247) {
                                        goto gm_lb55;
                                    }
                                    memmove(((uint8_t *)&F.l48), ((uint8_t *)&F.l47), 48);
                                    gm_closure t248 = F.l1;
                                    gm_closure t249 = t248;
                                    gm_node *ccx250 = t249.cx;
                                    void * t251 = t249.code;
                                    gm_b0_1 t252;
                                    if (GM_PTR_TAGGED(t251)) {
                                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t251))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l48)));
                                    } else {
                                        t252 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t251)(t249.env, ccx250, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l48)));
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb54;
                                }
gm_lb55:;
                            }
gm_lb54:;
                            goto gm_lb51;
                        }
gm_lb53:;
                        {
                            uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&F.l42) + 0));
                            uint8_t t254 = (uint8_t)(t253 == ((uint8_t)2ULL));
                            if (!t254) {
                                goto gm_lb56;
                            }
                            memmove(((uint8_t *)&F.l49), (((uint8_t *)&F.l42) + 16), 48);
                            memmove(((uint8_t *)&F.l50), ((((uint8_t *)&F.l42) + 16) + 48), 56);
                            gm_closure t255 = F.l1;
                            gm_closure t256 = t255;
                            gm_node *ccx257 = t256.cx;
                            void * t258 = t256.code;
                            gm_b0_1 t259;
                            if (GM_PTR_TAGGED(t258)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t258))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l49)));
                            } else {
                                t259 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t258)(t256.env, ccx257, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l49)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_closure t260 = gm_l2;
                            void * t261 = (void *)((uint8_t *)&F.l50);
                            gm_closure t262 = t260;
                            gm_node *ccx263 = t262.cx;
                            void * t264 = t262.code;
                            gm_b0_1 t265;
                            if (GM_PTR_TAGGED(t264)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t264))(t261);
                            } else {
                                t265 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t264)(t262.env, ccx263, NULL, t261);
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb51;
                        }
gm_lb56:;
                        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 344, 22);
                    }
gm_lb51:;
                    goto gm_lb1;
                }
gm_lb50:;
            }
            {
                {
                    uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t267 = (uint8_t)(t266 == ((uint8_t)12ULL));
                    if (!t267) {
                        goto gm_lb57;
                    }
                    memmove(((uint8_t *)&F.l51), ((((uint8_t *)t88) + 16) + 16), 48);
                    memmove(((uint8_t *)&F.l52), ((((uint8_t *)t88) + 16) + 64), 48);
                    memmove(((uint8_t *)&F.l53), ((((uint8_t *)t88) + 16) + 112), 48);
                    gm_closure t268 = F.l1;
                    gm_closure t269 = t268;
                    gm_node *ccx270 = t269.cx;
                    void * t271 = t269.code;
                    gm_b0_1 t272;
                    if (GM_PTR_TAGGED(t271)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t271))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l51)));
                    } else {
                        t272 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t271)(t269.env, ccx270, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l51)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t273 = F.l1;
                    gm_closure t274 = t273;
                    gm_node *ccx275 = t274.cx;
                    void * t276 = t274.code;
                    gm_b0_1 t277;
                    if (GM_PTR_TAGGED(t276)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t276))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l52)));
                    } else {
                        t277 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t276)(t274.env, ccx275, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l52)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_closure t278 = F.l1;
                    gm_closure t279 = t278;
                    gm_node *ccx280 = t279.cx;
                    void * t281 = t279.code;
                    gm_b0_1 t282;
                    if (GM_PTR_TAGGED(t281)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t281))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l53)));
                    } else {
                        t282 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t281)(t279.env, ccx280, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l53)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb57:;
                {
                    uint8_t t283 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t284 = (uint8_t)(t283 == ((uint8_t)13ULL));
                    if (!t284) {
                        goto gm_lb58;
                    }
                    memmove(((uint8_t *)&F.l54), (((uint8_t *)t88) + 16), 64);
                    gm_b16_8 t285 = GM_LD(gm_b16_8, ((((uint8_t *)t88) + 16) + 96));
                    gm_l55 = t285;
                    memmove(((uint8_t *)&F.l56), ((((uint8_t *)t88) + 16) + 112), 64);
                    {
                        {
                            uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&F.l54) + 0));
                            uint8_t t287 = (uint8_t)(t286 == ((uint8_t)1ULL));
                            if (!t287) {
                                goto gm_lb60;
                            }
                            memmove(((uint8_t *)&F.l57), (((uint8_t *)&F.l54) + 16), 48);
                            gm_closure t288 = F.l1;
                            gm_closure t289 = t288;
                            gm_node *ccx290 = t289.cx;
                            void * t291 = t289.code;
                            gm_b0_1 t292;
                            if (GM_PTR_TAGGED(t291)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t291))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l57)));
                            } else {
                                t292 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t291)(t289.env, ccx290, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l57)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb59;
                        }
gm_lb60:;
                    }
gm_lb59:;
                    gm_b16_8 t293 = gm_l55;
                    gm_l58 = t293;
                    gm_l59 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t295 = gm_l59;
                            uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                            uint8_t t297 = (t295 < t296);
                            if (!t297) {
                                goto gm_lb61;
                            }
                            {
                                void * t298 = GM_LD(void *, ((uint8_t *)&gm_l58));
                                uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                                uint64_t t300 = gm_l59;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l60), (((uint8_t *)t298) + (uintptr_t)t300 * 48), 48);
                                        gm_closure t301 = F.l1;
                                        gm_closure t302 = t301;
                                        gm_node *ccx303 = t302.cx;
                                        void * t304 = t302.code;
                                        gm_b0_1 t305;
                                        if (GM_PTR_TAGGED(t304)) {
                                            ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t304))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l60)));
                                        } else {
                                            t305 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t304)(t302.env, ccx303, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l60)));
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_ll294;
                                        }
                                        goto gm_lb64;
                                    }
gm_lb65:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 366, 13);
                                }
gm_lb64:;
                            }
gm_lb63:;
                            uint64_t t306 = gm_l59;
                            uint64_t r307; bool o308 = __builtin_add_overflow(t306, ((uint64_t)1ULL), &r307);
                            uint64_t t309 = r307;
                            gm_l59 = t309;
                        }
                    }
                    goto gm_ls310;
gm_ll294:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb61; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb63; }
                    }
                    goto gm_lad;
gm_ls310:;
gm_lb61:;
                    {
                        {
                            uint8_t t311 = (uint8_t)((((uint8_t *)&F.l56))[48] != 0);
                            if (!t311) {
                                goto gm_lb67;
                            }
                            memmove(((uint8_t *)&F.l61), ((uint8_t *)&F.l56), 48);
                            gm_closure t312 = F.l1;
                            gm_closure t313 = t312;
                            gm_node *ccx314 = t313.cx;
                            void * t315 = t313.code;
                            gm_b0_1 t316;
                            if (GM_PTR_TAGGED(t315)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t315))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l61)));
                            } else {
                                t316 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t315)(t313.env, ccx314, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l61)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb66;
                        }
gm_lb67:;
                    }
gm_lb66:;
                    goto gm_lb1;
                }
gm_lb58:;
            }
            {
                {
                    {
                        {
                            uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t318 = (uint8_t)(t317 == ((uint8_t)29ULL));
                            if (!t318) {
                                goto gm_lb70;
                            }
                            memmove(((uint8_t *)&F.l62), (((uint8_t *)t88) + 16), 48);
                            goto gm_lb69;
                        }
gm_lb70:;
                        {
                            uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                            uint8_t t320 = (uint8_t)(t319 == ((uint8_t)31ULL));
                            if (!t320) {
                                goto gm_lb71;
                            }
                            memmove(((uint8_t *)&F.l62), (((uint8_t *)t88) + 16), 48);
                            goto gm_lb69;
                        }
gm_lb71:;
                        goto gm_lb68;
                    }
gm_lb69:;
                    gm_closure t321 = F.l1;
                    gm_closure t322 = t321;
                    gm_node *ccx323 = t322.cx;
                    void * t324 = t322.code;
                    gm_b0_1 t325;
                    if (GM_PTR_TAGGED(t324)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t324))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l62)));
                    } else {
                        t325 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t324)(t322.env, ccx323, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l62)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb68:;
                {
                    uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t327 = (uint8_t)(t326 == ((uint8_t)32ULL));
                    if (!t327) {
                        goto gm_lb72;
                    }
                    memmove(((uint8_t *)&F.l64), (((uint8_t *)t88) + 16), 64);
                    {
                        {
                            uint8_t t328 = (uint8_t)((((uint8_t *)&F.l64))[48] != 0);
                            if (!t328) {
                                goto gm_lb74;
                            }
                            memmove(((uint8_t *)&F.l65), ((uint8_t *)&F.l64), 48);
                            gm_closure t329 = F.l1;
                            gm_closure t330 = t329;
                            gm_node *ccx331 = t330.cx;
                            void * t332 = t330.code;
                            gm_b0_1 t333;
                            if (GM_PTR_TAGGED(t332)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t332))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l65)));
                            } else {
                                t333 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t332)(t330.env, ccx331, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l65)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb73;
                        }
gm_lb74:;
                    }
gm_lb73:;
                    goto gm_lb1;
                }
gm_lb72:;
                {
                    uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t335 = (uint8_t)(t334 == ((uint8_t)37ULL));
                    if (!t335) {
                        goto gm_lb75;
                    }
                    memmove(((uint8_t *)&F.l66), ((((uint8_t *)t88) + 16) + 32), 64);
                    {
                        {
                            uint8_t t336 = (uint8_t)((((uint8_t *)&F.l66))[48] != 0);
                            if (!t336) {
                                goto gm_lb77;
                            }
                            memmove(((uint8_t *)&F.l67), ((uint8_t *)&F.l66), 48);
                            gm_closure t337 = F.l1;
                            gm_closure t338 = t337;
                            gm_node *ccx339 = t338.cx;
                            void * t340 = t338.code;
                            gm_b0_1 t341;
                            if (GM_PTR_TAGGED(t340)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t340))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l67)));
                            } else {
                                t341 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t340)(t338.env, ccx339, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l67)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb76;
                        }
gm_lb77:;
                    }
gm_lb76:;
                    goto gm_lb1;
                }
gm_lb75:;
                {
                    uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)t88) + 0));
                    uint8_t t343 = (uint8_t)(t342 == ((uint8_t)38ULL));
                    if (!t343) {
                        goto gm_lb78;
                    }
                    memmove(((uint8_t *)&F.l68), (((uint8_t *)t88) + 16), 80);
                    {
                        {
                            {
                                {
                                    uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&F.l68) + 0));
                                    uint8_t t345 = (uint8_t)(t344 == ((uint8_t)1ULL));
                                    if (!t345) {
                                        goto gm_lb82;
                                    }
                                    memmove(((uint8_t *)&F.l69), ((((uint8_t *)&F.l68) + 16) + 16), 48);
                                    goto gm_lb81;
                                }
gm_lb82:;
                                {
                                    uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&F.l68) + 0));
                                    uint8_t t347 = (uint8_t)(t346 == ((uint8_t)2ULL));
                                    if (!t347) {
                                        goto gm_lb83;
                                    }
                                    memmove(((uint8_t *)&F.l69), ((((uint8_t *)&F.l68) + 16) + 16), 48);
                                    goto gm_lb81;
                                }
gm_lb83:;
                                goto gm_lb80;
                            }
gm_lb81:;
                            gm_closure t348 = F.l1;
                            gm_closure t349 = t348;
                            gm_node *ccx350 = t349.cx;
                            void * t351 = t349.code;
                            gm_b0_1 t352;
                            if (GM_PTR_TAGGED(t351)) {
                                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t351))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l69)));
                            } else {
                                t352 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t351)(t349.env, ccx350, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l69)));
                            }
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb79;
                        }
gm_lb80:;
                        {
                            uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&F.l68) + 0));
                            uint8_t t354 = (uint8_t)(t353 == ((uint8_t)0ULL));
                            if (!t354) {
                                goto gm_lb84;
                            }
                            goto gm_lb79;
                        }
gm_lb84:;
                        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 372, 24);
                    }
gm_lb79:;
                    goto gm_lb1;
                }
gm_lb78:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb85:;
                gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 327, 5);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_place_ao9jtnb(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_place_ao9jtnb F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t13 = gm_l0;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 40));
    gm_l1 = t14;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l2;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t20 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t18) + (uintptr_t)t20 * 80), 80);
                        {
                            {
                                {
                                    {
                                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
                                        if (!t22) {
                                            goto gm_lb9;
                                        }
                                        memmove(((uint8_t *)&F.l4), (((uint8_t *)&F.l3) + 16), 48);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
                                        if (!t24) {
                                            goto gm_lb10;
                                        }
                                        memmove(((uint8_t *)&F.l4), (((uint8_t *)&F.l3) + 16), 48);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_closure t25 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_for_each_value_j9v56m8 *)((&F.h)->up))->l1));
                                gm_closure t26 = t25;
                                gm_node *ccx27 = t26.cx;
                                void * t28 = t26.code;
                                gm_b0_1 t29;
                                if (GM_PTR_TAGGED(t28)) {
                                    ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t28))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l4)));
                                } else {
                                    t29 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t28)(t26.env, ccx27, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l4)));
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                goto gm_lb6;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 321, 13);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 320, 9);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t30 = gm_l2;
            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
            uint64_t t33 = r31;
            gm_l2 = t33;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_place_type_5jm3hnm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_place_type_5jm3hnm F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    void * gm_l2; /* p */
    uint32_t gm_l3; /* l */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* l */
    uint32_t gm_l6; /* outer */
    gm_b16_8 gm_l7; /* locals */
    uint64_t gm_l8; /* decl */
    gm_tkn42u7na3t81 gm_l9; /* found */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t88rcucg1b7uk gm_l13; /* ct */
    uint32_t gm_l14; /* tmp */
    gm_t88rcucg1b7uk gm_l15; /* t */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l19; /* i */
    gm_t88rcucg1b7uk gm_l20; /* elem */
    gm_t88rcucg1b7uk gm_l21; /* pt */
    GM_STACK_CHECK(&F, 1061u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t88rcucg1b7uk gm_l22;
    gm_t9805qtonbsag gm_l23;
    uint32_t gm_l24;
    gm_t88rcucg1b7uk gm_l25;
    gm_tkn42u7na3t81 gm_l26;
    gm_t88rcucg1b7uk gm_l27;
    gm_t88rcucg1b7uk gm_l28;
    gm_tkn42u7na3t81 gm_l29;
    gm_t88rcucg1b7uk gm_l30;
    gm_t88rcucg1b7uk gm_l31;
    gm_t53at80pfdus3 gm_l32;
    gm_tkn42u7na3t81 gm_l33;
    gm_t88rcucg1b7uk gm_l34;
    {
        void * t26 = gm_l2;
        {
            {
                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
                if (!t28) {
                    goto gm_lb2;
                }
                uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)t26) + 8));
                gm_l3 = t29;
                uint32_t t30 = gm_l3;
                uint64_t t31 = (uint64_t)t30;
                void * t32 = gm_l1;
                uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t32) + 8) + 8));
                uint8_t t34 = (t31 >= t33);
                if (t34) {
                    gm_tkn42u7na3t81 t35;
                    memset(&t35, 0, sizeof t35);
                    F.ret = t35;
                    goto gm_fret;
                }
                void * t36 = gm_l1;
                void * t37 = GM_LD(void *, (((uint8_t *)t36) + 8));
                uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 8) + 8));
                uint32_t t39 = gm_l3;
                uint64_t t40 = (uint64_t)t39;
                if ((uintptr_t)t40 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t40, t38, gm_srch081ittgol24, 396, 13);
                gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t37) + (uintptr_t)t40 * 32));
                gm_l22 = t41;
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
                if (!t43) {
                    goto gm_lb3;
                }
                uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)t26) + 8));
                gm_l4 = t44;
                uint32_t t45 = GM_LD(uint32_t, ((((uint8_t *)t26) + 8) + 4));
                gm_l5 = t45;
                void * t46 = gm_l0;
                void * t47 = gm_l1;
                uint32_t t48 = GM_LD(uint32_t, ((uint8_t *)t47));
                uint32_t t49 = gm_l4;
                gm_t9805qtonbsag t50 = gm_f_ancestor_o28bfm6(t46, t48, t49);
                gm_l23 = t50;
                uint8_t t51 = (uint8_t)((((uint8_t *)&gm_l23))[4] != 0);
                if (t51) {
                    uint32_t t52 = GM_LD(uint32_t, ((uint8_t *)&gm_l23));
                    gm_l24 = t52;
                } else {
                    gm_tkn42u7na3t81 t53;
                    memset(&t53, 0, sizeof t53);
                    F.ret = t53;
                    goto gm_fret;
                }
                uint32_t t54 = gm_l24;
                gm_l6 = t54;
                void * t55 = gm_l0;
                void * t56 = GM_LD(void *, (((uint8_t *)t55) + 384));
                uint64_t t57 = GM_LD(uint64_t, ((((uint8_t *)t55) + 384) + 8));
                uint32_t t58 = gm_l6;
                uint64_t t59 = (uint64_t)t58;
                if ((uintptr_t)t59 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t59, t57, gm_srch081ittgol24, 400, 23);
                gm_b16_8 t60 = GM_LD(gm_b16_8, ((((uint8_t *)t56) + (uintptr_t)t59 * 64) + 24));
                gm_l7 = t60;
                uint32_t t61 = gm_l5;
                uint64_t t62 = (uint64_t)t61;
                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t64 = (t62 >= t63);
                if (t64) {
                    gm_tkn42u7na3t81 t65;
                    memset(&t65, 0, sizeof t65);
                    F.ret = t65;
                    goto gm_fret;
                }
                void * t66 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint32_t t68 = gm_l5;
                uint64_t t69 = (uint64_t)t68;
                if ((uintptr_t)t69 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t69, t67, gm_srch081ittgol24, 402, 13);
                gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t66) + (uintptr_t)t69 * 40) + 16));
                gm_l22 = t70;
                goto gm_lb1;
            }
gm_lb3:;
            {
                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                uint8_t t72 = (uint8_t)(t71 == ((uint8_t)2ULL));
                if (!t72) {
                    goto gm_lb4;
                }
                uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t26) + 8));
                gm_l8 = t73;
                gm_tkn42u7na3t81 t74;
                memset(&t74, 0, sizeof t74);
                gm_l9 = t74;
                void * t75 = gm_l0;
                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)t75) + 1568));
                gm_l10 = t76;
                gm_l11 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t77 = gm_l11;
                        uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint8_t t79 = (t77 < t78);
                        if (!t79) {
                            goto gm_lb5;
                        }
                        {
                            void * t80 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t82 = gm_l11;
                            {
                                {
                                    memmove(((uint8_t *)&F.l12), (((uint8_t *)t80) + (uintptr_t)t82 * 104), 104);
                                    uint64_t t83 = GM_LD(uint64_t, ((uint8_t *)&F.l12));
                                    uint64_t t84 = gm_l8;
                                    uint8_t t85 = ((t83 == t84));
                                    if (t85) {
                                        gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 24));
                                        gm_tkn42u7na3t81 t87;
                                        GM_ST(uint8_t, (uint8_t *)&t87 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t87 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t87), t86);
                                        ((uint8_t *)&t87)[4] = 1;
                                        gm_l9 = t87;
                                    }
                                    goto gm_lb8;
                                }
gm_lb9:;
                                gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 406, 13);
                            }
gm_lb8:;
                        }
gm_lb7:;
                        uint64_t t88 = gm_l11;
                        uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                        uint64_t t91 = r89;
                        gm_l11 = t91;
                    }
                }
gm_lb5:;
                uint8_t t92 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
                if (t92) {
                    gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l9));
                    gm_l25 = t93;
                } else {
                    gm_tkn42u7na3t81 t94;
                    memset(&t94, 0, sizeof t94);
                    F.ret = t94;
                    goto gm_fret;
                }
                gm_t88rcucg1b7uk t95 = gm_l25;
                gm_l22 = t95;
                goto gm_lb1;
            }
gm_lb4:;
            {
                uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                uint8_t t97 = (uint8_t)(t96 == ((uint8_t)3ULL));
                if (!t97) {
                    goto gm_lb10;
                }
                gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t26) + 8) + 28));
                gm_l13 = t98;
                gm_t88rcucg1b7uk t99 = gm_l13;
                gm_l22 = t99;
                goto gm_lb1;
            }
gm_lb10:;
            {
                uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                uint8_t t101 = (uint8_t)(t100 == ((uint8_t)4ULL));
                if (!t101) {
                    goto gm_lb11;
                }
                uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)t26) + 8));
                gm_l14 = t102;
                uint32_t t103 = gm_l14;
                uint64_t t104 = (uint64_t)t103;
                void * t105 = gm_l1;
                uint64_t t106 = GM_LD(uint64_t, ((((uint8_t *)t105) + 24) + 8));
                uint8_t t107 = (t104 >= t106);
                if (t107) {
                    gm_tkn42u7na3t81 t108;
                    memset(&t108, 0, sizeof t108);
                    F.ret = t108;
                    goto gm_fret;
                }
                void * t109 = gm_l0;
                void * t110 = gm_l1;
                void * t111 = GM_LD(void *, (((uint8_t *)t110) + 24));
                uint64_t t112 = GM_LD(uint64_t, ((((uint8_t *)t110) + 24) + 8));
                uint32_t t113 = gm_l14;
                uint64_t t114 = (uint64_t)t113;
                if ((uintptr_t)t114 >= (uintptr_t)(t112)) gm_trap_index((uintptr_t)t114, t112, gm_srch081ittgol24, 414, 24);
                gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + (uintptr_t)t114 * 4));
                gm_tkn42u7na3t81 t116 = gm_f_pointee_5ui0dk7(t109, GM_CONV(gm_csguankko4lmhd, t115));
                gm_l26 = t116;
                uint8_t t117 = (uint8_t)((((uint8_t *)&gm_l26))[4] != 0);
                if (t117) {
                    gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26));
                    gm_l27 = t118;
                } else {
                    gm_tkn42u7na3t81 t119;
                    memset(&t119, 0, sizeof t119);
                    F.ret = t119;
                    goto gm_fret;
                }
                gm_t88rcucg1b7uk t120 = gm_l27;
                gm_l22 = t120;
                goto gm_lb1;
            }
gm_lb11:;
            gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 393, 13);
        }
gm_lb1:;
    }
    {
        gm_t88rcucg1b7uk t121 = gm_l22;
        gm_l15 = t121;
        void * t122 = gm_l2;
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)t122) + 40));
        gm_l16 = t123;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t124 = gm_l17;
                uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t126 = (t124 < t125);
                if (!t126) {
                    goto gm_lb12;
                }
                {
                    void * t127 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t129 = gm_l17;
                    {
                        {
                            memmove(((uint8_t *)&F.l18), (((uint8_t *)t127) + (uintptr_t)t129 * 80), 80);
                            {
                                {
                                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)0ULL));
                                    if (!t131) {
                                        goto gm_lb18;
                                    }
                                    uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&F.l18) + 16));
                                    gm_l19 = t132;
                                    void * t133 = gm_l0;
                                    gm_t88rcucg1b7uk t134 = gm_l15;
                                    uint32_t t135 = gm_l19;
                                    gm_tkn42u7na3t81 t136 = gm_f_field_type_ph14klv((void *)NULL, cx, NULL, t133, t134, t135);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l29 = t136;
                                    uint8_t t137 = (uint8_t)((((uint8_t *)&gm_l29))[4] != 0);
                                    if (t137) {
                                        gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l29));
                                        gm_l30 = t138;
                                    } else {
                                        gm_tkn42u7na3t81 t139;
                                        memset(&t139, 0, sizeof t139);
                                        F.ret = t139;
                                        goto gm_fret;
                                    }
                                    gm_t88rcucg1b7uk t140 = gm_l30;
                                    gm_l28 = t140;
                                    goto gm_lb17;
                                }
gm_lb18:;
                                {
                                    uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                    uint8_t t142 = (uint8_t)(t141 == ((uint8_t)1ULL));
                                    if (!t142) {
                                        goto gm_lb19;
                                    }
                                    {
                                        {
                                            void * t143 = gm_l0;
                                            void * t144 = (void *)(((uint8_t *)t143) + 8);
                                            gm_t88rcucg1b7uk t145 = gm_l15;
                                            gm_f_kind_jcnt4hm((void *)&gm_l32, t144, GM_CONV(gm_csguankko4lmhd, t145));
                                            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                                            uint8_t t147 = (uint8_t)(t146 == ((uint8_t)11ULL));
                                            if (!t147) {
                                                goto gm_lb21;
                                            }
                                            gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l32) + 8) + 8));
                                            gm_l20 = t148;
                                            gm_t88rcucg1b7uk t149 = gm_l20;
                                            gm_l31 = t149;
                                            goto gm_lb20;
                                        }
gm_lb21:;
                                        gm_tkn42u7na3t81 t150;
                                        memset(&t150, 0, sizeof t150);
                                        F.ret = t150;
                                        goto gm_fret;
                                        gm_l31 = GM_ZERO(gm_t88rcucg1b7uk);
                                    }
gm_lb20:;
                                    gm_t88rcucg1b7uk t151 = gm_l31;
                                    gm_l28 = t151;
                                    goto gm_lb17;
                                }
gm_lb19:;
                                {
                                    uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)2ULL));
                                    if (!t153) {
                                        goto gm_lb22;
                                    }
                                    gm_t88rcucg1b7uk t154 = gm_l15;
                                    gm_l28 = t154;
                                    goto gm_lb17;
                                }
gm_lb22:;
                                {
                                    uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                    uint8_t t156 = (uint8_t)(t155 == ((uint8_t)3ULL));
                                    if (!t156) {
                                        goto gm_lb23;
                                    }
                                    void * t157 = gm_l0;
                                    gm_t88rcucg1b7uk t158 = gm_l15;
                                    gm_tkn42u7na3t81 t159 = gm_f_pointee_5ui0dk7(t157, GM_CONV(gm_csguankko4lmhd, t158));
                                    gm_l33 = t159;
                                    uint8_t t160 = (uint8_t)((((uint8_t *)&gm_l33))[4] != 0);
                                    if (t160) {
                                        gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l33));
                                        gm_l34 = t161;
                                    } else {
                                        gm_tkn42u7na3t81 t162;
                                        memset(&t162, 0, sizeof t162);
                                        F.ret = t162;
                                        goto gm_fret;
                                    }
                                    gm_t88rcucg1b7uk t163 = gm_l34;
                                    gm_l28 = t163;
                                    goto gm_lb17;
                                }
gm_lb23:;
                                {
                                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)4ULL));
                                    if (!t165) {
                                        goto gm_lb24;
                                    }
                                    gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l18) + 16) + 8));
                                    gm_l21 = t166;
                                    gm_t88rcucg1b7uk t167 = gm_l21;
                                    gm_l28 = t167;
                                    goto gm_lb17;
                                }
gm_lb24:;
                                gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 418, 13);
                            }
gm_lb17:;
                            gm_t88rcucg1b7uk t168 = gm_l28;
                            gm_l15 = t168;
                            goto gm_lb15;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 417, 5);
                    }
gm_lb15:;
                }
gm_lb14:;
                uint64_t t169 = gm_l17;
                uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                uint64_t t172 = r170;
                gm_l17 = t172;
            }
        }
gm_lb12:;
    }
    {
        gm_t88rcucg1b7uk t173 = gm_l15;
        gm_tkn42u7na3t81 t174;
        GM_ST(uint8_t, (uint8_t *)&t174 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t174 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t174), t173);
        ((uint8_t *)&t174)[4] = 1;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_pointee_5ui0dk7(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pointee_5ui0dk7 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_tkn42u7na3t81 gm_l4;
    gm_t53at80pfdus3 gm_l5;
    void * t6 = gm_l0;
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)8ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l2 = t11;
            gm_t88rcucg1b7uk t12 = gm_l2;
            gm_tkn42u7na3t81 t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t13), t12);
            ((uint8_t *)&t13)[4] = 1;
            gm_l4 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)9ULL));
            if (!t15) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l3 = t16;
            gm_t88rcucg1b7uk t17 = gm_l3;
            gm_tkn42u7na3t81 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t18), t17);
            ((uint8_t *)&t18)[4] = 1;
            gm_l4 = t18;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_tkn42u7na3t81 t19;
            memset(&t19, 0, sizeof t19);
            gm_l4 = t19;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 447, 48);
    }
gm_lb1:;
    gm_tkn42u7na3t81 t20 = gm_l4;
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_value_type_p69quvj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_type_p69quvj F;
    void * gm_l0; /* f */
    uint32_t gm_l2; /* t */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_t88rcucg1b7uk gm_l5; /* ty */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk gm_l7;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
            gm_l2 = t12;
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 24));
            uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 24) + 8));
            uint32_t t16 = gm_l2;
            uint64_t t17 = (uint64_t)t16;
            if ((uintptr_t)t17 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t17, t15, gm_srch081ittgol24, 432, 17);
            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + (uintptr_t)t17 * 4));
            gm_l7 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
                    if (!t20) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 16));
                    gm_l3 = t21;
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)2ULL));
                    if (!t23) {
                        goto gm_lb6;
                    }
                    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 16));
                    gm_l3 = t24;
                    goto gm_lb4;
                }
gm_lb6:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)3ULL));
                    if (!t26) {
                        goto gm_lb7;
                    }
                    gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 16));
                    gm_l3 = t27;
                    goto gm_lb4;
                }
gm_lb7:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)4ULL));
                    if (!t29) {
                        goto gm_lb8;
                    }
                    gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 16));
                    gm_l3 = t30;
                    goto gm_lb4;
                }
gm_lb8:;
                goto gm_lb3;
            }
gm_lb4:;
            gm_t88rcucg1b7uk t31 = gm_l3;
            gm_l7 = t31;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srch081ittgol24, 431, 57);
    }
gm_lb1:;
    gm_t88rcucg1b7uk t32 = gm_l7;
    F.ret = t32;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_value_type_p69quvj_c(void * a0, gm_t7b7esi3n9r90 a1) { return gm_f_value_type_p69quvj(a0, &a1); }
