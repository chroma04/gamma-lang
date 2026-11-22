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
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; gm_tl874t2atnlh3 f3; } gm_tgk5j0pob2pqa;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_b16_8 f0; gm_tv2uf4leil5hq f1; } gm_taiafe1jasg4m;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgk5j0pob2pqa) == 24 && _Alignof(gm_tgk5j0pob2pqa) == 4 && offsetof(gm_tgk5j0pob2pqa, f0) == 0 && offsetof(gm_tgk5j0pob2pqa, f1) == 4 && offsetof(gm_tgk5j0pob2pqa, f2) == 8 && offsetof(gm_tgk5j0pob2pqa, f3) == 12, "layout of `LabelBreak`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t08olq97rscn3 f1; } gm_tp9ftj4p31ha1;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
typedef struct { gm_toceffb697p0u f0; uint8_t f1; uint8_t pe[3]; } gm_trj4m8pa99qp7;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { gm_b16_8 f0; gm_tjsp7bhdn2e8j f1; uint8_t p2[5]; void * f2; } gm_t4d1qrgh0vnq1;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_taiafe1jasg4m) == 80 && _Alignof(gm_taiafe1jasg4m) == 8 && offsetof(gm_taiafe1jasg4m, f0) == 0 && offsetof(gm_taiafe1jasg4m, f1) == 16, "layout of `(attrs: []Attribute, binding: Binding)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tp9ftj4p31ha1) == 40 && _Alignof(gm_tp9ftj4p31ha1) == 8 && offsetof(gm_tp9ftj4p31ha1, f0) == 0 && offsetof(gm_tp9ftj4p31ha1, f1) == 8, "layout of `(fn_id: u32, name: Ident)`");
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trj4m8pa99qp7) == 20 && _Alignof(gm_trj4m8pa99qp7) == 4 && offsetof(gm_trj4m8pa99qp7, f0) == 0 && offsetof(gm_trj4m8pa99qp7, f1) == 16, "layout of `?NeedsAt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_thdmr9qicg35r f2; uint8_t pe[4]; } gm_tti6qnvf9jg1p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_ttja7ii61nila;
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t4d1qrgh0vnq1) == 32 && _Alignof(gm_t4d1qrgh0vnq1) == 8 && offsetof(gm_t4d1qrgh0vnq1, f0) == 0 && offsetof(gm_t4d1qrgh0vnq1, f1) == 16 && offsetof(gm_t4d1qrgh0vnq1, f2) == 24, "layout of `(places: []Expr, op: AssignOp, value: ^Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
typedef struct { gm_ttja7ii61nila f0; uint8_t f1; uint8_t pe[7]; } gm_t9t5bnrf784qu;
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tti6qnvf9jg1p) == 72 && _Alignof(gm_tti6qnvf9jg1p) == 8 && offsetof(gm_tti6qnvf9jg1p, f0) == 0 && offsetof(gm_tti6qnvf9jg1p, f1) == 40 && offsetof(gm_tti6qnvf9jg1p, f2) == 56, "layout of `InferTable`");
_Static_assert(sizeof(gm_ttja7ii61nila) == 48 && _Alignof(gm_ttja7ii61nila) == 8 && offsetof(gm_ttja7ii61nila, f0) == 0 && offsetof(gm_ttja7ii61nila, f1) == 8, "layout of `IfElse`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { gm_t0d02142k4tu8 f0; void * f1; void * f2; } gm_t15nkjiutlvf3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { gm_b16_8 f0; gm_t9t5bnrf784qu f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t99g4dkn8rrfb;
_Static_assert(sizeof(gm_t9t5bnrf784qu) == 56 && _Alignof(gm_t9t5bnrf784qu) == 8 && offsetof(gm_t9t5bnrf784qu, f0) == 0 && offsetof(gm_t9t5bnrf784qu, f1) == 48, "layout of `?IfElse`");
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t15nkjiutlvf3) == 80 && _Alignof(gm_t15nkjiutlvf3) == 8 && offsetof(gm_t15nkjiutlvf3, f0) == 0 && offsetof(gm_t15nkjiutlvf3, f1) == 64 && offsetof(gm_t15nkjiutlvf3, f2) == 72, "layout of `(pat: Pattern, value: ^Expr, fallback: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_b16_8 v1; } f1; } gm_t4t568o7qnv02;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_t99g4dkn8rrfb) == 88 && _Alignof(gm_t99g4dkn8rrfb) == 8 && offsetof(gm_t99g4dkn8rrfb, f0) == 0 && offsetof(gm_t99g4dkn8rrfb, f1) == 16 && offsetof(gm_t99g4dkn8rrfb, f2) == 72, "layout of `IfExpr`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_taiafe1jasg4m v0; gm_t4d1qrgh0vnq1 v1; void * v2; gm_tgtub0v94d5u5 v3; void * v4; gm_t43d6nbdfap6p v5; void * v6; gm_t15nkjiutlvf3 v7; gm_t79rbdv4rafe5 v8; } f1; } gm_tem8p0t66u1ce;
typedef struct { gm_trqfvmjtt4sij f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t f6; uint8_t p7[3]; uint64_t f7; gm_t9805qtonbsag f8; uint8_t f9; uint8_t f10; uint8_t p11[2]; gm_tkn42u7na3t81 f11; uint8_t p12[4]; gm_t403eokj7ifg9 f12; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; } gm_tjhr0bdm69ios;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
typedef struct { void * f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_tti6qnvf9jg1p f4; gm_t88rcucg1b7uk f5; uint8_t p6[4]; gm_tnpifv20irnph f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t p10[4]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_t9805qtonbsag f13; gm_trj4m8pa99qp7 f14; uint8_t p15[4]; gm_trqfvmjtt4sij f15; uint8_t f16; uint8_t p17[7]; gm_trqfvmjtt4sij f17; uint8_t f18; uint8_t p19[7]; uint64_t f19; gm_trqfvmjtt4sij f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; uint8_t f24; uint8_t p25[7]; gm_trqfvmjtt4sij f25; uint8_t f26; uint8_t p27[3]; gm_tkn42u7na3t81 f27; uint8_t p28[4]; gm_trqfvmjtt4sij f28; gm_t403eokj7ifg9 f29; gm_trqfvmjtt4sij f30; gm_trqfvmjtt4sij f31; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; uint8_t f38; uint8_t p39[7]; gm_trqfvmjtt4sij f39; uint8_t f40; uint8_t p41[7]; gm_t4c7ipjj9s3fp f41; void * f42; } gm_tta5be0c75tfq;
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
_Static_assert(sizeof(gm_t4t568o7qnv02) == 208 && _Alignof(gm_t4t568o7qnv02) == 8 && offsetof(gm_t4t568o7qnv02, f0) == 0 && offsetof(gm_t4t568o7qnv02, f1) == 8, "layout of `UntypedOr`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_tjhr0bdm69ios f0; uint8_t f1; uint8_t pe[7]; } gm_ta7fvse6v13pf;
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tem8p0t66u1ce) == 120 && _Alignof(gm_tem8p0t66u1ce) == 8 && offsetof(gm_tem8p0t66u1ce, f0) == 0 && offsetof(gm_tem8p0t66u1ce, f1) == 8, "layout of `StmtKind`");
typedef struct { gm_t930q5l38hdap f0; uint32_t f1; uint8_t pe[4]; } gm_tfj8gdjm726n4;
_Static_assert(sizeof(gm_tjhr0bdm69ios) == 384 && _Alignof(gm_tjhr0bdm69ios) == 8 && offsetof(gm_tjhr0bdm69ios, f0) == 0 && offsetof(gm_tjhr0bdm69ios, f1) == 40 && offsetof(gm_tjhr0bdm69ios, f2) == 48 && offsetof(gm_tjhr0bdm69ios, f3) == 64 && offsetof(gm_tjhr0bdm69ios, f4) == 104 && offsetof(gm_tjhr0bdm69ios, f5) == 144 && offsetof(gm_tjhr0bdm69ios, f6) == 148 && offsetof(gm_tjhr0bdm69ios, f7) == 152 && offsetof(gm_tjhr0bdm69ios, f8) == 160 && offsetof(gm_tjhr0bdm69ios, f9) == 168 && offsetof(gm_tjhr0bdm69ios, f10) == 169 && offsetof(gm_tjhr0bdm69ios, f11) == 172 && offsetof(gm_tjhr0bdm69ios, f12) == 184 && offsetof(gm_tjhr0bdm69ios, f13) == 304 && offsetof(gm_tjhr0bdm69ios, f14) == 344, "layout of `Frame`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_b16_8 f0; gm_t930q5l38hdap f1; } gm_tmhicb6huo2ra;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tem8p0t66u1ce f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to88ae1b67aev;
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tta5be0c75tfq) == 1360 && _Alignof(gm_tta5be0c75tfq) == 8 && offsetof(gm_tta5be0c75tfq, f0) == 0 && offsetof(gm_tta5be0c75tfq, f1) == 8 && offsetof(gm_tta5be0c75tfq, f2) == 16 && offsetof(gm_tta5be0c75tfq, f3) == 56 && offsetof(gm_tta5be0c75tfq, f4) == 96 && offsetof(gm_tta5be0c75tfq, f5) == 168 && offsetof(gm_tta5be0c75tfq, f6) == 176 && offsetof(gm_tta5be0c75tfq, f7) == 192 && offsetof(gm_tta5be0c75tfq, f8) == 232 && offsetof(gm_tta5be0c75tfq, f9) == 272 && offsetof(gm_tta5be0c75tfq, f10) == 280 && offsetof(gm_tta5be0c75tfq, f11) == 320 && offsetof(gm_tta5be0c75tfq, f12) == 360 && offsetof(gm_tta5be0c75tfq, f13) == 400 && offsetof(gm_tta5be0c75tfq, f14) == 408 && offsetof(gm_tta5be0c75tfq, f15) == 432 && offsetof(gm_tta5be0c75tfq, f16) == 472 && offsetof(gm_tta5be0c75tfq, f17) == 480 && offsetof(gm_tta5be0c75tfq, f18) == 520 && offsetof(gm_tta5be0c75tfq, f19) == 528 && offsetof(gm_tta5be0c75tfq, f20) == 536 && offsetof(gm_tta5be0c75tfq, f21) == 576 && offsetof(gm_tta5be0c75tfq, f22) == 616 && offsetof(gm_tta5be0c75tfq, f23) == 656 && offsetof(gm_tta5be0c75tfq, f24) == 696 && offsetof(gm_tta5be0c75tfq, f25) == 704 && offsetof(gm_tta5be0c75tfq, f26) == 744 && offsetof(gm_tta5be0c75tfq, f27) == 748 && offsetof(gm_tta5be0c75tfq, f28) == 760 && offsetof(gm_tta5be0c75tfq, f29) == 800 && offsetof(gm_tta5be0c75tfq, f30) == 920 && offsetof(gm_tta5be0c75tfq, f31) == 960 && offsetof(gm_tta5be0c75tfq, f32) == 1000 && offsetof(gm_tta5be0c75tfq, f33) == 1040 && offsetof(gm_tta5be0c75tfq, f34) == 1080 && offsetof(gm_tta5be0c75tfq, f35) == 1120 && offsetof(gm_tta5be0c75tfq, f36) == 1160 && offsetof(gm_tta5be0c75tfq, f37) == 1200 && offsetof(gm_tta5be0c75tfq, f38) == 1240 && offsetof(gm_tta5be0c75tfq, f39) == 1248 && offsetof(gm_tta5be0c75tfq, f40) == 1288 && offsetof(gm_tta5be0c75tfq, f41) == 1296 && offsetof(gm_tta5be0c75tfq, f42) == 1352, "layout of `FnCx`");
_Static_assert(sizeof(gm_ta7fvse6v13pf) == 392 && _Alignof(gm_ta7fvse6v13pf) == 8 && offsetof(gm_ta7fvse6v13pf, f0) == 0 && offsetof(gm_ta7fvse6v13pf, f1) == 384, "layout of `?Frame`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
_Static_assert(sizeof(gm_tfj8gdjm726n4) == 216 && _Alignof(gm_tfj8gdjm726n4) == 8 && offsetof(gm_tfj8gdjm726n4, f0) == 0 && offsetof(gm_tfj8gdjm726n4, f1) == 208, "layout of `(binding: LocalBinding, depth: u32)`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tmhicb6huo2ra) == 224 && _Alignof(gm_tmhicb6huo2ra) == 8 && offsetof(gm_tmhicb6huo2ra, f0) == 0 && offsetof(gm_tmhicb6huo2ra, f1) == 16, "layout of `Binding`");
_Static_assert(sizeof(gm_to88ae1b67aev) == 144 && _Alignof(gm_to88ae1b67aev) == 8 && offsetof(gm_to88ae1b67aev, f0) == 0 && offsetof(gm_to88ae1b67aev, f1) == 8 && offsetof(gm_to88ae1b67aev, f2) == 128, "layout of `Stmt`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
typedef struct { gm_tfj8gdjm726n4 f0; uint8_t f1; uint8_t pe[7]; } gm_tsf6kp4ohnuqs;
_Static_assert(sizeof(gm_tsf6kp4ohnuqs) == 224 && _Alignof(gm_tsf6kp4ohnuqs) == 8 && offsetof(gm_tsf6kp4ohnuqs, f0) == 0 && offsetof(gm_tsf6kp4ohnuqs, f1) == 216, "layout of `?(binding: LocalBinding, depth: u32)`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_block_expected_bv4ukm2 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* stmts */
    gm_tnvnsr1bfvtbd l7; /* cause */
    gm_t4gut2c9fj1kt l13; /* d */
    gm_t0d02142k4tu8 l15; /* pat */
    gm_t79rbdv4rafe5 l19; /* rest */
    gm_trqfvmjtt4sij l20; /* fb_stmts */
    gm_t79rbdv4rafe5 l21; /* fb */
    gm_trqfvmjtt4sij l22; /* parts */
    gm_trqfvmjtt4sij l23; /* branches */
    void * ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_typed_e7svg7n {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_check_bodies_qrmq5ll {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_fn_body_kc8b374 {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_trqfvmjtt4sij l6; /* numeric */
    gm_td7pkamr64l0c l10; /* outer */
    gm_tta5be0c75tfq l11; /* cx */
    gm_tp9ftj4p31ha1 l17; /* g */
    gm_trqfvmjtt4sij l18; /* params */
    gm_t5v0c8ia6a5mo l21; /* p */
    gm_b0_1 ret;
};
struct gm_fr_clear_vns7pn2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_cond_99ajlhk {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_tq5j0547p8452 l5; /* p */
    gm_t0d02142k4tu8 l8; /* pat */
    gm_b16_8 ret;
};
struct gm_fr_declare_3l5mhoj {
    gm_frame h;
    gm_t930q5l38hdap l2; /* b */
    gm_t0ialelq3ql1k l4; /* v */
    gm_ts78ca2amthgu l5; /* c */
    gm_ts78ca2amthgu l6; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_declare_temps_a7fc14v {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stmts */
    void * ret;
};
struct gm_fr_defer_stmt_hm15mru {
    gm_frame h;
    gm_tj44oivvogvtl l10; /* p */
    gm_tv0k2vg3054a6 l14; /* o */
    gm_trqfvmjtt4sij l16; /* stmts */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_depth_fr7pn6n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_early_fallback_error_4lq49mb {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3g747ka {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7athnlt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3p9jvl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fdfu2r0 {
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
struct gm_fr_empty_slice_jvoelso {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lgqbbc4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_li9u7hg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lio7tu0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ojer7i1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oldiiq9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p3s8hj6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tcmn71c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u0hn964 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vefkkhp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vhibpt8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_fn_vo5kel1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
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
struct gm_fr_exit_fn_kj6vbu8 {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* f */
    gm_tk9n3vlila9qg l2; /* out */
    gm_tk9n3vlila9qg ret;
};
struct gm_fr_expect_bool_cdo5g1q {
    gm_frame h;
    gm_t53at80pfdus3 l2; /* k */
    void * ret;
};
struct gm_fr_finish_block_body_4vol3tv {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* stmts */
    void * ret;
};
struct gm_fr_finish_m8t0599 {
    gm_frame h;
    gm_tl93vjpcesj48 l7; /* n */
    gm_b0_1 ret;
};
struct gm_fr_fn_body_37q3l3a {
    gm_frame h;
    gm_t79rbdv4rafe5 l4; /* b */
    void * ret;
};
struct gm_fr_fn_cx_hgvq15e {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* scopes */
    gm_trqfvmjtt4sij l5; /* depths */
    gm_tta5be0c75tfq ret;
};
struct gm_fr_forget_label_breaks_vl4mn72 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_05vut39 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_07fj847 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9i45r57 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_biupho2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cn0m7al {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ggjb732 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n7840ue {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p8e3d4v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q32bjcv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r1s7qdu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s8qs6vn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_hash_map_njcsknk {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_infer_table_6o0km8j {
    gm_frame h;
    gm_tti6qnvf9jg1p ret;
};
struct gm_fr_is_bool_ajhjgeg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
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
struct gm_fr_leave_module_scope_4hp5mc4 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0ouok33 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5tqpoea {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6dnse27 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7ltkg0u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_88khgsi {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_clrolmo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_d0n30gc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_e6ie1fq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fs7kvfg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fu87ckm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g3vmkan_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lgereu6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rp32tbu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_t7903ef {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_taqq58r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tmk0paj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_frame_r6k3kmp {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_local_at_p388mm4 {
    gm_frame h;
    gm_tsoie1ppgdvcg ret;
};
struct gm_fr_lookup_local_6f38t7p {
    gm_frame h;
    gm_tqvu78rpi6q1h ret;
};
struct gm_fr_lookup_local_at_ntm4scs {
    gm_frame h;
    gm_tsf6kp4ohnuqs ret;
};
struct gm_fr_missing_return_s12s32d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_never_cause_p5ir1i2 {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_tnvnsr1bfvtbd ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_new_label_3207tcb {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_new_local_003h9es {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_note_capture_0p0af7r {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_pop_f3uajlg_2 {
    gm_frame h;
    gm_tjhr0bdm69ios l2; /* x */
    gm_ta7fvse6v13pf ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_00ljs05 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_0ie7e7i {
    gm_frame h;
    gm_tcuumjkg5eni6 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8h1r7s6_2 {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_am7bo0v {
    gm_frame h;
    gm_tsoie1ppgdvcg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud {
    gm_frame h;
    gm_to88ae1b67aev l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp {
    gm_frame h;
    gm_tq5j0547p8452 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_lng5b06 {
    gm_frame h;
    gm_t8enl5g7b22jb l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_nr0n00f {
    gm_frame h;
    gm_tmhicb6huo2ra l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ola4v1l {
    gm_frame h;
    gm_tb07pp6njg0bk l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s39fiuo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_scope_mu7j3lg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_temps_upm64pn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5jip0id {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_78ppoki {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8mal8et {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8r12kcb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_efufsip {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_igbb8pf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r51vrc8_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_sh871mr {
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
struct gm_fr_save_frame_8libfct {
    gm_frame h;
    gm_tjhr0bdm69ios ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
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
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_stmt_4145gsn {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* binding */
    gm_tkacitepg5fk0 l6; /* a */
    gm_t43d6nbdfap6p l11; /* w */
    gm_t79rbdv4rafe5 l12; /* b */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_stmt_diverges_en1hv79 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stmt_expr_evs4ic8 {
    gm_frame h;
    gm_t79rbdv4rafe5 l3; /* b */
    void * ret;
};
struct gm_fr_stmt_never_cause_r2lf089 {
    gm_frame h;
    gm_tnvnsr1bfvtbd ret;
};
struct gm_fr_swap_frame_4j63sdr {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* f */
    gm_tjhr0bdm69ios l2; /* out */
    gm_tjhr0bdm69ios ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_truncate_eoppkeo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_try_untyped_8j8up35 {
    gm_frame h;
    gm_t930q5l38hdap l3; /* b */
    gm_t0ialelq3ql1k l4; /* v */
    gm_t5vbkdq81qfs7 l7; /* r */
    gm_t0ialelq3ql1k l8; /* v */
    gm_t0ialelq3ql1k l9; /* x */
    gm_t5vbkdq81qfs7 l14; /* a */
    gm_t5vbkdq81qfs7 l15; /* b */
    gm_t0ialelq3ql1k l16; /* x */
    gm_t0ialelq3ql1k l17; /* y */
    gm_t0ialelq3ql1k l18; /* v */
    gm_tkv1vap90vht2 ret;
};
struct gm_fr_uninit_message_u2c3peu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unscope_type_params_bofhhep {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_untyped_op_7an9o22 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_when_stmt_3pk7eon {
    gm_frame h;
    gm_t79rbdv4rafe5 l3; /* b */
    void * ret;
};
struct gm_fr_while_loop_86vcgbb {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_assign_a0848vq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tjsp7bhdn2e8j a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_binary_9sb7899(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binding_cond_v86td9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binding_igkhmn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_body_when_cond_2coms3p(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_cancel_stmt_pfi7e6b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bodies_qrmq5ll(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_effects_mrg0sal(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_body_kc8b374(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pending_handlers_o_sunakri(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_vns7pn2(void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_cond_99ajlhk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_declare_temps_a7fc14v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_defer_stmt_hm15mru(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
__attribute__((aligned(4))) void gm_f_early_fallback_error_4lq49mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3g747ka(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7athnlt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3p9jvl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fdfu2r0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lgqbbc4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_li9u7hg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lio7tu0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojer7i1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p3s8hj6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u0hn964(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vefkkhp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tk9n3vlila9qg gm_f_exit_fn_kj6vbu8_c(void * a0);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_expr_rk98p2r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finalize_k1g9q48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_finish_block_body_4vol3tv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_fn_body_37q3l3a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void * gm_f_for_loop_b6hjsbq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_format_05vut39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_07fj847(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9i45r57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_biupho2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cn0m7al(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ggjb732(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_n7840ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_p8e3d4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q32bjcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r1s7qdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s8qs6vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_njcsknk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_if_expr_tiak47t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void gm_f_infer_table_6o0km8j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0ouok33(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5tqpoea(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6dnse27(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7ltkg0u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_88khgsi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_clrolmo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_d0n30gc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fs7kvfg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fu87ckm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lgereu6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rp32tbu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t7903ef(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_taqq58r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tmk0paj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp_c(void * a0, gm_tjhr0bdm69ios a1);
__attribute__((aligned(4))) void gm_f_local_at_p388mm4(void *gm_out, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tsoie1ppgdvcg gm_f_local_at_p388mm4_c(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqvu78rpi6q1h gm_f_lookup_local_6f38t7p_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_lookup_local_at_ntm4scs(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tsf6kp4ohnuqs gm_f_lookup_local_at_ntm4scs_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_match_expr_npcoo60(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_missing_return_s12s32d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_negate_fohael7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_never_cause_p5ir1i2(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_never_cause_p5ir1i2_c(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ulodmu9(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_note_capture_0p0af7r(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_2(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_00ljs05(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ie7e7i(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8h1r7s6_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lng5b06(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_nr0n00f(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ola4v1l(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s39fiuo(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_temps_upm64pn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_33kejrs(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5jip0id(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_78ppoki(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8mal8et(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_efufsip(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_igbb8pf(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sh871mr(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remove_d75fgh2(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_te9updc(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_v59hrnl(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_save_frame_8libfct(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_save_frame_8libfct_c(void * a0);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_4145gsn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79_c(void * a0, gm_trcmmd12cefb1 a1);
__attribute__((aligned(4))) void * gm_f_stmt_expr_evs4ic8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_never_cause_r2lf089(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_stmt_never_cause_r2lf089_c(void * a0, gm_trcmmd12cefb1 a1);
inline __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_swap_frame_4j63sdr_c(void * a0, gm_tjhr0bdm69ios a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_untyped_op_7an9o22(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_when_stmt_3pk7eon(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_while_loop_86vcgbb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_with_expr_vi78dtj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k1oghq2gkp4[40] __attribute__((aligned(8)));
extern uint8_t gm_kbeesj1a2or[40] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern const uint8_t gm_s069kf53uhd4o[45];
extern const uint8_t gm_s0rfpnv4c3k1l[38];
extern const uint8_t gm_s3k1sno8a2ipv[70];
extern const uint8_t gm_s7b7j2m1f6fle[71];
extern const uint8_t gm_s846hbv2ad5li[17];
extern const uint8_t gm_s86s8nstj5qij[54];
extern const uint8_t gm_s993c5b6eq922[54];
extern const uint8_t gm_s9abd2e9it4et[67];
extern const uint8_t gm_sadg245agtuao[45];
extern const uint8_t gm_sb3sf0ef436fi[40];
extern const uint8_t gm_sb5avtmeu66m7[144];
extern const uint8_t gm_seiql9tkbkhim[52];
extern const uint8_t gm_ses9h0rg965i4[54];
extern const uint8_t gm_sf4qubhm4h2hm[32];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfp78bn17ei97[60];
extern const uint8_t gm_she92i1u1a4v5[30];
extern const uint8_t gm_shl6ef6if4a86[80];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sjeqed5d3f5pb[26];
extern const uint8_t gm_sju4s3ndpnsge[107];
extern const uint8_t gm_sm9otrmuq5hp3[34];
extern const uint8_t gm_souep3pirutur[55];
extern const uint8_t gm_sp6l5tr58ip76[23];
extern const uint8_t gm_sqc0827akcmi8[40];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src276s8986o4r3[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sroje34o1kfta[38];
extern const uint8_t gm_ss6qrj79r47rb[70];
extern const uint8_t gm_sscfkpmei7e4o[29];
extern const uint8_t gm_su8f8u3vs6aep[110];
extern const uint8_t gm_sums36kmdot2i[109];
extern const uint8_t gm_sutvcnnben4aj[59];
extern const uint8_t gm_svfte0jrv51gc[30];
extern const gm_tmpl gm_tpl5b7905cihaqk;
extern const gm_tmpl gm_tpl97n87h3pcva1;
extern const gm_tmpl gm_tpl9dqq7pjdmqnl;
extern const gm_tmpl gm_tplamv2e4uailq7;
extern const gm_tmpl gm_tplcl8es85s6qrs;
extern const gm_tmpl gm_tplctjp7jb08m5t;
extern const gm_tmpl gm_tplg500ggupdf4i;
extern const gm_tmpl gm_tplhatoh83mi4is;
extern const gm_tmpl gm_tpljpjg7sv41er0;
extern const gm_tmpl gm_tpljucl1tv02je2;
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_tmpl gm_tplqrah664loq9e;
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
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_l1;
    gm_tl874t2atnlh3 t6 = gm_l2;
    void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
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
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_block_q09sn1p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    uint8_t t4 = gm_l2;
    gm_tkn42u7na3t81 t5;
    memset(&t5, 0, sizeof t5);
    void * t6 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) void gm_f_clear_vns7pn2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_vns7pn2 F;
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
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depth_fr7pn6n F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 576) + 8));
    uint32_t t4 = (uint32_t)t3;
    F.ret = t4;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3g747ka(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3g747ka F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7athnlt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7athnlt F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 224)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3p9jvl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3p9jvl F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 224)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fdfu2r0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fdfu2r0 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jvoelso F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lgqbbc4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lgqbbc4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_li9u7hg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_li9u7hg F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lio7tu0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lio7tu0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojer7i1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ojer7i1 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 72)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oldiiq9 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p3s8hj6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p3s8hj6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed_2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 384)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tcmn71c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u0hn964(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u0hn964 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vefkkhp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vefkkhp F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vhibpt8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj F;
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
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_module_scope_011vo5a F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_trqfvmjtt4sij t4 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 1688));
    gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t3) + 1728));
    gm_td7pkamr64l0c t6;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t6 + 40), t5);
    F.l1 = t6;
    void * t7 = gm_l0;
    gm_trqfvmjtt4sij t8;
    gm_f_list_1jabh22((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t7) + 1688), t8);
    void * t9 = gm_l0;
    gm_trqfvmjtt4sij t10;
    gm_f_list_0bj26n7((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t9) + 1728), t10);
    memmove(gm_out, ((uint8_t *)&F.l1), 80);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_format_05vut39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_05vut39 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb5avtmeu66m7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(143));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplctjp7jb08m5t), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_07fj847(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_07fj847 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sums36kmdot2i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(108));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhatoh83mi4is), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9i45r57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9i45r57 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s86s8nstj5qij));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljucl1tv02je2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_biupho2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_biupho2 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svfte0jrv51gc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(29));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplamv2e4uailq7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cn0m7al(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cn0m7al F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfp78bn17ei97));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(59));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqrah664loq9e), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_e79cvtl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sig8ahbvm8eb8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpll8q455cp9mhi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ggjb732(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ggjb732 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sutvcnnben4aj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg500ggupdf4i), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n7840ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n7840ue F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su8f8u3vs6aep));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(109));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9dqq7pjdmqnl), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p8e3d4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_p8e3d4v F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9abd2e9it4et));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljpjg7sv41er0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q32bjcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q32bjcv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3k1sno8a2ipv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl97n87h3pcva1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r1s7qdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r1s7qdu F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf4qubhm4h2hm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcl8es85s6qrs), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s8qs6vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s8qs6vn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shl6ef6if4a86));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(79));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5b7905cihaqk), t3);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_njcsknk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_njcsknk F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_p3s8hj6();
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
inline __attribute__((aligned(4))) void gm_f_infer_table_6o0km8j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_infer_table_6o0km8j F;
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_e6ie1fq((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
    gm_thdmr9qicg35r t4;
    memset(&t4, 0, sizeof t4);
    gm_tti6qnvf9jg1p t5;
    GM_ST(uint32_t, (uint8_t *)&t5 + 68, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 40), t3);
    GM_ST(gm_thdmr9qicg35r, ((uint8_t *)&t5 + 56), t4);
    memmove(gm_out, ((uint8_t *)&t5), 72);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_bool_ajhjgeg F;
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
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)4ULL)));
    gm_t53at80pfdus3 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)4ULL));
    F.ret = t6;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_module_scope_4hp5mc4 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    memmove((((uint8_t *)t2) + 1688), ((uint8_t *)a1), 40);
    void * t3 = gm_l0;
    memmove((((uint8_t *)t3) + 1728), (((uint8_t *)a1) + 40), 40);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_0ouok33(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0ouok33 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_tcmn71c();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_5tqpoea(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5tqpoea F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vefkkhp();
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
inline __attribute__((aligned(4))) void gm_f_list_6dnse27(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6dnse27 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lgqbbc4();
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
inline __attribute__((aligned(4))) void gm_f_list_7ltkg0u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7ltkg0u F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vhibpt8();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_88khgsi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_88khgsi F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qm5tp1g();
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
inline __attribute__((aligned(4))) void gm_f_list_clrolmo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_clrolmo F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_li9u7hg();
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
inline __attribute__((aligned(4))) void gm_f_list_d0n30gc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_d0n30gc F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_c3p9jvl();
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
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_e6ie1fq F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_oldiiq9();
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
inline __attribute__((aligned(4))) void gm_f_list_fs7kvfg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fs7kvfg F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_7athnlt();
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
inline __attribute__((aligned(4))) void gm_f_list_fu87ckm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fu87ckm F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3g747ka();
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
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g3vmkan_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_p5lo8ed_2();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vj3ckdj();
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
inline __attribute__((aligned(4))) void gm_f_list_lgereu6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lgereu6 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_u0hn964();
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
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p9s2t99 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6trg8r7();
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
inline __attribute__((aligned(4))) void gm_f_list_rp32tbu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rp32tbu F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jvoelso();
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
inline __attribute__((aligned(4))) void gm_f_list_t7903ef(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t7903ef F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lio7tu0();
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
inline __attribute__((aligned(4))) void gm_f_list_taqq58r(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_taqq58r F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ojer7i1();
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
inline __attribute__((aligned(4))) void gm_f_list_tmk0paj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tmk0paj F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fdfu2r0();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lmbdkhb();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)5ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_2(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_f3uajlg_2 F;
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
        gm_ta7fvse6v13pf t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 392);
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
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t8) + (uintptr_t)t13 * 384), 384);
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
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 384)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_tjhr0bdm69ios t26 = GM_LD(gm_tjhr0bdm69ios, ((uint8_t *)&F.l2));
    gm_ta7fvse6v13pf t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 385, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 386, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 388, 0);
    GM_ST(gm_tjhr0bdm69ios, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[384] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 392);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_ruvva9t F;
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
        gm_te5gbhu3us3k4 t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 56);
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
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t8) + (uintptr_t)t13 * 48), 48);
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
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_t8enl5g7b22jb t26 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)&F.l2));
    gm_te5gbhu3us3k4 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 52, 0);
    GM_ST(gm_t8enl5g7b22jb, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[48] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 56);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_00ljs05(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_00ljs05 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lio7tu0();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_efufsip(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)a1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ie7e7i(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0ie7e7i F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ojer7i1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_igbb8pf(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)a1), 72);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 72)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8h1r7s6_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8h1r7s6_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_p5lo8ed_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_r51vrc8_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 384), ((uint8_t *)a1), 384);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 384)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_am7bo0v F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6trg8r7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8r12kcb(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud F;
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
        gm_b16_8 t9 = gm_f_empty_slice_vj3ckdj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h08har5(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 144), ((uint8_t *)a1), 144);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lmbdkhb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4bta8ne(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 80), ((uint8_t *)&F.l1), 80);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2onqfca(t8, ((uint64_t)1ULL), t10);
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
    uint32_t t18 = gm_l1;
    GM_ST(uint32_t, (((uint8_t *)t16) + (uintptr_t)t17 * 4), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lng5b06(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_lng5b06 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_u0hn964();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8mal8et(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)a1), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_nr0n00f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_nr0n00f F;
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
        gm_b16_8 t9 = gm_f_empty_slice_7athnlt();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5jip0id(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 224), ((uint8_t *)a1), 224);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ola4v1l(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ola4v1l F;
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
        gm_b16_8 t9 = gm_f_empty_slice_f6g91fe();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_78ppoki(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 224), ((uint8_t *)a1), 224);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s39fiuo(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s39fiuo F;
    void * gm_l0; /* l */
    gm_t66j1im9ip128 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_li9u7hg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_sh871mr(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)80ULL), ((uint64_t)8ULL));
    F.ret = t8;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5jip0id(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5jip0id F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)224ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_78ppoki(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_78ppoki F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)224ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8mal8et(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8mal8et F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8r12kcb F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_efufsip(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_efufsip F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_igbb8pf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_igbb8pf F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)72ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r51vrc8_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)384ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sh871mr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_sh871mr F;
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1608) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 1648) + 8));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 1728) + 8));
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1688) + 8));
    gm_t66j1im9ip128 t10;
    GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t5);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 16), t7);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 24), t9);
    memmove(gm_out, ((uint8_t *)&t10), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
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
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t6, t7);
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
inline __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_swap_frame_4j63sdr F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_f_save_frame_8libfct((void *)&F.l2, t2);
    void * t3 = gm_l0;
    gm_f_load_frame_r6k3kmp(t3, &(*(gm_tjhr0bdm69ios *)((uint8_t *)a1)));
    memmove(gm_out, ((uint8_t *)&F.l2), 384);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_tl874t2atnlh3 t5 = gm_l2;
    uint32_t t6 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t2, t3, t4, ((uint8_t)1), t5);
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
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_eoppkeo F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
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
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unscope_type_params_bofhhep F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_f_restore_scope_edqq1v8(t2, &(*(gm_t66j1im9ip128 *)((uint8_t *)a1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 132u);
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
    gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_tl874t2atnlh3 t9 = gm_l1;
    void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)gm_c3t36dlh4g5));
    gm_b16_8 t5 = gm_l2;
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_f_format_e79cvtl((void *)&gm_l3, (void *)NULL, cx, NULL, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srclv0dcbbfhves, 643, 19);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_block_expected_bv4ukm2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    void * gm_l5; /* tail */
    uint8_t gm_l6; /* diverges */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    void * gm_l12; /* s */
    gm_tbgd14esf0jvi gm_l14; /* c */
    void * gm_l16; /* v */
    void * gm_l17; /* fallback */
    gm_tl874t2atnlh3 gm_l18; /* rest_span */
    void * gm_l24; /* lowered */
    uint64_t gm_l25; /* mark */
    void * gm_l26; /* ex */
    gm_trl2khmuphncd gm_l27; /* x */
    void * gm_l28; /* e */
    gm_tmn7n0n9raou5 gm_l29; /* kind */
    void * gm_l30; /* inner */
    gm_t88rcucg1b7uk gm_l31; /* fallback_ty */
    uint64_t gm_l32; /* k */
    gm_tl874t2atnlh3 gm_l33; /* ospan */
    void * gm_l34; /* e */
    void * gm_l35; /* ex */
    uint64_t gm_l36; /* mark */
    gm_trcmmd12cefb1 gm_l37; /* st */
    void * gm_l38; /* t */
    gm_t88rcucg1b7uk gm_l39; /* ty */
    GM_STACK_CHECK(&F, 3806u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tl874t2atnlh3 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tq7s98ihonkju gm_l44;
    gm_t88rcucg1b7uk gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_t88rcucg1b7uk gm_l48;
    gm_t88rcucg1b7uk gm_l49;
    {
        void * t30 = gm_l0;
        gm_b0_1 t31 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t32;
        gm_f_list_ir0h3tc_2((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t32;
        gm_l5 = ((void *)0);
        gm_l6 = ((uint8_t)0);
        gm_tnvnsr1bfvtbd t33;
        memset(&t33, 0, sizeof t33);
        F.l7 = t33;
        void * t34 = gm_l1;
        uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t34) + 8) + 8));
        gm_l8 = t35;
        gm_l10 = ((uint64_t)0ULL);
        uint64_t t36 = gm_l8;
        gm_l9 = t36;
        {
            for (;;) {
                {
                    uint64_t t37 = gm_l10;
                    uint64_t t38 = gm_l9;
                    uint8_t t39 = (t37 < t38);
                    if (!t39) {
                        goto gm_lb1;
                    }
                    {
                        {
                            {
                                {
                                    {
                                        uint64_t t40 = gm_l10;
                                        gm_l11 = t40;
                                        void * t41 = gm_l1;
                                        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 8));
                                        uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t41) + 8) + 8));
                                        uint64_t t44 = gm_l11;
                                        if ((uintptr_t)t44 >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)t44, t43, gm_src276s8986o4r3, 738, 15);
                                        void * t45 = (void *)(((uint8_t *)t42) + (uintptr_t)t44 * 144);
                                        gm_l12 = t45;
                                        uint8_t t46 = gm_l6;
                                        if (t46) {
                                            void * t47 = gm_l12;
                                            gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 128));
                                            gm_b16_8 t49;
                                            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s846hbv2ad5li));
                                            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(16));
                                            gm_t4gut2c9fj1kt t50;
                                            gm_f_error_s6hn871((void *)&t50, (void *)NULL, cx, NULL, t48, t49);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l13 = t50;
                                            {
                                                {
                                                    uint8_t t51 = (uint8_t)((((uint8_t *)&F.l7))[32] != 0);
                                                    if (!t51) {
                                                        goto gm_lb7;
                                                    }
                                                    memmove(((uint8_t *)&gm_l14), ((uint8_t *)&F.l7), 32);
                                                    void * t52 = (void *)((uint8_t *)&F.l13);
                                                    gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l14));
                                                    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 16));
                                                    gm_f_add_label_50fj7vv(t52, GM_CONV(gm_cseahvg8v7p0vs, t53), t54);
                                                    goto gm_lb6;
                                                }
gm_lb7:;
                                            }
gm_lb6:;
                                            void * t55 = gm_l0;
                                            void * t56 = GM_LD(void *, ((uint8_t *)t55));
                                            gm_b0_1 t57 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t56, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l13)));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb1;
                                        }
                                        {
                                            {
                                                void * t58 = gm_l12;
                                                uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t58) + 8) + 0));
                                                uint8_t t60 = (uint8_t)(t59 == ((uint8_t)7ULL));
                                                if (!t60) {
                                                    goto gm_lb9;
                                                }
                                                memmove(((uint8_t *)&F.l15), ((((uint8_t *)t58) + 8) + 8), 64);
                                                void * t61 = GM_LD(void *, (((((uint8_t *)t58) + 8) + 8) + 64));
                                                gm_l16 = t61;
                                                void * t62 = GM_LD(void *, (((((uint8_t *)t58) + 8) + 8) + 72));
                                                gm_l17 = t62;
                                                uint64_t t63 = gm_l11;
                                                uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                                if (o65) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 747, 29);
                                                uint64_t t66 = r64;
                                                uint64_t t67 = gm_l8;
                                                uint8_t t68 = (t66 < t67);
                                                if (t68) {
                                                    void * t69 = gm_l1;
                                                    void * t70 = GM_LD(void *, (((uint8_t *)t69) + 8));
                                                    uint64_t t71 = GM_LD(uint64_t, ((((uint8_t *)t69) + 8) + 8));
                                                    uint64_t t72 = gm_l11;
                                                    uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                                                    if (o74) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 747, 49);
                                                    uint64_t t75 = r73;
                                                    if ((uintptr_t)t75 >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)t75, t71, gm_src276s8986o4r3, 747, 41);
                                                    gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t70) + (uintptr_t)t75 * 144) + 128));
                                                    void * t77 = gm_l1;
                                                    uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)t77) + 24));
                                                    void * t79 = gm_l1;
                                                    uint32_t t80 = GM_LD(uint32_t, ((((uint8_t *)t79) + 24) + 8));
                                                    void * t81 = gm_l1;
                                                    uint32_t t82 = GM_LD(uint32_t, ((((uint8_t *)t81) + 24) + 8));
                                                    gm_tl874t2atnlh3 t83;
                                                    GM_ST(uint32_t, ((uint8_t *)&t83 + 0), t78);
                                                    GM_ST(uint32_t, ((uint8_t *)&t83 + 4), t80);
                                                    GM_ST(uint32_t, ((uint8_t *)&t83 + 8), t82);
                                                    gm_tl874t2atnlh3 t84 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t76), GM_CONV(gm_cseahvg8v7p0vs, t83)));
                                                    gm_l40 = t84;
                                                } else {
                                                    void * t85 = gm_l12;
                                                    uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)t85) + 128));
                                                    void * t87 = gm_l12;
                                                    uint32_t t88 = GM_LD(uint32_t, ((((uint8_t *)t87) + 128) + 8));
                                                    void * t89 = gm_l12;
                                                    uint32_t t90 = GM_LD(uint32_t, ((((uint8_t *)t89) + 128) + 8));
                                                    gm_tl874t2atnlh3 t91;
                                                    GM_ST(uint32_t, ((uint8_t *)&t91 + 0), t86);
                                                    GM_ST(uint32_t, ((uint8_t *)&t91 + 4), t88);
                                                    GM_ST(uint32_t, ((uint8_t *)&t91 + 8), t90);
                                                    gm_l40 = t91;
                                                }
                                                gm_tl874t2atnlh3 t92 = gm_l40;
                                                gm_l18 = t92;
                                                void * t93 = gm_l1;
                                                uint32_t t94 = GM_LD(uint32_t, ((uint8_t *)t93));
                                                void * t95 = gm_l1;
                                                void * t96 = GM_LD(void *, (((uint8_t *)t95) + 8));
                                                uint64_t t97 = GM_LD(uint64_t, ((((uint8_t *)t95) + 8) + 8));
                                                uint64_t t98 = gm_l11;
                                                uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                                                if (o100) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 748, 56);
                                                uint64_t t101 = r99;
                                                uint64_t t102 = (uintptr_t)t101;
                                                uint64_t t103 = (uintptr_t)t97;
                                                if (t102 > t103 || t103 > (uintptr_t)t97) gm_trap_slice(t102, t103, t97, gm_src276s8986o4r3, 748, 48);
                                                uint64_t t104 = ((uintptr_t)t97) - (uintptr_t)t101;
                                                gm_b16_8 t105;
                                                GM_ST(void *, (uint8_t *)&t105, (void *)(((uint8_t *)t96 + (uintptr_t)t101 * 144)));
                                                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(t104));
                                                gm_tl874t2atnlh3 t106 = gm_l18;
                                                gm_t79rbdv4rafe5 t107;
                                                GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t107 + 36, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t107 + 0), t94);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t107 + 8), t105);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t107 + 24), t106);
                                                F.l19 = t107;
                                                gm_trqfvmjtt4sij t108;
                                                gm_f_list_ir0h3tc((void *)&t108, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.l20 = t108;
                                                void * t109 = (void *)((uint8_t *)&F.l20);
                                                void * t110 = gm_l12;
                                                uint32_t t111 = GM_LD(uint32_t, ((uint8_t *)t110));
                                                void * t112 = gm_l17;
                                                void * t113 = gm_l17;
                                                gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t113) + 72));
                                                gm_tem8p0t66u1ce t115;
                                                memset(&t115, 0, sizeof t115);
                                                GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)2ULL));
                                                GM_ST(void *, ((uint8_t *)&t115 + 8), t112);
                                                gm_to88ae1b67aev t116;
                                                GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t116 + 140, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t116 + 0), t111);
                                                GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t116 + 8), t115);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t116 + 128), t114);
                                                gm_tftb1odc739t5 t117 = gm_f_push_d6c76ud(t109, &t116);
                                                gm_l41 = t117;
                                                uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                                if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_src276s8986o4r3, 750, 13);
                                                void * t119 = gm_l1;
                                                uint32_t t120 = GM_LD(uint32_t, ((uint8_t *)t119));
                                                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                void * t122 = gm_l17;
                                                gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 72));
                                                gm_t79rbdv4rafe5 t124;
                                                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t124 + 36, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t124 + 0), t120);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t124 + 8), t121);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t124 + 24), t123);
                                                F.l21 = t124;
                                                gm_trqfvmjtt4sij t125;
                                                gm_f_list_v5d1o9b((void *)&t125, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.l22 = t125;
                                                void * t126 = (void *)((uint8_t *)&F.l22);
                                                void * t127 = gm_l16;
                                                gm_t0d02142k4tu8 t128 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l15));
                                                gm_tuqaojgkqdcop t129;
                                                GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t129 + 0), t128);
                                                GM_ST(void *, ((uint8_t *)&t129 + 64), t127);
                                                gm_tq5j0547p8452 t130;
                                                GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_tuqaojgkqdcop, ((uint8_t *)&t130 + 8), t129);
                                                gm_tftb1odc739t5 t131 = gm_f_push_eb7kbhp(t126, &t130);
                                                gm_l42 = t131;
                                                uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                                if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src276s8986o4r3, 753, 13);
                                                gm_trqfvmjtt4sij t133;
                                                gm_f_list_taqq58r((void *)&t133, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.l23 = t133;
                                                void * t134 = (void *)((uint8_t *)&F.l23);
                                                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                                                gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l15) + 48));
                                                void * t137 = gm_l16;
                                                gm_tl874t2atnlh3 t138 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t137) + 72));
                                                gm_tl874t2atnlh3 t139 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t136), GM_CONV(gm_cseahvg8v7p0vs, t138)));
                                                gm_t08olq97rscn3 t140;
                                                GM_ST(uint32_t, (uint8_t *)&t140 + 28, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t140 + 0), t135);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t140 + 16), t139);
                                                gm_t79rbdv4rafe5 t141 = GM_LD(gm_t79rbdv4rafe5, ((uint8_t *)&F.l19));
                                                gm_tcuumjkg5eni6 t142;
                                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t142 + 0), t140);
                                                GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t142 + 32), t141);
                                                gm_tftb1odc739t5 t143 = gm_f_push_0ie7e7i(t134, &t142);
                                                gm_l43 = t143;
                                                uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                                if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_src276s8986o4r3, 755, 13);
                                                gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                                                gm_t79rbdv4rafe5 t146 = GM_LD(gm_t79rbdv4rafe5, ((uint8_t *)&F.l21));
                                                gm_ttja7ii61nila t147;
                                                GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t147 + 8), t146);
                                                gm_t9t5bnrf784qu t148;
                                                GM_ST(uint8_t, (uint8_t *)&t148 + 49, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t148 + 50, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t148 + 52, 0);
                                                GM_ST(gm_ttja7ii61nila, ((uint8_t *)&t148), t147);
                                                ((uint8_t *)&t148)[48] = 1;
                                                void * t149 = gm_l12;
                                                gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t149) + 128));
                                                gm_t99g4dkn8rrfb t151;
                                                GM_ST(uint32_t, (uint8_t *)&t151 + 84, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t151 + 0), t145);
                                                GM_ST(gm_t9t5bnrf784qu, ((uint8_t *)&t151 + 16), t148);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t151 + 72), t150);
                                                gm_tq7s98ihonkju t152 = gm_f_new_ulodmu9((void *)NULL, cx, NULL, &t151, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbeesj1a2or)));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l44 = t152;
                                                uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                                if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_src276s8986o4r3, 756, 24);
                                                void * t154 = GM_LD(void *, (((uint8_t *)&gm_l44) + 8));
                                                gm_l24 = t154;
                                                void * t155 = gm_l0;
                                                uint64_t t156 = GM_LD(uint64_t, ((((uint8_t *)t155) + 1200) + 8));
                                                gm_l25 = t156;
                                                void * t157 = gm_l0;
                                                void * t158 = gm_l24;
                                                uint8_t t159 = gm_l2;
                                                gm_tkn42u7na3t81 t160 = gm_l3;
                                                void * t161 = gm_f_if_expr_tiak47t((void *)NULL, cx, NULL, t157, t158, t159, t160);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l26 = t161;
                                                uint8_t t162 = gm_l2;
                                                uint8_t t163 = (uint8_t)!t162;
                                                if (t163) {
                                                    void * t164 = gm_l0;
                                                    uint64_t t165 = gm_l25;
                                                    void * t166 = (void *)((uint8_t *)&F.l4);
                                                    gm_b0_1 t167 = gm_f_push_temps_upm64pn((void *)NULL, cx, NULL, t164, t165, t166);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                void * t168 = gm_l0;
                                                void * t169 = GM_LD(void *, ((uint8_t *)t168));
                                                void * t170 = (void *)(((uint8_t *)t169) + 8);
                                                void * t171 = gm_l26;
                                                gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t171));
                                                gm_t53at80pfdus3 t173;
                                                gm_f_kind_jcnt4hm((void *)&t173, t170, GM_CONV(gm_csguankko4lmhd, t172));
                                                uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&t173) + 0));
                                                uint8_t t175 = (uint8_t)((t174 == ((uint8_t)5ULL)));
                                                gm_t53at80pfdus3 t176;
                                                memset(&t176, 0, sizeof t176);
                                                GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)5ULL));
                                                gm_l6 = t175;
                                                {
                                                    {
                                                        void * t177 = gm_l26;
                                                        uint8_t t178 = GM_LD(uint8_t, ((((uint8_t *)t177) + 8) + 0));
                                                        uint8_t t179 = (uint8_t)(t178 == ((uint8_t)47ULL));
                                                        if (!t179) {
                                                            goto gm_lb11;
                                                        }
                                                        memmove(((uint8_t *)&gm_l27), ((((uint8_t *)t177) + 8) + 8), 32);
                                                        {
                                                            {
                                                                uint8_t t180 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l27) + 24)) != 0);
                                                                if (!t180) {
                                                                    goto gm_lb13;
                                                                }
                                                                void * t181 = GM_LD(void *, (((uint8_t *)&gm_l27) + 24));
                                                                gm_l28 = t181;
                                                                {
                                                                    {
                                                                        void * t182 = gm_l28;
                                                                        uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t182) + 8) + 0));
                                                                        uint8_t t184 = (uint8_t)(t183 == ((uint8_t)32ULL));
                                                                        if (!t184) {
                                                                            goto gm_lb15;
                                                                        }
                                                                        gm_tmn7n0n9raou5 t185 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t182) + 8) + 8));
                                                                        gm_l29 = t185;
                                                                        void * t186 = GM_LD(void *, (((((uint8_t *)t182) + 8) + 8) + 8));
                                                                        gm_l30 = t186;
                                                                        gm_tmn7n0n9raou5 t187 = gm_l29;
                                                                        uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&t187) + 0));
                                                                        uint8_t t189 = (uint8_t)((t188 == ((uint8_t)5ULL)));
                                                                        if (!t189) {
                                                                            goto gm_lb15;
                                                                        }
                                                                        void * t190 = gm_l30;
                                                                        gm_t88rcucg1b7uk t191 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t190));
                                                                        gm_tmn7n0n9raou5 t192;
                                                                        GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                                                                        GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                                                                        GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)5ULL));
                                                                        gm_l45 = t191;
                                                                        goto gm_lb14;
                                                                    }
