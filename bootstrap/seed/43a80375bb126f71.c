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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_t586m754bse2v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_td7tfovebirdn;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tgat3d8836orb;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_ti71ll5knnvij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; } gm_tn96msocmj81h;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; uint32_t f6; uint8_t pe[4]; } gm_tobu7tlrkigeq;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t586m754bse2v) == 120 && _Alignof(gm_t586m754bse2v) == 8 && offsetof(gm_t586m754bse2v, f0) == 0 && offsetof(gm_t586m754bse2v, f1) == 16 && offsetof(gm_t586m754bse2v, f2) == 32 && offsetof(gm_t586m754bse2v, f3) == 40 && offsetof(gm_t586m754bse2v, f4) == 56 && offsetof(gm_t586m754bse2v, f5) == 72 && offsetof(gm_t586m754bse2v, f6) == 88 && offsetof(gm_t586m754bse2v, f7) == 104, "layout of `([]u8, string, u32, []u8, []u8, []u8, []u8, []u8)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td7tfovebirdn) == 40 && _Alignof(gm_td7tfovebirdn) == 8 && offsetof(gm_td7tfovebirdn, f0) == 0 && offsetof(gm_td7tfovebirdn, f1) == 16 && offsetof(gm_td7tfovebirdn, f2) == 32, "layout of `([]u8, string, u32)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgat3d8836orb) == 40 && _Alignof(gm_tgat3d8836orb) == 8 && offsetof(gm_tgat3d8836orb, f0) == 0 && offsetof(gm_tgat3d8836orb, f1) == 4 && offsetof(gm_tgat3d8836orb, f2) == 8 && offsetof(gm_tgat3d8836orb, f3) == 24, "layout of `(u32, u32, string, string)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_ti71ll5knnvij) == 72 && _Alignof(gm_ti71ll5knnvij) == 8 && offsetof(gm_ti71ll5knnvij, f0) == 0 && offsetof(gm_ti71ll5knnvij, f1) == 16 && offsetof(gm_ti71ll5knnvij, f2) == 32 && offsetof(gm_ti71ll5knnvij, f3) == 40 && offsetof(gm_ti71ll5knnvij, f4) == 56, "layout of `(string, []u8, usize, []u8, string)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tn96msocmj81h) == 136 && _Alignof(gm_tn96msocmj81h) == 8 && offsetof(gm_tn96msocmj81h, f0) == 0 && offsetof(gm_tn96msocmj81h, f1) == 16 && offsetof(gm_tn96msocmj81h, f2) == 32 && offsetof(gm_tn96msocmj81h, f3) == 48 && offsetof(gm_tn96msocmj81h, f4) == 64 && offsetof(gm_tn96msocmj81h, f5) == 72 && offsetof(gm_tn96msocmj81h, f6) == 88 && offsetof(gm_tn96msocmj81h, f7) == 104 && offsetof(gm_tn96msocmj81h, f8) == 120, "layout of `(string, string, string, string, u64, string, string, string, string)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tobu7tlrkigeq) == 88 && _Alignof(gm_tobu7tlrkigeq) == 8 && offsetof(gm_tobu7tlrkigeq, f0) == 0 && offsetof(gm_tobu7tlrkigeq, f1) == 16 && offsetof(gm_tobu7tlrkigeq, f2) == 24 && offsetof(gm_tobu7tlrkigeq, f3) == 40 && offsetof(gm_tobu7tlrkigeq, f4) == 56 && offsetof(gm_tobu7tlrkigeq, f5) == 64 && offsetof(gm_tobu7tlrkigeq, f6) == 80, "layout of `([]u8, u32, string, string, u64, string, u32)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_call_closure_mha9p1e {
    gm_frame h;
    gm_tddtjfggdat0d l6; /* sig */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_fn_q122g0r {
    gm_frame h;
    gm_trqfvmjtt4sij l15; /* tys */
    gm_trqfvmjtt4sij l22; /* args */
    gm_trqfvmjtt4sij l28; /* rest */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_indirect_khk02ee {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* ps */
    gm_trqfvmjtt4sij l18; /* pargs */
    gm_b16_8 ret;
};
struct gm_fr_call_result_vg14o5p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_call_site_8n74to1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_callee_name_4k9m9s6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_calls_emit_plain_euobq8f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_calls_native_set_iq1d80t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_conv_attr_m0b89pc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_equal_hcuag9d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_format_stub_53r45gt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_ptr_type_f9efs3c {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* ps */
    gm_t9bmh80edou9c l5; /* <temporary array> */
    gm_b16_8 ret;
};
struct gm_fr_fn_ptr_type_plain_bi8eu4l {
    gm_frame h;
    gm_tddtjfggdat0d l2; /* sig */
    uint8_t ret;
};
struct gm_fr_fn_sink_stub_5ipff9h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_format_01h4tbj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0mp0khb {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_18tu4v6 {
    gm_frame h;
    gm_t586m754bse2v l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t586m754bse2v l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_35to3im {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_37aah2s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3o89hhj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3s8s1g6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_570tujl {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5p0qm94 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6m84e0r {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7tqfnpi {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_99gc605 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_drnt54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eir8aii {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g55p0hj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h6b2ofo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ht2r43m {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_itcjtai {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jp1qc7o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0bpsnf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lc73epv {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tif3elqk01bdl l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mgf81l5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n1vqu08 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nivudut {
    gm_frame h;
    gm_tgat3d8836orb l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tgat3d8836orb l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p19pl6n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qr5c6un {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r9dvh2e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ru16275 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_gamma_arg_ldsagr2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_gamma_args_jb6l3ik {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_gamma_value_rgbfhcc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_0v7c424 {
    gm_frame h;
    gm_tttl9nsdo1iu6 l1; /* args */
    gm_tttl9nsdo1iu6 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_3fol08t {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_98ea3qh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_bg1q0dr {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_ch11let {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_gg3n7rd {
    gm_frame h;
    gm_td7tfovebirdn l1; /* args */
    gm_td7tfovebirdn l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_i03em5a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_i5ai2tu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_icst3ps {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* args */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_lot5c36 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_qg7idri {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_qqk2cu4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_ubknr7g {
    gm_frame h;
    gm_t586m754bse2v l1; /* args */
    gm_t586m754bse2v l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_up3h3jj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_uu8i6ue {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_v355qja {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_native_op_r81a4at {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_out_call_uj833o2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_out_stub_ils4a69 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_perform_t77ghfg {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_piece_stub_m8mb7rc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_plain_args_o7acjm5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_plain_value_e380k1u {
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
struct gm_fr_push_k3l9nic {
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
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_result_var_rn01cvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_stub_perform_m3dbb7g {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
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
struct gm_fr_write_502hm86 {
    gm_frame h;
    gm_ti71ll5knnvij l1; /* args */
    gm_ti71ll5knnvij l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_6o17mnv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_fp9dg1h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_mv65o15 {
    gm_frame h;
    gm_tobu7tlrkigeq l1; /* args */
    gm_tobu7tlrkigeq l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_otf2npe {
    gm_frame h;
    gm_tn96msocmj81h l1; /* args */
    gm_tn96msocmj81h l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_scdecr6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_vp4v8ut {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) void gm_f_call_fn_q122g0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_call_indirect_khk02ee(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, uint8_t a7, uint8_t a8);
__attribute__((aligned(4))) gm_b16_8 gm_f_call_result_vg14o5p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_call_site_8n74to1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_calls_emit_plain_euobq8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) void gm_f_ctl_perform_be6h6eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_direct_perform_vc33l18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_ptr_type_f9efs3c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_fn_ptr_type_plain_bi8eu4l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_format_01h4tbj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_0mp0khb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_18tu4v6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_35to3im(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_37aah2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3o89hhj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3s8s1g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_570tujl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5p0qm94(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6hrn7j8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_6m84e0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7q3a2d9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7tqfnpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_drnt54k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eir8aii(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ev2so3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_g55p0hj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ht2r43m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_itcjtai(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jp1qc7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l0bpsnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lc73epv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mgf81l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_n1vqu08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n5jb9pb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nivudut(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_p19pl6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_r9dvh2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ru16275(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_arg_ldsagr2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_args_jb6l3ik(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ctl_oclllo3(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_temp_rrllgdr(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_0v7c424(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3fol08t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7afcl2l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8uttu8h(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9379f24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_98ea3qh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9ar5ccv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9fi0qm1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ao39327(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bg1q0dr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ch11let(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_fa323qa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gg3n7rd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_i03em5a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_i5ai2tu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_icst3ps(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_lot5c36(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_nlc1tuc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qg7idri(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rho83iv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rosnm2q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_slpvrc7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ubknr7g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_up3h3jj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v4quu36(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_may_unwind_irl2ktm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_native_op_r81a4at(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, const void * a4, gm_t88rcucg1b7uk a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_passed_spec_rvmvr8h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_plain_args_o7acjm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_queue_spec_khngsmv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stub_perform_m3dbb7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_502hm86(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_53s6qp5(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6o17mnv(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fp9dg1h(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gca9chf(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hvsp2g7(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_isshs6q(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_mv65o15(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_scdecr6(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vp4v8ut(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vq1ifl4(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s021pqnhopt43[5];
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0aqhubr4upni[7];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2r03t2n892pm[27];
extern const uint8_t gm_s312ibu3ccl18[12];
extern const uint8_t gm_s329kcr9363eu[11];
extern const uint8_t gm_s3f8u490f8neh[31];
extern const uint8_t gm_s3jl3afv8nlvv[4];
extern const uint8_t gm_s3vqae7u5ljtm[14];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4a6h3o2n8s33[94];
extern const uint8_t gm_s4n8c66u7mg94[19];
extern const uint8_t gm_s4q8cddc9473e[14];
extern const uint8_t gm_s4uh375udgni3[206];
extern const uint8_t gm_s519l85f26pmp[81];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5ie9tn1qnf6v[9];
extern const uint8_t gm_s5u07ahpa78ud[27];
extern const uint8_t gm_s6kp8voc0etjv[60];
extern const uint8_t gm_s6qop16borqmo[16];
extern const uint8_t gm_s71l0hs29ihls[39];
extern const uint8_t gm_s8o6lm1cn4eo5[3];
extern const uint8_t gm_s99236k10livu[17];
extern const uint8_t gm_s9go9vvkpcjpd[48];
extern const uint8_t gm_s9u3cqhrfmcru[13];
extern const uint8_t gm_satkhe9qra7p5[13];
extern const uint8_t gm_sauknalgur9no[11];
extern const uint8_t gm_sb6j3egm9oljk[5];
extern const uint8_t gm_sbejm626620qa[94];
extern const uint8_t gm_sbvdvkd7bikd7[25];
extern const uint8_t gm_sc4r4l9k6f577[3];
extern const uint8_t gm_sdu7bd5v9q750[65];
extern const uint8_t gm_seftt830rf80r[30];
extern const uint8_t gm_sf3fmcr9mrflm[15];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shfuf0tdpqgc0[320];
extern const uint8_t gm_shjri07u8kc84[27];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sigjilgl161c6[5];
extern const uint8_t gm_siihmb4jo3dsn[125];
extern const uint8_t gm_sil41urphp1ol[12];
extern const uint8_t gm_sjvtpaes95ji4[27];
extern const uint8_t gm_sk8csn7us23m3[9];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_skmsgkj4jeo7n[6];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sl84tf4h2e7v7[29];
extern const uint8_t gm_sl8of2tnl835f[6];
extern const uint8_t gm_slq58vfha8k9h[2];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_sn6kdlfrvokdu[7];
extern const uint8_t gm_sn9fg2capllh2[79];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_sofgn7ilt7vsg[17];
extern const uint8_t gm_soh97sflbds87[11];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_sono5n5lrjd7h[10];
extern const uint8_t gm_sp5b2hbslij9l[5];
extern const uint8_t gm_spkcu8lv7g8jc[5];
extern const uint8_t gm_spsh7mv5lihcc[34];
extern const uint8_t gm_sq75i0jov46r1[63];
extern const uint8_t gm_sqbq94vn3heqs[49];
extern const uint8_t gm_sqf3cuedcn484[21];
extern const uint8_t gm_sqi7tb51umq69[13];
extern const uint8_t gm_sqmpncmbmfe90[8];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sqvgecd0v5l54[208];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcmeaag4q4qrjl[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st6to3kuud2ge[46];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_stf7lvoc7oe54[16];
extern const uint8_t gm_stm46orgt9h8k[317];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_stuj0mv5nl55t[4];
extern const uint8_t gm_su2c9m69n1vjb[31];
extern const uint8_t gm_sufil5dud65av[62];
extern const uint8_t gm_sujs43rrijg57[102];
extern const uint8_t gm_suptd32i8k1qp[26];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_tmpl gm_tpl0qht8d5me9ih;
extern const gm_tmpl gm_tpl0v0dinrpdv6d;
extern const gm_tmpl gm_tpl0v2pofusugn6;
extern const gm_tmpl gm_tpl1blafg62e77e;
extern const gm_tmpl gm_tpl32o7ct8vibbi;
extern const gm_tmpl gm_tpl56mvlv70ecs8;
extern const gm_tmpl gm_tpl578lpoev0s9s;
extern const gm_tmpl gm_tpl5uqj3jbiq5f0;
extern const gm_tmpl gm_tpl6gmot6d7lh6n;
extern const gm_tmpl gm_tpl7b1ibjg22p4i;
extern const gm_tmpl gm_tpl7dh44jmr11le;
extern const gm_tmpl gm_tpl81nh98hgo1rk;
extern const gm_tmpl gm_tpl9bim66fl3b37;
extern const gm_tmpl gm_tpl9qchhbq9qlbm;
extern const gm_tmpl gm_tpla1fscog446dv;
extern const gm_tmpl gm_tplak7vr9q0b05e;
extern const gm_tmpl gm_tpldff5gvnhne6f;
extern const gm_tmpl gm_tple4upi9cflnq6;
extern const gm_tmpl gm_tple6jpca094i2j;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplf3en286s8fa5;
extern const gm_tmpl gm_tplf9q60in6m019;
extern const gm_tmpl gm_tplfrm74l83ht8l;
extern const gm_tmpl gm_tpli2ivqo4qj5ok;
extern const gm_tmpl gm_tplk1dctbs79mmk;
extern const gm_tmpl gm_tplk5t5jhqqvnff;
extern const gm_tmpl gm_tplkjh7li83dd9g;
extern const gm_tmpl gm_tplkp06s34ois53;
extern const gm_tmpl gm_tpll9g0ell7ocpk;
extern const gm_tmpl gm_tpllc6r78fq9cl8;
extern const gm_tmpl gm_tplmjmservvuqtp;
extern const gm_tmpl gm_tplmm5mgnjalbfr;
extern const gm_tmpl gm_tplnndmi09f8nur;
extern const gm_tmpl gm_tplnr2qhie4t5ta;
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const gm_tmpl gm_tplo4mr4l55q97r;
extern const gm_tmpl gm_tploc8iqi9f2el1;
extern const gm_tmpl gm_tplp4f8nloo6fdo;
extern const gm_tmpl gm_tplr9ocl0upgsti;
extern const gm_tmpl gm_tpls2ou5a11vcn7;
extern const gm_tmpl gm_tpls5bjfpqmr7e0;
extern const gm_tmpl gm_tplsfrupsaoln3v;
extern const gm_tmpl gm_tplsthb0beh35ts;
extern const gm_tmpl gm_tpltdh9l4qjtpek;
extern const gm_tmpl gm_tpltl0rfkesf8ms;
extern const gm_tmpl gm_tplu9669cm9mqja;
extern const gm_tmpl gm_tplue2kdcikgcof;
extern const gm_tmpl gm_tplv1shkj7l87mc;
extern const gm_tmpl gm_tplv8ckuk7p1s8m;
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
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_callee_name_4k9m9s6 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_b16_8 t4 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_calls_native_set_iq1d80t F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* res */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* r */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)a1))[16] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_l3 = t5;
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l3;
            gm_b16_8 t8 = gm_l2;
            gm_t6j6smj7qoctu t9;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t7);
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t8);
            gm_b0_1 t10 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t6, &t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conv_attr_m0b89pc F;
    uint8_t gm_l0; /* win64 */
    GM_STACK_CHECK_SHALLOW(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    uint8_t t2 = gm_l0;
    if (t2) {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skdpq4t6o94o2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
        gm_l1 = t3;
    } else {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(0));
        gm_l1 = t4;
    }
    gm_b16_8 t5 = gm_l1;
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 216));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 216) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vogsd72 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_0jmnnpk(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 16);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) void gm_f_format_01h4tbj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_01h4tbj F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn9fg2capllh2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(78));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0v2pofusugn6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_0mp0khb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0mp0khb F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbvdvkd7bikd7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldff5gvnhne6f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_18tu4v6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_18tu4v6 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqbq94vn3heqs));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_t586m754bse2v t3 = GM_LD(gm_t586m754bse2v, ((uint8_t *)a0));
    gm_f_format_7q3a2d9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllc6r78fq9cl8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_35to3im(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_35to3im F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjvtpaes95ji4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_n5jb9pb((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tploc8iqi9f2el1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_37aah2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_37aah2s F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdu7bd5v9q750));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9qchhbq9qlbm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3o89hhj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3o89hhj F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s519l85f26pmp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(80));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplf9q60in6m019), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3s8s1g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3s8s1g6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_stf7lvoc7oe54));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(15));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplmjmservvuqtp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_570tujl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_570tujl F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2r03t2n892pm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnr2qhie4t5ta), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5p0qm94(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5p0qm94 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl84tf4h2e7v7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl32o7ct8vibbi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6m84e0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6m84e0r F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sauknalgur9no));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1blafg62e77e), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7tqfnpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7tqfnpi F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_satkhe9qra7p5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfrm74l83ht8l), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_99gc605 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smhvmmegaur89));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnv6l3ecgreid), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_drnt54k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_drnt54k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s71l0hs29ihls));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(38));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplo4mr4l55q97r), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eir8aii(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_eir8aii F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn6kdlfrvokdu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplp4f8nloo6fdo), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g55p0hj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_g55p0hj F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sf3fmcr9mrflm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpls2ou5a11vcn7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ht2r43m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ht2r43m F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s329kcr9363eu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkjh7li83dd9g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_itcjtai(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_itcjtai F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4q8cddc9473e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls5bjfpqmr7e0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jp1qc7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jp1qc7o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sil41urphp1ol));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltl0rfkesf8ms), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l0bpsnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l0bpsnf F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqmpncmbmfe90));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnndmi09f8nur), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lc73epv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lc73epv F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5u07ahpa78ud));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tif3elqk01bdl t3 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
    gm_f_format_6hrn7j8((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7dh44jmr11le), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_n1vqu08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n1vqu08 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sujs43rrijg57));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(101));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7b1ibjg22p4i), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_nivudut(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nivudut F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s312ibu3ccl18));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_tgat3d8836orb t3 = GM_LD(gm_tgat3d8836orb, ((uint8_t *)a0));
    gm_f_format_ev2so3i((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplue2kdcikgcof), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_p19pl6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p19pl6n F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_soh97sflbds87));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplv8ckuk7p1s8m), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_r9dvh2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_r9dvh2e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spkcu8lv7g8jc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl56mvlv70ecs8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ru16275(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ru16275 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6kp8voc0etjv));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(59));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkp06s34ois53), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_t53at80pfdus3 t6;
    gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)5ULL)));
    gm_t53at80pfdus3 t9;
    memset(&t9, 0, sizeof t9);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)5ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_unit_n83ttnb(t4, GM_CONV(gm_csguankko4lmhd, t5));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (t4) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_l2 = t7;
    }
    uint8_t t8 = gm_l2;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_f_format_8n626pi((void *)&gm_l3, (void *)NULL, cx, NULL, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcht195tfrfuqo, 82, 91);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b16_8 t10 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t2, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_0v7c424(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_0v7c424 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq75i0jov46r1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_b0_1 t4 = gm_f_linef_nlc1tuc((void *)NULL, cx, NULL, t2, &(*(gm_tttl9nsdo1iu6 *)((uint8_t *)a1)), ((void *)&gm_tple4upi9cflnq6), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3fol08t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3fol08t F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbejm626620qa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(93));
    gm_b0_1 t4 = gm_f_linef_rho83iv((void *)NULL, cx, NULL, t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tpl6gmot6d7lh6n), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_98ea3qh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_98ea3qh F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_suptd32i8k1qp));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(25));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl0v0dinrpdv6d), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bg1q0dr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_bg1q0dr F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4n8c66u7mg94));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpli2ivqo4qj5ok), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ch11let(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_ch11let F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_seftt830rf80r));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(29));
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplv1shkj7l87mc), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gg3n7rd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_gg3n7rd F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3f8u490f8neh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_b0_1 t4 = gm_f_linef_v4quu36((void *)NULL, cx, NULL, t2, &(*(gm_td7tfovebirdn *)((uint8_t *)a1)), ((void *)&gm_tplsfrupsaoln3v), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_i03em5a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_i03em5a F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shjri07u8kc84));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(26));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpltdh9l4qjtpek), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_i5ai2tu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_i5ai2tu F;
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
    gm_b0_1 t4 = gm_f_linef_9ar5ccv((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tple6jpca094i2j), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_icst3ps(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_icst3ps F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6qop16borqmo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_b0_1 t4 = gm_f_linef_8uttu8h((void *)NULL, cx, NULL, t2, &(*(gm_t0uuhaljsf410 *)((uint8_t *)a1)), ((void *)&gm_tpl0qht8d5me9ih), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qg7idri(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qg7idri F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sufil5dud65av));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
    gm_b0_1 t4 = gm_f_linef_rosnm2q((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplk1dctbs79mmk), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_up3h3jj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_up3h3jj F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s99236k10livu));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(16));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplmm5mgnjalbfr), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_v355qja F;
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
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpla1fscog446dv), t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_out_call_uj833o2 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t4;
    void * t5 = gm_l0;
    gm_b16_8 t6 = gm_l2;
    gm_b16_8 t7 = gm_l4;
    gm_b16_8 t8 = gm_l3;
    gm_tfeab279c16rr t9;
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t6);
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t7);
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 32), t8);
    gm_b0_1 t10 = gm_f_linef_bg1q0dr((void *)NULL, cx, NULL, t5, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t11 = gm_l4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t4) {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqtc9fffut4s2));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(12));
        gm_l2 = t5;
    } else {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_b16_8 t8 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t8;
    }
    gm_b16_8 t9 = gm_l2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_plain_cty_q9lhlci F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_mirror_lcjegsf((void *)&gm_l3, (void *)NULL, cx, NULL, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_l2 = t7;
            gm_b16_8 t8 = gm_l2;
            F.ret = t8;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_b16_8 t11 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t9, t10);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_result_var_rn01cvq F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = (uint8_t)(((((uint8_t *)t4) + 48))[16] != 0);
            if (!t5) {
                goto gm_lb2;
            }
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 48));
            gm_l2 = t6;
            void * t7 = gm_l0;
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t7) + 48), t8);
            gm_b16_8 t9 = gm_l2;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t10, t11);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_502hm86(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_502hm86 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqvgecd0v5l54));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(207));
    gm_tftb1odc739t5 t4 = gm_f_write_hvsp2g7(t2, &(*(gm_ti71ll5knnvij *)((uint8_t *)a1)), ((void *)&gm_tplr9ocl0upgsti), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6o17mnv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_6o17mnv F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stm46orgt9h8k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(316));
    gm_tftb1odc739t5 t4 = gm_f_write_isshs6q(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplsthb0beh35ts), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fp9dg1h(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_fp9dg1h F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s021pqnhopt43));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl5uqj3jbiq5f0), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_mv65o15(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_mv65o15 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shfuf0tdpqgc0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(319));
    gm_tftb1odc739t5 t4 = gm_f_write_53s6qp5(t2, &(*(gm_tobu7tlrkigeq *)((uint8_t *)a1)), ((void *)&gm_tpl578lpoev0s9s), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_otf2npe F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4uh375udgni3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(205));
    gm_tftb1odc739t5 t4 = gm_f_write_gca9chf(t2, &(*(gm_tn96msocmj81h *)((uint8_t *)a1)), ((void *)&gm_tplu9669cm9mqja), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_scdecr6(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_scdecr6 F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sofgn7ilt7vsg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(16));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl81nh98hgo1rk), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vp4v8ut(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_vp4v8ut F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sofgn7ilt7vsg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(16));
    gm_tftb1odc739t5 t5 = gm_f_write_vq1ifl4(t2, t3, ((void *)&gm_tpl81nh98hgo1rk), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_call_closure_mha9p1e F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* cv */
    gm_t88rcucg1b7uk gm_l2; /* cty */
    gm_b16_8 gm_l3; /* vals */
    gm_b16_8 gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* ccx */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_tiilmfd16v3fc gm_l11; /* eff */
    uint32_t gm_l12; /* id */
    gm_b16_8 gm_l13; /* n */
    gm_b16_8 gm_l14; /* cx */
    gm_b16_8 gm_l15; /* r */
    GM_STACK_CHECK(&F, 1215u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t53at80pfdus3 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    {
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, ((uint8_t *)t9));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_l2;
            gm_f_kind_ghvav22((void *)&gm_l16, t11, GM_CONV(gm_csguankko4lmhd, t12));
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)15ULL));
            if (!t14) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l16) + 8), 48);
            void * t15 = gm_l0;
            gm_t88rcucg1b7uk t16 = gm_l2;
            gm_b16_8 t17 = gm_l1;
            gm_b16_8 t18 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t18;
            void * t19 = gm_l0;
            uint32_t t20 = gm_f_fresh_5kokjmg(t19);
            gm_t88rcucg1b7uk t21;
            GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t20);
            gm_f_format_qr5c6un((void *)&gm_l17, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcmeaag4q4qrjl, 165, 12);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
            gm_l8 = t23;
            void * t24 = gm_l0;
            gm_b16_8 t25 = gm_l8;
            gm_b16_8 t26 = gm_l7;
            gm_t6j6smj7qoctu t27;
            GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t25);
            GM_ST(gm_b16_8, ((uint8_t *)&t27 + 16), t26);
            gm_b0_1 t28 = gm_f_linef_lot5c36((void *)NULL, cx, NULL, t24, &t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
            gm_l9 = t29;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t30 = gm_l10;
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t32 = (t30 < t31);
                    if (!t32) {
                        goto gm_lb3;
                    }
                    {
                        void * t33 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t35 = gm_l10;
                        {
                            {
                                memmove(((uint8_t *)&gm_l11), (((uint8_t *)t33) + (uintptr_t)t35 * 24), 24);
                                void * t36 = gm_l0;
                                void * t37 = GM_LD(void *, ((uint8_t *)t36));
                                void * t38 = (void *)((uint8_t *)&gm_l11);
                                uint32_t t39 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t37, t38);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l12 = t39;
                                void * t40 = gm_l0;
                                uint32_t t41 = gm_f_fresh_5kokjmg(t40);
                                gm_t88rcucg1b7uk t42;
                                GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t41);
                                gm_f_format_mgf81l5((void *)&gm_l18, (void *)NULL, cx, NULL, t42);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcmeaag4q4qrjl, 169, 14);
                                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                gm_l13 = t44;
                                void * t45 = gm_l0;
                                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)t45) + 200));
                                gm_l14 = t46;
                                void * t47 = gm_l0;
                                gm_b16_8 t48 = gm_l13;
                                gm_b16_8 t49 = gm_l14;
                                uint32_t t50 = gm_l12;
                                gm_b16_8 t51 = gm_l13;
                                gm_b16_8 t52 = gm_l13;
                                gm_b16_8 t53 = gm_l8;
                                gm_b16_8 t54 = gm_l8;
                                gm_b16_8 t55 = gm_l13;
                                gm_t586m754bse2v t56;
                                GM_ST(uint32_t, (uint8_t *)&t56 + 36, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t48);
                                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 16), t49);
                                GM_ST(uint32_t, ((uint8_t *)&t56 + 32), t50);
                                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 40), t51);
                                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 56), t52);
                                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 72), t53);
                                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 88), t54);
                                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 104), t55);
                                gm_b0_1 t57 = gm_f_linef_ubknr7g((void *)NULL, cx, NULL, t47, &t56);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 167, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t58 = gm_l10;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l10 = t61;
                }
            }