gm_lb15:;
                                                                    void * t193 = gm_l28;
                                                                    gm_t88rcucg1b7uk t194 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t193));
                                                                    gm_l45 = t194;
                                                                }
gm_lb14:;
                                                                gm_t88rcucg1b7uk t195 = gm_l45;
                                                                gm_l31 = t195;
                                                                {
                                                                    {
                                                                        void * t196 = gm_l0;
                                                                        void * t197 = GM_LD(void *, ((uint8_t *)t196));
                                                                        void * t198 = (void *)(((uint8_t *)t197) + 8);
                                                                        gm_t88rcucg1b7uk t199 = gm_l31;
                                                                        gm_t53at80pfdus3 t200;
                                                                        gm_f_kind_jcnt4hm((void *)&t200, t198, GM_CONV(gm_csguankko4lmhd, t199));
                                                                        uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t200) + 0));
                                                                        uint8_t t202 = (uint8_t)(!(t201 == ((uint8_t)5ULL)));
                                                                        if (!t202) {
                                                                            goto gm_lb17;
                                                                        }
                                                                        void * t203 = gm_l0;
                                                                        void * t204 = GM_LD(void *, ((uint8_t *)t203));
                                                                        void * t205 = (void *)(((uint8_t *)t204) + 8);
                                                                        void * t206 = gm_l28;
                                                                        gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t206));
                                                                        gm_t53at80pfdus3 t208;
                                                                        memset(&t208, 0, sizeof t208);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)5ULL));
                                                                        uint8_t t209 = gm_f_is_error_8k27gd1(t205, GM_CONV(gm_csguankko4lmhd, t207));
                                                                        uint8_t t210 = (uint8_t)!t209;
                                                                        if (!t210) {
                                                                            goto gm_lb17;
                                                                        }
                                                                        gm_l32 = ((uint64_t)0ULL);
                                                                        {
                                                                            for (;;) {
                                                                                uint64_t t211 = gm_l32;
                                                                                void * t212 = gm_l0;
                                                                                uint64_t t213 = GM_LD(uint64_t, ((((uint8_t *)t212) + 320) + 8));
                                                                                uint8_t t214 = (t211 < t213);
                                                                                if (!t214) {
                                                                                    goto gm_lb18;
                                                                                }
                                                                                {
                                                                                    {
                                                                                        {
                                                                                            void * t215 = gm_l0;
                                                                                            void * t216 = GM_LD(void *, (((uint8_t *)t215) + 320));
                                                                                            uint64_t t217 = GM_LD(uint64_t, ((((uint8_t *)t215) + 320) + 8));
                                                                                            uint64_t t218 = gm_l32;
                                                                                            if ((uintptr_t)t218 >= (uintptr_t)(t217)) gm_trap_index((uintptr_t)t218, t217, gm_src276s8986o4r3, 769, 60);
                                                                                            uint8_t t219 = GM_LD(uint8_t, ((((uint8_t *)t216) + (uintptr_t)t218 * 48) + 0));
                                                                                            uint8_t t220 = (uint8_t)(t219 == ((uint8_t)0ULL));
                                                                                            if (!t220) {
                                                                                                goto gm_lb22;
                                                                                            }
                                                                                            gm_tl874t2atnlh3 t221 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t216) + (uintptr_t)t218 * 48) + 8) + 4));
                                                                                            gm_l33 = t221;
                                                                                            gm_tl874t2atnlh3 t222 = gm_l33;
                                                                                            void * t223 = gm_l17;
                                                                                            gm_tl874t2atnlh3 t224 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t223) + 72));
                                                                                            uint8_t t225 = (gm_eqff85ac10o3(((uint8_t *)&t222), ((uint8_t *)&t224)));
                                                                                            if (!t225) {
                                                                                                goto gm_lb22;
                                                                                            }
                                                                                            void * t226 = gm_l0;
                                                                                            void * t227 = (void *)(((uint8_t *)t226) + 320);
                                                                                            uint64_t t228 = gm_l32;
                                                                                            gm_tpm220j5qhjke t229;
                                                                                            gm_f_remove_te9updc((void *)&t229, t227, t228);
                                                                                            goto gm_lb21;
                                                                                        }
gm_lb22:;
                                                                                        uint64_t t230 = gm_l32;
                                                                                        uint64_t r231; bool o232 = __builtin_add_overflow(t230, ((uint64_t)1ULL), &r231);
                                                                                        if (o232) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 772, 33);
                                                                                        uint64_t t233 = r231;
                                                                                        gm_l32 = t233;
                                                                                    }
gm_lb21:;
                                                                                }
gm_lb20:;
                                                                            }
                                                                        }
gm_lb18:;
                                                                        void * t234 = gm_l0;
                                                                        void * t235 = gm_l17;
                                                                        gm_tl874t2atnlh3 t236 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t235) + 72));
                                                                        gm_b16_8 t237;
                                                                        GM_ST(void *, (uint8_t *)&t237, (void *)(gm_sju4s3ndpnsge));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(106));
                                                                        gm_b0_1 t238 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t234, t236, t237);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_l6 = ((uint8_t)1);
                                                                        goto gm_lb16;
                                                                    }
gm_lb17:;
                                                                }
gm_lb16:;
                                                                goto gm_lb12;
                                                            }
gm_lb13:;
                                                        }
gm_lb12:;
                                                        goto gm_lb10;
                                                    }
gm_lb11:;
                                                }
gm_lb10:;
                                                uint8_t t239 = gm_l2;
                                                if (t239) {
                                                    void * t240 = gm_l26;
                                                    gm_l5 = t240;
                                                } else {
                                                    void * t241 = (void *)((uint8_t *)&F.l4);
                                                    void * t242 = gm_l26;
                                                    gm_trcmmd12cefb1 t243;
                                                    GM_ST(uint8_t, (uint8_t *)&t243 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t243 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t243 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t243 + 16, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t243 + 24, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t243 + 0), ((uint8_t)4ULL));
                                                    GM_ST(void *, ((uint8_t *)&t243 + 8), t242);
                                                    gm_tftb1odc739t5 t244 = gm_f_push_d6c76ud_2(t241, &t243);
                                                    gm_l46 = t244;
                                                    uint8_t t245 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                    if (!t245) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_src276s8986o4r3, 780, 43);
                                                }
                                                goto gm_lb1;
                                            }
gm_lb9:;
                                        }
gm_lb8:;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t246 = gm_l11;
                                                uint64_t r247; bool o248 = __builtin_add_overflow(t246, ((uint64_t)1ULL), &r247);
                                                if (o248) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 783, 12);
                                                uint64_t t249 = r247;
                                                uint64_t t250 = gm_l8;
                                                uint8_t t251 = ((t249 == t250));
                                                if (!t251) {
                                                    goto gm_lb24;
                                                }
                                                uint8_t t252 = gm_l2;
                                                if (!t252) {
                                                    goto gm_lb24;
                                                }
                                                {
                                                    {
                                                        void * t253 = gm_l12;
                                                        uint8_t t254 = GM_LD(uint8_t, ((((uint8_t *)t253) + 8) + 0));
                                                        uint8_t t255 = (uint8_t)(t254 == ((uint8_t)2ULL));
                                                        if (!t255) {
                                                            goto gm_lb26;
                                                        }
                                                        void * t256 = GM_LD(void *, ((((uint8_t *)t253) + 8) + 8));
                                                        gm_l34 = t256;
                                                        void * t257 = gm_l0;
                                                        void * t258 = gm_l34;
                                                        gm_tkn42u7na3t81 t259 = gm_l3;
                                                        void * t260 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t257, t258, t259);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l35 = t260;
                                                        void * t261 = gm_l0;
                                                        void * t262 = GM_LD(void *, ((uint8_t *)t261));
                                                        void * t263 = (void *)(((uint8_t *)t262) + 8);
                                                        void * t264 = gm_l35;
                                                        gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t264));
                                                        gm_t53at80pfdus3 t266;
                                                        gm_f_kind_jcnt4hm((void *)&t266, t263, GM_CONV(gm_csguankko4lmhd, t265));
                                                        uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&t266) + 0));
                                                        uint8_t t268 = (uint8_t)((t267 == ((uint8_t)5ULL)));
                                                        gm_t53at80pfdus3 t269;
                                                        memset(&t269, 0, sizeof t269);
                                                        GM_ST(uint8_t, ((uint8_t *)&t269 + 0), ((uint8_t)5ULL));
                                                        gm_l6 = t268;
                                                        void * t270 = gm_l35;
                                                        gm_l5 = t270;
                                                        goto gm_lb1;
                                                    }
gm_lb26:;
                                                }
gm_lb25:;
                                                goto gm_lb23;
                                            }
gm_lb24:;
                                        }
gm_lb23:;
                                        void * t271 = gm_l0;
                                        uint64_t t272 = GM_LD(uint64_t, ((((uint8_t *)t271) + 1200) + 8));
                                        gm_l36 = t272;
                                        void * t273 = gm_l0;
                                        void * t274 = gm_l12;
                                        gm_f_stmt_4145gsn((void *)&gm_l37, (void *)NULL, cx, NULL, t273, t274);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t275 = gm_l0;
                                        uint64_t t276 = gm_l36;
                                        void * t277 = (void *)((uint8_t *)&F.l4);
                                        gm_b0_1 t278 = gm_f_push_temps_upm64pn((void *)NULL, cx, NULL, t275, t276, t277);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t279 = gm_l0;
                                        void * t280 = GM_LD(void *, ((uint8_t *)t279));
                                        uint8_t t281 = gm_f_stmt_diverges_en1hv79(t280, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l37)));
                                        gm_l6 = t281;
                                        uint8_t t282 = gm_l6;
                                        if (t282) {
                                            void * t283 = gm_l0;
                                            void * t284 = GM_LD(void *, ((uint8_t *)t283));
                                            gm_trcmmd12cefb1 t285 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)&gm_l37));
                                            gm_f_stmt_never_cause_r2lf089((void *)&F.l7, t284, &t285);
                                        }
                                        void * t286 = (void *)((uint8_t *)&F.l4);
                                        gm_tftb1odc739t5 t287 = gm_f_push_d6c76ud_2(t286, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l37)));
                                        gm_l47 = t287;
                                    }
                                    {
                                        uint8_t t288 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                        if (!t288) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_src276s8986o4r3, 796, 9);
                                        goto gm_lb4;
                                    }
                                }
gm_lb5:;
                            }
                            {
                                gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 737, 5);
                            }
                        }
gm_lb4:;
                    }
gm_lb3:;
                }
                {
                    uint64_t t289 = gm_l10;
                    uint64_t r290; bool o291 = __builtin_add_overflow(t289, ((uint64_t)1ULL), &r290);
                    uint64_t t292 = r290;
                    gm_l10 = t292;
                }
            }
        }
gm_lb1:;
    }
    {
        void * t293 = gm_l0;
        uint64_t t294 = GM_LD(uint64_t, ((((uint8_t *)t293) + 1040) + 8));
        uint8_t t295 = (t294 > ((uint64_t)0ULL));
        if (t295) {
            void * t296 = gm_l0;
            gm_b0_1 t297 = gm_f_check_pending_handlers_o_sunakri((void *)NULL, cx, NULL, t296);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t298 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t298);
        {
            {
                uint8_t t299 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t299) {
                    goto gm_lb28;
                }
                void * t300 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l38 = t300;
                void * t301 = gm_l38;
                gm_t88rcucg1b7uk t302 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t301));
                gm_l48 = t302;
                goto gm_lb27;
            }
gm_lb28:;
            uint8_t t303 = gm_l6;
            if (t303) {
                void * t304 = gm_l0;
                void * t305 = GM_LD(void *, ((uint8_t *)t304));
                void * t306 = (void *)(((uint8_t *)t305) + 8);
                gm_t88rcucg1b7uk t307 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t306);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l49 = t307;
            } else {
                void * t308 = gm_l0;
                void * t309 = GM_LD(void *, ((uint8_t *)t308));
                void * t310 = (void *)(((uint8_t *)t309) + 8);
                gm_t88rcucg1b7uk t311 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t310);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l49 = t311;
            }
            gm_t88rcucg1b7uk t312 = gm_l49;
            gm_l48 = t312;
        }
gm_lb27:;
        gm_t88rcucg1b7uk t313 = gm_l48;
        gm_l39 = t313;
        gm_t88rcucg1b7uk t314 = gm_l39;
        gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        void * t316 = gm_l5;
        void * t317 = gm_l1;
        gm_tl874t2atnlh3 t318 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t317) + 24));
        void * t319 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t314, t315, t316, t318);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t319;
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
extern __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
extern __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_block_typed_e7svg7n F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_b16_8 gm_l1; /* stmts */
    void * gm_l2; /* tail */
    gm_tl874t2atnlh3 gm_l3; /* span */
    GM_STACK_CHECK(&F, 261u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tq7s98ihonkju gm_l4;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    void * t4 = gm_l2;
    gm_tl874t2atnlh3 t5 = gm_l3;
    gm_tgabaikr08mjg t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t3);
    GM_ST(void *, ((uint8_t *)&t6 + 16), t4);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 24), t5);
    gm_tq7s98ihonkju t7 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t6, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1oghq2gkp4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src276s8986o4r3, 988, 30);
    void * t9 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
    gm_tl874t2atnlh3 t10 = gm_l3;
    gm_t2b6tm8c0vgo1 t11;
    memset(&t11, 0, sizeof t11);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)46ULL));
    GM_ST(void *, ((uint8_t *)&t11 + 8), t9);
    void * t12 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t11, t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_bodies_qrmq5ll(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_bodies_qrmq5ll F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* i */
    gm_tf5srh4mp4b3i gm_l2; /* next */
    uint64_t gm_l3; /* k */
    gm_t5r2drn9pjqkt gm_l4; /* body */
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l5;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t8 = gm_l0;
                uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 424) + 8));
                gm_l1 = t9;
                gm_tf5srh4mp4b3i t10;
                memset(&t10, 0, sizeof t10);
                gm_l2 = t10;
                {
                    for (;;) {
                        uint64_t t11 = gm_l1;
                        uint8_t t12 = (t11 > ((uint64_t)0ULL));
                        if (!t12) {
                            goto gm_lb4;
                        }
                        {
                            uint64_t t13 = gm_l1;
                            uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
                            if (o15) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 359, 13);
                            uint64_t t16 = r14;
                            gm_l1 = t16;
                            void * t17 = gm_l0;
                            void * t18 = GM_LD(void *, ((uint8_t *)t17));
                            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 200));
                            uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t18) + 200) + 8));
                            void * t21 = gm_l0;
                            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 424));
                            uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 424) + 8));
                            uint64_t t24 = gm_l1;
                            if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_src276s8986o4r3, 360, 38);
                            uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t22) + (uintptr_t)t24 * 24) + 8));
                            if ((uintptr_t)t25 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t25, t20, gm_src276s8986o4r3, 360, 20);
                            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t19) + (uintptr_t)t25 * 184) + 112));
                            uint8_t t27 = (uint8_t)!t26;
                            if (t27) {
                                uint64_t t28 = gm_l1;
                                gm_tf5srh4mp4b3i t29;
                                GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t29), t28);
                                ((uint8_t *)&t29)[8] = 1;
                                gm_l2 = t29;
                                goto gm_lb4;
                            }
                        }
gm_lb6:;
                    }
                }
gm_lb4:;
                uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                if (t30) {
                    uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                    gm_l5 = t31;
                } else {
                    goto gm_lb1;
                }
                uint64_t t32 = gm_l5;
                gm_l3 = t32;
                void * t33 = gm_l0;
                void * t34 = (void *)(((uint8_t *)t33) + 424);
                uint64_t t35 = gm_l3;
                gm_f_remove_d75fgh2((void *)&gm_l4, t34, t35);
                void * t36 = gm_l0;
                uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                void * t39 = GM_LD(void *, (((uint8_t *)&gm_l4) + 16));
                gm_b0_1 t40 = gm_f_check_fn_body_kc8b374((void *)NULL, cx, NULL, t36, t37, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_body_kc8b374(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, void * a3) {
    struct gm_fr_check_fn_body_kc8b374 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* fn_id */
    uint64_t gm_l2; /* module */
    void * gm_l3; /* lit */
    gm_t66j1im9ip128 gm_l5; /* scope_len */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* g */
    uint8_t gm_l12; /* no_needs */
    gm_b16_8 gm_l13; /* e */
    uint32_t gm_l14; /* hi */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint32_t gm_l22; /* id */
    void * gm_l23; /* body */
    GM_STACK_CHECK(&F, 3042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_tftb1odc739t5 gm_l28;
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 344));
        uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 344) + 8));
        uint32_t t30 = gm_l1;
        uint64_t t31 = (uint64_t)t30;
        if ((uintptr_t)t31 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t31, t29, gm_src276s8986o4r3, 373, 12);
        memmove(((uint8_t *)&F.l4), (((uint8_t *)t28) + (uintptr_t)t31 * 136), 136);
        void * t32 = gm_l0;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_f_scope_type_params_2di4dm5((void *)&gm_l5, (void *)NULL, cx, NULL, t32, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t35;
        gm_f_list_go2qg0b((void *)&t35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t35;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 80));
        gm_l7 = t36;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t37 = gm_l8;
                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t39 = (t37 < t38);
                if (!t39) {
                    goto gm_lb2;
                }
                {
                    void * t40 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t42 = gm_l8;
                    {
                        {
                            uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)t40) + (uintptr_t)t42 * 4));
                            gm_l9 = t43;
                            void * t44 = gm_l0;
                            uint32_t t45 = gm_l9;
                            gm_t9qkh0rsooc8d t46;
                            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)2ULL));
                            uint8_t t47 = gm_f_param_has_2r09oc7(t44, t45, t46);
                            if (t47) {
                                void * t48 = (void *)((uint8_t *)&F.l6);
                                uint32_t t49 = gm_l9;
                                gm_tftb1odc739t5 t50 = gm_f_push_im9d4ro(t48, t49);
                                gm_l24 = t50;
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src276s8986o4r3, 378, 35);
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 377, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t52 = gm_l8;
                uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                uint64_t t55 = r53;
                gm_l8 = t55;
            }
        }
gm_lb2:;
        void * t56 = gm_l0;
        void * t57 = (void *)(((uint8_t *)t56) + 2240);
        uint32_t t58 = gm_l1;
        gm_tftb1odc739t5 t59 = gm_f_push_im9d4ro(t57, t58);
        gm_l25 = t59;
        uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src276s8986o4r3, 380, 5);
        void * t61 = gm_l0;
        gm_f_enter_module_scope_011vo5a((void *)&F.l10, (void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_ld26_1;
        }
        void * t62 = gm_l0;
        uint64_t t63 = gm_l2;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        gm_tnpifv20irnph t65 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 64));
        gm_tta5be0c75tfq t66;
        gm_f_fn_cx_hgvq15e((void *)&t66, (void *)NULL, cx, NULL, t62, t63, t64, t65);
        if (gm_unw.kind) {
            goto gm_ld26_2;
        }
        F.l11 = t66;
        uint32_t t67 = gm_l1;
        gm_t9805qtonbsag t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t68), t67);
        ((uint8_t *)&t68)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)&F.l11) + 400), t68);
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        GM_ST(gm_b16_8, ((((uint8_t *)&F.l11) + 96) + 40), t69);
        gm_l12 = ((uint8_t)0);
        {
            {
                void * t70 = gm_l3;
                uint8_t t71 = (uint8_t)(((((uint8_t *)t70) + 32))[16] != 0);
                if (!t71) {
                    goto gm_lb8;
                }
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)t70) + 32));
                gm_l13 = t72;
                uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint8_t t74 = ((t73 == ((uint64_t)0ULL)));
                gm_l12 = t74;
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
        uint8_t t75 = gm_l12;
        uint8_t t76 = (uint8_t)!t75;
        if (t76) {
            void * t77 = gm_l3;
            uint32_t t78 = GM_LD(uint32_t, ((((uint8_t *)t77) + 56) + 8));
            gm_l14 = t78;
            void * t79 = gm_l3;
            uint32_t t80 = GM_LD(uint32_t, (((uint8_t *)t79) + 56));
            uint32_t t81 = gm_l14;
            uint32_t t82 = gm_l14;
            gm_tl874t2atnlh3 t83;
            GM_ST(uint32_t, ((uint8_t *)&t83 + 0), t80);
            GM_ST(uint32_t, ((uint8_t *)&t83 + 4), t81);
            GM_ST(uint32_t, ((uint8_t *)&t83 + 8), t82);
            void * t84 = gm_l3;
            {
                {
                    uint8_t t85 = (uint8_t)(((((uint8_t *)t84) + 32))[16] != 0);
                    if (t85) {
                        goto gm_lb10;
                    }
                    gm_l26 = ((uint8_t)0);
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_l26 = ((uint8_t)1);
                    goto gm_lb9;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 392, 102);
            }
gm_lb9:;
            uint8_t t86 = gm_l26;
            gm_toceffb697p0u t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t87 + 0), t83);
            GM_ST(uint8_t, ((uint8_t *)&t87 + 12), t86);
            gm_trj4m8pa99qp7 t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 18, 0);
            GM_ST(gm_toceffb697p0u, ((uint8_t *)&t88), t87);
            ((uint8_t *)&t88)[16] = 1;
            GM_ST(gm_trj4m8pa99qp7, (((uint8_t *)&F.l11) + 408), t88);
        }
        void * t89 = gm_l0;
        gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)t89) + 2392));
        gm_l15 = t90;
        gm_l16 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t91 = gm_l16;
                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t93 = (t91 < t92);
                if (!t93) {
                    goto gm_lb12;
                }
                {
                    void * t94 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint64_t t96 = gm_l16;
                    {
                        {
                            memmove(((uint8_t *)&F.l17), (((uint8_t *)t94) + (uintptr_t)t96 * 40), 40);
                            uint32_t t97 = GM_LD(uint32_t, ((uint8_t *)&F.l17));
                            uint32_t t98 = gm_l1;
                            uint8_t t99 = ((t97 == t98));
                            if (t99) {
                                void * t100 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 8));
                                uint32_t t102 = gm_l1;
                                gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l17) + 8) + 16));
                                gm_t930q5l38hdap t104;
                                memset(&t104, 0, sizeof t104);
                                GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)3ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t104 + 8), t102);
                                gm_b0_1 t105 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t100, t101, &t104, t103);
                                if (gm_unw.kind) {
                                    goto gm_ld26_2;
                                }
                            }
                            goto gm_lb15;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 395, 5);
                    }