gm_lb3:;
            void * t62 = gm_l0;
            gm_b16_8 t63 = gm_l7;
            gm_tnpifv20irnph t64;
            GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
            gm_f_format_l0bpsnf((void *)&gm_l19, (void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcmeaag4q4qrjl, 173, 27);
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
            gm_b16_8 t68 = gm_l7;
            gm_tnpifv20irnph t69;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
            gm_f_format_eir8aii((void *)&gm_l20, (void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcmeaag4q4qrjl, 173, 54);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_b16_8 t73 = gm_l8;
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            gm_b16_8 t75 = gm_l3;
            gm_b16_8 t76 = gm_l4;
            gm_t88rcucg1b7uk t77 = gm_l5;
            gm_b16_8 t78 = gm_f_call_indirect_khk02ee((void *)NULL, cx, NULL, t62, t67, t72, t74, t75, t76, t77, ((uint8_t)1), ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t78;
            void * t79 = gm_l0;
            gm_b0_1 t80 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t81 = gm_l15;
            gm_t5o0tpv5jka2l t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 32, 0);
            ((uint8_t *)&t82)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t82 + 8), t81);
            memmove(gm_out, ((uint8_t *)&t82), 40);
            goto gm_fdone;
        }
gm_lb2:;
        gm_trap("reached `@unreachable()`", gm_srcmeaag4q4qrjl, 163, 40);
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
__attribute__((aligned(4))) void gm_f_call_fn_q122g0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, gm_b16_8 a3) {
    struct gm_fr_call_fn_q122g0r F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* rt */
    uint32_t gm_l2; /* f */
    gm_b16_8 gm_l3; /* vals */
    void * gm_l4; /* def */
    gm_b16_8 gm_l5; /* env */
    uint32_t gm_l6; /* p */
    uint32_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* l */
    gm_b16_8 gm_l10; /* up */
    uint32_t gm_l11; /* fid */
    gm_b16_8 gm_l12; /* name */
    uint32_t gm_l13; /* sn */
    uint32_t gm_l14; /* s */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l18; /* p */
    gm_b16_8 gm_l19; /* r */
    uint8_t gm_l20; /* foreign_ */
    uint64_t gm_l21; /* n */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    gm_t88rcucg1b7uk gm_l26; /* t */
    gm_b16_8 gm_l27; /* call */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l31; /* a */
    gm_b16_8 gm_l32; /* c2 */
    gm_b16_8 gm_l33; /* cx */
    gm_b16_8 gm_l34; /* args */
    gm_b16_8 gm_l35; /* call */
    GM_STACK_CHECK(&F, 2776u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tommf4j9upomc gm_l36;
    gm_t9805qtonbsag gm_l37;
    gm_tftb1odc739t5 gm_l38;
    uint8_t gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_b16_8 gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tabb710d9mq2e gm_l48;
    {
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, ((uint8_t *)t37));
        uint32_t t39 = gm_l2;
        gm_b0_1 t40 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t41 = gm_l0;
        void * t42 = GM_LD(void *, ((uint8_t *)t41));
        void * t43 = GM_LD(void *, ((uint8_t *)t42));
        uint32_t t44 = gm_l2;
        void * t45 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t45;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s04qus0luoga5));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
        gm_l5 = t46;
        {
            {
                void * t47 = gm_l4;
                uint8_t t48 = (uint8_t)(((((uint8_t *)t47) + 88))[4] != 0);
                if (!t48) {
                    goto gm_lb2;
                }
                uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)t47) + 88));
                gm_l6 = t49;
                gm_l8 = ((uint32_t)0ULL);
                gm_l7 = ((uint32_t)64ULL);
                {
                    for (;;) {
                        uint32_t t50 = gm_l8;
                        uint32_t t51 = gm_l7;
                        uint8_t t52 = (t50 < t51);
                        if (!t52) {
                            goto gm_lb3;
                        }
                        {
                            {
                                {
                                    uint32_t t53 = gm_l8;
                                    gm_l9 = t53;
                                    void * t54 = gm_l0;
                                    uint32_t t55 = gm_l9;
                                    gm_f_frame_up_g8486eu((void *)&gm_l36, (void *)NULL, cx, NULL, t54, t55);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l36));
                                    gm_l10 = t56;
                                    uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)&gm_l36) + 16));
                                    gm_l11 = t57;
                                    uint32_t t58 = gm_l11;
                                    uint32_t t59 = gm_l6;
                                    uint8_t t60 = ((t58 == t59));
                                    if (t60) {
                                        gm_b16_8 t61 = gm_l10;
                                        gm_l5 = t61;
                                        goto gm_lb3;
                                    }
                                    goto gm_lb6;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 100, 9);
                            }