gm_lb15:;
                }
gm_lb14:;
                uint64_t t106 = gm_l16;
                uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                uint64_t t109 = r107;
                gm_l16 = t109;
            }
        }
gm_lb12:;
        gm_trqfvmjtt4sij t110;
        gm_f_list_go2qg0b((void *)&t110, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld26_2;
        }
        F.l18 = t110;
        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
        gm_l19 = t111;
        gm_l20 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t112 = gm_l20;
                uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint8_t t114 = (t112 < t113);
                if (!t114) {
                    goto gm_lb17;
                }
                {
                    void * t115 = GM_LD(void *, ((uint8_t *)&gm_l19));
                    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                    uint64_t t117 = gm_l20;
                    {
                        {
                            memmove(((uint8_t *)&F.l21), (((uint8_t *)t115) + (uintptr_t)t117 * 56), 56);
                            {
                                {
                                    uint8_t t118 = (uint8_t)(((((uint8_t *)&F.l21) + 40))[4] != 0);
                                    if (t118) {
                                        goto gm_lb23;
                                    }
                                    gm_l27 = ((uint8_t)0);
                                    goto gm_lb22;
                                }
gm_lb23:;
                                {
                                    gm_l27 = ((uint8_t)1);
                                    goto gm_lb22;
                                }
gm_lb24:;
                                gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 400, 12);
                            }
gm_lb22:;
                            uint8_t t119 = gm_l27;
                            if (t119) {
                                goto gm_lb19;
                            }
                            void * t120 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                            gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                            gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 20));
                            uint32_t t124 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t120, t121, t122, ((uint8_t)0), t123);
                            if (gm_unw.kind) {
                                goto gm_ld26_2;
                            }
                            gm_l22 = t124;
                            gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                            gm_b16_8 t126;
                            GM_ST(void *, (uint8_t *)&t126, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(1));
                            uint8_t t127 = (!gm_eq0iqiubfm7e(((uint8_t *)&t125), ((uint8_t *)&t126)));
                            if (t127) {
                                void * t128 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                                uint32_t t130 = gm_l22;
                                gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 20));
                                gm_t930q5l38hdap t132;
                                memset(&t132, 0, sizeof t132);
                                GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)0ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t132 + 8), t130);
                                gm_b0_1 t133 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t128, t129, &t132, t131);
                                if (gm_unw.kind) {
                                    goto gm_ld26_2;
                                }
                            }
                            void * t134 = (void *)((uint8_t *)&F.l18);
                            uint32_t t135 = gm_l22;
                            gm_tftb1odc739t5 t136 = gm_f_push_im9d4ro(t134, t135);
                            gm_l28 = t136;
                            uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                            if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_src276s8986o4r3, 403, 9);
                            goto gm_lb20;
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 399, 5);
                    }
gm_lb20:;
                }
gm_lb19:;
                uint64_t t138 = gm_l20;
                uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                uint64_t t141 = r139;
                gm_l20 = t141;
            }
        }
gm_lb17:;
        void * t142 = (void *)((uint8_t *)&F.l11);
        void * t143 = gm_l3;
        void * t144 = (void *)(((uint8_t *)t143) + 88);
        void * t145 = gm_f_fn_body_37q3l3a((void *)NULL, cx, NULL, t142, t144);
        if (gm_unw.kind) {
            goto gm_ld26_2;
        }
        gm_l23 = t145;
        void * t146 = (void *)((uint8_t *)&F.l11);
        void * t147 = gm_l23;
        gm_b0_1 t148 = gm_f_finish_m8t0599((void *)NULL, cx, NULL, t146, t147);
        if (gm_unw.kind) {
            goto gm_ld26_2;
        }
        void * t149 = gm_l0;
        gm_f_unscope_type_params_bofhhep(t149, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l5)));
        void * t150 = gm_l0;
        void * t151 = GM_LD(void *, (((uint8_t *)t150) + 384));
        uint64_t t152 = GM_LD(uint64_t, ((((uint8_t *)t150) + 384) + 8));
        uint32_t t153 = gm_l1;
        uint64_t t154 = (uint64_t)t153;
        if ((uintptr_t)t154 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t154, t152, gm_src276s8986o4r3, 408, 5);
        gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
        gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 16));
        void * t157 = gm_l23;
        gm_t9805qtonbsag t158;
        memset(&t158, 0, sizeof t158);
        gm_t9qkh0rsooc8d t159;
        GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)0ULL));
        gm_tdgfduqjheqdm t160;
        GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t160 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t160 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t160 + 60, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t160 + 0), t159);
        GM_ST(gm_b16_8, ((uint8_t *)&t160 + 8), t155);
        GM_ST(gm_b16_8, ((uint8_t *)&t160 + 24), t156);
        GM_ST(void *, ((uint8_t *)&t160 + 40), t157);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t160 + 48), t158);
        GM_ST(uint8_t, ((uint8_t *)&t160 + 56), ((uint8_t)0));
        GM_ST(gm_tdgfduqjheqdm, (((uint8_t *)t151) + (uintptr_t)t154 * 64), t160);
    }
    {
        void * t161 = gm_l0;
        gm_f_leave_module_scope_4hp5mc4(t161, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l10)));
    }
    {
        void * t162 = gm_l0;
        void * t163 = (void *)(((uint8_t *)t162) + 2240);
        gm_t9805qtonbsag t164 = gm_f_pop_570g79n(t163);
    }
    goto gm_ls165;
gm_ld26_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t166 = gm_l0;
        gm_f_leave_module_scope_4hp5mc4(t166, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l10)));
        gm_unw = gm_saved;
    }
gm_ld26_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t167 = gm_l0;
        void * t168 = (void *)(((uint8_t *)t167) + 2240);
        gm_t9805qtonbsag t169 = gm_f_pop_570g79n(t168);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls165:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cond_99ajlhk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_cond_99ajlhk F;
    void * gm_l0; /* cx */
    void * gm_l1; /* c */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l6; /* e */
    void * gm_l7; /* ex */
    void * gm_l9; /* value */
    GM_STACK_CHECK(&F, 435u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_trqfvmjtt4sij t10;
    gm_f_list_v5d1o9b_2((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t10;
    void * t11 = gm_l1;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
    gm_l3 = t12;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l4;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t18 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t16) + (uintptr_t)t18 * 80), 80);
                        {
                            {
                                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
                                if (!t20) {
                                    goto gm_lb7;
                                }
                                void * t21 = GM_LD(void *, (((uint8_t *)&F.l5) + 8));
                                gm_l6 = t21;
                                void * t22 = gm_l0;
                                void * t23 = gm_l6;
                                void * t24 = gm_l0;
                                void * t25 = GM_LD(void *, ((uint8_t *)t24));
                                void * t26 = (void *)(((uint8_t *)t25) + 8);
                                gm_t88rcucg1b7uk t27 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t26);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tkn42u7na3t81 t28;
                                GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28), t27);
                                ((uint8_t *)&t28)[4] = 1;
                                void * t29 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t22, t23, t28);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l7 = t29;
                                void * t30 = (void *)((uint8_t *)&F.l2);
                                void * t31 = gm_l0;
                                void * t32 = gm_l7;
                                void * t33 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t31, t32);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tlr9dp305htm7 t34;
                                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t34 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
                                GM_ST(void *, ((uint8_t *)&t34 + 8), t33);
                                gm_tftb1odc739t5 t35 = gm_f_push_eb7kbhp_2(t30, &t34);
                                gm_l10 = t35;
                                uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src276s8986o4r3, 930, 17);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t38 = (uint8_t)(t37 == ((uint8_t)0ULL));
                                if (!t38) {
                                    goto gm_lb8;
                                }
                                memmove(((uint8_t *)&F.l8), (((uint8_t *)&F.l5) + 8), 64);
                                void * t39 = GM_LD(void *, ((((uint8_t *)&F.l5) + 8) + 64));
                                gm_l9 = t39;
                                void * t40 = (void *)((uint8_t *)&F.l2);
                                void * t41 = gm_l0;
                                void * t42 = (void *)((uint8_t *)&F.l8);
                                void * t43 = gm_l9;
                                gm_tlr9dp305htm7 t44;
                                gm_f_binding_cond_v86td9t((void *)&t44, (void *)NULL, cx, NULL, t41, t42, t43);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t45 = gm_f_push_eb7kbhp_2(t40, &t44);
                                gm_l11 = t45;
                                uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_src276s8986o4r3, 932, 34);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 927, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 926, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l4;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l4 = t50;
        }
    }
gm_lb1:;
    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_declare_3l5mhoj F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    gm_tl874t2atnlh3 gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 3405u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    uint8_t gm_l10;
    gm_tqvu78rpi6q1h gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_b16_8 t12 = gm_l1;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sfdbfrv46lihd));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(1));
    uint8_t t14 = (gm_eq0iqiubfm7e(((uint8_t *)&t12), ((uint8_t *)&t13)));
    if (t14) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t15 = gm_l0;
    gm_b16_8 t16 = gm_l1;
    gm_f_lookup_local_6f38t7p((void *)&gm_l11, t15, t16);
    {
        {
            uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l11))[208] != 0);
            if (t17) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 511, 8);
    }
gm_lb1:;
    uint8_t t18 = gm_l10;
    if (t18) {
        void * t19 = gm_l0;
        gm_tl874t2atnlh3 t20 = gm_l3;
        gm_b16_8 t21 = gm_l1;
        gm_tnpifv20irnph t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
        gm_f_format_q32bjcv((void *)&gm_l12, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src276s8986o4r3, 512, 24);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_b0_1 t26 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t19, t20, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        {
            {
                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                uint8_t t28 = (uint8_t)(!(t27 == ((uint8_t)2ULL)));
                if (!t28) {
                    goto gm_lb5;
                }
                void * t29 = gm_l0;
                void * t30 = GM_LD(void *, ((uint8_t *)t29));
                void * t31 = gm_l0;
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 8));
                gm_b16_8 t33 = gm_l1;
                gm_t930q5l38hdap t34 = GM_LD(gm_t930q5l38hdap, ((uint8_t *)&F.l2));
                gm_t930q5l38hdap t35;
                memset(&t35, 0, sizeof t35);
                GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)2ULL));
                uint8_t t36 = gm_f_names_type_rjdgib9((void *)NULL, cx, NULL, t30, t32, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t36) {
                    goto gm_lb5;
                }
                void * t37 = gm_l0;
                gm_tl874t2atnlh3 t38 = gm_l3;
                gm_b16_8 t39 = gm_l1;
                gm_tnpifv20irnph t40;
                GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
                gm_f_format_p8e3d4v((void *)&gm_l13, (void *)NULL, cx, NULL, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src276s8986o4r3, 514, 24);
                gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                gm_b0_1 t44 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t37, t38, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb4;
            }
gm_lb5:;
            gm_b16_8 t45 = gm_l1;
            uint8_t t46 = gm_f_is_reserved_type_name_tuq4qsj(t45);
            if (t46) {
                void * t47 = gm_l0;
                gm_tl874t2atnlh3 t48 = gm_l3;
                gm_b16_8 t49 = gm_l1;
                gm_tnpifv20irnph t50;
                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t49);
                gm_f_format_9i45r57((void *)&gm_l14, (void *)NULL, cx, NULL, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src276s8986o4r3, 516, 24);
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                gm_b0_1 t54 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t47, t48, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
gm_lb4:;
    }
    {
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)1ULL));
            if (!t56) {
                goto gm_lb7;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)&F.l2) + 8), 200);
            void * t57 = gm_l0;
            void * t58 = GM_LD(void *, ((uint8_t *)t57));
            void * t59 = (void *)(((uint8_t *)t58) + 1688);
            gm_b16_8 t60 = gm_l1;
            gm_t0ialelq3ql1k t61 = GM_LD(gm_t0ialelq3ql1k, ((uint8_t *)&F.l4));
            gm_t1hofbajv6dns t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t62 + 8), t61);
            gm_tb07pp6njg0bk t63;
            GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t60);
            GM_ST(gm_t1hofbajv6dns, ((uint8_t *)&t63 + 16), t62);
            gm_tftb1odc739t5 t64 = gm_f_push_ola4v1l(t59, &t63);
            gm_l15 = t64;
            uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src276s8986o4r3, 521, 24);
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)4ULL));
            if (!t67) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&F.l5), (((uint8_t *)&F.l2) + 8), 40);
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, ((uint8_t *)t68));
            void * t70 = (void *)(((uint8_t *)t69) + 1688);
            gm_b16_8 t71 = gm_l1;
            memmove(((uint8_t *)&F.l6), ((uint8_t *)&F.l5), 40);
            gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l6));
            gm_l7 = t72;
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
            gm_l8 = t73;
            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 24));
            gm_l9 = t74;
            gm_t88rcucg1b7uk t75 = gm_l7;
            gm_b16_8 t76 = gm_l8;
            gm_tl874t2atnlh3 t77 = gm_l9;
            gm_ts78ca2amthgu t78;
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 36, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t78 + 0), t75);
            GM_ST(gm_b16_8, ((uint8_t *)&t78 + 8), t76);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t78 + 24), t77);
            gm_t1hofbajv6dns t79;
            memset(&t79, 0, sizeof t79);
            GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)1ULL));
            GM_ST(gm_ts78ca2amthgu, ((uint8_t *)&t79 + 8), t78);
            gm_tb07pp6njg0bk t80;
            GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t71);
            GM_ST(gm_t1hofbajv6dns, ((uint8_t *)&t80 + 16), t79);
            gm_tftb1odc739t5 t81 = gm_f_push_ola4v1l(t70, &t80);
            gm_l16 = t81;
            uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src276s8986o4r3, 522, 22);
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)0ULL));
            if (!t84) {
                goto gm_lb9;
            }
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, ((uint8_t *)t85));
            void * t87 = (void *)(((uint8_t *)t86) + 1688);
            gm_b16_8 t88 = gm_l1;
            gm_t1hofbajv6dns t89;
            memset(&t89, 0, sizeof t89);
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)2ULL));
            gm_tb07pp6njg0bk t90;
            GM_ST(gm_b16_8, ((uint8_t *)&t90 + 0), t88);
            GM_ST(gm_t1hofbajv6dns, ((uint8_t *)&t90 + 16), t89);
            gm_tftb1odc739t5 t91 = gm_f_push_ola4v1l(t87, &t90);
            gm_l17 = t91;
            uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src276s8986o4r3, 523, 20);
            goto gm_lb6;
        }
gm_lb9:;
        {
            goto gm_lb6;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 520, 5);
    }
gm_lb6:;
    void * t93 = gm_l0;
    void * t94 = GM_LD(void *, (((uint8_t *)t93) + 56));
    uint64_t t95 = GM_LD(uint64_t, ((((uint8_t *)t93) + 56) + 8));
    void * t96 = gm_l0;
    uint64_t t97 = GM_LD(uint64_t, ((((uint8_t *)t96) + 56) + 8));
    uint64_t r98; bool o99 = __builtin_sub_overflow(t97, ((uint64_t)1ULL), &r98);
    if (o99) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 526, 21);
    uint64_t t100 = r98;
    if ((uintptr_t)t100 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t100, t95, gm_src276s8986o4r3, 526, 5);
    void * t101 = (void *)(((uint8_t *)t94) + (uintptr_t)t100 * 40);
    gm_b16_8 t102 = gm_l1;
    gm_t930q5l38hdap t103 = GM_LD(gm_t930q5l38hdap, ((uint8_t *)&F.l2));
    gm_tmhicb6huo2ra t104;
    GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t102);
    GM_ST(gm_t930q5l38hdap, ((uint8_t *)&t104 + 16), t103);
    gm_tftb1odc739t5 t105 = gm_f_push_nr0n00f(t101, &t104);
    gm_l18 = t105;
    uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src276s8986o4r3, 526, 5);
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
inline __attribute__((aligned(4))) void * gm_f_declare_temps_a7fc14v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_declare_temps_a7fc14v F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* mark */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1200) + 8));
    uint64_t t4 = gm_l1;
    uint8_t t5 = (t3 <= t4);
    if (t5) {
        void * t6 = gm_l2;
        F.ret = t6;
        goto gm_fret;
    }
    gm_trqfvmjtt4sij t7;
    gm_f_list_ir0h3tc_2((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    void * t8 = gm_l0;
    uint64_t t9 = gm_l1;
    void * t10 = (void *)((uint8_t *)&F.l3);
    gm_b0_1 t11 = gm_f_push_temps_upm64pn((void *)NULL, cx, NULL, t8, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = gm_l2;
    gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t12));
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    void * t15 = gm_l2;
    void * t16 = gm_l2;
    gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
    void * t18 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t13, t14, t15, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
__attribute__((aligned(4))) void gm_f_defer_stmt_hm15mru(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2) {
    struct gm_fr_defer_stmt_hm15mru F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* is_err */
    void * gm_l2; /* body */
    uint8_t gm_l3; /* was */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* performed_base */
    gm_trcmmd12cefb1 gm_l6; /* inner */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l11; /* exits */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l15; /* e */
    void * gm_l17; /* x */
    GM_STACK_CHECK(&F, 863u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l18;
    void * gm_l19;
    gm_tftb1odc739t5 gm_l20;
    void * t15 = gm_l0;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 520));
    gm_l3 = t16;
    void * t17 = gm_l0;
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 528));
    gm_l4 = t18;
    void * t19 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t19) + 520), ((uint8_t)1));
    void * t20 = gm_l0;
    void * t21 = gm_l0;
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t21) + 232) + 8));
    GM_ST(uint64_t, (((uint8_t *)t20) + 528), t22);
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t23) + 192) + 8));
    gm_l5 = t24;
    void * t25 = gm_l0;
    gm_b0_1 t26 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t27 = gm_l0;
    void * t28 = gm_l2;
    gm_f_stmt_4145gsn((void *)&gm_l6, (void *)NULL, cx, NULL, t27, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t29 = gm_l0;
    gm_f_pop_scope_g5j7o4e(t29);
    uint64_t t30 = gm_l5;
    gm_l8 = t30;
    void * t31 = gm_l0;
    uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t31) + 192) + 8));
    gm_l7 = t32;
    {
        for (;;) {
            uint64_t t33 = gm_l8;
            uint64_t t34 = gm_l7;
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t36 = gm_l8;
                        gm_l9 = t36;
                        void * t37 = gm_l0;
                        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 192));
                        uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 192) + 8));
                        uint64_t t40 = gm_l9;
                        if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_src276s8986o4r3, 850, 14);
                        memmove(((uint8_t *)&F.l10), (((uint8_t *)t38) + (uintptr_t)t40 * 40), 40);
                        gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 24));
                        void * t42 = gm_l0;
                        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 536));
                        uint8_t t44 = 0;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t43) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t43))) + i * 12, ((uint8_t *)&t41))) { t44 = 1; break; }
                        uint8_t t45 = (uint8_t)(t44);
                        if (t45) {
                            goto gm_lb3;
                        }
                        gm_l11 = ((uint8_t)0);
                        void * t46 = gm_l0;
                        void * t47 = GM_LD(void *, ((uint8_t *)t46));
                        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 1048));
                        uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)t47) + 1048) + 8));
                        uint32_t t50 = GM_LD(uint32_t, ((uint8_t *)&F.l10));
                        uint64_t t51 = (uint64_t)t50;
                        if ((uintptr_t)t51 >= (uintptr_t)(t49)) gm_trap_index((uintptr_t)t51, t49, gm_src276s8986o4r3, 853, 18);
                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)t48) + (uintptr_t)t51 * 56) + 40));
                        gm_l12 = t52;
                        gm_l13 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t53 = gm_l13;
                                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint8_t t55 = (t53 < t54);
                                if (!t55) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t56 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint64_t t58 = gm_l13;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l14), (((uint8_t *)t56) + (uintptr_t)t58 * 48), 48);
                                            gm_t9qkh0rsooc8d t59 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l14) + 16));
                                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
                                            uint8_t t61 = (uint8_t)(!(t60 == ((uint8_t)0ULL)));
                                            gm_t9qkh0rsooc8d t62;
                                            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
                                            if (t61) {
                                                gm_l11 = ((uint8_t)1);
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 853, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t63 = gm_l13;
                                uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                uint64_t t66 = r64;
                                gm_l13 = t66;
                            }
                        }
gm_lb6:;
                        uint8_t t67 = gm_l11;
                        if (t67) {
                            void * t68 = gm_l0;
                            gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 24));
                            void * t70 = gm_l0;
                            void * t71 = GM_LD(void *, ((uint8_t *)t70));
                            void * t72 = (void *)(((uint8_t *)t71) + 8);
                            void * t73 = (void *)((uint8_t *)&F.l10);
                            gm_b16_8 t74 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t72, t73);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t75;
                            GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t74);
                            gm_f_format_n7840ue((void *)&gm_l18, (void *)NULL, cx, NULL, t75);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                            if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src276s8986o4r3, 857, 30);
                            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                            gm_b0_1 t79 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t68, t69, t78);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 849, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t80 = gm_l8;
            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
            uint64_t t83 = r81;
            gm_l8 = t83;
        }
    }
gm_lb1:;
    void * t84 = gm_l0;
    uint8_t t85 = gm_l3;
    GM_ST(uint8_t, (((uint8_t *)t84) + 520), t85);
    void * t86 = gm_l0;
    uint64_t t87 = gm_l4;
    GM_ST(uint64_t, (((uint8_t *)t86) + 528), t87);
    {
        {
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)4ULL));
            if (!t89) {
                goto gm_lb12;
            }
            void * t90 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
            gm_l15 = t90;
            void * t91 = gm_l15;
            gm_l19 = t91;
            goto gm_lb11;
        }
gm_lb12:;
        {
            gm_trqfvmjtt4sij t92;
            gm_f_list_ir0h3tc_2((void *)&t92, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l16 = t92;
            void * t93 = (void *)((uint8_t *)&F.l16);
            gm_tftb1odc739t5 t94 = gm_f_push_d6c76ud_2(t93, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l6)));
            gm_l20 = t94;
            uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src276s8986o4r3, 866, 13);
            void * t96 = gm_l0;
            void * t97 = GM_LD(void *, ((uint8_t *)t96));
            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
            void * t99 = gm_l2;
            gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t99) + 128));
            void * t101 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t97, t98, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t101;
            goto gm_lb11;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 862, 10);
    }
gm_lb11:;
    void * t102 = gm_l19;
    gm_l17 = t102;
    uint8_t t103 = gm_l1;
    void * t104 = gm_l17;
    gm_tgtub0v94d5u5 t105;
    GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), t103);
    GM_ST(void *, ((uint8_t *)&t105 + 8), t104);
    gm_trcmmd12cefb1 t106;
    GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t106 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)5ULL));
    GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t106 + 8), t105);
    memmove(gm_out, ((uint8_t *)&t106), 32);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
inline __attribute__((aligned(4))) void gm_f_early_fallback_error_4lq49mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_early_fallback_error_4lq49mb F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* settled_at */
    gm_b16_8 gm_l4; /* t */
    GM_STACK_CHECK(&F, 474u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t4;
    gm_tl874t2atnlh3 t5 = gm_l1;
    gm_b16_8 t6 = gm_l4;
    gm_tnpifv20irnph t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
    gm_f_format_07fj847((void *)&gm_l6, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src276s8986o4r3, 994, 27);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    gm_t4gut2c9fj1kt t11;
    gm_f_error_s6hn871((void *)&t11, (void *)NULL, cx, NULL, t5, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t11;
    void * t12 = (void *)((uint8_t *)&F.l5);
    gm_tl874t2atnlh3 t13 = gm_l3;
    gm_b16_8 t14 = gm_l4;
    gm_tnpifv20irnph t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
    gm_f_format_r1s7qdu((void *)&gm_l7, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src276s8986o4r3, 995, 29);
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
    gm_f_add_label_50fj7vv(t12, GM_CONV(gm_cseahvg8v7p0vs, t13), t18);
    memmove(gm_out, ((uint8_t *)&F.l5), 184);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3) {
    struct gm_fr_enter_fn_vo5kel1 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* fn_id */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_tnpifv20irnph gm_l3; /* declared */
    GM_STACK_CHECK(&F, 933u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 576);
    void * t4 = gm_l0;
    gm_tjhr0bdm69ios t5;
    gm_f_save_frame_8libfct((void *)&t5, t4);
    gm_tftb1odc739t5 t6 = gm_f_push_8h1r7s6_2(t3, &t5);
    gm_l4 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src276s8986o4r3, 579, 5);
    void * t8 = gm_l0;
    gm_trqfvmjtt4sij t9;
    gm_f_list_go2qg0b((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t8) + 1200), t9);
    void * t10 = gm_l0;
    gm_trqfvmjtt4sij t11;
    gm_f_list_084fcd6((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t10) + 1000), t11);
    void * t12 = gm_l0;
    gm_t403eokj7ifg9 t13;
    memset(&t13, 0, sizeof t13);
    GM_ST(gm_t403eokj7ifg9, (((uint8_t *)t12) + 800), t13);
    void * t14 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t14) + 744), ((uint8_t)0));
    void * t15 = gm_l0;
    gm_tkn42u7na3t81 t16;
    memset(&t16, 0, sizeof t16);
    GM_ST(gm_tkn42u7na3t81, (((uint8_t *)t15) + 748), t16);
    void * t17 = gm_l0;
    gm_trqfvmjtt4sij t18;
    gm_f_list_p9s2t99((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t17) + 16), t18);
    void * t19 = gm_l0;
    gm_t88rcucg1b7uk t20 = gm_l2;
    GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 168), t20);
    void * t21 = gm_l0;
    gm_tnpifv20irnph t22 = gm_l3;
    GM_ST(gm_tnpifv20irnph, (((uint8_t *)t21) + 176), t22);
    void * t23 = gm_l0;
    gm_trqfvmjtt4sij t24;
    gm_f_list_7ltkg0u((void *)&t24, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t23) + 192), t24);
    void * t25 = gm_l0;
    gm_trqfvmjtt4sij t26;
    gm_f_list_lgereu6((void *)&t26, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t25) + 232), t26);
    void * t27 = gm_l0;
    GM_ST(uint32_t, (((uint8_t *)t27) + 272), ((uint32_t)0ULL));
    void * t28 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t28) + 520), ((uint8_t)0));
    void * t29 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t29) + 528), ((uint64_t)0ULL));
    void * t30 = gm_l0;
    uint32_t t31 = gm_l1;
    gm_t9805qtonbsag t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t32), t31);
    ((uint8_t *)&t32)[4] = 1;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t30) + 400), t32);
    void * t33 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t33) + 696), ((uint8_t)0));
    void * t34 = gm_l0;
    gm_b0_1 t35 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t34);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exit_fn_kj6vbu8 F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 1011u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_ta7fvse6v13pf gm_l3;
    gm_tjhr0bdm69ios gm_l4;
    void * t2 = gm_l0;
    gm_f_pop_scope_g5j7o4e(t2);
    void * t3 = gm_l0;
    void * t4 = (void *)(((uint8_t *)t3) + 576);
    gm_f_pop_f3uajlg_2((void *)&gm_l3, t4);
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[384] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l3), 384);
    } else {
        void * t6 = gm_l0;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 192));
        void * t10 = gm_l0;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 696));
        gm_tk9n3vlila9qg t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t7);
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 16), t9);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 32), t11);
        memmove(gm_out, ((uint8_t *)&t12), 40);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l1), ((uint8_t *)&gm_l4), 384);
    void * t13 = gm_l0;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 16));
    void * t15 = gm_l0;
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 192));
    void * t17 = gm_l0;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t17) + 696));
    gm_tk9n3vlila9qg t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t19 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t19 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t16);
    GM_ST(uint8_t, ((uint8_t *)&t19 + 32), t18);
    F.l2 = t19;
    void * t20 = gm_l0;
    gm_f_load_frame_r6k3kmp(t20, &(*(gm_tjhr0bdm69ios *)((uint8_t *)&F.l1)));
    memmove(gm_out, ((uint8_t *)&F.l2), 40);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tk9n3vlila9qg gm_f_exit_fn_kj6vbu8_c(void * a0) { gm_tk9n3vlila9qg gm_r; gm_f_exit_fn_kj6vbu8(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expect_bool_cdo5g1q F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint8_t gm_l4; /* mask */
    gm_tlcm4lvu3g1a8 gm_l5; /* p */
    gm_b16_8 gm_l6; /* hint */
    GM_STACK_CHECK(&F, 827u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tb7u19loo3j87 gm_l7;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    void * t9 = gm_l1;
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
    gm_f_kind_jcnt4hm((void *)&F.l2, t8, GM_CONV(gm_csguankko4lmhd, t10));
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t12 = (uint8_t)(!(t11 == ((uint8_t)4ULL)));
            if (!t12) {
                goto gm_lb2;
            }
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t14 = (uint8_t)(!(t13 == ((uint8_t)5ULL)));
            if (!t14) {
                goto gm_lb2;
            }
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t16 = (uint8_t)(!(t15 == ((uint8_t)26ULL)));
            if (!t16) {
                goto gm_lb2;
            }
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 96);
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, ((uint8_t *)t19));
            void * t21 = gm_l1;
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t21));
            gm_t53at80pfdus3 t23 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)&F.l2));
            gm_t53at80pfdus3 t24;
            memset(&t24, 0, sizeof t24);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)4ULL));
            gm_t53at80pfdus3 t25 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)&F.l2));
            gm_t53at80pfdus3 t26;
            memset(&t26, 0, sizeof t26);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)5ULL));
            gm_t53at80pfdus3 t27 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)&F.l2));
            gm_t53at80pfdus3 t28;
            memset(&t28, 0, sizeof t28);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)26ULL));
            gm_t88rcucg1b7uk t29 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t29;
            gm_l4 = ((uint8_t)0);
            {
                {
                    void * t30 = gm_l0;
                    void * t31 = GM_LD(void *, ((uint8_t *)t30));
                    void * t32 = (void *)(((uint8_t *)t31) + 8);
                    gm_t88rcucg1b7uk t33 = gm_l3;
                    gm_f_vector_parts_crrfopm((void *)&gm_l7, t32, GM_CONV(gm_csguankko4lmhd, t33));
                    uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
                    if (!t34) {
                        goto gm_lb4;
                    }
                    gm_tlcm4lvu3g1a8 t35 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l7));
                    gm_l5 = t35;
                    void * t36 = gm_l0;
                    void * t37 = GM_LD(void *, ((uint8_t *)t36));
                    void * t38 = (void *)(((uint8_t *)t37) + 8);
                    gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t40 = gm_f_is_bool_ajhjgeg(t38, GM_CONV(gm_csguankko4lmhd, t39));
                    gm_l4 = t40;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            uint8_t t41 = gm_l4;
            if (t41) {
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s069kf53uhd4o));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(44));
                gm_l8 = t42;
            } else {
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_she92i1u1a4v5));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(29));
                gm_l8 = t43;
            }
            gm_b16_8 t44 = gm_l8;
            gm_l6 = t44;
            void * t45 = gm_l0;
            void * t46 = gm_l1;
            gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 72));
            void * t48 = gm_l0;
            gm_t88rcucg1b7uk t49 = gm_l3;
            gm_b16_8 t50 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t51 = gm_l6;
            gm_t6j6smj7qoctu t52;
            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t50);
            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 16), t51);
            gm_f_format_biupho2((void *)&gm_l9, (void *)NULL, cx, NULL, &t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src276s8986o4r3, 945, 26);
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
            gm_b0_1 t56 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t45, t47, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t57 = gm_l1;
    F.ret = t57;
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
__attribute__((aligned(4))) void * gm_f_finish_block_body_4vol3tv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_finish_block_body_4vol3tv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* block */
    void * gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ok */
    gm_tl874t2atnlh3 gm_l6; /* span */
    void * gm_l7; /* success */
    GM_STACK_CHECK(&F, 762u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 168));
    gm_l3 = t7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_l4 = t10;
    {
        {
            void * t11 = gm_l4;
            gm_t88rcucg1b7uk t12 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l9, t11, GM_CONV(gm_csguankko4lmhd, t12));
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)14ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l9) + 8) + 4));
            gm_l5 = t15;
            void * t16 = gm_l4;
            gm_t88rcucg1b7uk t17 = gm_l5;
            uint8_t t18 = gm_f_is_unit_n83ttnb(t16, GM_CONV(gm_csguankko4lmhd, t17));
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = gm_l4;
            void * t20 = gm_l1;
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t20));
            gm_t53at80pfdus3 t22;
            gm_f_kind_jcnt4hm((void *)&t22, t19, GM_CONV(gm_csguankko4lmhd, t21));
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            uint8_t t24 = (uint8_t)(!(t23 == ((uint8_t)5ULL)));
            if (!t24) {
                goto gm_lb2;
            }
            void * t25 = gm_l2;
            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 24));
            void * t27 = gm_l2;
            uint32_t t28 = GM_LD(uint32_t, ((((uint8_t *)t27) + 24) + 8));
            void * t29 = gm_l2;
            uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)t29) + 24) + 8));
            gm_tl874t2atnlh3 t31;
            GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t26);
            GM_ST(uint32_t, ((uint8_t *)&t31 + 4), t28);
            GM_ST(uint32_t, ((uint8_t *)&t31 + 8), t30);
            gm_t53at80pfdus3 t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)5ULL));
            gm_l6 = t31;
            void * t33 = gm_l0;
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, ((uint8_t *)t34));
            gm_tl874t2atnlh3 t36 = gm_l6;
            void * t37 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t38 = gm_l3;
            void * t39 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t33, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t39;
            gm_trqfvmjtt4sij t40;
            gm_f_list_ir0h3tc_2((void *)&t40, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l8 = t40;
            void * t41 = (void *)((uint8_t *)&F.l8);
            void * t42 = gm_l1;
            gm_trcmmd12cefb1 t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
            gm_tftb1odc739t5 t44 = gm_f_push_d6c76ud_2(t41, &t43);
            gm_l10 = t44;
            uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src276s8986o4r3, 699, 9);
            gm_t88rcucg1b7uk t46 = gm_l3;
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
            void * t48 = gm_l7;
            void * t49 = gm_l2;
            gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 24));
            void * t51 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t46, t47, t48, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t51;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t52 = gm_l4;
            gm_t88rcucg1b7uk t53 = gm_l3;
            uint8_t t54 = gm_f_is_unit_n83ttnb(t52, GM_CONV(gm_csguankko4lmhd, t53));
            uint8_t t55 = (uint8_t)!t54;
            if (!t55) {
                goto gm_lb4;
            }
            void * t56 = gm_l4;
            void * t57 = gm_l1;
            gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t57));
            gm_t53at80pfdus3 t59;
            gm_f_kind_jcnt4hm((void *)&t59, t56, GM_CONV(gm_csguankko4lmhd, t58));
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
            uint8_t t61 = (uint8_t)(!(t60 == ((uint8_t)5ULL)));
            if (!t61) {
                goto gm_lb4;
            }
            void * t62 = gm_l0;
            void * t63 = gm_l2;
            gm_t88rcucg1b7uk t64 = gm_l3;
            gm_t53at80pfdus3 t65;
            memset(&t65, 0, sizeof t65);
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)5ULL));
            gm_b0_1 t66 = gm_f_missing_return_s12s32d((void *)NULL, cx, NULL, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t67 = gm_l1;
    F.ret = t67;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_finish_m8t0599 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l8; /* root */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    void * gm_l12; /* sig */
    void * gm_l13; /* def */
    GM_STACK_CHECK(&F, 825u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l14;
    void * t19 = gm_l0;
    void * t20 = gm_l1;
    gm_b0_1 t21 = gm_f_finalize_k1g9q48((void *)NULL, cx, NULL, t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t22 = gm_l0;
    gm_b0_1 t23 = gm_f_check_effects_mrg0sal((void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = ((uint64_t)0ULL);
    void * t24 = gm_l0;
    uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t24) + 16) + 8));
    gm_l2 = t25;
    {
        for (;;) {
            uint64_t t26 = gm_l3;
            uint64_t t27 = gm_l2;
            uint8_t t28 = (t26 < t27);
            if (!t28) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t29 = gm_l3;
                        gm_l4 = t29;
                        void * t30 = gm_l0;
                        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 16));
                        uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t30) + 16) + 8));
                        uint64_t t33 = gm_l4;
                        if ((uintptr_t)t33 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t33, t32, gm_src276s8986o4r3, 417, 9);
                        void * t34 = gm_l0;
                        void * t35 = (void *)(((uint8_t *)t34) + 96);
                        void * t36 = gm_l0;
                        void * t37 = GM_LD(void *, ((uint8_t *)t36));
                        void * t38 = gm_l0;
                        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 16));
                        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t38) + 16) + 8));
                        uint64_t t41 = gm_l4;
                        if ((uintptr_t)t41 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t41, t40, gm_src276s8986o4r3, 417, 56);
                        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t39) + (uintptr_t)t41 * 40) + 16));
                        gm_t88rcucg1b7uk t43 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t35, t37, t42);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_t88rcucg1b7uk, ((((uint8_t *)t31) + (uintptr_t)t33 * 40) + 16), t43);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 416, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t44 = gm_l3;
            uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
            uint64_t t47 = r45;
            gm_l3 = t47;
        }
    }
gm_lb1:;
    void * t48 = gm_l0;
    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 704));
    gm_l5 = t49;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t50 = gm_l6;
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t52 = (t50 < t51);
            if (!t52) {
                goto gm_lb6;
            }
            {
                void * t53 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t55 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t53) + (uintptr_t)t55 * 48), 48);
                        {
                            {
                                void * t56 = gm_l0;
                                uint8_t t57 = (uint8_t)(((((uint8_t *)t56) + 400))[4] != 0);
                                if (!t57) {
                                    goto gm_lb12;
                                }
                                uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)t56) + 400));
                                gm_l8 = t58;
                                void * t59 = gm_l0;
                                void * t60 = GM_LD(void *, ((uint8_t *)t59));
                                void * t61 = (void *)(((uint8_t *)t60) + 2856);
                                uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)&F.l7));
                                uint32_t t63 = gm_l8;
                                gm_tftb1odc739t5 t64 = gm_f_put_33kejrs(t61, t62, t63);
                                gm_l14 = t64;
                                uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src276s8986o4r3, 420, 31);
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        void * t66 = gm_l0;
                        void * t67 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
                        gm_b0_1 t68 = gm_f_fin_expr_rk98p2r((void *)NULL, cx, NULL, t66, t67);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = ((uint64_t)0ULL);
                        uint64_t t69 = GM_LD(uint64_t, ((((uint8_t *)&F.l7) + 24) + 8));
                        gm_l9 = t69;
                        {
                            for (;;) {
                                uint64_t t70 = gm_l10;
                                uint64_t t71 = gm_l9;
                                uint8_t t72 = (t70 < t71);
                                if (!t72) {
                                    goto gm_lb13;
                                }
                                {
                                    {
                                        {
                                            uint64_t t73 = gm_l10;
                                            gm_l11 = t73;
                                            void * t74 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
                                            uint64_t t75 = GM_LD(uint64_t, ((((uint8_t *)&F.l7) + 24) + 8));
                                            uint64_t t76 = gm_l11;
                                            void * t77 = gm_l0;
                                            void * t78 = (void *)(((uint8_t *)t77) + 96);
                                            void * t79 = gm_l0;
                                            void * t80 = GM_LD(void *, ((uint8_t *)t79));
                                            void * t81 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
                                            uint64_t t82 = GM_LD(uint64_t, ((((uint8_t *)&F.l7) + 24) + 8));
                                            uint64_t t83 = gm_l11;
                                            gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t81) + (uintptr_t)t83 * 40) + 16));
                                            gm_t88rcucg1b7uk t85 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t78, t80, t84);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            GM_ST(gm_t88rcucg1b7uk, ((((uint8_t *)t74) + (uintptr_t)t76 * 40) + 16), t85);
                                            goto gm_lb16;
                                        }
gm_lb17:;
                                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 422, 9);
                                    }
gm_lb16:;
                                }
gm_lb15:;
                                uint64_t t86 = gm_l10;
                                uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
                                uint64_t t89 = r87;
                                gm_l10 = t89;
                            }
                        }
gm_lb13:;
                        void * t90 = gm_l0;
                        void * t91 = GM_LD(void *, ((uint8_t *)t90));
                        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 344));
                        uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t91) + 344) + 8));
                        uint32_t t94 = GM_LD(uint32_t, ((uint8_t *)&F.l7));
                        uint64_t t95 = (uint64_t)t94;
                        if ((uintptr_t)t95 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t95, t93, gm_src276s8986o4r3, 424, 17);
                        void * t96 = (void *)(((uint8_t *)t92) + (uintptr_t)t95 * 136);
                        gm_l12 = t96;
                        void * t97 = gm_l12;
                        void * t98 = gm_l0;
                        void * t99 = (void *)(((uint8_t *)t98) + 96);
                        void * t100 = gm_l0;
                        void * t101 = GM_LD(void *, ((uint8_t *)t100));
                        void * t102 = gm_l12;
                        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 56));
                        gm_t88rcucg1b7uk t104 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t99, t101, t103);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t97) + 56), t104);
                        void * t105 = gm_l0;
                        void * t106 = GM_LD(void *, ((uint8_t *)t105));
                        void * t107 = GM_LD(void *, (((uint8_t *)t106) + 384));
                        uint64_t t108 = GM_LD(uint64_t, ((((uint8_t *)t106) + 384) + 8));
                        uint32_t t109 = GM_LD(uint32_t, ((uint8_t *)&F.l7));
                        uint64_t t110 = (uint64_t)t109;
                        if ((uintptr_t)t110 >= (uintptr_t)(t108)) gm_trap_index((uintptr_t)t110, t108, gm_src276s8986o4r3, 426, 17);
                        void * t111 = (void *)(((uint8_t *)t107) + (uintptr_t)t110 * 64);
                        gm_l13 = t111;
                        void * t112 = gm_l13;
                        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
                        GM_ST(gm_b16_8, (((uint8_t *)t112) + 8), t113);
                        void * t114 = gm_l13;
                        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
                        GM_ST(gm_b16_8, (((uint8_t *)t114) + 24), t115);
                        void * t116 = gm_l13;
                        void * t117 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
                        GM_ST(void *, (((uint8_t *)t116) + 40), t117);
                        void * t118 = gm_l13;
                        GM_ST(uint8_t, (((uint8_t *)t118) + 56), ((uint8_t)1));
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 419, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t119 = gm_l6;
            uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
            uint64_t t122 = r120;
            gm_l6 = t122;
        }
    }
gm_lb6:;
    void * t123 = gm_l0;
    void * t124 = (void *)(((uint8_t *)t123) + 704);
    gm_f_clear_vns7pn2(t124);
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
inline __attribute__((aligned(4))) void * gm_f_fn_body_37q3l3a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_body_37q3l3a F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    void * gm_l3; /* e */
    void * gm_l5; /* checked */
    GM_STACK_CHECK(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * gm_l6;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 168));
    gm_l2 = t6;
    void * t7 = gm_l1;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t7) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            void * t10 = GM_LD(void *, (((uint8_t *)t7) + 8));
            gm_l3 = t10;
            void * t11 = gm_l0;
            void * t12 = gm_l0;
            void * t13 = gm_l3;
            gm_t88rcucg1b7uk t14 = gm_l2;
            gm_tkn42u7na3t81 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[4] = 1;
            void * t16 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t12, t13, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t17 = gm_l2;
            void * t18 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t11, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t7) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)t7) + 8), 40);
            void * t21 = gm_l0;
            void * t22 = gm_l0;
            void * t23 = (void *)((uint8_t *)&F.l4);
            void * t24 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t22, t23, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t25 = (void *)((uint8_t *)&F.l4);
            void * t26 = gm_f_finish_block_body_4vol3tv((void *)NULL, cx, NULL, t21, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t26;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 668, 16);
    }
gm_lb1:;
    void * t27 = gm_l6;
    gm_l5 = t27;
    void * t28 = gm_l0;
    void * t29 = gm_l5;
    void * t30 = gm_f_declare_temps_a7fc14v((void *)NULL, cx, NULL, t28, ((uint64_t)0ULL), t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t30;
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
inline __attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3) {
    struct gm_fr_fn_cx_hgvq15e F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_tnpifv20irnph gm_l3; /* declared */
    GM_STACK_CHECK(&F, 2822u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t2;
    gm_f_list_t7903ef((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t2;
    void * t3 = (void *)((uint8_t *)&F.l4);
    gm_trqfvmjtt4sij t4;
    gm_f_list_fs7kvfg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t5 = gm_f_push_00ljs05(t3, &t4);
    gm_l6 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src276s8986o4r3, 283, 5);
    gm_trqfvmjtt4sij t7;
    gm_f_list_go2qg0b((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t7;
    void * t8 = (void *)((uint8_t *)&F.l5);
    gm_tftb1odc739t5 t9 = gm_f_push_im9d4ro(t8, ((uint32_t)0ULL));
    gm_l7 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src276s8986o4r3, 285, 5);
    void * t11 = gm_l0;
    uint64_t t12 = gm_l1;
    gm_trqfvmjtt4sij t13;
    gm_f_list_p9s2t99((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t14 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l4));
    gm_tti6qnvf9jg1p t15;
    gm_f_infer_table_6o0km8j((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t16 = gm_l2;
    gm_tnpifv20irnph t17 = gm_l3;
    gm_trqfvmjtt4sij t18;
    gm_f_list_7ltkg0u((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t19;
    gm_f_list_lgereu6((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t20;
    gm_f_list_d0n30gc((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t21;
    gm_f_list_88khgsi((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t22;
    gm_f_list_7s4om10((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9805qtonbsag t23;
    memset(&t23, 0, sizeof t23);
    gm_trj4m8pa99qp7 t24;
    memset(&t24, 0, sizeof t24);
    gm_trqfvmjtt4sij t25;
    gm_f_list_5tqpoea((void *)&t25, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t26;
    gm_f_list_0ouok33((void *)&t26, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t27;
    gm_f_list_7s4om10((void *)&t27, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t28;
    gm_f_list_g3vmkan_2((void *)&t28, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t29 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
    gm_trqfvmjtt4sij t30;
    gm_f_list_clrolmo((void *)&t30, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t31;
    gm_f_list_rp32tbu((void *)&t31, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tkn42u7na3t81 t32;
    memset(&t32, 0, sizeof t32);
    gm_trqfvmjtt4sij t33;
    gm_f_list_go2qg0b((void *)&t33, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t403eokj7ifg9 t34;
    memset(&t34, 0, sizeof t34);
    gm_trqfvmjtt4sij t35;
    gm_f_list_fu87ckm((void *)&t35, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t36;
    gm_f_list_7s4om10((void *)&t36, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t37;
    gm_f_list_084fcd6((void *)&t37, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t38;
    gm_f_list_tmk0paj((void *)&t38, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t39;
    gm_f_list_6dnse27((void *)&t39, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t40;
    gm_f_list_7ltkg0u((void *)&t40, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t41;
    gm_f_list_ir0h3tc_2((void *)&t41, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t42;
    gm_f_list_go2qg0b((void *)&t42, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t43;
    gm_f_list_7s4om10((void *)&t43, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t44;
    gm_f_hash_map_njcsknk((void *)&t44, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tta5be0c75tfq t45;
    memset(&t45, 0, sizeof t45);
    GM_ST(void *, ((uint8_t *)&t45 + 0), t11);
    GM_ST(uint64_t, ((uint8_t *)&t45 + 8), t12);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 16), t13);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 56), t14);
    GM_ST(gm_tti6qnvf9jg1p, ((uint8_t *)&t45 + 96), t15);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t45 + 168), t16);
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t45 + 176), t17);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 192), t18);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 232), t19);
    GM_ST(uint32_t, ((uint8_t *)&t45 + 272), ((uint32_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 280), t20);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 320), t21);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 360), t22);
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t45 + 400), t23);
    GM_ST(gm_trj4m8pa99qp7, ((uint8_t *)&t45 + 408), t24);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 432), t25);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 472), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 480), t26);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 520), ((uint8_t)0));
    GM_ST(uint64_t, ((uint8_t *)&t45 + 528), ((uint64_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 536), t27);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 576), t28);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 616), t29);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 656), t30);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 696), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 704), t31);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 744), ((uint8_t)0));
    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t45 + 748), t32);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 760), t33);
    GM_ST(gm_t403eokj7ifg9, ((uint8_t *)&t45 + 800), t34);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 920), t35);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 960), t36);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 1000), t37);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 1040), t38);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 1080), t39);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 1120), t40);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 1160), t41);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 1200), t42);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 1240), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t45 + 1248), t43);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 1288), ((uint8_t)0));
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t45 + 1296), t44);
    GM_ST(void *, ((uint8_t *)&t45 + 1352), ((void *)0));
    memmove(gm_out, ((uint8_t *)&t45), 1360);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_forget_label_breaks_vl4mn72 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* id */
    uint8_t gm_l2; /* found */
    uint64_t gm_l3; /* i */
    uint32_t gm_l4; /* depth */
    GM_STACK_CHECK_SHALLOW(&F, 211u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint8_t)0);
    gm_l3 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint32_t t8 = gm_f_depth_fr7pn6n(t7);
    gm_l4 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 432) + 8));
            uint8_t t12 = (t9 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t13 = gm_l0;
                        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 432));
                        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 432) + 8));
                        uint64_t t16 = gm_l3;
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_src276s8986o4r3, 971, 12);
                        uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)t14) + (uintptr_t)t16 * 24) + 4));
                        uint32_t t18 = gm_l1;
                        uint8_t t19 = ((t17 == t18));
                        if (!t19) {
                            goto gm_lb5;
                        }
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 432));
                        uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 432) + 8));
                        uint64_t t23 = gm_l3;
                        if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_src276s8986o4r3, 971, 53);
                        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t21) + (uintptr_t)t23 * 24));
                        uint32_t t25 = gm_l4;
                        uint8_t t26 = ((t24 == t25));
                        if (!t26) {
                            goto gm_lb5;
                        }
                        gm_l2 = ((uint8_t)1);
                        void * t27 = gm_l0;
                        void * t28 = (void *)(((uint8_t *)t27) + 432);
                        uint64_t t29 = gm_l3;
                        gm_tgk5j0pob2pqa t30;
                        gm_f_remove_v59hrnl((void *)&t30, t28, t29);
                        goto gm_lb4;
                    }