gm_lb6:;
                        }
gm_lb5:;
                        uint32_t t62 = gm_l8;
                        uint32_t r63; bool o64 = __builtin_add_overflow(t62, ((uint32_t)1ULL), &r63);
                        uint32_t t65 = r63;
                        gm_l8 = t65;
                    }
                }
gm_lb3:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(0));
        gm_l12 = t66;
        {
            {
                void * t67 = gm_l0;
                uint8_t t68 = (uint8_t)(((((uint8_t *)t67) + 40))[4] != 0);
                if (!t68) {
                    goto gm_lb9;
                }
                uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)t67) + 40));
                gm_l13 = t69;
                void * t70 = gm_l0;
                gm_t9805qtonbsag t71;
                memset(&t71, 0, sizeof t71);
                GM_ST(gm_t9805qtonbsag, (((uint8_t *)t70) + 40), t71);
                void * t72 = gm_l0;
                void * t73 = GM_LD(void *, ((uint8_t *)t72));
                uint32_t t74 = gm_l2;
                uint32_t t75 = gm_l13;
                gm_b16_8 t76 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t73, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t76;
                goto gm_lb8;
            }
gm_lb9:;
        }
gm_lb8:;
    }
    {
        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
        uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
        if (t78) {
            {
                {
                    void * t79 = gm_l0;
                    uint32_t t80 = gm_l2;
                    gm_t9805qtonbsag t81 = gm_f_passed_spec_rvmvr8h((void *)NULL, cx, NULL, t79, t80);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l37 = t81;
                    uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l37))[4] != 0);
                    if (!t82) {
                        goto gm_lb11;
                    }
                    uint32_t t83 = GM_LD(uint32_t, ((uint8_t *)&gm_l37));
                    gm_l14 = t83;
                    void * t84 = gm_l0;
                    void * t85 = GM_LD(void *, ((uint8_t *)t84));
                    uint32_t t86 = gm_l2;
                    uint32_t t87 = gm_l14;
                    gm_b16_8 t88 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t85, t86, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t88;
                    goto gm_lb10;
                }
gm_lb11:;
                void * t89 = gm_l0;
                void * t90 = GM_LD(void *, ((uint8_t *)t89));
                uint32_t t91 = gm_l2;
                gm_b16_8 t92 = gm_f_callee_name_4k9m9s6((void *)NULL, cx, NULL, t90, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t92;
            }
gm_lb10:;
        }
        gm_trqfvmjtt4sij t93;
        gm_f_list_nof7vfj((void *)&t93, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t93;
        void * t94 = gm_l4;
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)t94) + 32));
        gm_l16 = t95;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t96 = gm_l17;
                uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t98 = (t96 < t97);
                if (!t98) {
                    goto gm_lb12;
                }
                {
                    void * t99 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t101 = gm_l17;
                    {
                        {
                            uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)t99) + (uintptr_t)t101 * 4));
                            gm_l18 = t102;
                            void * t103 = (void *)((uint8_t *)&F.l15);
                            void * t104 = gm_l4;
                            void * t105 = GM_LD(void *, (((uint8_t *)t104) + 48));
                            uint64_t t106 = GM_LD(uint64_t, ((((uint8_t *)t104) + 48) + 8));
                            uint32_t t107 = gm_l18;
                            uint64_t t108 = (uint64_t)t107;
                            if ((uintptr_t)t108 >= (uintptr_t)(t106)) gm_trap_index((uintptr_t)t108, t106, gm_srcmeaag4q4qrjl, 117, 36);
                            gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t105) + (uintptr_t)t108 * 40) + 16));
                            gm_tftb1odc739t5 t110 = gm_f_push_k3l9nic(t103, GM_CONV(gm_csguankko4lmhd, t109));
                            gm_l38 = t110;
                            uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                            if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcmeaag4q4qrjl, 117, 27);
                            goto gm_lb15;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 117, 5);
                    }
gm_lb15:;
                }
gm_lb14:;
                uint64_t t112 = gm_l17;
                uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                uint64_t t115 = r113;
                gm_l17 = t115;
            }
        }
gm_lb12:;
        gm_b16_8 t116;
        GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(0));
        gm_l19 = t116;
    }
    {
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, ((uint8_t *)t117));
        uint32_t t119 = gm_l2;
        uint8_t t120 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t118, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t120) {
            void * t121 = gm_l4;
            {
                {
                    uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t121) + 96) + 0));
                    uint8_t t123 = (uint8_t)(t122 == ((uint8_t)3ULL));
                    if (!t123) {
                        goto gm_lb18;
                    }
                    gm_l39 = ((uint8_t)1);
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    gm_l39 = ((uint8_t)0);
                    goto gm_lb17;
                }
gm_lb19:;
                gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 121, 21);
            }
gm_lb17:;
            uint8_t t124 = gm_l39;
            gm_l20 = t124;
            uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
            uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t127 = (t125 < t126 ? t125 : t126);
            gm_l21 = t127;
            gm_trqfvmjtt4sij t128;
            gm_f_list_epvducg((void *)&t128, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l22 = t128;
            gm_l24 = ((uint64_t)0ULL);
            uint64_t t129 = gm_l21;
            gm_l23 = t129;
            {
                for (;;) {
                    uint64_t t130 = gm_l24;
                    uint64_t t131 = gm_l23;
                    uint8_t t132 = (t130 < t131);
                    if (!t132) {
                        goto gm_lb20;
                    }
                    {
                        {
                            {
                                uint64_t t133 = gm_l24;
                                gm_l25 = t133;
                                void * t134 = GM_LD(void *, ((uint8_t *)&F.l15));
                                uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                uint64_t t136 = gm_l25;
                                if ((uintptr_t)t136 >= (uintptr_t)(t135)) gm_trap_index((uintptr_t)t136, t135, gm_srcmeaag4q4qrjl, 125, 18);
                                gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t134) + (uintptr_t)t136 * 4));
                                gm_l26 = t137;
                                {
                                    {
                                        uint8_t t138 = gm_l20;
                                        uint8_t t139 = (uint8_t)!t138;
                                        if (!t139) {
                                            goto gm_lb26;
                                        }
                                        void * t140 = gm_l0;
                                        void * t141 = GM_LD(void *, ((uint8_t *)t140));
                                        void * t142 = (void *)(((uint8_t *)t141) + 8);
                                        gm_t88rcucg1b7uk t143 = gm_l26;
                                        uint8_t t144 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t142, t143);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        if (!t144) {
                                            goto gm_lb26;
                                        }
                                        void * t145 = (void *)((uint8_t *)&F.l22);
                                        void * t146 = gm_l0;
                                        gm_t88rcucg1b7uk t147 = gm_l26;
                                        void * t148 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t150 = gm_l25;
                                        if ((uintptr_t)t150 >= (uintptr_t)(t149)) gm_trap_index((uintptr_t)t150, t149, gm_srcmeaag4q4qrjl, 128, 43);
                                        gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)t148) + (uintptr_t)t150 * 16));
                                        gm_b16_8 t152 = gm_f_gamma_arg_ldsagr2((void *)NULL, cx, NULL, t146, t147, t151);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t153 = gm_f_push_3ef746b(t145, t152);
                                        gm_l40 = t153;
                                        uint8_t t154 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                        if (!t154) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcmeaag4q4qrjl, 128, 17);
                                        goto gm_lb25;
                                    }
gm_lb26:;
                                    void * t155 = (void *)((uint8_t *)&F.l22);
                                    void * t156 = gm_l0;
                                    gm_t88rcucg1b7uk t157 = gm_l26;
                                    void * t158 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t160 = gm_l25;
                                    if ((uintptr_t)t160 >= (uintptr_t)(t159)) gm_trap_index((uintptr_t)t160, t159, gm_srcmeaag4q4qrjl, 130, 45);
                                    gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)t158) + (uintptr_t)t160 * 16));
                                    gm_b16_8 t162 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t156, t157, t161);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tftb1odc739t5 t163 = gm_f_push_3ef746b(t155, t162);
                                    gm_l41 = t163;
                                    uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                    if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcmeaag4q4qrjl, 130, 17);
                                }
gm_lb25:;
                                goto gm_lb23;
                            }
gm_lb24:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 124, 9);
                        }
gm_lb23:;
                    }
gm_lb22:;
                    uint64_t t165 = gm_l24;
                    uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                    uint64_t t168 = r166;
                    gm_l24 = t168;
                }
            }
gm_lb20:;
            void * t169 = (void *)((uint8_t *)&F.l22);
            void * t170 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t172 = gm_l21;
            uint64_t t173 = (uintptr_t)t172;
            uint64_t t174 = (uintptr_t)t171;
            if (t173 > t174 || t174 > (uintptr_t)t171) gm_trap_slice(t173, t174, t171, gm_srcmeaag4q4qrjl, 133, 21);
            uint64_t t175 = ((uintptr_t)t171) - (uintptr_t)t172;
            gm_b16_8 t176;
            GM_ST(void *, (uint8_t *)&t176, (void *)(((uint8_t *)t170 + (uintptr_t)t172 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(t175));
            gm_tftb1odc739t5 t177 = gm_f_extend_vogsd72(t169, t176);
            gm_l42 = t177;
            uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
            if (!t178) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcmeaag4q4qrjl, 133, 9);
            gm_b16_8 t179 = gm_l12;
            gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
            gm_b16_8 t182;
            GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l43, (void *)NULL, cx, NULL, t181, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
            if (!t183) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_srcmeaag4q4qrjl, 134, 51);
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
            gm_t6j6smj7qoctu t185;
            GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t179);
            GM_ST(gm_b16_8, ((uint8_t *)&t185 + 16), t184);
            gm_f_format_99gc605((void *)&gm_l44, (void *)NULL, cx, NULL, &t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
            if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_srcmeaag4q4qrjl, 134, 24);
            gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
            gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
            gm_l27 = t188;
            {
                {
                    uint8_t t189 = gm_l20;
                    uint8_t t190 = (uint8_t)!t189;
                    if (!t190) {
                        goto gm_lb28;
                    }
                    void * t191 = gm_l0;
                    void * t192 = GM_LD(void *, ((uint8_t *)t191));
                    void * t193 = (void *)(((uint8_t *)t192) + 8);
                    void * t194 = gm_l4;
                    gm_t88rcucg1b7uk t195 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t194) + 64));
                    uint8_t t196 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t193, t195);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!t196) {
                        goto gm_lb28;
                    }
                    gm_trqfvmjtt4sij t197;
                    gm_f_list_uvfu1k1((void *)&t197, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l28 = t197;
                    gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                    gm_l29 = t198;
                    gm_l30 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t199 = gm_l30;
                            uint64_t t200 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                            uint8_t t201 = (t199 < t200);
                            if (!t201) {
                                goto gm_lb29;
                            }
                            {
                                void * t202 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                uint64_t t204 = gm_l30;
                                {
                                    {
                                        gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)t202) + (uintptr_t)t204 * 16));
                                        gm_l31 = t205;
                                        void * t206 = (void *)((uint8_t *)&F.l28);
                                        gm_b16_8 t207 = gm_l31;
                                        gm_tnpifv20irnph t208;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t208 + 0), t207);
                                        gm_tftb1odc739t5 t209 = gm_f_write_fp9dg1h(t206, t208);
                                        gm_l45 = t209;
                                        uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                        if (!t210) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcmeaag4q4qrjl, 137, 35);
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 137, 13);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t211 = gm_l30;
                            uint64_t r212; bool o213 = __builtin_add_overflow(t211, ((uint64_t)1ULL), &r212);
                            uint64_t t214 = r212;
                            gm_l30 = t214;
                        }
                    }
gm_lb29:;
                    void * t215 = gm_l0;
                    void * t216 = gm_l4;
                    gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t216) + 64));
                    gm_b16_8 t218 = gm_l12;
                    gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                    gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                    gm_b16_8 t221 = gm_f_out_call_uj833o2((void *)NULL, cx, NULL, t215, t217, t218, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t221;
                    goto gm_lb27;
                }
gm_lb28:;
                void * t222 = gm_l0;
                void * t223 = GM_LD(void *, ((uint8_t *)t222));
                void * t224 = (void *)(((uint8_t *)t223) + 8);
                void * t225 = gm_l4;
                gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t225) + 64));
                uint8_t t227 = gm_f_is_void_vs2outo(t224, GM_CONV(gm_csguankko4lmhd, t226));
                if (t227) {
                    void * t228 = gm_l0;
                    gm_b16_8 t229 = gm_l27;
                    gm_tnpifv20irnph t230;
                    GM_ST(gm_b16_8, ((uint8_t *)&t230 + 0), t229);
                    gm_b0_1 t231 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t228, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t232 = gm_l0;
                    gm_t88rcucg1b7uk t233 = gm_l1;
                    gm_b16_8 t234 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t232, t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t234;
                } else {
                    {
                        {
                            uint8_t t235 = gm_l20;
                            uint8_t t236 = (uint8_t)!t235;
                            if (!t236) {
                                goto gm_lb35;
                            }
                            void * t237 = gm_l0;
                            void * t238 = GM_LD(void *, ((uint8_t *)t237));
                            void * t239 = (void *)(((uint8_t *)t238) + 8);
                            void * t240 = gm_l4;
                            gm_t88rcucg1b7uk t241 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t240) + 64));
                            uint8_t t242 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t239, t241);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (!t242) {
                                goto gm_lb35;
                            }
                            gm_b16_8 t243 = gm_l27;
                            gm_l46 = t243;
                            goto gm_lb34;
                        }
gm_lb35:;
                        void * t244 = gm_l0;
                        void * t245 = gm_l4;
                        gm_t88rcucg1b7uk t246 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t245) + 64));
                        gm_b16_8 t247 = gm_l27;
                        gm_b16_8 t248 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t244, t246, t247);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l46 = t248;
                    }
gm_lb34:;
                    gm_b16_8 t249 = gm_l46;
                    gm_l32 = t249;
                    void * t250 = gm_l0;
                    gm_t88rcucg1b7uk t251 = gm_l1;
                    gm_b16_8 t252 = gm_l32;
                    gm_b16_8 t253 = gm_f_call_result_vg14o5p((void *)NULL, cx, NULL, t250, t251, t252);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t253;
                }
            }
gm_lb27:;
        } else {
            void * t254 = gm_l0;
            gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)t254) + 200));
            gm_l33 = t255;
            void * t256 = gm_l0;
            gm_b16_8 t257 = gm_l3;
            gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
            gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
            gm_b16_8 t260 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t256, t257, t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t260;
            void * t261 = gm_l0;
            void * t262 = GM_LD(void *, ((uint8_t *)t261));
            void * t263 = (void *)(((uint8_t *)t262) + 8);
            void * t264 = gm_l4;
            gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t264) + 64));
            uint8_t t266 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t263, t265);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t266) {
                void * t267 = gm_l0;
                void * t268 = gm_l4;
                gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t268) + 64));
                gm_b16_8 t270 = gm_l12;
                gm_b16_8 t271 = gm_l5;
                gm_b16_8 t272 = gm_l33;
                gm_b16_8 t273 = gm_l34;
                gm_tfeab279c16rr t274;
                GM_ST(gm_b16_8, ((uint8_t *)&t274 + 0), t271);
                GM_ST(gm_b16_8, ((uint8_t *)&t274 + 16), t272);
                GM_ST(gm_b16_8, ((uint8_t *)&t274 + 32), t273);
                gm_f_format_0mp0khb((void *)&gm_l47, (void *)NULL, cx, NULL, &t274);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t275 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                if (!t275) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srcmeaag4q4qrjl, 150, 44);
                gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                gm_b16_8 t278 = gm_f_out_call_uj833o2((void *)NULL, cx, NULL, t267, t269, t270, t277);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t278;
            } else {
                gm_b16_8 t279 = gm_l12;
                gm_b16_8 t280 = gm_l5;
                gm_b16_8 t281 = gm_l33;
                gm_b16_8 t282 = gm_l34;
                gm_t0uuhaljsf410 t283;
                GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t279);
                GM_ST(gm_b16_8, ((uint8_t *)&t283 + 16), t280);
                GM_ST(gm_b16_8, ((uint8_t *)&t283 + 32), t281);
                GM_ST(gm_b16_8, ((uint8_t *)&t283 + 48), t282);
                gm_f_format_570tujl((void *)&gm_l48, (void *)NULL, cx, NULL, &t283);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t284 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                if (!t284) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srcmeaag4q4qrjl, 152, 21);
                gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                gm_l35 = t285;
                void * t286 = gm_l0;
                gm_t88rcucg1b7uk t287 = gm_l1;
                gm_b16_8 t288 = gm_l35;
                gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&t288));
                gm_b16_8 t290 = gm_f_call_result_vg14o5p((void *)NULL, cx, NULL, t286, t287, t289);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t290;
            }
        }
    }
    {
        void * t291 = gm_l0;
        void * t292 = GM_LD(void *, ((uint8_t *)t291));
        uint32_t t293 = gm_l2;
        uint8_t t294 = gm_f_may_unwind_irl2ktm((void *)NULL, cx, NULL, t292, t293);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t294) {
            void * t295 = gm_l0;
            gm_b0_1 t296 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t295);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b16_8 t297 = gm_l19;
        gm_t5o0tpv5jka2l t298;
        GM_ST(uint8_t, (uint8_t *)&t298 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t298 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t298 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t298 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t298 + 32, 0);
        ((uint8_t *)&t298)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t298 + 8), t297);
        memmove(gm_out, ((uint8_t *)&t298), 40);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_call_indirect_khk02ee(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, uint8_t a7, uint8_t a8) {
    struct gm_fr_call_indirect_khk02ee F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* env */
    gm_b16_8 gm_l3; /* cx */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    uint8_t gm_l7; /* hidden */
    uint8_t gm_l8; /* win64 */
    gm_b16_8 gm_l9; /* plain_ret */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t88rcucg1b7uk gm_l13; /* p */
    gm_b16_8 gm_l14; /* plain_params */
    gm_b16_8 gm_l15; /* plain_ty */
    gm_b16_8 gm_l16; /* p */
    gm_th2lqbo77pvsc gm_l17; /* res */
    gm_b16_8 gm_l19; /* plain_call */
    gm_b16_8 gm_l20; /* fp */
    gm_b16_8 gm_l21; /* args */
    gm_b16_8 gm_l22; /* out */
    gm_b16_8 gm_l23; /* r */
    gm_b16_8 gm_l24; /* hidden_call */
    gm_th2lqbo77pvsc gm_l25; /* rv */
    gm_b16_8 gm_l26; /* r */
    GM_STACK_CHECK(&F, 2502u);
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
    gm_l8 = a8;
    gm_b16_8 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_b16_8 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    uint8_t gm_l36;
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, ((uint8_t *)t16));
        void * t18 = (void *)(((uint8_t *)t17) + 8);
        gm_t88rcucg1b7uk t19 = gm_l6;
        uint8_t t20 = gm_f_is_void_vs2outo(t18, GM_CONV(gm_csguankko4lmhd, t19));
        if (t20) {
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
            gm_l27 = t21;
        } else {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, ((uint8_t *)t22));
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l6;
            gm_b16_8 t26 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t26;
        }
        gm_b16_8 t27 = gm_l27;
        gm_l9 = t27;
        gm_trqfvmjtt4sij t28;
        gm_f_list_epvducg((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t28;
        gm_b16_8 t29 = gm_l5;
        gm_l11 = t29;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t30 = gm_l12;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t32 = (t30 < t31);
                if (!t32) {
                    goto gm_lb1;
                }
                {
                    void * t33 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t35 = gm_l12;
                    {
                        {
                            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + (uintptr_t)t35 * 4));
                            gm_l13 = t36;
                            void * t37 = (void *)((uint8_t *)&F.l10);
                            void * t38 = gm_l0;
                            void * t39 = GM_LD(void *, ((uint8_t *)t38));
                            void * t40 = (void *)(((uint8_t *)t39) + 8);
                            gm_t88rcucg1b7uk t41 = gm_l13;
                            gm_b16_8 t42 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t40, t41);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t43 = gm_f_push_3ef746b(t37, t42);
                            gm_l28 = t43;
                            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcmeaag4q4qrjl, 207, 20);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 207, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t45 = gm_l12;
                uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                uint64_t t48 = r46;
                gm_l12 = t48;
            }
        }
gm_lb1:;
        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
        uint8_t t50 = ((t49 == ((uint64_t)0ULL)));
        if (t50) {
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(4));
            gm_l29 = t51;
        } else {
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l30, (void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcmeaag4q4qrjl, 208, 67);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_l29 = t57;
        }
    }
    {
        gm_b16_8 t58 = gm_l29;
        gm_l14 = t58;
        gm_b16_8 t59 = gm_l9;
        uint8_t t60 = gm_l8;
        gm_b16_8 t61 = gm_f_conv_attr_m0b89pc(t60);
        gm_b16_8 t62 = gm_l14;
        gm_tfeab279c16rr t63;
        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t59);
        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 16), t61);
        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 32), t62);
        gm_f_format_7tqfnpi((void *)&gm_l31, (void *)NULL, cx, NULL, &t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcmeaag4q4qrjl, 209, 17);
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
        gm_l15 = t65;
        void * t66 = gm_l0;
        void * t67 = gm_l0;
        void * t68 = GM_LD(void *, ((uint8_t *)t67));
        void * t69 = GM_LD(void *, ((uint8_t *)t68));
        void * t70 = GM_LD(void *, ((uint8_t *)t69));
        void * t71 = (void *)(((uint8_t *)t70) + 8);
        gm_t53at80pfdus3 t72;
        memset(&t72, 0, sizeof t72);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t73 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t71, &t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t74 = gm_l1;
        gm_b16_8 t75 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t66, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t75;
        gm_th2lqbo77pvsc t76;
        memset(&t76, 0, sizeof t76);
        gm_l17 = t76;
        void * t77 = gm_l0;
        void * t78 = GM_LD(void *, ((uint8_t *)t77));
        void * t79 = (void *)(((uint8_t *)t78) + 8);
        gm_t88rcucg1b7uk t80 = gm_l6;
        uint8_t t81 = gm_f_is_never_in5ftma(t79, GM_CONV(gm_csguankko4lmhd, t80));
        uint8_t t82 = (uint8_t)!t81;
        if (t82) {
            void * t83 = gm_l0;
            gm_t88rcucg1b7uk t84 = gm_l6;
            gm_b16_8 t85 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t86), t85);
            ((uint8_t *)&t86)[16] = 1;
            gm_l17 = t86;
        }
        void * t87 = gm_l0;
        gm_b16_8 t88 = gm_l4;
        gm_b16_8 t89 = gm_l5;
        gm_f_plain_args_o7acjm5((void *)&F.l18, (void *)NULL, cx, NULL, t87, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t90 = gm_l15;
        gm_b16_8 t91 = gm_l16;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_b16_8 t94;
        GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l32, (void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
        if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcmeaag4q4qrjl, 214, 80);
        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
        gm_tfeab279c16rr t97;
        GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t90);
        GM_ST(gm_b16_8, ((uint8_t *)&t97 + 16), t91);
        GM_ST(gm_b16_8, ((uint8_t *)&t97 + 32), t96);
        gm_f_format_35to3im((void *)&gm_l33, (void *)NULL, cx, NULL, &t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcmeaag4q4qrjl, 214, 26);
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        gm_l19 = t100;
        void * t101 = gm_l0;
        gm_t88rcucg1b7uk t102 = gm_l6;
        gm_b16_8 t103 = gm_l19;
        gm_b16_8 t104 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t101, t102, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t104;
    }
    {
        uint8_t t105 = gm_l7;
        if (t105) {
            void * t106 = gm_l0;
            gm_t88rcucg1b7uk t107 = gm_l6;
            gm_b16_8 t108 = gm_l5;
            gm_b16_8 t109 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t106, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t109;
            void * t110 = gm_l0;
            gm_b16_8 t111 = gm_l4;
            gm_b16_8 t112 = gm_l5;
            gm_b16_8 t113 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t110, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t113;
            gm_b16_8 t114;
            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(0));
            gm_l22 = t114;
            {
                {
                    uint8_t t115 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
                    if (!t115) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                    gm_l23 = t116;
                    void * t117 = gm_l0;
                    void * t118 = GM_LD(void *, ((uint8_t *)t117));
                    void * t119 = (void *)(((uint8_t *)t118) + 8);
                    gm_t88rcucg1b7uk t120 = gm_l6;
                    uint8_t t121 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t119, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t121) {
                        gm_b16_8 t122 = gm_l23;
                        gm_tnpifv20irnph t123;
                        GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t122);
                        gm_f_format_itcjtai((void *)&gm_l34, (void *)NULL, cx, NULL, t123);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                        if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcmeaag4q4qrjl, 221, 44);
                        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                        gm_l22 = t126;
                    }
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            gm_b16_8 t127 = gm_l20;
            gm_b16_8 t128 = gm_l16;
            gm_b16_8 t129 = gm_l22;
            gm_b16_8 t130 = gm_l2;
            gm_b16_8 t131 = gm_l3;
            gm_b16_8 t132 = gm_l21;
            gm_tif3elqk01bdl t133;
            GM_ST(gm_b16_8, ((uint8_t *)&t133 + 0), t127);
            GM_ST(gm_b16_8, ((uint8_t *)&t133 + 16), t128);
            GM_ST(gm_b16_8, ((uint8_t *)&t133 + 32), t129);
            GM_ST(gm_b16_8, ((uint8_t *)&t133 + 48), t130);
            GM_ST(gm_b16_8, ((uint8_t *)&t133 + 64), t131);
            GM_ST(gm_b16_8, ((uint8_t *)&t133 + 80), t132);
            gm_f_format_lc73epv((void *)&gm_l35, (void *)NULL, cx, NULL, &t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
            if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcmeaag4q4qrjl, 223, 24);
            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
            gm_l24 = t135;
            void * t136 = gm_l0;
            gm_b16_8 t137 = gm_l16;
            gm_tnpifv20irnph t138;
            GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
            gm_b0_1 t139 = gm_f_linef_98ea3qh((void *)NULL, cx, NULL, t136, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t140 = gm_l0;
            void * t141 = gm_l0;
            uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t141) + 144));
            uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
            if (o144) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 225, 9);
            uint64_t t145 = r143;
            GM_ST(uint64_t, (((uint8_t *)t140) + 144), t145);
            void * t146 = gm_l0;
            gm_t88rcucg1b7uk t147 = gm_l6;
            gm_b16_8 t148 = gm_l19;
            gm_b0_1 t149 = gm_f_calls_emit_plain_euobq8f((void *)NULL, cx, NULL, t146, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)), t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t150 = gm_l0;
            void * t151 = gm_l0;
            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)t151) + 144));
            uint64_t r153; bool o154 = __builtin_sub_overflow(t152, ((uint64_t)1ULL), &r153);
            if (o154) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 227, 9);
            uint64_t t155 = r153;
            GM_ST(uint64_t, (((uint8_t *)t150) + 144), t155);
            void * t156 = gm_l0;
            gm_b16_8 t157;
            GM_ST(void *, (uint8_t *)&t157, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(8));
            gm_b0_1 t158 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t159 = gm_l0;
            void * t160 = gm_l0;
            uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)t160) + 144));
            uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
            if (o163) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 229, 9);
            uint64_t t164 = r162;
            GM_ST(uint64_t, (((uint8_t *)t159) + 144), t164);
            memmove(((uint8_t *)&gm_l25), ((uint8_t *)&gm_l17), 24);
            {
                {
                    {
                        {
                            uint8_t t165 = (uint8_t)((((uint8_t *)&gm_l25))[16] != 0);
                            if (t165) {
                                goto gm_lb11;
                            }
                            gm_l36 = ((uint8_t)0);
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            gm_l36 = ((uint8_t)1);
                            goto gm_lb10;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 231, 12);
                    }
gm_lb10:;
                    uint8_t t166 = gm_l36;
                    if (!t166) {
                        goto gm_lb9;
                    }
                    uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint8_t t168 = ((t167 == ((uint64_t)0ULL)));
                    if (!t168) {
                        goto gm_lb9;
                    }
                    void * t169 = gm_l0;
                    uint8_t t170 = (uint8_t)((((uint8_t *)&gm_l25))[16] != 0);
                    if (!t170) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 232, 34);
                    gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                    gm_b16_8 t172 = gm_l24;
                    gm_t6j6smj7qoctu t173;
                    GM_ST(gm_b16_8, ((uint8_t *)&t173 + 0), t171);
                    GM_ST(gm_b16_8, ((uint8_t *)&t173 + 16), t172);
                    gm_b0_1 t174 = gm_f_linef_i5ai2tu((void *)NULL, cx, NULL, t169, &t173);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                void * t175 = gm_l0;
                gm_b16_8 t176 = gm_l24;
                gm_tnpifv20irnph t177;
                GM_ST(gm_b16_8, ((uint8_t *)&t177 + 0), t176);
                gm_b0_1 t178 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t175, t177);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb8:;
            void * t179 = gm_l0;
            void * t180 = gm_l0;
            uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)t180) + 144));
            uint64_t r182; bool o183 = __builtin_sub_overflow(t181, ((uint64_t)1ULL), &r182);
            if (o183) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 236, 9);
            uint64_t t184 = r182;
            GM_ST(uint64_t, (((uint8_t *)t179) + 144), t184);
            void * t185 = gm_l0;
            gm_b16_8 t186;
            GM_ST(void *, (uint8_t *)&t186, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(1));
            gm_b0_1 t187 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t185, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t188 = gm_l0;
            gm_t88rcucg1b7uk t189 = gm_l6;
            gm_b16_8 t190 = gm_l19;
            gm_b0_1 t191 = gm_f_calls_emit_plain_euobq8f((void *)NULL, cx, NULL, t188, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)), t189, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        {
            {
                uint8_t t192 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
                if (!t192) {
                    goto gm_lb14;
                }
                gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                gm_l26 = t193;
                void * t194 = gm_l0;
                void * t195 = GM_LD(void *, ((uint8_t *)t194));
                void * t196 = (void *)(((uint8_t *)t195) + 8);
                gm_t88rcucg1b7uk t197 = gm_l6;
                uint8_t t198 = gm_f_is_unit_17uennd(t196, GM_CONV(gm_csguankko4lmhd, t197));
                uint8_t t199 = (uint8_t)!t198;
                if (t199) {
                    gm_b16_8 t200 = gm_l26;
                    F.ret = t200;
                    goto gm_fret;
                }
                goto gm_lb13;
            }
gm_lb14:;
        }