gm_lb5:;
                    uint64_t t31 = gm_l3;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 975, 13);
                    uint64_t t34 = r32;
                    gm_l3 = t34;
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint8_t t35 = gm_l2;
    F.ret = t35;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_load_frame_r6k3kmp F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    memmove((((uint8_t *)t2) + 16), ((uint8_t *)a1), 40);
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 40));
    GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 168), t4);
    void * t5 = gm_l0;
    gm_tnpifv20irnph t6 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)a1) + 48));
    GM_ST(gm_tnpifv20irnph, (((uint8_t *)t5) + 176), t6);
    void * t7 = gm_l0;
    memmove((((uint8_t *)t7) + 192), (((uint8_t *)a1) + 64), 40);
    void * t8 = gm_l0;
    memmove((((uint8_t *)t8) + 232), (((uint8_t *)a1) + 104), 40);
    void * t9 = gm_l0;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)a1) + 144));
    GM_ST(uint32_t, (((uint8_t *)t9) + 272), t10);
    void * t11 = gm_l0;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a1) + 148));
    GM_ST(uint8_t, (((uint8_t *)t11) + 520), t12);
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)a1) + 152));
    GM_ST(uint64_t, (((uint8_t *)t13) + 528), t14);
    void * t15 = gm_l0;
    gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)a1) + 160));
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t15) + 400), t16);
    void * t17 = gm_l0;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a1) + 168));
    GM_ST(uint8_t, (((uint8_t *)t17) + 696), t18);
    void * t19 = gm_l0;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a1) + 169));
    GM_ST(uint8_t, (((uint8_t *)t19) + 744), t20);
    void * t21 = gm_l0;
    gm_tkn42u7na3t81 t22 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)a1) + 172));
    GM_ST(gm_tkn42u7na3t81, (((uint8_t *)t21) + 748), t22);
    void * t23 = gm_l0;
    memmove((((uint8_t *)t23) + 800), (((uint8_t *)a1) + 184), 120);
    void * t24 = gm_l0;
    memmove((((uint8_t *)t24) + 1000), (((uint8_t *)a1) + 304), 40);
    void * t25 = gm_l0;
    memmove((((uint8_t *)t25) + 1200), (((uint8_t *)a1) + 344), 40);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp_c(void * a0, gm_tjhr0bdm69ios a1) { gm_f_load_frame_r6k3kmp(a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_local_at_p388mm4(void *gm_out, void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_at_p388mm4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* depth */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint32_t t4 = gm_f_depth_fr7pn6n(t3);
    uint8_t t5 = ((t2 == t4));
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 16) + 8));
        uint32_t t9 = gm_l2;
        uint64_t t10 = (uint64_t)t9;
        if ((uintptr_t)t10 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t10, t8, gm_src276s8986o4r3, 565, 37);
        memmove(gm_out, (((uint8_t *)t7) + (uintptr_t)t10 * 40), 40);
        goto gm_fdone;
    }
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, (((uint8_t *)t11) + 576));
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t11) + 576) + 8));
    uint32_t t14 = gm_l1;
    uint64_t t15 = (uint64_t)t14;
    if ((uintptr_t)t15 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t15, t13, gm_src276s8986o4r3, 566, 12);
    void * t16 = GM_LD(void *, (((uint8_t *)t12) + (uintptr_t)t15 * 384));
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t12) + (uintptr_t)t15 * 384) + 8));
    uint32_t t18 = gm_l2;
    uint64_t t19 = (uint64_t)t18;
    if ((uintptr_t)t19 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t19, t17, gm_src276s8986o4r3, 566, 12);
    memmove(gm_out, (((uint8_t *)t16) + (uintptr_t)t19 * 40), 40);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tsoie1ppgdvcg gm_f_local_at_p388mm4_c(void * a0, uint32_t a1, uint32_t a2) { gm_tsoie1ppgdvcg gm_r; gm_f_local_at_p388mm4(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_local_6f38t7p F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* scope */
    uint64_t gm_l4; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 899u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 56) + 8));
    gm_l2 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t10 > ((uint64_t)0ULL));
            if (!t11) {
                goto gm_lb1;
            }
            {
                uint64_t t12 = gm_l2;
                uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
                if (o14) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 496, 9);
                uint64_t t15 = r13;
                gm_l2 = t15;
                void * t16 = gm_l0;
                void * t17 = GM_LD(void *, (((uint8_t *)t16) + 56));
                uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 56) + 8));
                uint64_t t19 = gm_l2;
                if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_src276s8986o4r3, 497, 18);
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t17) + (uintptr_t)t19 * 40));
                gm_l3 = t20;
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                gm_l4 = t21;
                {
                    for (;;) {
                        uint64_t t22 = gm_l4;
                        uint8_t t23 = (t22 > ((uint64_t)0ULL));
                        if (!t23) {
                            goto gm_lb4;
                        }
                        {
                            uint64_t t24 = gm_l4;
                            uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                            if (o26) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 500, 13);
                            uint64_t t27 = r25;
                            gm_l4 = t27;
                            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t30 = gm_l4;
                            if ((uintptr_t)t30 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t30, t29, gm_src276s8986o4r3, 501, 16);
                            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t28) + (uintptr_t)t30 * 224));
                            gm_b16_8 t32 = gm_l1;
                            uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
                            if (t33) {
                                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t36 = gm_l4;
                                if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_src276s8986o4r3, 501, 47);
                                gm_t930q5l38hdap t37 = GM_LD(gm_t930q5l38hdap, ((((uint8_t *)t34) + (uintptr_t)t36 * 224) + 16));
                                gm_tqvu78rpi6q1h t38;
                                GM_ST(uint8_t, (uint8_t *)&t38 + 209, 0);
                                GM_ST(uint16_t, (uint8_t *)&t38 + 210, 0);
                                GM_ST(uint32_t, (uint8_t *)&t38 + 212, 0);
                                GM_ST(gm_t930q5l38hdap, ((uint8_t *)&t38), t37);
                                ((uint8_t *)&t38)[208] = 1;
                                memmove(gm_out, ((uint8_t *)&t38), 216);
                                goto gm_fdone;
                            }
                        }
gm_lb6:;
                    }
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tqvu78rpi6q1h t39;
    memset(&t39, 0, sizeof t39);
    memmove(gm_out, ((uint8_t *)&t39), 216);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tqvu78rpi6q1h gm_f_lookup_local_6f38t7p_c(void * a0, gm_b16_8 a1) { gm_tqvu78rpi6q1h gm_r; gm_f_lookup_local_6f38t7p(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_lookup_local_at_ntm4scs(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_local_at_ntm4scs F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* scope */
    uint64_t gm_l4; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 1167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 56) + 8));
    gm_l2 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t10 > ((uint64_t)0ULL));
            if (!t11) {
                goto gm_lb1;
            }
            {
                uint64_t t12 = gm_l2;
                uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
                if (o14) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 545, 9);
                uint64_t t15 = r13;
                gm_l2 = t15;
                void * t16 = gm_l0;
                void * t17 = GM_LD(void *, (((uint8_t *)t16) + 56));
                uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 56) + 8));
                uint64_t t19 = gm_l2;
                if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_src276s8986o4r3, 546, 18);
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t17) + (uintptr_t)t19 * 40));
                gm_l3 = t20;
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                gm_l4 = t21;
                {
                    for (;;) {
                        uint64_t t22 = gm_l4;
                        uint8_t t23 = (t22 > ((uint64_t)0ULL));
                        if (!t23) {
                            goto gm_lb4;
                        }
                        {
                            uint64_t t24 = gm_l4;
                            uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                            if (o26) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 549, 13);
                            uint64_t t27 = r25;
                            gm_l4 = t27;
                            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t30 = gm_l4;
                            if ((uintptr_t)t30 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t30, t29, gm_src276s8986o4r3, 550, 16);
                            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t28) + (uintptr_t)t30 * 224));
                            gm_b16_8 t32 = gm_l1;
                            uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
                            if (t33) {
                                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t36 = gm_l4;
                                if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_src276s8986o4r3, 550, 57);
                                void * t37 = gm_l0;
                                void * t38 = GM_LD(void *, (((uint8_t *)t37) + 616));
                                uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 616) + 8));
                                uint64_t t40 = gm_l2;
                                if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_src276s8986o4r3, 550, 80);
                                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t38) + (uintptr_t)t40 * 4));
                                gm_t930q5l38hdap t42 = GM_LD(gm_t930q5l38hdap, ((((uint8_t *)t34) + (uintptr_t)t36 * 224) + 16));
                                gm_tfj8gdjm726n4 t43;
                                GM_ST(uint32_t, (uint8_t *)&t43 + 212, 0);
                                GM_ST(gm_t930q5l38hdap, ((uint8_t *)&t43 + 0), t42);
                                GM_ST(uint32_t, ((uint8_t *)&t43 + 208), t41);
                                gm_tsf6kp4ohnuqs t44;
                                GM_ST(uint8_t, (uint8_t *)&t44 + 217, 0);
                                GM_ST(uint16_t, (uint8_t *)&t44 + 218, 0);
                                GM_ST(uint32_t, (uint8_t *)&t44 + 220, 0);
                                GM_ST(gm_tfj8gdjm726n4, ((uint8_t *)&t44), t43);
                                ((uint8_t *)&t44)[216] = 1;
                                memmove(gm_out, ((uint8_t *)&t44), 224);
                                goto gm_fdone;
                            }
                        }
gm_lb6:;
                    }
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tsf6kp4ohnuqs t45;
    memset(&t45, 0, sizeof t45);
    memmove(gm_out, ((uint8_t *)&t45), 224);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tsf6kp4ohnuqs gm_f_lookup_local_at_ntm4scs_c(void * a0, gm_b16_8 a1) { gm_tsf6kp4ohnuqs gm_r; gm_f_lookup_local_at_ntm4scs(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_missing_return_s12s32d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_missing_return_s12s32d F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    void * gm_l3; /* e */
    uint64_t gm_l4; /* k */
    gm_t88rcucg1b7uk gm_l5; /* ty */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 846u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t16 = gm_l1;
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t16) + 8) + 8));
    uint8_t t18 = (t17 > ((uint64_t)0ULL));
    if (t18) {
        {
            {
                void * t19 = gm_l1;
                void * t20 = GM_LD(void *, (((uint8_t *)t19) + 8));
                uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 8) + 8));
                void * t22 = gm_l1;
                uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t22) + 8) + 8));
                uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 709, 32);
                uint64_t t26 = r24;
                if ((uintptr_t)t26 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t26, t21, gm_src276s8986o4r3, 709, 24);
                uint8_t t27 = GM_LD(uint8_t, (((((uint8_t *)t20) + (uintptr_t)t26 * 144) + 8) + 0));
                uint8_t t28 = (uint8_t)(t27 == ((uint8_t)2ULL));
                if (!t28) {
                    goto gm_lb2;
                }
                void * t29 = GM_LD(void *, (((((uint8_t *)t20) + (uintptr_t)t26 * 144) + 8) + 8));
                gm_l3 = t29;
                void * t30 = gm_l3;
                {
                    {
                        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
                        uint8_t t32 = (uint8_t)(t31 == ((uint8_t)40ULL));
                        if (!t32) {
                            goto gm_lb4;
                        }
                        gm_l8 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
                        uint8_t t34 = (uint8_t)(t33 == ((uint8_t)41ULL));
                        if (!t34) {
                            goto gm_lb5;
                        }
                        gm_l8 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
                        uint8_t t36 = (uint8_t)(t35 == ((uint8_t)43ULL));
                        if (!t36) {
                            goto gm_lb6;
                        }
                        gm_l8 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb6:;
                    {
                        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
                        uint8_t t38 = (uint8_t)(t37 == ((uint8_t)42ULL));
                        if (!t38) {
                            goto gm_lb7;
                        }
                        gm_l8 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb7:;
                    {
                        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
                        uint8_t t40 = (uint8_t)(t39 == ((uint8_t)44ULL));
                        if (!t40) {
                            goto gm_lb8;
                        }
                        gm_l8 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb8:;
                    {
                        gm_l8 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 709, 58);
                }
gm_lb3:;
                uint8_t t41 = gm_l8;
                if (!t41) {
                    goto gm_lb2;
                }
                void * t42 = gm_l0;
                uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t42) + 320) + 8));
                gm_l4 = t43;
                {
                    for (;;) {
                        uint64_t t44 = gm_l4;
                        uint8_t t45 = (t44 > ((uint64_t)0ULL));
                        if (!t45) {
                            goto gm_lb10;
                        }
                        {
                            uint64_t t46 = gm_l4;
                            uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
                            if (o48) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 712, 17);
                            uint64_t t49 = r47;
                            gm_l4 = t49;
                            {
                                {
                                    void * t50 = gm_l0;
                                    void * t51 = GM_LD(void *, (((uint8_t *)t50) + 320));
                                    uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t50) + 320) + 8));
                                    uint64_t t53 = gm_l4;
                                    if ((uintptr_t)t53 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t53, t52, gm_src276s8986o4r3, 713, 45);
                                    uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t51) + (uintptr_t)t53 * 48) + 0));
                                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)0ULL));
                                    if (!t55) {
                                        goto gm_lb14;
                                    }
                                    gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t51) + (uintptr_t)t53 * 48) + 8));
                                    gm_l5 = t56;
                                    gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t51) + (uintptr_t)t53 * 48) + 8) + 4));
                                    gm_l6 = t57;
                                    gm_tl874t2atnlh3 t58 = gm_l6;
                                    void * t59 = gm_l3;
                                    gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 72));
                                    uint8_t t61 = (!gm_eqff85ac10o3(((uint8_t *)&t58), ((uint8_t *)&t60)));
                                    if (t61) {
                                        goto gm_lb12;
                                    }
                                    void * t62 = gm_l0;
                                    void * t63 = (void *)(((uint8_t *)t62) + 96);
                                    void * t64 = gm_l0;
                                    void * t65 = GM_LD(void *, ((uint8_t *)t64));
                                    gm_t88rcucg1b7uk t66 = gm_l5;
                                    gm_t88rcucg1b7uk t67 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t63, t65, t66);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l7 = t67;
                                    void * t68 = gm_l0;
                                    void * t69 = GM_LD(void *, ((uint8_t *)t68));
                                    void * t70 = (void *)(((uint8_t *)t69) + 8);
                                    gm_t88rcucg1b7uk t71 = gm_l7;
                                    uint8_t t72 = gm_f_is_unit_n83ttnb(t70, GM_CONV(gm_csguankko4lmhd, t71));
                                    if (t72) {
                                        gm_l9 = ((uint8_t)1);
                                    } else {
                                        void * t73 = gm_l0;
                                        void * t74 = GM_LD(void *, ((uint8_t *)t73));
                                        void * t75 = (void *)(((uint8_t *)t74) + 8);
                                        gm_t88rcucg1b7uk t76 = gm_l7;
                                        gm_t53at80pfdus3 t77;
                                        gm_f_kind_jcnt4hm((void *)&t77, t75, GM_CONV(gm_csguankko4lmhd, t76));
                                        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
                                        uint8_t t79 = (uint8_t)((t78 == ((uint8_t)5ULL)));
                                        gm_t53at80pfdus3 t80;
                                        memset(&t80, 0, sizeof t80);
                                        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)5ULL));
                                        gm_l9 = t79;
                                    }
                                    uint8_t t81 = gm_l9;
                                    if (t81) {
                                        goto gm_lb12;
                                    }
                                    void * t82 = gm_l0;
                                    void * t83 = (void *)(((uint8_t *)t82) + 320);
                                    uint64_t t84 = gm_l4;
                                    gm_tpm220j5qhjke t85;
                                    gm_f_remove_te9updc((void *)&t85, t83, t84);
                                    void * t86 = gm_l0;
                                    void * t87 = gm_l3;
                                    gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
                                    void * t89 = gm_l0;
                                    gm_t88rcucg1b7uk t90 = gm_l2;
                                    gm_b16_8 t91 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t89, t90);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t92;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t91);
                                    gm_f_format_05vut39((void *)&gm_l10, (void *)NULL, cx, NULL, t92);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                    if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src276s8986o4r3, 718, 34);
                                    gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                                    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                                    gm_b0_1 t96 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t88, t95);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
gm_lb14:;
                                goto gm_lb12;
                            }
gm_lb13:;
                        }
gm_lb12:;
                    }
                }
gm_lb10:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t97 = gm_l0;
    void * t98 = gm_l1;
    uint32_t t99 = GM_LD(uint32_t, (((uint8_t *)t98) + 24));
    void * t100 = gm_l1;
    uint32_t t101 = GM_LD(uint32_t, ((((uint8_t *)t100) + 24) + 8));
    void * t102 = gm_l1;
    uint32_t t103 = GM_LD(uint32_t, ((((uint8_t *)t102) + 24) + 8));
    gm_tl874t2atnlh3 t104;
    GM_ST(uint32_t, ((uint8_t *)&t104 + 0), t99);
    GM_ST(uint32_t, ((uint8_t *)&t104 + 4), t101);
    GM_ST(uint32_t, ((uint8_t *)&t104 + 8), t103);
    void * t105 = gm_l0;
    gm_t88rcucg1b7uk t106 = gm_l2;
    gm_b16_8 t107 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t105, t106);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t108;
    GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
    gm_f_format_s8qs6vn((void *)&gm_l11, (void *)NULL, cx, NULL, t108);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src276s8986o4r3, 723, 76);
    gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
    gm_b0_1 t112 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t97, t104, t111);
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
__attribute__((aligned(4))) void gm_f_never_cause_p5ir1i2(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_never_cause_p5ir1i2 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l3; /* b */
    void * gm_l4; /* t */
    gm_b16_8 gm_l5; /* why */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l6;
    gm_b16_8 gm_l7;
    uint8_t gm_l8;
    void * t22 = gm_l1;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)48ULL));
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sqc0827akcmi8));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(39));
            gm_l6 = t25;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)51ULL));
            if (!t27) {
                goto gm_lb3;
            }
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sm9otrmuq5hp3));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(33));
            gm_l6 = t28;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)49ULL));
            if (!t30) {
                goto gm_lb4;
            }
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sscfkpmei7e4o));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(28));
            gm_l6 = t31;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)50ULL));
            if (!t33) {
                goto gm_lb5;
            }
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_ses9h0rg965i4));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(53));
            gm_l6 = t34;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)52ULL));
            if (!t36) {
                goto gm_lb6;
            }
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sp6l5tr58ip76));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(22));
            gm_l6 = t37;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)34ULL));
            if (!t39) {
                goto gm_lb7;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t22) + 8) + 8), 40);
            {
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)1ULL));
                    if (!t41) {
                        goto gm_lb9;
                    }
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    gm_l8 = ((uint8_t)0);
                    goto gm_lb8;
                }
gm_lb10:;
                gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 1026, 32);
            }
gm_lb8:;
            uint8_t t42 = gm_l8;
            if (t42) {
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s7b7j2m1f6fle));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(70));
                gm_l7 = t43;
            } else {
                gm_b16_8 t44;
                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_seiql9tkbkhim));
                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(51));
                gm_l7 = t44;
            }
            gm_b16_8 t45 = gm_l7;
            gm_l6 = t45;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)47ULL));
            if (!t47) {
                goto gm_lb11;
            }
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(gm_souep3pirutur));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(54));
            gm_l6 = t48;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)42ULL));
            if (!t50) {
                goto gm_lb12;
            }
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sjeqed5d3f5pb));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(25));
            gm_l6 = t51;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)53ULL));
            if (!t53) {
                goto gm_lb13;
            }
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sroje34o1kfta));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(37));
            gm_l6 = t54;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)54ULL));
            if (!t56) {
                goto gm_lb14;
            }
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s0rfpnv4c3k1l));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(37));
            gm_l6 = t57;
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)44ULL));
            if (!t59) {
                goto gm_lb15;
            }
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s993c5b6eq922));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(53));
            gm_l6 = t60;
            goto gm_lb1;
        }
gm_lb15:;
        {
            uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)59ULL));
            if (!t62) {
                goto gm_lb16;
            }
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sb3sf0ef436fi));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(39));
            gm_l6 = t63;
            goto gm_lb1;
        }
gm_lb16:;
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)46ULL));
            if (!t65) {
                goto gm_lb17;
            }
            void * t66 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l3 = t66;
            {
                {
                    void * t67 = gm_l3;
                    uint8_t t68 = (uint8_t)(GM_LD(void *, (((uint8_t *)t67) + 16)) != 0);
                    if (!t68) {
                        goto gm_lb19;
                    }
                    void * t69 = GM_LD(void *, (((uint8_t *)t67) + 16));
                    gm_l4 = t69;
                    void * t70 = gm_l0;
                    void * t71 = (void *)(((uint8_t *)t70) + 8);
                    void * t72 = gm_l4;
                    gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t72));
                    gm_t53at80pfdus3 t74;
                    gm_f_kind_jcnt4hm((void *)&t74, t71, GM_CONV(gm_csguankko4lmhd, t73));
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t74) + 0));
                    uint8_t t76 = (uint8_t)((t75 == ((uint8_t)5ULL)));
                    if (!t76) {
                        goto gm_lb19;
                    }
                    void * t77 = gm_l0;
                    void * t78 = gm_l4;
                    gm_t53at80pfdus3 t79;
                    memset(&t79, 0, sizeof t79);
                    GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)5ULL));
                    gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t77, t78);
                    goto gm_fdone;
                }
gm_lb19:;
            }
gm_lb18:;
            void * t80 = gm_l3;
            uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)t80) + 8));
            uint8_t t82 = (t81 > ((uint64_t)0ULL));
            if (t82) {
                void * t83 = gm_l0;
                void * t84 = gm_l3;
                void * t85 = GM_LD(void *, ((uint8_t *)t84));
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t84) + 8));
                void * t87 = gm_l3;
                uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)t87) + 8));
                uint64_t r89; bool o90 = __builtin_sub_overflow(t88, ((uint64_t)1ULL), &r89);
                if (o90) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 1035, 69);
                uint64_t t91 = r89;
                if ((uintptr_t)t91 >= (uintptr_t)(t86)) gm_trap_index((uintptr_t)t91, t86, gm_src276s8986o4r3, 1035, 61);
                gm_trcmmd12cefb1 t92 = GM_LD(gm_trcmmd12cefb1, (((uint8_t *)t85) + (uintptr_t)t91 * 32));
                gm_f_stmt_never_cause_r2lf089((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t83, &t92);
                goto gm_fdone;
            }
            gm_tnvnsr1bfvtbd t93;
            memset(&t93, 0, sizeof t93);
            memmove(gm_out, ((uint8_t *)&t93), 40);
            goto gm_fdone;
        }
gm_lb17:;
        {
            gm_tnvnsr1bfvtbd t94;
            memset(&t94, 0, sizeof t94);
            memmove(gm_out, ((uint8_t *)&t94), 40);
            goto gm_fdone;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 1020, 19);
    }
gm_lb1:;
    gm_b16_8 t95 = gm_l6;
    gm_l5 = t95;
    void * t96 = gm_l1;
    gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
    gm_b16_8 t98 = gm_l5;
    gm_tbgd14esf0jvi t99;
    GM_ST(uint32_t, (uint8_t *)&t99 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t99 + 0), t97);
    GM_ST(gm_b16_8, ((uint8_t *)&t99 + 16), t98);
    gm_tnvnsr1bfvtbd t100;
    GM_ST(uint8_t, (uint8_t *)&t100 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t100 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t100 + 36, 0);
    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t100), t99);
    ((uint8_t *)&t100)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t100), 40);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_never_cause_p5ir1i2_c(void * a0, void * a1) { gm_tnvnsr1bfvtbd gm_r; gm_f_never_cause_p5ir1i2(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3) {
    struct gm_fr_new_label_3207tcb F;
    void * gm_l0; /* cx */
    gm_th2lqbo77pvsc gm_l1; /* name */
    uint8_t gm_l2; /* is_loop */
    gm_t1qr0p6kln6tf gm_l3; /* value */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 207u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l5;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 272));
    gm_l4 = t3;
    void * t4 = gm_l0;
    void * t5 = gm_l0;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 272));
    uint32_t r7; bool o8 = __builtin_add_overflow(t6, ((uint32_t)1ULL), &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 657, 5);
    uint32_t t9 = r7;
    GM_ST(uint32_t, (((uint8_t *)t4) + 272), t9);
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 232);
    uint32_t t12 = gm_l4;
    uint8_t t13 = gm_l2;
    gm_t1qr0p6kln6tf t14 = gm_l3;
    gm_th2lqbo77pvsc t15 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l1));
    gm_t8enl5g7b22jb t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 29, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 30, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 44, 0);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t16 + 0), t15);
    GM_ST(uint32_t, ((uint8_t *)&t16 + 24), t12);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 28), t13);
    GM_ST(gm_t1qr0p6kln6tf, ((uint8_t *)&t16 + 32), t14);
    gm_tftb1odc739t5 t17 = gm_f_push_lng5b06(t11, &t16);
    gm_l5 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src276s8986o4r3, 658, 5);
    uint32_t t19 = gm_l4;
    F.ret = t19;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_new_local_003h9es F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint8_t gm_l3; /* mutable */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 171u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l5;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_b16_8 t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    uint8_t t6 = gm_l3;
    gm_tl874t2atnlh3 t7 = gm_l4;
    gm_tsoie1ppgdvcg t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 21, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 22, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t4);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t8 + 16), t5);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 20), t6);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t8 + 24), t7);
    gm_tftb1odc739t5 t9 = gm_f_push_am7bo0v(t3, &t8);
    gm_l5 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src276s8986o4r3, 440, 5);
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 16) + 8));
    uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
    if (o14) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 441, 13);
    uint64_t t15 = r13;
    uint32_t t16 = (uint32_t)t15;
    F.ret = t16;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_note_capture_0p0af7r(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_note_capture_0p0af7r F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* depth */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t7 = gm_l1;
    void * t8 = gm_l0;
    uint32_t t9 = gm_f_depth_fr7pn6n(t8);
    uint8_t t10 = (t7 >= t9);
    if (t10) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t11 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t11) + 696), ((uint8_t)1));
    uint32_t t12 = gm_l1;
    uint64_t t13 = (uint64_t)t12;
    uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
    if (o15) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 573, 14);
    uint64_t t16 = r14;
    gm_l3 = t16;
    void * t17 = gm_l0;
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t17) + 576) + 8));
    gm_l2 = t18;
    {
        for (;;) {
            uint64_t t19 = gm_l3;
            uint64_t t20 = gm_l2;
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t22 = gm_l3;
                        gm_l4 = t22;
                        void * t23 = gm_l0;
                        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 576));
                        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 576) + 8));
                        uint64_t t26 = gm_l4;
                        if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_src276s8986o4r3, 573, 53);
                        GM_ST(uint8_t, ((((uint8_t *)t24) + (uintptr_t)t26 * 384) + 168), ((uint8_t)1));
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 573, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t27 = gm_l3;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
            uint64_t t30 = r28;
            gm_l3 = t30;
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
extern __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_push_scope_mu7j3lg F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK(&F, 171u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 56);
    gm_trqfvmjtt4sij t4;
    gm_f_list_fs7kvfg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t5 = gm_f_push_00ljs05(t3, &t4);
    gm_l1 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 1, gm_src276s8986o4r3, 530, 5);
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 616);
    void * t9 = gm_l0;
    uint32_t t10 = gm_f_depth_fr7pn6n(t9);
    gm_tftb1odc739t5 t11 = gm_f_push_im9d4ro(t8, t10);
    gm_l2 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src276s8986o4r3, 531, 5);
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 656);
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    gm_t66j1im9ip128 t17;
    gm_f_scope_mark_i3b2mvo((void *)&t17, t16);
    gm_tftb1odc739t5 t18 = gm_f_push_s39fiuo(t14, &t17);
    gm_l3 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src276s8986o4r3, 532, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_temps_upm64pn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_push_temps_upm64pn F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* mark */
    void * gm_l2; /* stmts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* t */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 1200));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 1200) + 8));
    uint64_t t10 = gm_l1;
    uint64_t t11 = (uintptr_t)t10;
    uint64_t t12 = (uintptr_t)t9;
    if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_src276s8986o4r3, 686, 14);
    uint64_t t13 = ((uintptr_t)t9) - (uintptr_t)t10;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)t10 * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    gm_l3 = t14;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t20 = gm_l4;
                {
                    {
                        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)t18) + (uintptr_t)t20 * 4));
                        gm_l5 = t21;
                        void * t22 = gm_l2;
                        uint32_t t23 = gm_l5;
                        gm_ttggq7ng4kp3p t24;
                        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t24 + 0), t23);
                        GM_ST(void *, ((uint8_t *)&t24 + 8), ((void *)0));
                        gm_trcmmd12cefb1 t25;
                        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t25 + 8), t24);
                        gm_tftb1odc739t5 t26 = gm_f_push_d6c76ud_2(t22, &t25);
                        gm_l6 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src276s8986o4r3, 686, 45);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 686, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t28 = gm_l4;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l4 = t31;
        }
    }
gm_lb1:;
    void * t32 = gm_l0;
    void * t33 = (void *)(((uint8_t *)t32) + 1200);
    uint64_t t34 = gm_l1;
    gm_f_truncate_eoppkeo(t33, t34);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_save_frame_8libfct(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_save_frame_8libfct F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 883u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 168));
    void * t5 = gm_l0;
    gm_tnpifv20irnph t6 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t5) + 176));
    void * t7 = gm_l0;
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 272));
    void * t11 = gm_l0;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t11) + 520));
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 528));
    void * t15 = gm_l0;
    gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t15) + 400));
    void * t17 = gm_l0;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t17) + 696));
    void * t19 = gm_l0;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t19) + 744));
    void * t21 = gm_l0;
    gm_tkn42u7na3t81 t22 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)t21) + 748));
    void * t23 = gm_l0;
    void * t24 = gm_l0;
    void * t25 = gm_l0;
    gm_trqfvmjtt4sij t26 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 16));
    gm_trqfvmjtt4sij t27 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t7) + 192));
    gm_trqfvmjtt4sij t28 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t8) + 232));
    gm_t403eokj7ifg9 t29 = GM_LD(gm_t403eokj7ifg9, (((uint8_t *)t23) + 800));
    gm_trqfvmjtt4sij t30 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t24) + 1000));
    gm_trqfvmjtt4sij t31 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t25) + 1200));
    gm_tjhr0bdm69ios t32;
    GM_ST(uint32_t, (uint8_t *)&t32 + 44, 0);
    GM_ST(uint8_t, (uint8_t *)&t32 + 149, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 150, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 170, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 180, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 0), t26);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32 + 40), t4);
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t32 + 48), t6);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 64), t27);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 104), t28);
    GM_ST(uint32_t, ((uint8_t *)&t32 + 144), t10);
    GM_ST(uint8_t, ((uint8_t *)&t32 + 148), t12);
    GM_ST(uint64_t, ((uint8_t *)&t32 + 152), t14);
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t32 + 160), t16);
    GM_ST(uint8_t, ((uint8_t *)&t32 + 168), t18);
    GM_ST(uint8_t, ((uint8_t *)&t32 + 169), t20);
    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t32 + 172), t22);
    GM_ST(gm_t403eokj7ifg9, ((uint8_t *)&t32 + 184), t29);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 304), t30);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 344), t31);
    memmove(gm_out, ((uint8_t *)&t32), 384);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_save_frame_8libfct_c(void * a0) { gm_tjhr0bdm69ios gm_r; gm_f_save_frame_8libfct(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_stmt_4145gsn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_4145gsn F;
    void * gm_l0; /* cx */
    void * gm_l1; /* s */
    gm_b16_8 gm_l2; /* attrs */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* places */
    gm_tjsp7bhdn2e8j gm_l8; /* op */
    void * gm_l9; /* value */
    void * gm_l10; /* e */
    uint8_t gm_l13; /* is_err */
    void * gm_l14; /* body */
    void * gm_l15; /* w */
    void * gm_l16; /* k */
    void * gm_l17; /* value */
    GM_STACK_CHECK(&F, 956u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l18;
    void * t17 = gm_l1;
    {
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
            if (!t19) {
                goto gm_lb2;
            }
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((((uint8_t *)t17) + 8) + 8));
            gm_l2 = t20;
            memmove(((uint8_t *)&F.l3), (((((uint8_t *)t17) + 8) + 8) + 16), 64);
            gm_b16_8 t21 = gm_l2;
            gm_l4 = t21;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t22 = gm_l5;
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t24 = (t22 < t23);
                    if (!t24) {
                        goto gm_lb3;
                    }
                    {
                        void * t25 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t27 = gm_l5;
                        {
                            {
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)t25) + (uintptr_t)t27 * 72), 72);
                                void * t28 = gm_l0;
                                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 56));
                                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                gm_tnpifv20irnph t31;
                                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
                                gm_f_format_ggjb732((void *)&gm_l18, (void *)NULL, cx, NULL, t31);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src276s8986o4r3, 808, 34);
                                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                                gm_b0_1 t35 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t28, t29, t34);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 807, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t36 = gm_l5;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l5 = t39;
                }
            }
gm_lb3:;
            void * t40 = gm_l0;
            void * t41 = (void *)((uint8_t *)&F.l3);
            gm_f_binding_igkhmn1((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
            if (!t43) {
                goto gm_lb8;
            }
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t17) + 8) + 8));
            gm_l7 = t44;
            gm_tjsp7bhdn2e8j t45 = GM_LD(gm_tjsp7bhdn2e8j, (((((uint8_t *)t17) + 8) + 8) + 16));
            gm_l8 = t45;
            void * t46 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 24));
            gm_l9 = t46;
            void * t47 = gm_l0;
            gm_b16_8 t48 = gm_l7;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_tjsp7bhdn2e8j t50 = gm_l8;
            void * t51 = gm_l9;
            void * t52 = gm_l1;
            gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 128));
            gm_f_assign_a0848vq((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t47, t49, t50, t51, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb8:;
        {
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)2ULL));
            if (!t55) {
                goto gm_lb9;
            }
            void * t56 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l10 = t56;
            void * t57 = gm_l0;
            void * t58 = gm_l10;
            void * t59 = gm_f_stmt_expr_evs4ic8((void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t60 + 8), t59);
            memmove(gm_out, ((uint8_t *)&t60), 32);
            goto gm_fdone;
        }
gm_lb9:;
        {
            uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)5ULL));
            if (!t62) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&F.l11), ((((uint8_t *)t17) + 8) + 8), 112);
            void * t63 = gm_l0;
            void * t64 = (void *)(((uint8_t *)&F.l11) + 40);
            void * t65 = (void *)(((uint8_t *)&F.l11) + 72);
            void * t66 = gm_l1;
            gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 128));
            void * t68 = gm_f_while_loop_86vcgbb((void *)NULL, cx, NULL, t63, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l11)), t64, t65, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t69 + 8), t68);
            memmove(gm_out, ((uint8_t *)&t69), 32);
            goto gm_fdone;
        }
gm_lb10:;
        {
            uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)8ULL));
            if (!t71) {
                goto gm_lb11;
            }
            memmove(((uint8_t *)&F.l12), ((((uint8_t *)t17) + 8) + 8), 40);
            void * t72 = gm_l0;
            void * t73 = (void *)((uint8_t *)&F.l12);
            void * t74 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t72, t73, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t75 + 8), t74);
            memmove(gm_out, ((uint8_t *)&t75), 32);
            goto gm_fdone;
        }
gm_lb11:;
        {
            uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)3ULL));
            if (!t77) {
                goto gm_lb12;
            }
            uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 8));
            gm_l13 = t78;
            void * t79 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 8));
            gm_l14 = t79;
            void * t80 = gm_l0;
            uint8_t t81 = gm_l13;
            void * t82 = gm_l14;
            gm_f_defer_stmt_hm15mru((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t80, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb12:;
        {
            uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)4ULL));
            if (!t84) {
                goto gm_lb13;
            }
            void * t85 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l15 = t85;
            void * t86 = gm_l0;
            void * t87 = gm_l15;
            void * t88 = gm_f_when_stmt_3pk7eon((void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t89;
            GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t89 + 8), t88);
            memmove(gm_out, ((uint8_t *)&t89), 32);
            goto gm_fdone;
        }
gm_lb13:;
        {
            uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)6ULL));
            if (!t91) {
                goto gm_lb14;
            }
            void * t92 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l16 = t92;
            void * t93 = gm_l0;
            void * t94 = gm_l16;
            void * t95 = gm_l1;
            gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t95) + 128));
            void * t97 = gm_f_cancel_stmt_pfi7e6b((void *)NULL, cx, NULL, t93, t94, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t98;
            GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t98 + 8), t97);
            memmove(gm_out, ((uint8_t *)&t98), 32);
            goto gm_fdone;
        }
gm_lb14:;
        {
            uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)7ULL));
            if (!t100) {
                goto gm_lb15;
            }
            void * t101 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 64));
            gm_l17 = t101;
            void * t102 = gm_l0;
            void * t103 = gm_l1;
            gm_tl874t2atnlh3 t104 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t103) + 128));
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_ss6qrj79r47rb));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(69));
            gm_b0_1 t106 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t102, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t107 = gm_l0;
            void * t108 = gm_l17;
            gm_tkn42u7na3t81 t109;
            memset(&t109, 0, sizeof t109);
            void * t110 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t107, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t111;
            GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t111 + 8), t110);
            memmove(gm_out, ((uint8_t *)&t111), 32);
            goto gm_fdone;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 805, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_diverges_en1hv79 F;
    void * gm_l0; /* s */
    gm_trcmmd12cefb1 gm_l1; /* st */
    void * gm_l2; /* e */
    void * gm_l3; /* x */
    void * gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 534u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)4ULL));
            if (!t13) {
                goto gm_lb2;
            }
            void * t14 = GM_LD(void *, (((uint8_t *)a1) + 8));
            gm_l2 = t14;
            void * t15 = gm_l0;
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            void * t17 = gm_l2;
            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
            gm_t53at80pfdus3 t19;
            gm_f_kind_jcnt4hm((void *)&t19, t16, GM_CONV(gm_csguankko4lmhd, t18));
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
            uint8_t t21 = (uint8_t)((t20 == ((uint8_t)5ULL)));
            gm_t53at80pfdus3 t22;
            memset(&t22, 0, sizeof t22);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)5ULL));
            gm_l5 = t21;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
            if (!t24) {
                goto gm_lb3;
            }
            uint8_t t25 = (uint8_t)(GM_LD(void *, ((((uint8_t *)a1) + 8) + 8)) != 0);
            if (t25) {
                goto gm_lb3;
            }
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb4;
            }
            uint8_t t28 = (uint8_t)(GM_LD(void *, ((((uint8_t *)a1) + 8) + 8)) != 0);
            if (!t28) {
                goto gm_lb4;
            }
            void * t29 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
            gm_l3 = t29;
            void * t30 = gm_l0;
            void * t31 = (void *)(((uint8_t *)t30) + 8);
            void * t32 = gm_l3;
            gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t32));
            gm_t53at80pfdus3 t34;
            gm_f_kind_jcnt4hm((void *)&t34, t31, GM_CONV(gm_csguankko4lmhd, t33));
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
            uint8_t t36 = (uint8_t)((t35 == ((uint8_t)5ULL)));
            gm_t53at80pfdus3 t37;
            memset(&t37, 0, sizeof t37);
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)5ULL));
            gm_l5 = t36;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)2ULL));
            if (!t39) {
                goto gm_lb5;
            }
            void * t40 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
            gm_l4 = t40;
            void * t41 = gm_l0;
            void * t42 = (void *)(((uint8_t *)t41) + 8);
            void * t43 = gm_l4;
            gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t43));
            gm_t53at80pfdus3 t45;
            gm_f_kind_jcnt4hm((void *)&t45, t42, GM_CONV(gm_csguankko4lmhd, t44));
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
            uint8_t t47 = (uint8_t)((t46 == ((uint8_t)5ULL)));
            gm_t53at80pfdus3 t48;
            memset(&t48, 0, sizeof t48);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)5ULL));
            gm_l5 = t47;
            goto gm_lb1;
        }
gm_lb5:;
        {
            {
                {
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
                    if (!t50) {
                        goto gm_lb8;
                    }
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)3ULL));
                    if (!t52) {
                        goto gm_lb9;
                    }
                    goto gm_lb7;
                }
gm_lb9:;
                {
                    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)5ULL));
                    if (!t54) {
                        goto gm_lb10;
                    }
                    goto gm_lb7;
                }
gm_lb10:;
                goto gm_lb6;
            }
gm_lb7:;
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 999, 56);
    }
gm_lb1:;
    uint8_t t55 = gm_l5;
    F.ret = t55;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79_c(void * a0, gm_trcmmd12cefb1 a1) { return gm_f_stmt_diverges_en1hv79(a0, &a1); }
__attribute__((aligned(4))) void * gm_f_stmt_expr_evs4ic8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_expr_evs4ic8 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* i */
    void * gm_l4; /* f */
    void * gm_l5; /* m */
    void * gm_l6; /* w */
    uint64_t gm_l7; /* errors */
    void * gm_l8; /* x */
    void * gm_l9; /* ex */
    GM_STACK_CHECK(&F, 560u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * gm_l10;
    gm_tftb1odc739t5 gm_l11;
    void * t9 = gm_l1;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)40ULL));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l2 = t12;
            void * t13 = gm_l0;
            void * t14 = gm_l2;
            gm_tkn42u7na3t81 t15;
            memset(&t15, 0, sizeof t15);
            void * t16 = gm_f_if_expr_tiak47t((void *)NULL, cx, NULL, t13, t14, ((uint8_t)0), t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t16;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)44ULL));
            if (!t18) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t9) + 8) + 8), 40);
            void * t19 = gm_l0;
            void * t20 = (void *)((uint8_t *)&F.l3);
            void * t21 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t19, t20, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t21;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)43ULL));
            if (!t23) {
                goto gm_lb4;
            }
            void * t24 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l4 = t24;
            void * t25 = gm_l0;
            void * t26 = gm_l4;
            void * t27 = gm_f_for_loop_b6hjsbq((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t27;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)41ULL));
            if (!t29) {
                goto gm_lb5;
            }
            void * t30 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l5 = t30;
            void * t31 = gm_l0;
            void * t32 = gm_l5;
            gm_tkn42u7na3t81 t33;
            memset(&t33, 0, sizeof t33);
            void * t34 = gm_f_match_expr_npcoo60((void *)NULL, cx, NULL, t31, t32, ((uint8_t)0), t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t34;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)42ULL));
            if (!t36) {
                goto gm_lb6;
            }
            void * t37 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l6 = t37;
            void * t38 = gm_l0;
            void * t39 = gm_l6;
            gm_tkn42u7na3t81 t40;
            memset(&t40, 0, sizeof t40);
            void * t41 = gm_f_with_expr_vi78dtj((void *)NULL, cx, NULL, t38, t39, ((uint8_t)0), t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t41;
            goto gm_lb1;
        }
gm_lb6:;
        {
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, ((uint8_t *)t42));
            void * t44 = GM_LD(void *, ((uint8_t *)t43));
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 8));
            uint64_t t46 = gm_f_error_count_omtt6qd(t45);
            gm_l7 = t46;
            void * t47 = gm_l0;
            void * t48 = gm_l1;
            gm_tkn42u7na3t81 t49;
            memset(&t49, 0, sizeof t49);
            void * t50 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t47, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t50;
            void * t51 = gm_l0;
            void * t52 = GM_LD(void *, ((uint8_t *)t51));
            void * t53 = GM_LD(void *, ((uint8_t *)t52));
            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 8));
            uint64_t t55 = gm_f_error_count_omtt6qd(t54);
            uint64_t t56 = gm_l7;
            uint8_t t57 = (!(t55 == t56));
            if (t57) {
                void * t58 = gm_l8;
                F.ret = t58;
                goto gm_fret;
            }
            void * t59 = gm_l8;
            gm_l10 = t59;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 875, 11);
    }
gm_lb1:;
    void * t60 = gm_l10;
    gm_l9 = t60;
    void * t61 = gm_l0;
    void * t62 = (void *)(((uint8_t *)t61) + 320);
    void * t63 = gm_l9;
    gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t63));
    void * t65 = gm_l1;
    gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
    gm_t2i8ne5ibd7v4 t67;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t67 + 0), t64);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t67 + 4), t66);
    gm_tpm220j5qhjke t68;
    GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t68 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t68 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t68 + 40, 0);
    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)&t68 + 8), t67);
    gm_tftb1odc739t5 t69 = gm_f_push_7koe9lk(t62, &t68);
    gm_l11 = t69;
    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_src276s8986o4r3, 890, 5);
    void * t71 = gm_l9;
    F.ret = t71;
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
inline __attribute__((aligned(4))) void gm_f_stmt_never_cause_r2lf089(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_never_cause_r2lf089 F;
    void * gm_l0; /* s */
    gm_trcmmd12cefb1 gm_l1; /* st */
    void * gm_l2; /* e */
    void * gm_l3; /* x */
    void * gm_l4; /* v */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)4ULL));
            if (!t13) {
                goto gm_lb2;
            }
            void * t14 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
            gm_l2 = t14;
            void * t15 = gm_l0;
            void * t16 = gm_l2;
            gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t15, t16);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb3;
            }
            uint8_t t19 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8)) != 0);
            if (t19) {
                goto gm_lb3;
            }
            gm_tnvnsr1bfvtbd t20;
            memset(&t20, 0, sizeof t20);
            memmove(gm_out, ((uint8_t *)&t20), 40);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
            if (!t22) {
                goto gm_lb4;
            }
            uint8_t t23 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8)) != 0);
            if (!t23) {
                goto gm_lb4;
            }
            void * t24 = GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8));
            gm_l3 = t24;
            void * t25 = gm_l0;
            void * t26 = gm_l3;
            gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t25, t26);
            goto gm_fdone;
        }
gm_lb4:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)2ULL));
            if (!t28) {
                goto gm_lb5;
            }
            void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8));
            gm_l4 = t29;
            void * t30 = gm_l0;
            void * t31 = gm_l4;
            gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t30, t31);
            goto gm_fdone;
        }
gm_lb5:;
        {
            {
                {
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)1ULL));
                    if (!t33) {
                        goto gm_lb8;
                    }
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)3ULL));
                    if (!t35) {
                        goto gm_lb9;
                    }
                    goto gm_lb7;
                }
gm_lb9:;
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)5ULL));
                    if (!t37) {
                        goto gm_lb10;
                    }
                    goto gm_lb7;
                }