gm_lb13:;
        void * t201 = gm_l0;
        gm_t88rcucg1b7uk t202 = gm_l6;
        gm_b16_8 t203 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t201, t202);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t203;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_call_result_vg14o5p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_call_result_vg14o5p F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* call */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_never_in5ftma(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (t6) {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l2;
        gm_tnpifv20irnph t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
        gm_b0_1 t10 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t7, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l1;
        gm_b16_8 t13 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t13;
        goto gm_fret;
    }
    void * t14 = gm_l0;
    gm_t88rcucg1b7uk t15 = gm_l1;
    gm_b16_8 t16 = gm_l2;
    gm_b16_8 t17 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t14, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t17;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_call_site_8n74to1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_call_site_8n74to1 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 1136) + 1));
            if (!t6) {
                goto gm_lb2;
            }
            gm_tl874t2atnlh3 t7 = gm_l1;
            gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
            uint8_t t9 = (!gm_eqff85ac10o3(((uint8_t *)&t7), ((uint8_t *)&t8)));
            if (!t9) {
                goto gm_lb2;
            }
            void * t10 = gm_l0;
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, ((uint8_t *)t11));
            gm_tl874t2atnlh3 t13 = gm_l1;
            gm_b16_8 t14 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t15;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
            gm_b0_1 t16 = gm_f_linef_up3h3jj((void *)NULL, cx, NULL, t10, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_emit_plain_euobq8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_calls_emit_plain_euobq8f F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* res */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_b16_8 gm_l3; /* plain_call */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_l4 = t5;
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, ((uint8_t *)t6));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l2;
            uint8_t t10 = gm_f_is_unit_17uennd(t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t11 = (uint8_t)!t10;
            if (t11) {
                void * t12 = gm_l0;
                gm_b16_8 t13 = gm_l4;
                gm_b16_8 t14 = gm_l3;
                gm_t6j6smj7qoctu t15;
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t13);
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 16), t14);
                gm_b0_1 t16 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t12, &t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t17 = gm_l0;
    gm_b16_8 t18 = gm_l3;
    gm_tnpifv20irnph t19;
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
    gm_b0_1 t20 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t17, t19);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_equal_hcuag9d F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* a */
    gm_b16_8 gm_l3; /* b */
    gm_b16_8 gm_l4; /* f */
    gm_b16_8 gm_l5; /* pa */
    gm_b16_8 gm_l6; /* pb */
    GM_STACK_CHECK(&F, 632u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
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
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)6ULL)));
    gm_t5v8n6qrn4rh4 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)6ULL));
    if (t8) {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = (void *)(((uint8_t *)t11) + 2024);
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, ((uint8_t *)t13));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_b16_8 t17 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t12, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t17;
        void * t18 = gm_l0;
        gm_t88rcucg1b7uk t19 = gm_l1;
        gm_b16_8 t20 = gm_l2;
        gm_b16_8 t21 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t21;
        void * t22 = gm_l0;
        gm_t88rcucg1b7uk t23 = gm_l1;
        gm_b16_8 t24 = gm_l3;
        gm_b16_8 t25 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t25;
        gm_b16_8 t26 = gm_l4;
        gm_b16_8 t27 = gm_l5;
        gm_b16_8 t28 = gm_l6;
        gm_tfeab279c16rr t29;
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t26);
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 16), t27);
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 32), t28);
        gm_f_format_ht2r43m((void *)&gm_l7, (void *)NULL, cx, NULL, &t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcmeaag4q4qrjl, 459, 16);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        F.ret = t32;
        goto gm_fret;
    }
    gm_b16_8 t33 = gm_l2;
    gm_b16_8 t34 = gm_l3;
    gm_t6j6smj7qoctu t35;
    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t33);
    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 16), t34);
    gm_f_format_6m84e0r((void *)&gm_l8, (void *)NULL, cx, NULL, &t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcmeaag4q4qrjl, 461, 12);
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fn_format_stub_53r45gt F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* sink */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* f */
    gm_b16_8 gm_l4; /* f */
    gm_b16_8 gm_l5; /* head */
    GM_STACK_CHECK(&F, 609u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sk8csn7us23m3));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_l2 = t4;
    {
        {
            void * t5 = gm_l0;
            void * t6 = GM_LD(void *, ((uint8_t *)t5));
            void * t7 = (void *)(((uint8_t *)t6) + 1216);
            gm_b16_8 t8 = gm_l2;
            gm_f_get_e3joqcc((void *)&gm_l6, t7, t8);
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_l3 = t10;
            gm_b16_8 t11 = gm_l3;
            F.ret = t11;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    void * t14 = (void *)(((uint8_t *)t13) + 1272);
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sl8of2tnl835f));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(5));
    gm_b16_8 t16 = gm_l2;
    gm_b16_8 t17 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t14, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t17;
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, ((uint8_t *)t18));
    void * t20 = (void *)(((uint8_t *)t19) + 1216);
    gm_b16_8 t21 = gm_l2;
    gm_b16_8 t22 = gm_l4;
    gm_tftb1odc739t5 t23 = gm_f_put_7shfu9u(t20, t21, t22);
    gm_l7 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcmeaag4q4qrjl, 356, 5);
    gm_b16_8 t25 = gm_l4;
    gm_tnpifv20irnph t26;
    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
    gm_f_format_n1vqu08((void *)&gm_l8, (void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcmeaag4q4qrjl, 357, 13);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_l5 = t28;
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    void * t31 = (void *)(((uint8_t *)t30) + 2024);
    gm_b16_8 t32 = gm_l5;
    gm_tnpifv20irnph t33;
    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
    gm_tftb1odc739t5 t34 = gm_f_write_vp4v8ut(t31, t33);
    gm_l9 = t34;
    uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcmeaag4q4qrjl, 358, 5);
    void * t36 = gm_l0;
    void * t37 = GM_LD(void *, ((uint8_t *)t36));
    void * t38 = (void *)((((uint8_t *)t37) + 2024) + 40);
    gm_b16_8 t39 = gm_l5;
    gm_b16_8 t40 = gm_l1;
    gm_t6j6smj7qoctu t41;
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t39);
    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 16), t40);
    gm_tftb1odc739t5 t42 = gm_f_write_6o17mnv(t38, &t41);
    gm_l10 = t42;
    uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcmeaag4q4qrjl, 359, 5);
    gm_b16_8 t44 = gm_l4;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_ptr_type_f9efs3c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_fn_ptr_type_f9efs3c F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* params */
    gm_b16_8 gm_l3; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* p */
    GM_STACK_CHECK(&F, 763u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
    gm_l3 = t7;
    gm_trqfvmjtt4sij t8;
    gm_f_list_epvducg((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t8;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    gm_t88rcucg1b7uk t12 = gm_l1;
    uint8_t t13 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t13) {
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
        gm_l3 = t14;
        void * t15 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s44ek1gecbajs));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(6));
        gm_tftb1odc739t5 t17 = gm_f_push_3ef746b(t15, t16);
        gm_l9 = t17;
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcmeaag4q4qrjl, 20, 9);
    } else {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        gm_t88rcucg1b7uk t22 = gm_l1;
        gm_b16_8 t23 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t23;
    }
    void * t24 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s44ek1gecbajs));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(6));
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sono5n5lrjd7h));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(9));
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sono5n5lrjd7h));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(9));
    gm_t9bmh80edou9c t28;
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t25);
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 16), t26);
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 32), t27);
    F.l5 = t28;
    void * t29 = (void *)((uint8_t *)&F.l5);
    uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t31 = (uintptr_t)((uint64_t)3ULL);
    if (t30 > t31 || t31 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t30, t31, ((uint64_t)3ULL), gm_srcmeaag4q4qrjl, 24, 15);
    uint64_t t32 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t29 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32));
    gm_tftb1odc739t5 t34 = gm_f_extend_vogsd72(t24, t33);
    gm_l10 = t34;
    uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcmeaag4q4qrjl, 24, 5);
    gm_b16_8 t36 = gm_l2;
    gm_l6 = t36;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t37 = gm_l7;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t39 = (t37 < t38);
            if (!t39) {
                goto gm_lb1;
            }
            {
                void * t40 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t42 = gm_l7;
                {
                    {
                        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t40) + (uintptr_t)t42 * 4));
                        gm_l8 = t43;
                        void * t44 = (void *)((uint8_t *)&F.l4);
                        void * t45 = gm_l0;
                        void * t46 = GM_LD(void *, ((uint8_t *)t45));
                        void * t47 = (void *)(((uint8_t *)t46) + 8);
                        gm_t88rcucg1b7uk t48 = gm_l8;
                        gm_b16_8 t49 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t47, t48);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t50 = gm_f_push_3ef746b(t44, t49);
                        gm_l11 = t50;
                        uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcmeaag4q4qrjl, 25, 23);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 25, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l7;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l7 = t55;
        }
    }
gm_lb1:;
    gm_b16_8 t56 = gm_l3;
    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
    gm_b16_8 t59;
    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sg5gdnrith2i6));
    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(2));
    gm_f_join_sfknljb((void *)&gm_l12, (void *)NULL, cx, NULL, t58, t59);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcmeaag4q4qrjl, 26, 40);
    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
    gm_t6j6smj7qoctu t62;
    GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t56);
    GM_ST(gm_b16_8, ((uint8_t *)&t62 + 16), t61);
    gm_f_format_p19pl6n((void *)&gm_l13, (void *)NULL, cx, NULL, &t62);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcmeaag4q4qrjl, 26, 12);
    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
    gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_ptr_type_plain_bi8eu4l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_fn_ptr_type_plain_bi8eu4l F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l3; /* inner */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l4;
    void * t6 = gm_l0;
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)16ULL));
            if (!t10) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l4) + 8), 48);
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)&F.l2) + 24);
            uint8_t t13 = gm_f_within_sys_2lc2ugq(t11, t12);
            F.ret = t13;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)13ULL));
            if (!t15) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
            gm_l3 = t16;
            void * t17 = gm_l0;
            gm_t88rcucg1b7uk t18 = gm_l3;
            uint8_t t19 = gm_f_fn_ptr_type_plain_bi8eu4l((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t19;
            goto gm_fret;
        }
gm_lb3:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 185, 5);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_fn_sink_stub_5ipff9h F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* str_ty */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* f */
    gm_b16_8 gm_l4; /* f */
    gm_t88rcucg1b7uk gm_l5; /* unit_ty */
    gm_tnqk2qt7j42o9 gm_l6; /* tys */
    gm_b16_8 gm_l7; /* head */
    gm_b16_8 gm_l8; /* pt */
    gm_b16_8 gm_l9; /* plain */
    gm_b16_8 gm_l10; /* hidden */
    GM_STACK_CHECK(&F, 1131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0aqhubr4upni));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_l2 = t4;
    {
        {
            void * t5 = gm_l0;
            void * t6 = GM_LD(void *, ((uint8_t *)t5));
            void * t7 = (void *)(((uint8_t *)t6) + 1216);
            gm_b16_8 t8 = gm_l2;
            gm_f_get_e3joqcc((void *)&gm_l11, t7, t8);
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l3 = t10;
            gm_b16_8 t11 = gm_l3;
            F.ret = t11;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    void * t14 = (void *)(((uint8_t *)t13) + 1272);
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sl8of2tnl835f));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(5));
    gm_b16_8 t16 = gm_l2;
    gm_b16_8 t17 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t14, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t17;
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, ((uint8_t *)t18));
    void * t20 = (void *)(((uint8_t *)t19) + 1216);
    gm_b16_8 t21 = gm_l2;
    gm_b16_8 t22 = gm_l4;
    gm_tftb1odc739t5 t23 = gm_f_put_7shfu9u(t20, t21, t22);
    gm_l12 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcmeaag4q4qrjl, 336, 5);
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, ((uint8_t *)t25));
    void * t27 = (void *)(((uint8_t *)t26) + 8);
    gm_t88rcucg1b7uk t28 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t28;
    gm_t88rcucg1b7uk t29 = gm_l1;
    gm_tnqk2qt7j42o9 t30;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30 + 0), t29);
    gm_l6 = t30;
    gm_b16_8 t31 = gm_l4;
    gm_tnpifv20irnph t32;
    GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
    gm_f_format_01h4tbj((void *)&gm_l13, (void *)NULL, cx, NULL, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcmeaag4q4qrjl, 339, 13);
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
    gm_l7 = t34;
    void * t35 = gm_l0;
    void * t36 = GM_LD(void *, ((uint8_t *)t35));
    void * t37 = (void *)(((uint8_t *)t36) + 2024);
    gm_b16_8 t38 = gm_l7;
    gm_tnpifv20irnph t39;
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
    gm_tftb1odc739t5 t40 = gm_f_write_vp4v8ut(t37, t39);
    gm_l14 = t40;
    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcmeaag4q4qrjl, 340, 5);
    void * t42 = gm_l0;
    void * t43 = GM_LD(void *, ((uint8_t *)t42));
    void * t44 = (void *)(((uint8_t *)t43) + 8);
    gm_t88rcucg1b7uk t45 = gm_l1;
    gm_b16_8 t46 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t44, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t46;
    void * t47 = gm_l0;
    void * t48 = GM_LD(void *, ((uint8_t *)t47));
    void * t49 = (void *)(((uint8_t *)t48) + 8);
    gm_t88rcucg1b7uk t50 = gm_l1;
    gm_b16_8 t51 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t49, t50);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t52 = gm_l0;
    gm_t88rcucg1b7uk t53 = gm_l1;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_so78814tnt5g5));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(1));
    gm_b16_8 t55 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t52, t53, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6j6smj7qoctu t56;
    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t51);
    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 16), t55);
    gm_f_format_drnt54k((void *)&gm_l15, (void *)NULL, cx, NULL, &t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcmeaag4q4qrjl, 342, 14);
    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
    gm_l9 = t58;
    void * t59 = gm_l0;
    gm_t88rcucg1b7uk t60 = gm_l5;
    void * t61 = (void *)((uint8_t *)&gm_l6);
    uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t63 = (uintptr_t)((uint64_t)1ULL);
    if (t62 > t63 || t63 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t62, t63, ((uint64_t)1ULL), gm_srcmeaag4q4qrjl, 343, 82);
    uint64_t t64 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t61 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64));
    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
    gm_b16_8 t67 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t59, t60, t66);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t68;
    GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
    gm_f_format_5p0qm94((void *)&gm_l16, (void *)NULL, cx, NULL, t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcmeaag4q4qrjl, 343, 15);
    gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
    gm_l10 = t70;
    void * t71 = gm_l0;
    void * t72 = GM_LD(void *, ((uint8_t *)t71));
    void * t73 = (void *)((((uint8_t *)t72) + 2024) + 40);
    gm_b16_8 t74 = gm_l7;
    gm_b16_8 t75 = gm_l8;
    void * t76 = gm_l0;
    uint64_t t77 = gm_f_ptr_bytes_eocneng(t76);
    gm_b16_8 t78 = gm_l9;
    gm_b16_8 t79 = gm_l10;
    gm_ti71ll5knnvij t80;
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t74);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 16), t75);
    GM_ST(uint64_t, ((uint8_t *)&t80 + 32), t77);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 40), t78);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 56), t79);
    gm_tftb1odc739t5 t81 = gm_f_write_502hm86(t73, &t80);
    gm_l17 = t81;
    uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcmeaag4q4qrjl, 344, 5);
    gm_b16_8 t83 = gm_l4;
    F.ret = t83;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_arg_ldsagr2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_gamma_arg_ldsagr2 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l4;
    uint8_t gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!t6;
    if (t7) {
        gm_b16_8 t8 = gm_l2;
        F.ret = t8;
        goto gm_fret;
    }
    gm_b16_8 t9 = gm_l2;
    uint8_t t10 = gm_f_is_temp_rrllgdr(t9);
    if (t10) {
        gm_l5 = ((uint8_t)1);
    } else {
        gm_b16_8 t11 = gm_l2;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_somhu4vltiil1));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
        uint8_t t13 = gm_f_starts_with_6vvt01e(t11, t12);
        gm_l5 = t13;
    }
    uint8_t t14 = gm_l5;
    if (t14) {
        gm_b16_8 t15 = gm_l2;
        gm_l4 = t15;
    } else {
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_b16_8 t18 = gm_l2;
        gm_b16_8 t19 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    gm_b16_8 t20 = gm_l4;
    gm_l3 = t20;
    gm_b16_8 t21 = gm_l3;
    gm_tnpifv20irnph t22;
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
    gm_f_format_h6b2ofo((void *)&gm_l6, (void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcmeaag4q4qrjl, 54, 12);
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_args_jb6l3ik(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_gamma_args_jb6l3ik F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* vals */
    gm_b16_8 gm_l2; /* tys */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 354u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t7;
    gm_f_list_uvfu1k1((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t10 = (t8 < t9 ? t8 : t9);
    gm_l4 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l5;
            uint64_t t12 = gm_l4;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l5;
                        gm_l6 = t14;
                        void * t15 = (void *)((uint8_t *)&F.l3);
                        void * t16 = gm_l0;
                        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t19 = gm_l6;
                        if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_srcmeaag4q4qrjl, 61, 72);
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t17) + (uintptr_t)t19 * 4));
                        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t23 = gm_l6;
                        if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcmeaag4q4qrjl, 61, 80);
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t21) + (uintptr_t)t23 * 16));
                        gm_b16_8 t25 = gm_f_gamma_arg_ldsagr2((void *)NULL, cx, NULL, t16, t20, t24);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t26;
                        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
                        gm_tftb1odc739t5 t27 = gm_f_write_fp9dg1h(t15, t26);
                        gm_l7 = t27;
                        uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcmeaag4q4qrjl, 61, 41);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 61, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l5;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l5 = t32;
        }
    }
gm_lb1:;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
    F.ret = t34;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_gamma_value_rgbfhcc F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l3;
    gm_th2lqbo77pvsc gm_l4;
    gm_tabb710d9mq2e gm_l5;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_mirror_lcjegsf((void *)&gm_l4, (void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (t9) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 37, 8);
    }
gm_lb1:;
    uint8_t t10 = gm_l3;
    if (t10) {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, ((uint8_t *)t11));
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_t88rcucg1b7uk t14 = gm_l1;
        gm_b16_8 t15 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t16 = gm_l2;
        gm_t6j6smj7qoctu t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
        gm_f_format_3s8s1g6((void *)&gm_l5, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmeaag4q4qrjl, 37, 47);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        F.ret = t20;
        goto gm_fret;
    }
    gm_b16_8 t21 = gm_l2;
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
__attribute__((aligned(4))) void gm_f_native_op_r81a4at(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, const void * a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_native_op_r81a4at F;
    void * gm_l0; /* fg */
    void * gm_l1; /* effect_ */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* vals */
    gm_th2lqbo77pvsc gm_l4; /* res */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tk8ebsa8coi4p gm_l6; /* amb */
    uint64_t gm_l7; /* p */
    gm_b16_8 gm_l8; /* f */
    gm_b16_8 gm_l9; /* u */
    gm_b16_8 gm_l10; /* s */
    gm_b16_8 gm_l11; /* s */
    gm_b16_8 gm_l12; /* some */
    gm_b16_8 gm_l13; /* none */
    GM_STACK_CHECK(&F, 1317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    gm_l5 = a5;
    uint8_t gm_l14;
    gm_b16_8 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    memmove(((uint8_t *)&gm_l6), (((uint8_t *)t8) + 48), 32);
    void * t9 = gm_l0;
    uint64_t t10 = gm_f_ptr_bytes_eocneng(t9);
    gm_l7 = t10;
    gm_t9805qtonbsag t11 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l6));
    void * t12 = gm_l1;
    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)t12));
    gm_t9805qtonbsag t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[4] = 1;
    uint8_t t15 = (gm_eqkricceou4u(((uint8_t *)&t11), ((uint8_t *)&t14)));
    if (t15) {
        gm_l14 = ((uint8_t)1);
    } else {
        gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 8));
        void * t17 = gm_l1;
        uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)t17));
        gm_t9805qtonbsag t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t19), t18);
        ((uint8_t *)&t19)[4] = 1;
        uint8_t t20 = (gm_eqkricceou4u(((uint8_t *)&t16), ((uint8_t *)&t19)));
        gm_l14 = t20;
    }
    uint8_t t21 = gm_l14;
    if (t21) {
        gm_t9805qtonbsag t22 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l6));
        void * t23 = gm_l1;
        uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)t23));
        gm_t9805qtonbsag t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t25), t24);
        ((uint8_t *)&t25)[4] = 1;
        uint8_t t26 = (gm_eqkricceou4u(((uint8_t *)&t22), ((uint8_t *)&t25)));
        if (t26) {
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s9u3cqhrfmcru));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(12));
            gm_l15 = t27;
        } else {
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sqi7tb51umq69));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(12));
            gm_l15 = t28;
        }
        gm_b16_8 t29 = gm_l15;
        gm_l8 = t29;
        void * t30 = gm_l0;
        gm_b16_8 t31 = gm_l8;
        void * t32 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t33, gm_srcmeaag4q4qrjl, 419, 87);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t32) + (uintptr_t)((uint64_t)0ULL) * 16));
        void * t35 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t36, gm_srcmeaag4q4qrjl, 419, 96);
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t35) + (uintptr_t)((uint64_t)0ULL) * 16));
        uint64_t t38 = gm_l7;
        gm_tttl9nsdo1iu6 t39;
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 16), t34);
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 32), t37);
        GM_ST(uint64_t, ((uint8_t *)&t39 + 48), t38);
        gm_b0_1 t40 = gm_f_linef_0v7c424((void *)NULL, cx, NULL, t30, &t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t41 = gm_l0;
        gm_b16_8 t42 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t42;
        void * t43 = gm_l0;
        gm_b16_8 t44 = gm_l9;
        gm_b0_1 t45 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t43, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        {
            {
                uint32_t t46 = gm_l2;
                uint8_t t47 = ((t46 == ((uint32_t)0ULL)));
                if (!t47) {
                    goto gm_lb2;
                }
                void * t48 = gm_l0;
                gm_b16_8 t49;
                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s9go9vvkpcjpd));
                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(47));
                gm_b0_1 t50 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t48, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t51 = gm_l0;
                gm_t88rcucg1b7uk t52 = gm_l5;
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sp5b2hbslij9l));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(4));
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sb6j3egm9oljk));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(4));
                gm_b16_8 t55 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t51, t52, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t55;
                void * t56 = gm_l0;
                gm_b16_8 t57 = gm_l10;
                gm_b0_1 t58 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t56, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint32_t t59 = gm_l2;
                uint8_t t60 = ((t59 == ((uint32_t)1ULL)));
                if (!t60) {
                    goto gm_lb3;
                }
                void * t61 = gm_l0;
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_su2c9m69n1vjb));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(30));
                gm_b0_1 t63 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t61, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t64 = gm_l0;
                void * t65 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t66)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t66, gm_srcmeaag4q4qrjl, 431, 123);
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)t65) + (uintptr_t)((uint64_t)0ULL) * 16));
                void * t68 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t69)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t69, gm_srcmeaag4q4qrjl, 431, 132);
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)t68) + (uintptr_t)((uint64_t)0ULL) * 16));
                uint64_t t71 = gm_l7;
                gm_tftksr4op114e t72;
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t67);
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 16), t70);
                GM_ST(uint64_t, ((uint8_t *)&t72 + 32), t71);
                gm_b0_1 t73 = gm_f_linef_3fol08t((void *)NULL, cx, NULL, t64, &t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t74 = gm_l0;
                void * t75 = gm_l0;
                void * t76 = GM_LD(void *, ((uint8_t *)t75));
                void * t77 = (void *)(((uint8_t *)t76) + 8);
                gm_t88rcucg1b7uk t78 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t79;
                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sc4r4l9k6f577));
                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(2));
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s3jl3afv8nlvv));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(3));
                gm_b16_8 t81 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t74, t78, t79, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t81;
                void * t82 = gm_l0;
                gm_t88rcucg1b7uk t83 = gm_l5;
                gm_b16_8 t84 = gm_l11;
                gm_b16_8 t85 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t82, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t85;
                void * t86 = gm_l0;
                gm_t88rcucg1b7uk t87 = gm_l5;
                gm_b16_8 t88 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t88;
                void * t89 = gm_l0;
                gm_b16_8 t90 = gm_l12;
                gm_b16_8 t91 = gm_l13;
                gm_t6j6smj7qoctu t92;
                GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t90);
                GM_ST(gm_b16_8, ((uint8_t *)&t92 + 16), t91);
                gm_th2lqbo77pvsc t93 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4));
                gm_f_format_g55p0hj((void *)&gm_l16, (void *)NULL, cx, NULL, &t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcmeaag4q4qrjl, 435, 42);
                gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                gm_b0_1 t97 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t89, &t93, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb3:;
            {
                void * t98 = gm_l0;
                void * t99 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t100, gm_srcmeaag4q4qrjl, 438, 56);
                gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)t99) + (uintptr_t)((uint64_t)0ULL) * 16));
                gm_tnpifv20irnph t102;
                GM_ST(gm_b16_8, ((uint8_t *)&t102 + 0), t101);
                gm_b0_1 t103 = gm_f_linef_i03em5a((void *)NULL, cx, NULL, t98, t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t104 = gm_l0;
                gm_b16_8 t105;
                GM_ST(void *, (uint8_t *)&t105, (void *)(gm_s04qus0luoga5));
                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(4));
                gm_b16_8 t106;
                GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sicgmpca63jln));
                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(1));
                gm_b16_8 t107;
                GM_ST(void *, (uint8_t *)&t107, (void *)(gm_svubtddeidu1d));
                GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(1));
                gm_t9qkh0rsooc8d t108;
                GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)3ULL));
                gm_b0_1 t109 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t104, t108, t105, t106, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb4:;
            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 423, 9);
        }