gm_lb10:;
                goto gm_lb6;
            }
gm_lb7:;
            gm_tnvnsr1bfvtbd t38;
            memset(&t38, 0, sizeof t38);
            memmove(gm_out, ((uint8_t *)&t38), 40);
            goto gm_fdone;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 1009, 5);
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_stmt_never_cause_r2lf089_c(void * a0, gm_trcmmd12cefb1 a1) { gm_tnvnsr1bfvtbd gm_r; gm_f_stmt_never_cause_r2lf089(&gm_r, a0, &a1); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_swap_frame_4j63sdr_c(void * a0, gm_tjhr0bdm69ios a1) { gm_tjhr0bdm69ios gm_r; gm_f_swap_frame_4j63sdr(&gm_r, a0, &a1); return gm_r; }
extern __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_try_untyped_8j8up35 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    void * gm_l5; /* inner */
    void * gm_l6; /* operand */
    gm_b16_8 gm_l10; /* m */
    gm_ts6oqppjh0ba3 gm_l11; /* op */
    void * gm_l12; /* lhs */
    void * gm_l13; /* rhs */
    gm_b16_8 gm_l19; /* m */
    GM_STACK_CHECK(&F, 7688u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqvu78rpi6q1h gm_l20;
    gm_t930q5l38hdap gm_l21;
    gm_tkv1vap90vht2 gm_l22;
    gm_t5vbkdq81qfs7 gm_l23;
    gm_t4t568o7qnv02 gm_l24;
    gm_tkv1vap90vht2 gm_l25;
    gm_t5vbkdq81qfs7 gm_l26;
    gm_tkv1vap90vht2 gm_l27;
    gm_t5vbkdq81qfs7 gm_l28;
    gm_t4t568o7qnv02 gm_l29;
    void * t22 = gm_l1;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)6ULL));
            if (!t24) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t22) + 8) + 8), 32);
            void * t25 = gm_l0;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_f_lookup_local_6f38t7p((void *)&gm_l20, t25, t26);
            uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l20))[208] != 0);
            if (t27) {
                memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l20), 208);
            } else {
                void * t28 = gm_l0;
                void * t29 = GM_LD(void *, ((uint8_t *)t28));
                void * t30 = gm_l0;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 8));
                void * t32 = gm_l1;
                gm_f_try_untyped_fj0h8i5((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t29, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l21), 208);
            {
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)1ULL));
                    if (!t34) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l4), (((uint8_t *)&F.l3) + 8), 200);
                    gm_t0ialelq3ql1k t35 = GM_LD(gm_t0ialelq3ql1k, ((uint8_t *)&F.l4));
                    gm_t5vbkdq81qfs7 t36;
                    GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t36 + 8), t35);
                    gm_tkv1vap90vht2 t37;
                    GM_ST(uint8_t, (uint8_t *)&t37 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t37 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t37 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t37), t36);
                    ((uint8_t *)&t37)[208] = 1;
                    memmove(gm_out, ((uint8_t *)&t37), 216);
                    goto gm_fdone;
                }
gm_lb4:;
                gm_tkv1vap90vht2 t38;
                memset(&t38, 0, sizeof t38);
                memmove(gm_out, ((uint8_t *)&t38), 216);
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        {
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)9ULL));
            if (!t40) {
                goto gm_lb5;
            }
            void * t41 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l5 = t41;
            void * t42 = gm_l0;
            void * t43 = gm_l5;
            gm_f_try_untyped_8j8up35((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb5:;
        {
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)12ULL));
            if (!t45) {
                goto gm_lb6;
            }
            uint8_t t46 = GM_LD(uint8_t, (((((uint8_t *)t22) + 8) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)0ULL));
            if (!t47) {
                goto gm_lb6;
            }
            void * t48 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l6 = t48;
            void * t49 = gm_l0;
            void * t50 = gm_l6;
            gm_f_try_untyped_8j8up35((void *)&gm_l22, (void *)NULL, cx, NULL, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t51 = (uint8_t)((((uint8_t *)&gm_l22))[208] != 0);
            if (t51) {
                memmove(((uint8_t *)&gm_l23), ((uint8_t *)&gm_l22), 208);
            } else {
                gm_tkv1vap90vht2 t52;
                memset(&t52, 0, sizeof t52);
                memmove(gm_out, ((uint8_t *)&t52), 216);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l23), 208);
            {
                {
                    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)0ULL));
                    if (!t54) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&F.l8), (((uint8_t *)&F.l7) + 8), 200);
                    void * t55 = (void *)((uint8_t *)&F.l8);
                    gm_f_negate_fohael7((void *)&gm_l24, (void *)NULL, cx, NULL, t55);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)0ULL));
                            if (!t57) {
                                goto gm_lb10;
                            }
                            memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l24) + 8), 200);
                            gm_t0ialelq3ql1k t58 = GM_LD(gm_t0ialelq3ql1k, ((uint8_t *)&F.l9));
                            gm_t5vbkdq81qfs7 t59;
                            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t59 + 8), t58);
                            gm_tkv1vap90vht2 t60;
                            GM_ST(uint8_t, (uint8_t *)&t60 + 209, 0);
                            GM_ST(uint16_t, (uint8_t *)&t60 + 210, 0);
                            GM_ST(uint32_t, (uint8_t *)&t60 + 212, 0);
                            GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t60), t59);
                            ((uint8_t *)&t60)[208] = 1;
                            memmove(gm_out, ((uint8_t *)&t60), 216);
                            goto gm_fdone;
                        }
gm_lb10:;
                        {
                            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)1ULL));
                            if (!t62) {
                                goto gm_lb11;
                            }
                            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                            gm_l10 = t63;
                            void * t64 = gm_l0;
                            void * t65 = gm_l1;
                            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                            gm_b16_8 t67 = gm_l10;
                            gm_b0_1 t68 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t66, t67);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t5vbkdq81qfs7 t69;
                            memset(&t69, 0, sizeof t69);
                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
                            gm_tkv1vap90vht2 t70;
                            GM_ST(uint8_t, (uint8_t *)&t70 + 209, 0);
                            GM_ST(uint16_t, (uint8_t *)&t70 + 210, 0);
                            GM_ST(uint32_t, (uint8_t *)&t70 + 212, 0);
                            GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t70), t69);
                            ((uint8_t *)&t70)[208] = 1;
                            memmove(gm_out, ((uint8_t *)&t70), 216);
                            goto gm_fdone;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 460, 13);
                    }
gm_lb9:;
                }
gm_lb8:;
                gm_t5vbkdq81qfs7 t71;
                memset(&t71, 0, sizeof t71);
                GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)1ULL));
                gm_tkv1vap90vht2 t72;
                GM_ST(uint8_t, (uint8_t *)&t72 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t72 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t72 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t72), t71);
                ((uint8_t *)&t72)[208] = 1;
                memmove(gm_out, ((uint8_t *)&t72), 216);
                goto gm_fdone;
            }
gm_lb7:;
        }
gm_lb6:;
        {
            uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)13ULL));
            if (!t74) {
                goto gm_lb12;
            }
            gm_ts6oqppjh0ba3 t75 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t22) + 8) + 8));
            gm_l11 = t75;
            void * t76 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l12 = t76;
            void * t77 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 16));
            gm_l13 = t77;
            gm_ts6oqppjh0ba3 t78 = gm_l11;
            uint8_t t79 = gm_f_untyped_op_7an9o22(t78);
            uint8_t t80 = (uint8_t)!t79;
            if (t80) {
                gm_tkv1vap90vht2 t81;
                memset(&t81, 0, sizeof t81);
                memmove(gm_out, ((uint8_t *)&t81), 216);
                goto gm_fdone;
            }
            void * t82 = gm_l0;
            void * t83 = gm_l12;
            gm_f_try_untyped_8j8up35((void *)&gm_l25, (void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t84 = (uint8_t)((((uint8_t *)&gm_l25))[208] != 0);
            if (t84) {
                memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l25), 208);
            } else {
                gm_tkv1vap90vht2 t85;
                memset(&t85, 0, sizeof t85);
                memmove(gm_out, ((uint8_t *)&t85), 216);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l14), ((uint8_t *)&gm_l26), 208);
            void * t86 = gm_l0;
            void * t87 = gm_l13;
            gm_f_try_untyped_8j8up35((void *)&gm_l27, (void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t88 = (uint8_t)((((uint8_t *)&gm_l27))[208] != 0);
            if (t88) {
                memmove(((uint8_t *)&gm_l28), ((uint8_t *)&gm_l27), 208);
            } else {
                gm_tkv1vap90vht2 t89;
                memset(&t89, 0, sizeof t89);
                memmove(gm_out, ((uint8_t *)&t89), 216);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l15), ((uint8_t *)&gm_l28), 208);
            {
                {
                    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)0ULL));
                    if (!t91) {
                        goto gm_lb14;
                    }
                    memmove(((uint8_t *)&F.l16), (((uint8_t *)&F.l14) + 8), 200);
                    {
                        {
                            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)0ULL));
                            if (!t93) {
                                goto gm_lb16;
                            }
                            memmove(((uint8_t *)&F.l17), (((uint8_t *)&F.l15) + 8), 200);
                            gm_ts6oqppjh0ba3 t94 = gm_l11;
                            void * t95 = (void *)((uint8_t *)&F.l16);
                            void * t96 = (void *)((uint8_t *)&F.l17);
                            gm_f_binary_9sb7899((void *)&gm_l29, (void *)NULL, cx, NULL, t94, t95, t96);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                    uint8_t t98 = (uint8_t)(t97 == ((uint8_t)0ULL));
                                    if (!t98) {
                                        goto gm_lb18;
                                    }
                                    memmove(((uint8_t *)&F.l18), (((uint8_t *)&gm_l29) + 8), 200);
                                    gm_t0ialelq3ql1k t99 = GM_LD(gm_t0ialelq3ql1k, ((uint8_t *)&F.l18));
                                    gm_t5vbkdq81qfs7 t100;
                                    GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t100 + 8), t99);
                                    gm_tkv1vap90vht2 t101;
                                    GM_ST(uint8_t, (uint8_t *)&t101 + 209, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t101 + 210, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t101 + 212, 0);
                                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t101), t100);
                                    ((uint8_t *)&t101)[208] = 1;
                                    memmove(gm_out, ((uint8_t *)&t101), 216);
                                    goto gm_fdone;
                                }
gm_lb18:;
                                {
                                    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                    uint8_t t103 = (uint8_t)(t102 == ((uint8_t)1ULL));
                                    if (!t103) {
                                        goto gm_lb19;
                                    }
                                    gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                                    gm_l19 = t104;
                                    void * t105 = gm_l0;
                                    void * t106 = gm_l1;
                                    gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 72));
                                    gm_b16_8 t108 = gm_l19;
                                    gm_b0_1 t109 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t105, t107, t108);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t5vbkdq81qfs7 t110;
                                    memset(&t110, 0, sizeof t110);
                                    GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)1ULL));
                                    gm_tkv1vap90vht2 t111;
                                    GM_ST(uint8_t, (uint8_t *)&t111 + 209, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t111 + 210, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t111 + 212, 0);
                                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t111), t110);
                                    ((uint8_t *)&t111)[208] = 1;
                                    memmove(gm_out, ((uint8_t *)&t111), 216);
                                    goto gm_fdone;
                                }
gm_lb19:;
                                gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 474, 13);
                            }
gm_lb17:;
                        }
gm_lb16:;
                        gm_t5vbkdq81qfs7 t112;
                        memset(&t112, 0, sizeof t112);
                        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
                        gm_tkv1vap90vht2 t113;
                        GM_ST(uint8_t, (uint8_t *)&t113 + 209, 0);
                        GM_ST(uint16_t, (uint8_t *)&t113 + 210, 0);
                        GM_ST(uint32_t, (uint8_t *)&t113 + 212, 0);
                        GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t113), t112);
                        ((uint8_t *)&t113)[208] = 1;
                        memmove(gm_out, ((uint8_t *)&t113), 216);
                        goto gm_fdone;
                    }
gm_lb15:;
                }
gm_lb14:;
                gm_t5vbkdq81qfs7 t114;
                memset(&t114, 0, sizeof t114);
                GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)1ULL));
                gm_tkv1vap90vht2 t115;
                GM_ST(uint8_t, (uint8_t *)&t115 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t115 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t115 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t115), t114);
                ((uint8_t *)&t115)[208] = 1;
                memmove(gm_out, ((uint8_t *)&t115), 216);
                goto gm_fdone;
            }
gm_lb13:;
        }
gm_lb12:;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, ((uint8_t *)t116));
            void * t118 = gm_l0;
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)t118) + 8));
            void * t120 = gm_l1;
            gm_f_try_untyped_fj0h8i5((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t117, t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 450, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_uninit_message_u2c3peu F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* hint */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l3;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t6, GM_CONV(gm_csguankko4lmhd, t7));
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)11ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 960, 16);
    }
gm_lb1:;
    uint8_t t10 = gm_l4;
    if (t10) {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sadg245agtuao));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(44));
        gm_l3 = t11;
    } else {
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
        gm_l3 = t12;
    }
    gm_b16_8 t13 = gm_l3;
    gm_l2 = t13;
    void * t14 = gm_l0;
    gm_t88rcucg1b7uk t15 = gm_l1;
    gm_b16_8 t16 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t17 = gm_l2;
    gm_t6j6smj7qoctu t18;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t16);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 16), t17);
    gm_f_format_cn0m7al((void *)&gm_l6, (void *)NULL, cx, NULL, &t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src276s8986o4r3, 961, 12);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
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
extern __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
extern __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_untyped_op_7an9o22(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_untyped_op_7an9o22 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    {
        {
            {
                {
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)0ULL));
                    if (!t25) {
                        goto gm_lb4;
                    }
                    gm_t9qkh0rsooc8d t26 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
                    gm_l1 = t26;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                    if (!t28) {
                        goto gm_lb5;
                    }
                    gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
                    gm_l1 = t29;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
                    gm_l1 = t32;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)3ULL));
                    if (!t34) {
                        goto gm_lb7;
                    }
                    gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
                    gm_l1 = t35;
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_t9qkh0rsooc8d t36 = gm_l1;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            uint8_t t38 = (uint8_t)((t37 == ((uint8_t)0ULL)));
            gm_t9qkh0rsooc8d t39;
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
            gm_l5 = t38;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)4ULL));
                    if (!t41) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)5ULL));
                    if (!t43) {
                        goto gm_lb11;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)6ULL));
                    if (!t45) {
                        goto gm_lb12;
                    }
                    goto gm_lb9;
                }
gm_lb12:;
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)7ULL));
                    if (!t47) {
                        goto gm_lb13;
                    }
                    goto gm_lb9;
                }
gm_lb13:;
                {
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)8ULL));
                    if (!t49) {
                        goto gm_lb14;
                    }
                    goto gm_lb9;
                }
gm_lb14:;
                {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)9ULL));
                    if (!t51) {
                        goto gm_lb15;
                    }
                    goto gm_lb9;
                }
gm_lb15:;
                {
                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)16ULL));
                    if (!t53) {
                        goto gm_lb16;
                    }
                    goto gm_lb9;
                }
gm_lb16:;
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)17ULL));
                    if (!t55) {
                        goto gm_lb17;
                    }
                    goto gm_lb9;
                }
gm_lb17:;
                {
                    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t57 = (uint8_t)(t56 == ((uint8_t)18ULL));
                    if (!t57) {
                        goto gm_lb18;
                    }
                    goto gm_lb9;
                }
gm_lb18:;
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)19ULL));
                    if (!t59) {
                        goto gm_lb19;
                    }
                    goto gm_lb9;
                }
gm_lb19:;
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)12ULL));
                    if (!t61) {
                        goto gm_lb20;
                    }
                    goto gm_lb9;
                }
gm_lb20:;
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)13ULL));
                    if (!t63) {
                        goto gm_lb21;
                    }
                    goto gm_lb9;
                }
gm_lb21:;
                goto gm_lb8;
            }
gm_lb9:;
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb8:;
        {
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb22:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 487, 44);
    }
gm_lb1:;
    uint8_t t64 = gm_l5;
    F.ret = t64;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_when_stmt_3pk7eon(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_when_stmt_3pk7eon F;
    void * gm_l0; /* cx */
    void * gm_l1; /* w */
    uint8_t gm_l2; /* taken */
    void * gm_l4; /* inner */
    GM_STACK_CHECK(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l5;
    uint8_t gm_l6;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    void * t10 = gm_l1;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    gm_tqs5p4n2tq19v t12 = gm_f_body_when_cond_2coms3p((void *)NULL, cx, NULL, t7, t9, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t12;
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
    if (t13) {
        uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
        gm_l6 = t14;
    } else {
        void * t15 = gm_l0;
        void * t16 = GM_LD(void *, ((uint8_t *)t15));
        void * t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 104));
        void * t19 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t16, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t19;
        goto gm_fret;
    }
    uint8_t t20 = gm_l6;
    gm_l2 = t20;
    uint8_t t21 = gm_l2;
    if (t21) {
        void * t22 = gm_l0;
        void * t23 = gm_l1;
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        void * t25 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t22, t24, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t25;
        goto gm_fret;
    }
    void * t26 = gm_l1;
    {
        {
            uint8_t t27 = (uint8_t)(((((uint8_t *)t26) + 48))[48] != 0);
            if (!t27) {
                goto gm_lb2;
            }
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t26) + 48) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
            if (!t29) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t26) + 48) + 8), 40);
            void * t30 = gm_l0;
            void * t31 = (void *)((uint8_t *)&F.l3);
            void * t32 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t30, t31, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t32;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t33 = (uint8_t)(((((uint8_t *)t26) + 48))[48] != 0);
            if (!t33) {
                goto gm_lb3;
            }
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t26) + 48) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)1ULL));
            if (!t35) {
                goto gm_lb3;
            }
            void * t36 = GM_LD(void *, ((((uint8_t *)t26) + 48) + 8));
            gm_l4 = t36;
            void * t37 = gm_l0;
            void * t38 = gm_l4;
            void * t39 = gm_f_when_stmt_3pk7eon((void *)NULL, cx, NULL, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t39;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t40 = (uint8_t)(((((uint8_t *)t26) + 48))[48] != 0);
            if (t40) {
                goto gm_lb4;
            }
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, ((uint8_t *)t41));
            void * t43 = gm_l1;
            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 104));
            void * t45 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t42, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t45;
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 831, 5);
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
__attribute__((aligned(4))) void * gm_f_while_loop_86vcgbb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_while_loop_86vcgbb F;
    void * gm_l0; /* cx */
    void * gm_l2; /* cond */
    void * gm_l3; /* body */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_th2lqbo77pvsc gm_l5; /* name */
    gm_t08olq97rscn3 gm_l6; /* l */
    gm_b16_8 gm_l7; /* c */
    uint32_t gm_l8; /* id */
    void * gm_l9; /* b */
    uint8_t gm_l10; /* infinite */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_tlr9dp305htm7 gm_l13; /* part */
    void * gm_l14; /* x */
    uint8_t gm_l15; /* breaks */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    void * gm_l17; /* blk */
    GM_STACK_CHECK(&F, 1170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t88rcucg1b7uk gm_l18;
    void * t16 = gm_l0;
    gm_b0_1 t17 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t18;
    memset(&t18, 0, sizeof t18);
    gm_l5 = t18;
    {
        {
            uint8_t t19 = (uint8_t)((((uint8_t *)&F.l1))[32] != 0);
            if (!t19) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l6), ((uint8_t *)&F.l1), 32);
            void * t20 = gm_l0;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 16));
            gm_t930q5l38hdap t23;
            memset(&t23, 0, sizeof t23);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)2ULL));
            gm_b0_1 t24 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t20, t21, &t23, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_th2lqbo77pvsc t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[16] = 1;
            gm_l5 = t26;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t27 = gm_l0;
    void * t28 = gm_l2;
    gm_b16_8 t29 = gm_f_cond_99ajlhk((void *)NULL, cx, NULL, t27, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t29;
    void * t30 = gm_l0;
    gm_t1qr0p6kln6tf t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
    uint32_t t32 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t30, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l5)), ((uint8_t)1), t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t32;
    void * t33 = gm_l0;
    void * t34 = gm_l3;
    void * t35 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t33, t34, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t35;
    void * t36 = gm_l0;
    void * t37 = (void *)(((uint8_t *)t36) + 232);
    gm_te5gbhu3us3k4 t38;
    gm_f_pop_ruvva9t((void *)&t38, t37);
    void * t39 = gm_l0;
    gm_f_pop_scope_g5j7o4e(t39);
    gm_l10 = ((uint8_t)1);
    gm_b16_8 t40 = gm_l7;
    gm_l11 = t40;
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t41 = gm_l12;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint8_t t43 = (t41 < t42);
            if (!t43) {
                goto gm_lb3;
            }
            {
                void * t44 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint64_t t46 = gm_l12;
                {
                    {
                        memmove(((uint8_t *)&gm_l13), (((uint8_t *)t44) + (uintptr_t)t46 * 24), 24);
                        {
                            {
                                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                uint8_t t48 = (uint8_t)(t47 == ((uint8_t)0ULL));
                                if (!t48) {
                                    goto gm_lb9;
                                }
                                void * t49 = GM_LD(void *, (((uint8_t *)&gm_l13) + 8));
                                gm_l14 = t49;
                                void * t50 = gm_l14;
                                uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 0));
                                uint8_t t52 = (uint8_t)(t51 == ((uint8_t)4ULL));
                                if (t52) {
                                    uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 8));
                                    t52 = (uint8_t)(t53 == ((uint8_t)1));
                                }
                                uint8_t t54 = (uint8_t)(!t52);
                                gm_t2b6tm8c0vgo1 t55 = GM_LD(gm_t2b6tm8c0vgo1, (((uint8_t *)t50) + 8));
                                gm_t2b6tm8c0vgo1 t56;
                                memset(&t56, 0, sizeof t56);
                                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)4ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t56 + 8), ((uint8_t)1));
                                if (t54) {
                                    gm_l10 = ((uint8_t)0);
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                            {
                                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                uint8_t t58 = (uint8_t)(t57 == ((uint8_t)1ULL));
                                if (!t58) {
                                    goto gm_lb10;
                                }
                                gm_l10 = ((uint8_t)0);
                                goto gm_lb8;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 912, 9);
                        }
gm_lb8:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 911, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t59 = gm_l12;
            uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
            uint64_t t62 = r60;
            gm_l12 = t62;
        }
    }
gm_lb3:;
    void * t63 = gm_l0;
    uint32_t t64 = gm_l8;
    uint8_t t65 = gm_f_forget_label_breaks_vl4mn72(t63, t64);
    gm_l15 = t65;
    {
        {
            uint8_t t66 = gm_l10;
            if (!t66) {
                goto gm_lb12;
            }
            uint8_t t67 = gm_l15;
            uint8_t t68 = (uint8_t)!t67;
            if (!t68) {
                goto gm_lb12;
            }
            void * t69 = gm_l0;
            void * t70 = GM_LD(void *, ((uint8_t *)t69));
            void * t71 = (void *)(((uint8_t *)t70) + 8);
            gm_t88rcucg1b7uk t72 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t72;
            goto gm_lb11;
        }
gm_lb12:;
        void * t73 = gm_l0;
        void * t74 = GM_LD(void *, ((uint8_t *)t73));
        void * t75 = (void *)(((uint8_t *)t74) + 8);
        gm_t88rcucg1b7uk t76 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t76;
    }
gm_lb11:;
    gm_t88rcucg1b7uk t77 = gm_l18;
    gm_l16 = t77;
    {
        {
            void * t78 = gm_l9;
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t78) + 8) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)46ULL));
            if (!t80) {
                goto gm_lb14;
            }
            void * t81 = GM_LD(void *, ((((uint8_t *)t78) + 8) + 8));
            gm_l17 = t81;
            gm_t88rcucg1b7uk t82 = gm_l16;
            uint32_t t83 = gm_l8;
            gm_b16_8 t84 = gm_l7;
            void * t85 = gm_l17;
            gm_t710lsrq1m8gk t86;
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t86 + 0), t83);
            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 8), t84);
            GM_ST(void *, ((uint8_t *)&t86 + 24), t85);
            GM_ST(void *, ((uint8_t *)&t86 + 32), ((void *)0));
            gm_tl874t2atnlh3 t87 = gm_l4;
            gm_t2b6tm8c0vgo1 t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t88 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t88 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)48ULL));
            GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t88 + 8), t86);
            void * t89 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t82, &t88, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t89;
            goto gm_fret;
        }
gm_lb14:;
        void * t90 = gm_l9;
        F.ret = t90;
        goto gm_fret;
    }
gm_lb13:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