gm_lb1:;
    }
    gm_tqgtd5gtgj65s t110;
    memset(&t110, 0, sizeof t110);
    ((uint8_t *)&t110)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t110 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t110), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_out_stub_ils4a69 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* effect_ */
    uint32_t gm_l2; /* id */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tk8ebsa8coi4p gm_l6; /* amb */
    gm_b16_8 gm_l7; /* writer */
    gm_b16_8 gm_l8; /* fp */
    gm_b16_8 gm_l9; /* pt */
    uint64_t gm_l10; /* p */
    gm_b16_8 gm_l11; /* name */
    gm_b16_8 gm_l12; /* key */
    gm_b16_8 gm_l13; /* f */
    gm_b16_8 gm_l14; /* f */
    gm_b16_8 gm_l15; /* sig */
    GM_STACK_CHECK(&F, 1535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_th2lqbo77pvsc gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    memmove(((uint8_t *)&gm_l6), (((uint8_t *)t6) + 48), 32);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
    gm_l7 = t7;
    gm_t9805qtonbsag t8 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l6));
    void * t9 = gm_l1;
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)t9));
    gm_t9805qtonbsag t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[4] = 1;
    uint8_t t12 = (gm_eqkricceou4u(((uint8_t *)&t8), ((uint8_t *)&t11)));
    if (t12) {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s9u3cqhrfmcru));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(12));
        gm_l7 = t13;
    } else {
        gm_t9805qtonbsag t14 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 8));
        void * t15 = gm_l1;
        uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)t15));
        gm_t9805qtonbsag t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t17), t16);
        ((uint8_t *)&t17)[4] = 1;
        uint8_t t18 = (gm_eqkricceou4u(((uint8_t *)&t14), ((uint8_t *)&t17)));
        if (t18) {
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sqi7tb51umq69));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(12));
            gm_l7 = t19;
        } else {
            gm_th2lqbo77pvsc t20;
            memset(&t20, 0, sizeof t20);
            memmove(gm_out, ((uint8_t *)&t20), 24);
            goto gm_fdone;
        }
    }
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, ((uint8_t *)t21));
    void * t23 = (void *)(((uint8_t *)t22) + 8);
    gm_t88rcucg1b7uk t24 = gm_l5;
    uint8_t t25 = gm_f_is_unit_17uennd(t23, GM_CONV(gm_csguankko4lmhd, t24));
    uint8_t t26 = (uint8_t)!t25;
    if (t26) {
        gm_l18 = ((uint8_t)1);
    } else {
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint8_t t28 = (!(t27 == ((uint64_t)1ULL)));
        gm_l18 = t28;
    }
    uint8_t t29 = gm_l18;
    if (t29) {
        gm_l17 = ((uint8_t)1);
    } else {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, ((uint8_t *)t30));
        void * t32 = (void *)(((uint8_t *)t31) + 8);
        void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t34, gm_srcmeaag4q4qrjl, 312, 71);
        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + (uintptr_t)((uint64_t)0ULL) * 4));
        uint8_t t36 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t32, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t36;
    }
    uint8_t t37 = gm_l17;
    if (t37) {
        gm_l16 = ((uint8_t)1);
    } else {
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, ((uint8_t *)t38));
        void * t40 = GM_LD(void *, ((uint8_t *)t39));
        void * t41 = gm_l1;
        uint32_t t42 = GM_LD(uint32_t, ((uint8_t *)t41));
        uint32_t t43 = gm_l3;
        uint8_t t44 = gm_f_is_ctl_oclllo3(t40, t42, t43);
        gm_l16 = t44;
    }
    uint8_t t45 = gm_l16;
    if (t45) {
        gm_th2lqbo77pvsc t46;
        memset(&t46, 0, sizeof t46);
        memmove(gm_out, ((uint8_t *)&t46), 24);
        goto gm_fdone;
    }
    void * t47 = gm_l0;
    gm_t88rcucg1b7uk t48 = gm_l5;
    gm_b16_8 t49 = gm_l4;
    gm_b16_8 t50 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t47, t48, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t50;
    void * t51 = gm_l0;
    void * t52 = GM_LD(void *, ((uint8_t *)t51));
    void * t53 = (void *)(((uint8_t *)t52) + 8);
    void * t54 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t55, gm_srcmeaag4q4qrjl, 316, 29);
    gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + (uintptr_t)((uint64_t)0ULL) * 4));
    gm_b16_8 t57 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t53, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t57;
    void * t58 = gm_l0;
    uint64_t t59 = gm_f_ptr_bytes_eocneng(t58);
    gm_l10 = t59;
    void * t60 = gm_l0;
    void * t61 = GM_LD(void *, ((uint8_t *)t60));
    void * t62 = GM_LD(void *, ((uint8_t *)t61));
    void * t63 = GM_LD(void *, ((uint8_t *)t62));
    void * t64 = (void *)(((uint8_t *)t63) + 8);
    void * t65 = gm_l1;
    uint32_t t66 = GM_LD(uint32_t, ((uint8_t *)t65));
    gm_b16_8 t67 = gm_f_effect_name_prpp0ip(t64, t66);
    gm_l11 = t67;
    uint32_t t68 = gm_l2;
    uint32_t t69 = gm_l3;
    gm_b16_8 t70 = gm_l7;
    gm_b16_8 t71 = gm_l9;
    gm_tgat3d8836orb t72;
    GM_ST(uint32_t, ((uint8_t *)&t72 + 0), t68);
    GM_ST(uint32_t, ((uint8_t *)&t72 + 4), t69);
    GM_ST(gm_b16_8, ((uint8_t *)&t72 + 8), t70);
    GM_ST(gm_b16_8, ((uint8_t *)&t72 + 24), t71);
    gm_f_format_nivudut((void *)&gm_l19, (void *)NULL, cx, NULL, &t72);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcmeaag4q4qrjl, 319, 12);
    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
    gm_l12 = t74;
    {
        {
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, ((uint8_t *)t75));
            void * t77 = (void *)(((uint8_t *)t76) + 1216);
            gm_b16_8 t78 = gm_l12;
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            gm_f_get_e3joqcc((void *)&gm_l20, t77, t79);
            uint8_t t80 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
            if (!t80) {
                goto gm_lb2;
            }
            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
            gm_l13 = t81;
            gm_b16_8 t82 = gm_l13;
            gm_th2lqbo77pvsc t83;
            GM_ST(uint8_t, (uint8_t *)&t83 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t83 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t83 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t83), t82);
            ((uint8_t *)&t83)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t83), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t84 = gm_l0;
    void * t85 = GM_LD(void *, ((uint8_t *)t84));
    void * t86 = (void *)(((uint8_t *)t85) + 1272);
    gm_b16_8 t87;
    GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sl8of2tnl835f));
    GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(5));
    gm_b16_8 t88 = gm_l12;
    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
    gm_b16_8 t90 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t86, t87, t89);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t90;
    void * t91 = gm_l0;
    void * t92 = GM_LD(void *, ((uint8_t *)t91));
    void * t93 = (void *)(((uint8_t *)t92) + 1216);
    gm_b16_8 t94 = gm_l12;
    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
    gm_b16_8 t96 = gm_l14;
    gm_tftb1odc739t5 t97 = gm_f_put_7shfu9u(t93, t95, t96);
    gm_l21 = t97;
    uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcmeaag4q4qrjl, 322, 5);
    gm_b16_8 t99 = gm_l14;
    gm_b16_8 t100 = gm_l9;
    gm_t6j6smj7qoctu t101;
    GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t99);
    GM_ST(gm_b16_8, ((uint8_t *)&t101 + 16), t100);
    gm_f_format_3o89hhj((void *)&gm_l22, (void *)NULL, cx, NULL, &t101);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcmeaag4q4qrjl, 323, 12);
    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
    gm_l15 = t103;
    void * t104 = gm_l0;
    void * t105 = GM_LD(void *, ((uint8_t *)t104));
    void * t106 = (void *)(((uint8_t *)t105) + 2024);
    gm_b16_8 t107 = gm_l15;
    gm_tnpifv20irnph t108;
    GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
    gm_tftb1odc739t5 t109 = gm_f_write_vp4v8ut(t106, t108);
    gm_l23 = t109;
    uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcmeaag4q4qrjl, 324, 5);
    void * t111 = gm_l0;
    void * t112 = GM_LD(void *, ((uint8_t *)t111));
    void * t113 = (void *)((((uint8_t *)t112) + 2024) + 40);
    gm_b16_8 t114 = gm_l15;
    uint32_t t115 = gm_l2;
    gm_b16_8 t116 = gm_l11;
    gm_b16_8 t117 = gm_l7;
    uint64_t t118 = gm_l10;
    gm_b16_8 t119 = gm_l8;
    uint32_t t120 = gm_l3;
    gm_tobu7tlrkigeq t121;
    GM_ST(uint32_t, (uint8_t *)&t121 + 20, 0);
    GM_ST(uint32_t, (uint8_t *)&t121 + 84, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t114);
    GM_ST(uint32_t, ((uint8_t *)&t121 + 16), t115);
    GM_ST(gm_b16_8, ((uint8_t *)&t121 + 24), t116);
    GM_ST(gm_b16_8, ((uint8_t *)&t121 + 40), t117);
    GM_ST(uint64_t, ((uint8_t *)&t121 + 56), t118);
    GM_ST(gm_b16_8, ((uint8_t *)&t121 + 64), t119);
    GM_ST(uint32_t, ((uint8_t *)&t121 + 80), t120);
    gm_tftb1odc739t5 t122 = gm_f_write_mv65o15(t113, &t121);
    gm_l24 = t122;
    uint8_t t123 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t123) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcmeaag4q4qrjl, 325, 5);
    gm_b16_8 t124 = gm_l14;
    gm_th2lqbo77pvsc t125;
    GM_ST(uint8_t, (uint8_t *)&t125 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t125 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t125 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t125), t124);
    ((uint8_t *)&t125)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t125), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6) {
    struct gm_fr_perform_t77ghfg F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* effect_ */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l8; /* r */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l10; /* n */
    gm_b16_8 gm_l11; /* cx */
    gm_b16_8 gm_l12; /* loc */
    gm_b16_8 gm_l13; /* name */
    gm_th2lqbo77pvsc gm_l14; /* res */
    gm_b16_8 gm_l15; /* fp */
    gm_b16_8 gm_l16; /* args */
    gm_b16_8 gm_l17; /* out */
    gm_b16_8 gm_l18; /* r */
    gm_b16_8 gm_l19; /* clause_call */
    gm_tk8ebsa8coi4p gm_l20; /* amb */
    uint8_t gm_l21; /* native */
    gm_th2lqbo77pvsc gm_l22; /* rv */
    gm_b16_8 gm_l23; /* r */
    GM_STACK_CHECK(&F, 2619u);
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
    gm_th2lqbo77pvsc gm_l24;
    gm_th2lqbo77pvsc gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tabb710d9mq2e gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    gm_tqgtd5gtgj65s gm_l31;
    uint8_t gm_l32;
    {
        void * t15 = gm_l0;
        void * t16 = GM_LD(void *, ((uint8_t *)t15));
        void * t17 = gm_l2;
        uint32_t t18 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t18;
        {
            {
                void * t19 = gm_l0;
                uint32_t t20 = gm_l7;
                void * t21 = gm_l2;
                uint32_t t22 = gm_l3;
                gm_b16_8 t23 = gm_l4;
                gm_b16_8 t24 = gm_l5;
                gm_t88rcucg1b7uk t25 = gm_l6;
                gm_f_direct_perform_vc33l18((void *)&gm_l24, (void *)NULL, cx, NULL, t19, t20, t21, t22, t23, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l24))[16] != 0);
                if (!t26) {
                    goto gm_lb2;
                }
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                gm_l8 = t27;
                gm_b16_8 t28 = gm_l8;
                gm_t5o0tpv5jka2l t29;
                GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t29 + 32, 0);
                ((uint8_t *)&t29)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
                memmove(gm_out, ((uint8_t *)&t29), 40);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                void * t30 = gm_l0;
                gm_tl874t2atnlh3 t31 = gm_l1;
                void * t32 = gm_l2;
                uint32_t t33 = gm_l7;
                uint32_t t34 = gm_l3;
                gm_b16_8 t35 = gm_l4;
                gm_b16_8 t36 = gm_l5;
                gm_t88rcucg1b7uk t37 = gm_l6;
                gm_f_stub_perform_m3dbb7g((void *)&gm_l25, (void *)NULL, cx, NULL, t30, t31, t32, t33, t34, t35, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l25))[16] != 0);
                if (!t38) {
                    goto gm_lb4;
                }
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                gm_l9 = t39;
                gm_b16_8 t40 = gm_l9;
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
            }
gm_lb4:;
        }
gm_lb3:;
        void * t42 = gm_l0;
        uint32_t t43 = gm_f_fresh_5kokjmg(t42);
        gm_t88rcucg1b7uk t44;
        GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t43);
        gm_f_format_r9dvh2e((void *)&gm_l26, (void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcmeaag4q4qrjl, 256, 10);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
        gm_l10 = t46;
        void * t47 = gm_l0;
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t47) + 200));
        gm_l11 = t48;
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, ((uint8_t *)t49));
        gm_tl874t2atnlh3 t51 = gm_l1;
        gm_b16_8 t52 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t52;
        void * t53 = gm_l0;
        void * t54 = GM_LD(void *, ((uint8_t *)t53));
        void * t55 = GM_LD(void *, ((uint8_t *)t54));
        void * t56 = GM_LD(void *, ((uint8_t *)t55));
        void * t57 = (void *)(((uint8_t *)t56) + 8);
        void * t58 = gm_l2;
        uint32_t t59 = GM_LD(uint32_t, ((uint8_t *)t58));
        gm_b16_8 t60 = gm_f_effect_name_prpp0ip(t57, t59);
        gm_l13 = t60;
    }
    {
        void * t61 = gm_l0;
        gm_b16_8 t62 = gm_l10;
        gm_b16_8 t63 = gm_l11;
        uint32_t t64 = gm_l7;
        gm_td7tfovebirdn t65;
        GM_ST(uint32_t, (uint8_t *)&t65 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t62);
        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 16), t63);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 32), t64);
        gm_b0_1 t66 = gm_f_linef_gg3n7rd((void *)NULL, cx, NULL, t61, &t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t67 = gm_l0;
        gm_b16_8 t68 = gm_l10;
        gm_b16_8 t69 = gm_l13;
        gm_b16_8 t70 = gm_l12;
        gm_tfeab279c16rr t71;
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t68);
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t69);
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 32), t70);
        gm_b0_1 t72 = gm_f_linef_qg7idri((void *)NULL, cx, NULL, t67, &t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t73 = gm_l0;
        void * t74 = GM_LD(void *, ((uint8_t *)t73));
        void * t75 = GM_LD(void *, ((uint8_t *)t74));
        void * t76 = gm_l2;
        uint32_t t77 = GM_LD(uint32_t, ((uint8_t *)t76));
        uint32_t t78 = gm_l3;
        uint8_t t79 = gm_f_is_ctl_oclllo3(t75, t77, t78);
        if (t79) {
            void * t80 = gm_l0;
            gm_b16_8 t81 = gm_l10;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            uint32_t t83 = gm_l3;
            gm_b16_8 t84 = gm_l4;
            gm_b16_8 t85 = gm_l5;
            gm_t88rcucg1b7uk t86 = gm_l6;
            gm_f_ctl_perform_be6h6eo((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t80, t82, t83, t84, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_th2lqbo77pvsc t87;
        memset(&t87, 0, sizeof t87);
        gm_l14 = t87;
        void * t88 = gm_l0;
        void * t89 = GM_LD(void *, ((uint8_t *)t88));
        void * t90 = (void *)(((uint8_t *)t89) + 8);
        gm_t88rcucg1b7uk t91 = gm_l6;
        uint8_t t92 = gm_f_is_never_in5ftma(t90, GM_CONV(gm_csguankko4lmhd, t91));
        uint8_t t93 = (uint8_t)!t92;
        if (t93) {
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = gm_l6;
            gm_b16_8 t96 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t97;
            GM_ST(uint8_t, (uint8_t *)&t97 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t97 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t97 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t97), t96);
            ((uint8_t *)&t97)[16] = 1;
            gm_l14 = t97;
        }
        void * t98 = gm_l0;
        gm_t88rcucg1b7uk t99 = gm_l6;
        gm_b16_8 t100 = gm_l5;
        gm_b16_8 t101 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t98, t99, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t101;
        void * t102 = gm_l0;
        gm_b16_8 t103 = gm_l4;
        gm_b16_8 t104 = gm_l5;
        gm_b16_8 t105 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t102, t103, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t105;
        gm_b16_8 t106;
        GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(0));
        gm_l17 = t106;
        {
            {
                uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                if (!t107) {
                    goto gm_lb6;
                }
                gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                gm_l18 = t108;
                void * t109 = gm_l0;
                void * t110 = GM_LD(void *, ((uint8_t *)t109));
                void * t111 = (void *)(((uint8_t *)t110) + 8);
                gm_t88rcucg1b7uk t112 = gm_l6;
                uint8_t t113 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t111, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t113) {
                    gm_b16_8 t114 = gm_l18;
                    gm_tnpifv20irnph t115;
                    GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
                    gm_f_format_itcjtai((void *)&gm_l27, (void *)NULL, cx, NULL, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                    if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srcmeaag4q4qrjl, 269, 40);
                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                    gm_l17 = t118;
                }
                goto gm_lb5;
            }
gm_lb6:;
        }
gm_lb5:;
    }
    {
        gm_b16_8 t119 = gm_l15;
        gm_b16_8 t120 = gm_l10;
        uint32_t t121 = gm_l3;
        gm_b16_8 t122 = gm_l17;
        gm_b16_8 t123 = gm_l10;
        gm_b16_8 t124 = gm_l10;
        gm_b16_8 t125 = gm_l10;
        gm_b16_8 t126 = gm_l16;
        gm_t586m754bse2v t127;
        GM_ST(uint32_t, (uint8_t *)&t127 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 0), t119);
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 16), t120);
        GM_ST(uint32_t, ((uint8_t *)&t127 + 32), t121);
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 40), t122);
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 56), t123);
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 72), t124);
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 88), t125);
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 104), t126);
        gm_f_format_18tu4v6((void *)&gm_l28, (void *)NULL, cx, NULL, &t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcmeaag4q4qrjl, 271, 20);
        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
        gm_l19 = t129;
        void * t130 = gm_l0;
        void * t131 = GM_LD(void *, ((uint8_t *)t130));
        void * t132 = GM_LD(void *, ((uint8_t *)t131));
        memmove(((uint8_t *)&gm_l20), (((uint8_t *)t132) + 48), 32);
        gm_t9805qtonbsag t133 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l20));
        void * t134 = gm_l2;
        uint32_t t135 = GM_LD(uint32_t, ((uint8_t *)t134));
        gm_t9805qtonbsag t136;
        GM_ST(uint8_t, (uint8_t *)&t136 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t136 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t136), t135);
        ((uint8_t *)&t136)[4] = 1;
        uint8_t t137 = (gm_eqkricceou4u(((uint8_t *)&t133), ((uint8_t *)&t136)));
        if (t137) {
            gm_l30 = ((uint8_t)1);
        } else {
            gm_t9805qtonbsag t138 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 8));
            void * t139 = gm_l2;
            uint32_t t140 = GM_LD(uint32_t, ((uint8_t *)t139));
            gm_t9805qtonbsag t141;
            GM_ST(uint8_t, (uint8_t *)&t141 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t141 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t141), t140);
            ((uint8_t *)&t141)[4] = 1;
            uint8_t t142 = (gm_eqkricceou4u(((uint8_t *)&t138), ((uint8_t *)&t141)));
            gm_l30 = t142;
        }
        uint8_t t143 = gm_l30;
        if (t143) {
            gm_l29 = ((uint8_t)1);
        } else {
            gm_t9805qtonbsag t144 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 16));
            void * t145 = gm_l2;
            uint32_t t146 = GM_LD(uint32_t, ((uint8_t *)t145));
            gm_t9805qtonbsag t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t147), t146);
            ((uint8_t *)&t147)[4] = 1;
            uint8_t t148 = (gm_eqkricceou4u(((uint8_t *)&t144), ((uint8_t *)&t147)));
            gm_l29 = t148;
        }
        uint8_t t149 = gm_l29;
        gm_l21 = t149;
        uint8_t t150 = gm_l21;
        if (t150) {
            void * t151 = gm_l0;
            gm_b16_8 t152 = gm_l10;
            gm_tnpifv20irnph t153;
            GM_ST(gm_b16_8, ((uint8_t *)&t153 + 0), t152);
            gm_b0_1 t154 = gm_f_linef_ch11let((void *)NULL, cx, NULL, t151, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t155 = gm_l0;
            void * t156 = gm_l0;
            uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)t156) + 144));
            uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
            if (o159) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 276, 9);
            uint64_t t160 = r158;
            GM_ST(uint64_t, (((uint8_t *)t155) + 144), t160);
            void * t161 = gm_l0;
            void * t162 = gm_l2;
            uint32_t t163 = gm_l3;
            gm_b16_8 t164 = gm_l4;
            gm_t88rcucg1b7uk t165 = gm_l6;
            gm_th2lqbo77pvsc t166 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l14));
            gm_f_native_op_r81a4at((void *)&gm_l31, (void *)NULL, cx, NULL, t161, t162, t163, t164, &t166, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (t167) {
            } else {
                gm_t08olq97rscn3 t168 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l31) + 8));
                gm_t5o0tpv5jka2l t169;
                GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                ((uint8_t *)&t169)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t169 + 8), t168);
                memmove(gm_out, ((uint8_t *)&t169), 40);
                goto gm_fdone;
            }
            void * t170 = gm_l0;
            void * t171 = gm_l0;
            uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)t171) + 144));
            uint64_t r173; bool o174 = __builtin_sub_overflow(t172, ((uint64_t)1ULL), &r173);
            if (o174) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 278, 9);
            uint64_t t175 = r173;
            GM_ST(uint64_t, (((uint8_t *)t170) + 144), t175);
            void * t176 = gm_l0;
            gm_b16_8 t177;
            GM_ST(void *, (uint8_t *)&t177, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(8));
            gm_b0_1 t178 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t176, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t179 = gm_l0;
            void * t180 = gm_l0;
            uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)t180) + 144));
            uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
            if (o183) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 280, 9);
            uint64_t t184 = r182;
            GM_ST(uint64_t, (((uint8_t *)t179) + 144), t184);
        }
    }
    {
        memmove(((uint8_t *)&gm_l22), ((uint8_t *)&gm_l14), 24);
        {
            {
                {
                    {
                        uint8_t t185 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                        if (t185) {
                            goto gm_lb10;
                        }
                        gm_l32 = ((uint8_t)0);
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        gm_l32 = ((uint8_t)1);
                        goto gm_lb9;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 283, 8);
                }
gm_lb9:;
                uint8_t t186 = gm_l32;
                if (!t186) {
                    goto gm_lb8;
                }
                uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t188 = ((t187 == ((uint64_t)0ULL)));
                if (!t188) {
                    goto gm_lb8;
                }
                void * t189 = gm_l0;
                uint8_t t190 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                if (!t190) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 284, 30);
                gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                gm_b16_8 t192 = gm_l19;
                gm_t6j6smj7qoctu t193;
                GM_ST(gm_b16_8, ((uint8_t *)&t193 + 0), t191);
                GM_ST(gm_b16_8, ((uint8_t *)&t193 + 16), t192);
                gm_b0_1 t194 = gm_f_linef_i5ai2tu((void *)NULL, cx, NULL, t189, &t193);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb7;
            }
gm_lb8:;
            void * t195 = gm_l0;
            gm_b16_8 t196 = gm_l19;
            gm_tnpifv20irnph t197;
            GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t196);
            gm_b0_1 t198 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t195, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
gm_lb7:;
        uint8_t t199 = gm_l21;
        if (t199) {
            void * t200 = gm_l0;
            void * t201 = gm_l0;
            uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)t201) + 144));
            uint64_t r203; bool o204 = __builtin_sub_overflow(t202, ((uint64_t)1ULL), &r203);
            if (o204) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 289, 9);
            uint64_t t205 = r203;
            GM_ST(uint64_t, (((uint8_t *)t200) + 144), t205);
            void * t206 = gm_l0;
            gm_b16_8 t207;
            GM_ST(void *, (uint8_t *)&t207, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(1));
            gm_b0_1 t208 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t206, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t209 = gm_l0;
        gm_b0_1 t210 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t209);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t211 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                if (!t211) {
                    goto gm_lb13;
                }
                gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                gm_l23 = t212;
                gm_b16_8 t213 = gm_l23;
                gm_t5o0tpv5jka2l t214;
                GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t214 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t214 + 32, 0);
                ((uint8_t *)&t214)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t214 + 8), t213);
                memmove(gm_out, ((uint8_t *)&t214), 40);
                goto gm_fdone;
            }
gm_lb13:;
        }
gm_lb12:;
        void * t215 = gm_l0;
        gm_t88rcucg1b7uk t216 = gm_l6;
        gm_b16_8 t217 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t215, t216);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t218;
        GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t218 + 32, 0);
        ((uint8_t *)&t218)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t218 + 8), t217);
        memmove(gm_out, ((uint8_t *)&t218), 40);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_piece_stub_m8mb7rc F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* core */
    uint8_t gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* str_ty */
    gm_b16_8 gm_l4; /* kind_text */
    gm_b16_8 gm_l5; /* key */
    gm_b16_8 gm_l6; /* f */
    gm_b16_8 gm_l7; /* f */
    gm_b16_8 gm_l8; /* pt */
    uint64_t gm_l9; /* p */
    gm_b16_8 gm_l10; /* loc */
    gm_b16_8 gm_l11; /* head */
    gm_b16_8 gm_l12; /* pre */
    gm_b16_8 gm_l13; /* data */
    gm_b16_8 gm_l14; /* len */
    gm_b16_8 gm_l15; /* call */
    gm_b16_8 gm_l16; /* post */
    GM_STACK_CHECK(&F, 1586u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_b16_8 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    uint8_t t4 = gm_l2;
    uint8_t t5 = ((t4 == ((uint8_t)115ULL)));
    if (t5) {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s03pmf867dcsr));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        gm_l17 = t6;
    } else {
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_slq58vfha8k9h));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        gm_l17 = t7;
    }
    gm_b16_8 t8 = gm_l17;
    gm_l4 = t8;
    gm_b16_8 t9 = gm_l1;
    gm_b16_8 t10 = gm_l4;
    gm_t6j6smj7qoctu t11;
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t9);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t10);
    gm_f_format_jp1qc7o((void *)&gm_l18, (void *)NULL, cx, NULL, &t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcmeaag4q4qrjl, 368, 12);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
    gm_l5 = t13;
    {
        {
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, ((uint8_t *)t14));
            void * t16 = (void *)(((uint8_t *)t15) + 1216);
            gm_b16_8 t17 = gm_l5;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_f_get_e3joqcc((void *)&gm_l19, t16, t18);
            uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
            if (!t19) {
                goto gm_lb2;
            }
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
            gm_l6 = t20;
            gm_b16_8 t21 = gm_l6;
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    void * t24 = (void *)(((uint8_t *)t23) + 1272);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sl8of2tnl835f));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(5));
    gm_b16_8 t26 = gm_l5;
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    gm_b16_8 t28 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t24, t25, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t28;
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    void * t31 = (void *)(((uint8_t *)t30) + 1216);
    gm_b16_8 t32 = gm_l5;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
    gm_b16_8 t34 = gm_l7;
    gm_tftb1odc739t5 t35 = gm_f_put_7shfu9u(t31, t33, t34);
    gm_l20 = t35;
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcmeaag4q4qrjl, 371, 5);
    void * t37 = gm_l0;
    void * t38 = GM_LD(void *, ((uint8_t *)t37));
    void * t39 = (void *)(((uint8_t *)t38) + 8);
    gm_t88rcucg1b7uk t40 = gm_l3;
    gm_b16_8 t41 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t39, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t41;
    void * t42 = gm_l0;
    uint64_t t43 = gm_f_ptr_bytes_eocneng(t42);
    gm_l9 = t43;
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_st6to3kuud2ge));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(45));
    gm_l10 = t44;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(0));
    gm_l11 = t45;
    uint8_t t46 = gm_l2;
    uint8_t t47 = ((t46 == ((uint8_t)115ULL)));
    if (t47) {
        gm_b16_8 t48 = gm_l7;
        gm_b16_8 t49 = gm_l10;
        gm_t6j6smj7qoctu t50;
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t48);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 16), t49);
        gm_f_format_ru16275((void *)&gm_l21, (void *)NULL, cx, NULL, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srcmeaag4q4qrjl, 377, 16);
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
        gm_l11 = t53;
    } else {
        gm_b16_8 t54 = gm_l7;
        gm_b16_8 t55 = gm_l10;
        gm_t6j6smj7qoctu t56;
        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t54);
        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 16), t55);
        gm_f_format_37aah2s((void *)&gm_l22, (void *)NULL, cx, NULL, &t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcmeaag4q4qrjl, 379, 16);
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_l11 = t59;
    }
    void * t60 = gm_l0;
    void * t61 = GM_LD(void *, ((uint8_t *)t60));
    void * t62 = (void *)(((uint8_t *)t61) + 2024);
    gm_b16_8 t63 = gm_l11;
    gm_tnpifv20irnph t64;
    GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
    gm_tftb1odc739t5 t65 = gm_f_write_scdecr6(t62, t64);
    gm_l23 = t65;
    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcmeaag4q4qrjl, 381, 5);
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(0));
    gm_l12 = t67;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_stafskn6bv24f));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
    gm_l13 = t68;
    gm_b16_8 t69;
    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_smnfu5n5pum1l));
    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(1));
    gm_l14 = t69;
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(0));
    gm_l15 = t70;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(0));
    gm_l16 = t71;
    uint8_t t72 = gm_l2;
    uint8_t t73 = (!(t72 == ((uint8_t)115ULL)));
    if (t73) {
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_siihmb4jo3dsn));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(124));
        gm_l12 = t74;
        gm_b16_8 t75;
        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s8o6lm1cn4eo5));
        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(2));
        gm_l13 = t75;
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_s579fb7l5l6df));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(2));
        gm_l14 = t76;
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s3vqae7u5ljtm));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(13));
        gm_l15 = t77;
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_spsh7mv5lihcc));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(33));
        gm_l16 = t78;
    }
    void * t79 = gm_l0;
    void * t80 = GM_LD(void *, ((uint8_t *)t79));
    void * t81 = (void *)((((uint8_t *)t80) + 2024) + 40);
    gm_b16_8 t82 = gm_l11;
    gm_b16_8 t83 = gm_l12;
    gm_b16_8 t84 = gm_l8;
    gm_b16_8 t85 = gm_l13;
    uint64_t t86 = gm_l9;
    gm_b16_8 t87 = gm_l14;
    gm_b16_8 t88 = gm_l15;
    gm_b16_8 t89 = gm_l1;
    gm_b16_8 t90 = gm_l16;
    gm_tn96msocmj81h t91;
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t82);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 16), t83);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 32), t84);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 48), t85);
    GM_ST(uint64_t, ((uint8_t *)&t91 + 64), t86);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 72), t87);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 88), t88);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 104), t89);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 120), t90);
    gm_tftb1odc739t5 t92 = gm_f_write_otf2npe(t81, &t91);
    gm_l24 = t92;
    uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcmeaag4q4qrjl, 395, 5);
    gm_b16_8 t94 = gm_l7;
    F.ret = t94;
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
inline __attribute__((aligned(4))) void gm_f_plain_args_o7acjm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_plain_args_o7acjm5 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* vals */
    gm_b16_8 gm_l2; /* tys */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t7;
    gm_f_list_epvducg((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t10 = (t8 < t9 ? t8 : t9);
    gm_l4 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l5;
            uint64_t t12 = gm_l4;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l5;
                        gm_l6 = t14;
                        void * t15 = (void *)((uint8_t *)&F.l3);
                        void * t16 = gm_l0;
                        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t19 = gm_l6;
                        if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_srcmeaag4q4qrjl, 44, 65);
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t17) + (uintptr_t)t19 * 4));
                        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t23 = gm_l6;
                        if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcmeaag4q4qrjl, 44, 73);
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t21) + (uintptr_t)t23 * 16));
                        gm_b16_8 t25 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t16, t20, t24);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t26 = gm_f_push_3ef746b(t15, t25);
                        gm_l7 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcmeaag4q4qrjl, 44, 41);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 44, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t28 = gm_l5;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l5 = t31;
        }
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&F.l3), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_plain_value_e380k1u F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* m */
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l4;
    gm_tabb710d9mq2e gm_l5;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = (void *)(((uint8_t *)t5) + 8);
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_f_mirror_lcjegsf((void *)&gm_l4, (void *)NULL, cx, NULL, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l3 = t9;
            gm_b16_8 t10 = gm_l3;
            gm_b16_8 t11 = gm_l2;
            gm_t6j6smj7qoctu t12;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t10);
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 16), t11);
            gm_f_format_3s8s1g6((void *)&gm_l5, (void *)NULL, cx, NULL, &t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmeaag4q4qrjl, 31, 40);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            F.ret = t15;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t16 = gm_l2;
    F.ret = t16;
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
extern __attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_stub_perform_m3dbb7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7) {
    struct gm_fr_stub_perform_m3dbb7g F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* effect_ */
    uint32_t gm_l3; /* id */
    uint32_t gm_l4; /* op */
    gm_b16_8 gm_l5; /* vals */
    gm_b16_8 gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_b16_8 gm_l8; /* f */
    gm_b16_8 gm_l9; /* cx */
    gm_b16_8 gm_l10; /* loc */
    GM_STACK_CHECK(&F, 541u);
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
    gm_th2lqbo77pvsc gm_l11;
    gm_b16_8 gm_l12;
    void * t2 = gm_l0;
    void * t3 = gm_l2;
    uint32_t t4 = gm_l3;
    uint32_t t5 = gm_l4;
    gm_b16_8 t6 = gm_l6;
    gm_t88rcucg1b7uk t7 = gm_l7;
    gm_f_out_stub_ils4a69((void *)&gm_l11, (void *)NULL, cx, NULL, t2, t3, t4, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l12 = t9;
    } else {
        gm_th2lqbo77pvsc t10;
        memset(&t10, 0, sizeof t10);
        memmove(gm_out, ((uint8_t *)&t10), 24);
        goto gm_fdone;
    }
    gm_b16_8 t11 = gm_l12;
    gm_l8 = t11;
    void * t12 = gm_l0;
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 200));
    gm_l9 = t13;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    gm_tl874t2atnlh3 t16 = gm_l1;
    gm_b16_8 t17 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t17;
    void * t18 = gm_l0;
    gm_b16_8 t19 = gm_l8;
    gm_b16_8 t20 = gm_l9;
    void * t21 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t22, gm_srcmeaag4q4qrjl, 404, 40);
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t21) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_b16_8 t24 = gm_l10;
    gm_t0uuhaljsf410 t25;
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t19);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t20);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 32), t23);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 48), t24);
    gm_b0_1 t26 = gm_f_linef_icst3ps((void *)NULL, cx, NULL, t18, &t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t27 = gm_l0;
    gm_b0_1 t28 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t29 = gm_l0;
    gm_t88rcucg1b7uk t30 = gm_l7;
    gm_b16_8 t31 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t32), t31);
    ((uint8_t *)&t32)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t32), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
