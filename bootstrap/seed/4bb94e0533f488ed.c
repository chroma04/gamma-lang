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
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { void * a[3]; } gm_thmgeksj5pdth;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thmgeksj5pdth) == 24 && _Alignof(gm_thmgeksj5pdth) == 8 && offsetof(gm_thmgeksj5pdth, a) == 0, "layout of `[3]^Expr`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_empty_slice_rcd2jln {
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
struct gm_fr_format_0gvqkke {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4nuejdf {
    gm_frame h;
    gm_t7neromnu8t62 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t7neromnu8t62 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7cgva50 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7r64gap {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8eujoej {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9rhpk5t {
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
struct gm_fr_format_frnomph {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gsgjkse {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ima6g5u {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jm4dd7q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jrngvku {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l035o11 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l2fmok5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m1f4vhf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mrlevsk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pdashts {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pepv3ej {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q2uenf3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qu5usm7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s7ci5ul {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ul5ufam {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_bool_ajhjgeg {
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
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_lane_error_72hoonn {
    gm_frame h;
    void * ret;
};
struct gm_fr_lane_needs_a6gcpil {
    gm_frame h;
    void * ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mask_ty_cj7ln0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
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
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_vector_binary_bicu7u0 {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_construction_fhm2mm5 {
    gm_frame h;
    gm_tkacitepg5fk0 l8; /* a */
    void * ret;
};
struct gm_fr_vector_conversion_32a9cuf {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_load_ssdvomk {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_method_hgn4sb1 {
    gm_frame h;
    gm_tc3oht4ksth9d l16; /* xe */
    void * ret;
};
struct gm_fr_vector_shuffle_5ime7a7 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* idx */
    gm_t7te2tdpebbt4 l12; /* a */
    gm_tc3oht4ksth9d l13; /* e */
    gm_tbci5ospk5nnn l17; /* i */
    gm_tbci5ospk5nnn l23; /* i */
    gm_trqfvmjtt4sij l26; /* all */
    gm_tcgrjq9toscke l29; /* i */
    void * ret;
};
struct gm_fr_vector_unary_2mbe97t {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) void gm_f_format_0gvqkke(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0q4nncb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8eujoej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8i47cpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_cr9a627(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_gsgjkse(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ima6g5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jm4dd7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_m1f4vhf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pepv3ej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ul5ufam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_lane_type_ok_rp8uq1p(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_simd_eafgrv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vector_shape_error_3invhhe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0709va30ttvj[277];
extern const uint8_t gm_s091lteo20dar[64];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1t08grtrn18m[9];
extern const uint8_t gm_s2jmrvqu8r8aq[58];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s6lddausbiitu[9];
extern const uint8_t gm_s73f72842a67m[45];
extern const uint8_t gm_s7uepsfl7trr3[70];
extern const uint8_t gm_s8guqcjcce9oc[22];
extern const uint8_t gm_s95g98ft0cnt1[155];
extern const uint8_t gm_s95gukpblkpua[33];
extern const uint8_t gm_s9scq8a60n125[74];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajso5sujsruj[47];
extern const uint8_t gm_salin070nim24[30];
extern const uint8_t gm_salpeh0v5rqdf[8];
extern const uint8_t gm_satil70qt25hp[52];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sbqoiitqtfv75[11];
extern const uint8_t gm_scqrlin5l437v[9];
extern const uint8_t gm_sd5imeip8klsb[40];
extern const uint8_t gm_sdinre9qvo2vq[28];
extern const uint8_t gm_sejjhtlr11ojd[60];
extern const uint8_t gm_sf1qp7r4ktsd1[11];
extern const uint8_t gm_sfmov9pb6t5lt[14];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sh1to1i0u67lu[9];
extern const uint8_t gm_shmgu63rvoce3[36];
extern const uint8_t gm_sioam4mlrjuar[52];
extern const uint8_t gm_sj7s3tnnvob4e[9];
extern const uint8_t gm_skdalh0o0hret[59];
extern const uint8_t gm_skftljk77e6mu[4];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slbn3chj3p1e1[49];
extern const uint8_t gm_slu3lsfv09uua[25];
extern const uint8_t gm_sm03uudimlmgi[59];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_smgfld86ipv3f[58];
extern const uint8_t gm_snc5ns3akncuq[6];
extern const uint8_t gm_snhskkd5kfgfa[31];
extern const uint8_t gm_sovcjofctijvj[47];
extern const uint8_t gm_spj0hqsg585et[14];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqhh38dkd5qvg[43];
extern const uint8_t gm_sqn4pcenn3reh[63];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7u9cfdjh7s0n[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srsa6dr3d9ut4[41];
extern const uint8_t gm_st4t0ufodvddp[59];
extern const uint8_t gm_stoidbi3vfa99[30];
extern const uint8_t gm_sut92jpq3kk88[11];
extern const uint8_t gm_sv8pauo1bhltl[9];
extern const uint8_t gm_svgoe41piv341[7];
extern const gm_tmpl gm_tpl0q3rsndpu857;
extern const gm_tmpl gm_tpl1umecooe9oti;
extern const gm_tmpl gm_tpl2lqr5lnbpsvm;
extern const gm_tmpl gm_tpl4524ekle00ud;
extern const gm_tmpl gm_tpl4phv0vr9as7f;
extern const gm_tmpl gm_tpl6boglt2rkmh4;
extern const gm_tmpl gm_tpl9kch76oh20pa;
extern const gm_tmpl gm_tplbds86ojmcoik;
extern const gm_tmpl gm_tplbnm8984s77af;
extern const gm_tmpl gm_tplellk9btkq1f2;
extern const gm_tmpl gm_tplgj9d3igk4i62;
extern const gm_tmpl gm_tplh5ff53j0j0d2;
extern const gm_tmpl gm_tpligtpf80arr7h;
extern const gm_tmpl gm_tplnt454lmhb7iv;
extern const gm_tmpl gm_tploj4uaptg6qnk;
extern const gm_tmpl gm_tplqnau1elkkogs;
extern const gm_tmpl gm_tplsh3mbm625h0g;
extern const gm_tmpl gm_tplsk9b8o9jgshf;
extern const gm_tmpl gm_tplt1si3io9mdpe;
extern const gm_tmpl gm_tplurkkmrij8g4f;
extern const gm_tmpl gm_tplv8s8fgl2lggu;
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
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) void gm_f_format_0gvqkke(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0gvqkke F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd5imeip8klsb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(39));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplt1si3io9mdpe), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4nuejdf F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s091lteo20dar));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(63));
    gm_t7neromnu8t62 t3 = GM_LD(gm_t7neromnu8t62, ((uint8_t *)a0));
    gm_f_format_0q4nncb((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpligtpf80arr7h), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7cgva50 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_satil70qt25hp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0q3rsndpu857), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7r64gap F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snhskkd5kfgfa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplh5ff53j0j0d2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8eujoej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8eujoej F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sovcjofctijvj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_cr9a627((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9kch76oh20pa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_frnomph F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7uepsfl7trr3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbnm8984s77af), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gsgjkse(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gsgjkse F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0709va30ttvj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(276));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1umecooe9oti), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ima6g5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ima6g5u F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s73f72842a67m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(44));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsk9b8o9jgshf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jm4dd7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jm4dd7q F;
    gm_tob1snuii5bme gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tob1snuii5bme gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sovcjofctijvj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_tob1snuii5bme t3 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)a0));
    gm_f_format_8i47cpq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9kch76oh20pa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jrngvku F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shmgu63rvoce3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnt454lmhb7iv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l035o11 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2jmrvqu8r8aq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv8s8fgl2lggu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l2fmok5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqn4pcenn3reh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6boglt2rkmh4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_m1f4vhf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_m1f4vhf F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9scq8a60n125));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(73));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqnau1elkkogs), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pepv3ej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pepv3ej F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm03uudimlmgi));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4524ekle00ud), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q2uenf3 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slbn3chj3p1e1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplellk9btkq1f2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qu5usm7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95g98ft0cnt1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(154));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsh3mbm625h0g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s7ci5ul F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95gukpblkpua));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tploj4uaptg6qnk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ul5ufam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ul5ufam F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srsa6dr3d9ut4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbds86ojmcoik), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
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
    gm_f_vector_parts_crrfopm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
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
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_mask_ty_cj7ln0g F;
    void * gm_l0; /* tys */
    uint64_t gm_l1; /* len */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t6 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t2, t3, t5);
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t2) {
        gm_t9p38b040grms t3;
        memset(&t3, 0, sizeof t3);
        memmove(gm_out, ((uint8_t *)&t3), 32);
        goto gm_fdone;
    }
    gm_tbci5ospk5nnn t4 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9p38b040grms gm_l2;
    unsigned __int128 gm_l3;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_f_to_u128_haka99r((void *)&gm_l2, &t2);
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        unsigned __int128 t4 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l2));
        gm_l3 = t4;
    } else {
        gm_tf5srh4mp4b3i t5;
        memset(&t5, 0, sizeof t5);
        F.ret = t5;
        goto gm_fret;
    }
    unsigned __int128 t6 = gm_l3;
    gm_l1 = t6;
    unsigned __int128 t7 = gm_l1;
    gm_tf5srh4mp4b3i t8;
    uint64_t t9 = (uint64_t)t7;
    gm_tf5srh4mp4b3i t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[8] = 1;
    gm_tf5srh4mp4b3i t11;
    memset(&t11, 0, sizeof t11);
    t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
    F.ret = t8;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_error_72hoonn F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* opn */
    gm_b16_8 gm_l2; /* what */
    gm_t88rcucg1b7uk gm_l3; /* vt */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l5;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l4;
    gm_b16_8 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = gm_l3;
    gm_b16_8 t8 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tfeab279c16rr t9;
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 32), t8);
    gm_f_format_ima6g5u((void *)&gm_l5, (void *)NULL, cx, NULL, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src7u9cfdjh7s0n, 124, 20);
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    gm_b0_1 t13 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    gm_tl874t2atnlh3 t16 = gm_l4;
    void * t17 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t15, t16);
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
inline __attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_needs_a6gcpil F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* what */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* lane */
    GM_STACK_CHECK(&F, 430u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tb7u19loo3j87 gm_l6;
    gm_tlcm4lvu3g1a8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_f_vector_parts_crrfopm((void *)&gm_l6, t4, GM_CONV(gm_csguankko4lmhd, t5));
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (!t6) gm_trap("unwrap of null", gm_src7u9cfdjh7s0n, 118, 13);
    gm_tlcm4lvu3g1a8 t7 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l6));
    gm_l7 = t7;
    gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
    gm_l5 = t8;
    void * t9 = gm_l0;
    gm_tl874t2atnlh3 t10 = gm_l4;
    gm_b16_8 t11 = gm_l1;
    gm_b16_8 t12 = gm_l3;
    void * t13 = gm_l0;
    gm_t88rcucg1b7uk t14 = gm_l2;
    gm_b16_8 t15 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t16 = gm_l0;
    gm_t88rcucg1b7uk t17 = gm_l5;
    gm_b16_8 t18 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0uuhaljsf410 t19;
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t11);
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t12);
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 32), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 48), t18);
    gm_f_format_0gvqkke((void *)&gm_l8, (void *)NULL, cx, NULL, &t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src7u9cfdjh7s0n, 119, 20);
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
    gm_b0_1 t23 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t9, t10, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    gm_tl874t2atnlh3 t26 = gm_l4;
    void * t27 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t27;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_binary_bicu7u0 F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* vt */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    gm_t88rcucg1b7uk gm_l8; /* lane */
    gm_b16_8 gm_l9; /* opn */
    void * gm_l10; /* r */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t9qkh0rsooc8d gm_l12; /* f */
    gm_ts6oqppjh0ba3 gm_l13; /* top */
    void * gm_l14; /* r */
    gm_t88rcucg1b7uk gm_l15; /* rt */
    gm_b16_8 gm_l16; /* hint */
    uint8_t gm_l17; /* lane_int */
    uint8_t gm_l18; /* lane_float */
    uint8_t gm_l19; /* numeric */
    gm_b16_8 gm_l20; /* m */
    gm_t9qkh0rsooc8d gm_l21; /* f */
    gm_t9qkh0rsooc8d gm_l22; /* f */
    gm_t9qkh0rsooc8d gm_l23; /* f */
    gm_ts6oqppjh0ba3 gm_l24; /* top */
    gm_ts6oqppjh0ba3 gm_l25; /* top */
    GM_STACK_CHECK(&F, 3199u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tb7u19loo3j87 gm_l26;
    gm_tlcm4lvu3g1a8 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_ts6oqppjh0ba3 gm_l32;
    gm_b16_8 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    uint8_t gm_l35;
    gm_b16_8 gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_ts6oqppjh0ba3 gm_l39;
    gm_ts6oqppjh0ba3 gm_l40;
    gm_tabb710d9mq2e gm_l41;
    {
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, ((uint8_t *)t56));
        void * t58 = (void *)(((uint8_t *)t57) + 8);
        gm_l5 = t58;
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 96);
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, ((uint8_t *)t61));
        void * t63 = gm_l2;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t63));
        gm_t88rcucg1b7uk t65 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t60, t62, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t65;
        void * t66 = gm_l5;
        gm_t88rcucg1b7uk t67 = gm_l6;
        gm_f_vector_parts_crrfopm((void *)&gm_l26, t66, GM_CONV(gm_csguankko4lmhd, t67));
        uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
        if (t68) {
            gm_tlcm4lvu3g1a8 t69 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l26));
            gm_l27 = t69;
        } else {
            void * t70 = gm_l0;
            void * t71 = GM_LD(void *, ((uint8_t *)t70));
            gm_tl874t2atnlh3 t72 = gm_l4;
            void * t73 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t73;
            goto gm_fret;
        }
        gm_tlcm4lvu3g1a8 t74 = gm_l27;
        gm_l7 = t74;
        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l8 = t75;
        gm_ts6oqppjh0ba3 t76 = gm_l1;
        gm_b16_8 t77 = gm_f_binop_text_0ljfc0r(t76);
        gm_l9 = t77;
        {
            {
                {
                    {
                        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t79 = (uint8_t)(t78 == ((uint8_t)20ULL));
                        if (!t79) {
                            goto gm_lb4;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t81 = (uint8_t)(t80 == ((uint8_t)21ULL));
                        if (!t81) {
                            goto gm_lb5;
                        }
                        goto gm_lb3;
                    }
gm_lb5:;
                    goto gm_lb2;
                }
gm_lb3:;
                void * t82 = gm_l0;
                void * t83 = gm_l3;
                gm_tkn42u7na3t81 t84;
                memset(&t84, 0, sizeof t84);
                void * t85 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t82, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t86 = gm_l0;
                gm_tl874t2atnlh3 t87 = gm_l4;
                gm_b16_8 t88 = gm_l9;
                gm_tnpifv20irnph t89;
                GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
                gm_f_format_frnomph((void *)&gm_l28, (void *)NULL, cx, NULL, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src7u9cfdjh7s0n, 37, 28);
                gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                gm_b0_1 t93 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t87, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t94 = gm_l0;
                void * t95 = GM_LD(void *, ((uint8_t *)t94));
                gm_tl874t2atnlh3 t96 = gm_l4;
                void * t97 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t97;
                goto gm_fret;
            }
gm_lb2:;
            {
                {
                    {
                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t99 = (uint8_t)(t98 == ((uint8_t)14ULL));
                        if (!t99) {
                            goto gm_lb8;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    {
                        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t101 = (uint8_t)(t100 == ((uint8_t)15ULL));
                        if (!t101) {
                            goto gm_lb9;
                        }
                        goto gm_lb7;
                    }
gm_lb9:;
                    {
                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t103 = (uint8_t)(t102 == ((uint8_t)10ULL));
                        if (!t103) {
                            goto gm_lb10;
                        }
                        goto gm_lb7;
                    }
gm_lb10:;
                    {
                        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t105 = (uint8_t)(t104 == ((uint8_t)11ULL));
                        if (!t105) {
                            goto gm_lb11;
                        }
                        goto gm_lb7;
                    }
gm_lb11:;
                    goto gm_lb6;
                }
gm_lb7:;
                void * t106 = gm_l0;
                void * t107 = gm_l3;
                gm_tkn42u7na3t81 t108;
                memset(&t108, 0, sizeof t108);
                void * t109 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t106, t107, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t110 = gm_l0;
                gm_tl874t2atnlh3 t111 = gm_l4;
                gm_b16_8 t112 = gm_l9;
                gm_tnpifv20irnph t113;
                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t112);
                gm_f_format_7r64gap((void *)&gm_l29, (void *)NULL, cx, NULL, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_src7u9cfdjh7s0n, 42, 28);
                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                gm_b0_1 t117 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t110, t111, t116);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t118 = gm_l0;
                void * t119 = GM_LD(void *, ((uint8_t *)t118));
                gm_tl874t2atnlh3 t120 = gm_l4;
                void * t121 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t119, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t121;
                goto gm_fret;
            }
gm_lb6:;
            {
                {
                    {
                        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t123 = (uint8_t)(t122 == ((uint8_t)3ULL));
                        if (!t123) {
                            goto gm_lb14;
                        }
                        goto gm_lb13;
                    }
gm_lb14:;
                    {
                        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t125 = (uint8_t)(t124 == ((uint8_t)6ULL));
                        if (!t125) {
                            goto gm_lb15;
                        }
                        goto gm_lb13;
                    }
gm_lb15:;
                    goto gm_lb12;
                }
gm_lb13:;
                void * t126 = gm_l0;
                void * t127 = gm_l3;
                gm_tkn42u7na3t81 t128;
                memset(&t128, 0, sizeof t128);
                void * t129 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t126, t127, t128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t129;
                void * t130 = gm_l5;
                gm_t88rcucg1b7uk t131 = gm_l8;
                uint8_t t132 = gm_f_is_integer_8d4f9ek(t130, GM_CONV(gm_csguankko4lmhd, t131));
                uint8_t t133 = (uint8_t)!t132;
                if (t133) {
                    void * t134 = gm_l0;
                    gm_tl874t2atnlh3 t135 = gm_l4;
                    gm_b16_8 t136 = gm_l9;
                    void * t137 = gm_l0;
                    gm_t88rcucg1b7uk t138 = gm_l6;
                    gm_b16_8 t139 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t137, t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t140;
                    GM_ST(gm_b16_8, ((uint8_t *)&t140 + 0), t136);
                    GM_ST(gm_b16_8, ((uint8_t *)&t140 + 16), t139);
                    gm_f_format_q2uenf3((void *)&gm_l30, (void *)NULL, cx, NULL, &t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_src7u9cfdjh7s0n, 48, 32);
                    gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                    gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                    gm_b0_1 t144 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t134, t135, t143);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t145 = gm_l0;
                    void * t146 = GM_LD(void *, ((uint8_t *)t145));
                    gm_tl874t2atnlh3 t147 = gm_l4;
                    void * t148 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t148;
                    goto gm_fret;
                }
                gm_ts6oqppjh0ba3 t149 = gm_l1;
                gm_t9qkh0rsooc8d t150;
                GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)3ULL));
                uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
                uint8_t t152 = (uint8_t)(t151 == ((uint8_t)3ULL));
                if (t152) {
                    gm_t9qkh0rsooc8d t153 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t149) + 1));
                    t152 = (uint8_t)gm_eqehhdtpeti1(((uint8_t *)&t153), ((uint8_t *)&t150));
                }
                uint8_t t154 = (uint8_t)(t152);
                gm_ts6oqppjh0ba3 t155;
                GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)3ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t155 + 1), t150);
                if (t154) {
                    void * t156 = gm_l0;
                    gm_tl874t2atnlh3 t157 = gm_l4;
                    gm_b16_8 t158;
                    GM_ST(void *, (uint8_t *)&t158, (void *)(gm_st4t0ufodvddp));
                    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(58));
                    gm_b0_1 t159 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t156, t157, t158);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t160 = gm_l0;
                    void * t161 = GM_LD(void *, ((uint8_t *)t160));
                    gm_tl874t2atnlh3 t162 = gm_l4;
                    void * t163 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t161, t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t163;
                    goto gm_fret;
                }
                {
                    {
                        void * t164 = gm_l0;
                        void * t165 = (void *)(((uint8_t *)t164) + 96);
                        void * t166 = gm_l5;
                        void * t167 = gm_l10;
                        gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t167));
                        gm_t9qkh0rsooc8d t169;
                        GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)1ULL));
                        uint8_t t170 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t165, t166, t168, t169);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t171 = (uint8_t)!t170;
                        if (!t171) {
                            goto gm_lb17;
                        }
                        void * t172 = gm_l5;
                        void * t173 = gm_l10;
                        gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t173));
                        uint8_t t175 = gm_f_is_error_8k27gd1(t172, GM_CONV(gm_csguankko4lmhd, t174));
                        uint8_t t176 = (uint8_t)!t175;
                        if (!t176) {
                            goto gm_lb17;
                        }
                        void * t177 = gm_l0;
                        void * t178 = (void *)(((uint8_t *)t177) + 96);
                        void * t179 = gm_l0;
                        void * t180 = GM_LD(void *, ((uint8_t *)t179));
                        void * t181 = gm_l10;
                        gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t181));
                        gm_t88rcucg1b7uk t183 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t178, t180, t182);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t183;
                        void * t184 = gm_l0;
                        void * t185 = gm_l10;
                        gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t185) + 72));
                        void * t187 = gm_l0;
                        gm_t88rcucg1b7uk t188 = gm_l11;
                        gm_b16_8 t189 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t187, t188);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t190;
                        GM_ST(gm_b16_8, ((uint8_t *)&t190 + 0), t189);
                        gm_f_format_pdashts((void *)&gm_l31, (void *)NULL, cx, NULL, t190);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                        if (!t191) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src7u9cfdjh7s0n, 57, 34);
                        gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                        gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                        gm_b0_1 t194 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t184, t186, t193);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb16;
                    }
gm_lb17:;
                }
gm_lb16:;
                {
                    {
                        uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t196 = (uint8_t)(t195 == ((uint8_t)3ULL));
                        if (!t196) {
                            goto gm_lb19;
                        }
                        gm_t9qkh0rsooc8d t197 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l12 = t197;
                        gm_t9qkh0rsooc8d t198 = gm_l12;
                        gm_ts6oqppjh0ba3 t199;
                        GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t199 + 1), t198);
                        gm_l32 = t199;
                        goto gm_lb18;
                    }
gm_lb19:;
                    gm_ts6oqppjh0ba3 t200;
                    GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)6ULL));
                    gm_l32 = t200;
                }
gm_lb18:;
                gm_ts6oqppjh0ba3 t201 = gm_l32;
                gm_l13 = t201;
                gm_t88rcucg1b7uk t202 = gm_l6;
                gm_ts6oqppjh0ba3 t203 = gm_l13;
                void * t204 = gm_l2;
                void * t205 = gm_l10;
                gm_trl9d7m1k5lbd t206;
                GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t206 + 0), t203);
                GM_ST(void *, ((uint8_t *)&t206 + 8), t204);
                GM_ST(void *, ((uint8_t *)&t206 + 16), t205);
                gm_tl874t2atnlh3 t207 = gm_l4;
                gm_t2b6tm8c0vgo1 t208;
                memset(&t208, 0, sizeof t208);
                GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)30ULL));
                GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t208 + 8), t206);
                void * t209 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t202, &t208, t207);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t209;
                goto gm_fret;
            }
gm_lb12:;
            {
                goto gm_lb1;
            }
gm_lb20:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 34, 5);
        }
gm_lb1:;
    }
    {
        void * t210 = gm_l0;
        void * t211 = gm_l3;
        gm_t88rcucg1b7uk t212 = gm_l6;
        gm_tkn42u7na3t81 t213;
        GM_ST(uint8_t, (uint8_t *)&t213 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t213 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t213), t212);
        ((uint8_t *)&t213)[4] = 1;
        void * t214 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t210, t211, t213);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t214;
        void * t215 = gm_l0;
        void * t216 = (void *)(((uint8_t *)t215) + 96);
        void * t217 = gm_l0;
        void * t218 = GM_LD(void *, ((uint8_t *)t217));
        void * t219 = gm_l14;
        gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t219));
        gm_t88rcucg1b7uk t221 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t216, t218, t220);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t221;
        void * t222 = gm_l5;
        gm_t88rcucg1b7uk t223 = gm_l15;
        uint8_t t224 = gm_f_is_error_8k27gd1(t222, GM_CONV(gm_csguankko4lmhd, t223));
        if (t224) {
            void * t225 = gm_l0;
            void * t226 = GM_LD(void *, ((uint8_t *)t225));
            gm_tl874t2atnlh3 t227 = gm_l4;
            void * t228 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t226, t227);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t228;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t229 = gm_l15;
        gm_t88rcucg1b7uk t230 = gm_l6;
        uint8_t t231 = (!gm_eq9m6mvtmgde(((uint8_t *)&t229), ((uint8_t *)&t230)));
        if (t231) {
            void * t232 = gm_l5;
            gm_t88rcucg1b7uk t233 = gm_l15;
            uint8_t t234 = gm_f_is_vector_gkq6b88(t232, GM_CONV(gm_csguankko4lmhd, t233));
            if (t234) {
                gm_b16_8 t235;
                GM_ST(void *, (uint8_t *)&t235, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(0));
                gm_l33 = t235;
            } else {
                gm_b16_8 t236;
                GM_ST(void *, (uint8_t *)&t236, (void *)(gm_smgfld86ipv3f));
                GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(57));
                gm_l33 = t236;
            }
            gm_b16_8 t237 = gm_l33;
            gm_l16 = t237;
            void * t238 = gm_l0;
            gm_tl874t2atnlh3 t239 = gm_l4;
            gm_b16_8 t240 = gm_l9;
            void * t241 = gm_l0;
            gm_t88rcucg1b7uk t242 = gm_l6;
            gm_b16_8 t243 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t241, t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t244 = gm_l0;
            gm_t88rcucg1b7uk t245 = gm_l15;
            gm_b16_8 t246 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t244, t245);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t247 = gm_l16;
            gm_t0uuhaljsf410 t248;
            GM_ST(gm_b16_8, ((uint8_t *)&t248 + 0), t240);
            GM_ST(gm_b16_8, ((uint8_t *)&t248 + 16), t243);
            GM_ST(gm_b16_8, ((uint8_t *)&t248 + 32), t246);
            GM_ST(gm_b16_8, ((uint8_t *)&t248 + 48), t247);
            gm_f_format_pepv3ej((void *)&gm_l34, (void *)NULL, cx, NULL, &t248);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t249 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
            if (!t249) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src7u9cfdjh7s0n, 69, 24);
            gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
            gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
            gm_b0_1 t252 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t238, t239, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t253 = gm_l0;
            void * t254 = GM_LD(void *, ((uint8_t *)t253));
            gm_tl874t2atnlh3 t255 = gm_l4;
            void * t256 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t254, t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t256;
            goto gm_fret;
        }
    }
    {
        void * t257 = gm_l5;
        gm_t88rcucg1b7uk t258 = gm_l8;
        uint8_t t259 = gm_f_is_integer_8d4f9ek(t257, GM_CONV(gm_csguankko4lmhd, t258));
        gm_l17 = t259;
        void * t260 = gm_l5;
        gm_t88rcucg1b7uk t261 = gm_l8;
        uint8_t t262 = gm_f_is_float_3jk89qu(t260, GM_CONV(gm_csguankko4lmhd, t261));
        gm_l18 = t262;
        uint8_t t263 = gm_l17;
        if (t263) {
            gm_l35 = ((uint8_t)1);
        } else {
            uint8_t t264 = gm_l18;
            gm_l35 = t264;
        }
        uint8_t t265 = gm_l35;
        gm_l19 = t265;
        {
            {
                {
                    {
                        uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t267 = (uint8_t)(t266 == ((uint8_t)16ULL));
                        if (!t267) {
                            goto gm_lb24;
                        }
                        goto gm_lb23;
                    }
gm_lb24:;
                    {
                        uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t269 = (uint8_t)(t268 == ((uint8_t)17ULL));
                        if (!t269) {
                            goto gm_lb25;
                        }
                        goto gm_lb23;
                    }
gm_lb25:;
                    {
                        uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t271 = (uint8_t)(t270 == ((uint8_t)18ULL));
                        if (!t271) {
                            goto gm_lb26;
                        }
                        goto gm_lb23;
                    }
gm_lb26:;
                    {
                        uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t273 = (uint8_t)(t272 == ((uint8_t)19ULL));
                        if (!t273) {
                            goto gm_lb27;
                        }
                        goto gm_lb23;
                    }
gm_lb27:;
                    goto gm_lb22;
                }
gm_lb23:;
                {
                    {
                        uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t275 = (uint8_t)(t274 == ((uint8_t)16ULL));
                        if (!t275) {
                            goto gm_lb29;
                        }
                        gm_b16_8 t276;
                        GM_ST(void *, (uint8_t *)&t276, (void *)(gm_sh1to1i0u67lu));
                        GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(8));
                        gm_l36 = t276;
                        goto gm_lb28;
                    }
gm_lb29:;
                    {
                        uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t278 = (uint8_t)(t277 == ((uint8_t)17ULL));
                        if (!t278) {
                            goto gm_lb30;
                        }
                        gm_b16_8 t279;
                        GM_ST(void *, (uint8_t *)&t279, (void *)(gm_sv8pauo1bhltl));
                        GM_ST(uintptr_t, (uint8_t *)&t279 + 8, (uintptr_t)(8));
                        gm_l36 = t279;
                        goto gm_lb28;
                    }
gm_lb30:;
                    {
                        uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t281 = (uint8_t)(t280 == ((uint8_t)18ULL));
                        if (!t281) {
                            goto gm_lb31;
                        }
                        gm_b16_8 t282;
                        GM_ST(void *, (uint8_t *)&t282, (void *)(gm_s1t08grtrn18m));
                        GM_ST(uintptr_t, (uint8_t *)&t282 + 8, (uintptr_t)(8));
                        gm_l36 = t282;
                        goto gm_lb28;
                    }
gm_lb31:;
                    {
                        gm_b16_8 t283;
                        GM_ST(void *, (uint8_t *)&t283, (void *)(gm_scqrlin5l437v));
                        GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(8));
                        gm_l36 = t283;
                        goto gm_lb28;
                    }
gm_lb32:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 77, 25);
                }
gm_lb28:;
                gm_b16_8 t284 = gm_l36;
                gm_l20 = t284;
                void * t285 = gm_l0;
                gm_tl874t2atnlh3 t286 = gm_l4;
                gm_b16_8 t287 = gm_l20;
                gm_tnpifv20irnph t288;
                GM_ST(gm_b16_8, ((uint8_t *)&t288 + 0), t287);
                gm_f_format_l2fmok5((void *)&gm_l37, (void *)NULL, cx, NULL, t288);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t289 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                if (!t289) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src7u9cfdjh7s0n, 83, 28);
                gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&t290));
                gm_b0_1 t292 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t285, t286, t291);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t293 = gm_l0;
                void * t294 = GM_LD(void *, ((uint8_t *)t293));
                gm_tl874t2atnlh3 t295 = gm_l4;
                void * t296 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t294, t295);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t296;
                goto gm_fret;
            }
gm_lb22:;
            {
                {
                    {
                        uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t298 = (uint8_t)(t297 == ((uint8_t)0ULL));
                        if (!t298) {
                            goto gm_lb35;
                        }
                        gm_t9qkh0rsooc8d t299 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l21 = t299;
                        goto gm_lb34;
                    }
gm_lb35:;
                    {
                        uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t301 = (uint8_t)(t300 == ((uint8_t)1ULL));
                        if (!t301) {
                            goto gm_lb36;
                        }
                        gm_t9qkh0rsooc8d t302 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l21 = t302;
                        goto gm_lb34;
                    }
gm_lb36:;
                    {
                        uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t304 = (uint8_t)(t303 == ((uint8_t)2ULL));
                        if (!t304) {
                            goto gm_lb37;
                        }
                        gm_t9qkh0rsooc8d t305 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l21 = t305;
                        goto gm_lb34;
                    }
gm_lb37:;
                    goto gm_lb33;
                }
gm_lb34:;
                uint8_t t306 = gm_l19;
                uint8_t t307 = (uint8_t)!t306;
                if (t307) {
                    void * t308 = gm_l0;
                    gm_b16_8 t309 = gm_l9;
                    gm_b16_8 t310;
                    GM_ST(void *, (uint8_t *)&t310, (void *)(gm_sfmov9pb6t5lt));
                    GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(13));
                    gm_t88rcucg1b7uk t311 = gm_l6;
                    gm_tl874t2atnlh3 t312 = gm_l4;
                    void * t313 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t308, t309, t310, t311, t312);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t313;
                    goto gm_fret;
                }
                gm_t9qkh0rsooc8d t314 = gm_l21;
                uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&t314) + 0));
                uint8_t t316 = (uint8_t)((t315 == ((uint8_t)3ULL)));
                gm_t9qkh0rsooc8d t317;
                GM_ST(uint8_t, ((uint8_t *)&t317 + 0), ((uint8_t)3ULL));
                if (t316) {
                    void * t318 = gm_l0;
                    gm_tl874t2atnlh3 t319 = gm_l4;
                    gm_b16_8 t320 = gm_l9;
                    gm_tnpifv20irnph t321;
                    GM_ST(gm_b16_8, ((uint8_t *)&t321 + 0), t320);
                    gm_f_format_l035o11((void *)&gm_l38, (void *)NULL, cx, NULL, t321);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t322 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                    if (!t322) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_src7u9cfdjh7s0n, 89, 32);
                    gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                    gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                    gm_b0_1 t325 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t318, t319, t324);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t326 = gm_l0;
                    void * t327 = GM_LD(void *, ((uint8_t *)t326));
                    gm_tl874t2atnlh3 t328 = gm_l4;
                    void * t329 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t327, t328);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t329;
                    goto gm_fret;
                }
                {
                    {
                        gm_t9qkh0rsooc8d t330 = gm_l21;
                        uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&t330) + 0));
                        uint8_t t332 = (uint8_t)(!(t331 == ((uint8_t)0ULL)));
                        if (!t332) {
                            goto gm_lb39;
                        }
                        uint8_t t333 = gm_l17;
                        uint8_t t334 = (uint8_t)!t333;
                        if (!t334) {
                            goto gm_lb39;
                        }
                        void * t335 = gm_l0;
                        gm_b16_8 t336 = gm_l9;
                        gm_b16_8 t337;
                        GM_ST(void *, (uint8_t *)&t337, (void *)(gm_spj0hqsg585et));
                        GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(13));
                        gm_t88rcucg1b7uk t338 = gm_l6;
                        gm_tl874t2atnlh3 t339 = gm_l4;
                        gm_t9qkh0rsooc8d t340;
                        GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)0ULL));
                        void * t341 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t335, t336, t337, t338, t339);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t341;
                        goto gm_fret;
                    }
gm_lb39:;
                }
gm_lb38:;
                {
                    {
                        uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t343 = (uint8_t)(t342 == ((uint8_t)0ULL));
                        if (!t343) {
                            goto gm_lb41;
                        }
                        gm_t9qkh0rsooc8d t344 = gm_l21;
                        gm_ts6oqppjh0ba3 t345;
                        GM_ST(uint8_t, ((uint8_t *)&t345 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t345 + 1), t344);
                        gm_l39 = t345;
                        goto gm_lb40;
                    }
gm_lb41:;
                    {
                        uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t347 = (uint8_t)(t346 == ((uint8_t)1ULL));
                        if (!t347) {
                            goto gm_lb42;
                        }
                        gm_t9qkh0rsooc8d t348 = gm_l21;
                        gm_ts6oqppjh0ba3 t349;
                        GM_ST(uint8_t, ((uint8_t *)&t349 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t349 + 1), t348);
                        gm_l39 = t349;
                        goto gm_lb40;
                    }
gm_lb42:;
                    {
                        gm_t9qkh0rsooc8d t350 = gm_l21;
                        gm_ts6oqppjh0ba3 t351;
                        GM_ST(uint8_t, ((uint8_t *)&t351 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t351 + 1), t350);
                        gm_l39 = t351;
                        goto gm_lb40;
                    }
gm_lb43:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 93, 30);
                }
gm_lb40:;
                gm_ts6oqppjh0ba3 t352 = gm_l39;
                gm_l24 = t352;
                gm_t88rcucg1b7uk t353 = gm_l6;
                gm_ts6oqppjh0ba3 t354 = gm_l24;
                void * t355 = gm_l2;
                void * t356 = gm_l14;
                gm_trl9d7m1k5lbd t357;
                GM_ST(uint16_t, (uint8_t *)&t357 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t357 + 4, 0);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t357 + 0), t354);
                GM_ST(void *, ((uint8_t *)&t357 + 8), t355);
                GM_ST(void *, ((uint8_t *)&t357 + 16), t356);
                gm_tl874t2atnlh3 t358 = gm_l4;
                gm_t2b6tm8c0vgo1 t359;
                memset(&t359, 0, sizeof t359);
                GM_ST(uint8_t, ((uint8_t *)&t359 + 0), ((uint8_t)30ULL));
                GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t359 + 8), t357);
                void * t360 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t353, &t359, t358);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t360;
                goto gm_fret;
            }
gm_lb33:;
            {
                uint8_t t361 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t362 = (uint8_t)(t361 == ((uint8_t)4ULL));
                if (!t362) {
                    goto gm_lb44;
                }
                uint8_t t363 = gm_l18;
                if (t363) {
                    gm_t88rcucg1b7uk t364 = gm_l6;
                    void * t365 = gm_l2;
                    void * t366 = gm_l14;
                    gm_ts6oqppjh0ba3 t367;
                    GM_ST(uint8_t, (uint8_t *)&t367 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t367 + 0), ((uint8_t)4ULL));
                    gm_trl9d7m1k5lbd t368;
                    GM_ST(uint16_t, (uint8_t *)&t368 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t368 + 4, 0);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t368 + 0), t367);
                    GM_ST(void *, ((uint8_t *)&t368 + 8), t365);
                    GM_ST(void *, ((uint8_t *)&t368 + 16), t366);
                    gm_tl874t2atnlh3 t369 = gm_l4;
                    gm_t2b6tm8c0vgo1 t370;
                    memset(&t370, 0, sizeof t370);
                    GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t370 + 8), t368);
                    void * t371 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t364, &t370, t369);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t371;
                    goto gm_fret;
                }
                goto gm_lb21;
            }
gm_lb44:;
            {
                {
                    {
                        uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t373 = (uint8_t)(t372 == ((uint8_t)7ULL));
                        if (!t373) {
                            goto gm_lb47;
                        }
                        goto gm_lb46;
                    }
gm_lb47:;
                    {
                        uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t375 = (uint8_t)(t374 == ((uint8_t)8ULL));
                        if (!t375) {
                            goto gm_lb48;
                        }
                        goto gm_lb46;
                    }
gm_lb48:;
                    {
                        uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t377 = (uint8_t)(t376 == ((uint8_t)9ULL));
                        if (!t377) {
                            goto gm_lb49;
                        }
                        goto gm_lb46;
                    }
gm_lb49:;
                    goto gm_lb45;
                }
gm_lb46:;
                uint8_t t378 = gm_l18;
                if (t378) {
                    void * t379 = gm_l0;
                    gm_b16_8 t380 = gm_l9;
                    gm_b16_8 t381;
                    GM_ST(void *, (uint8_t *)&t381, (void *)(gm_slu3lsfv09uua));
                    GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)(24));
                    gm_t88rcucg1b7uk t382 = gm_l6;
                    gm_tl874t2atnlh3 t383 = gm_l4;
                    void * t384 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t379, t380, t381, t382, t383);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t384;
                    goto gm_fret;
                }
                {
                    {
                        uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t386 = (uint8_t)(t385 == ((uint8_t)7ULL));
                        if (!t386) {
                            goto gm_lb51;
                        }
                        gm_ts6oqppjh0ba3 t387;
                        GM_ST(uint8_t, (uint8_t *)&t387 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t387 + 0), ((uint8_t)7ULL));
                        gm_l40 = t387;
                        goto gm_lb50;
                    }
gm_lb51:;
                    {
                        uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t389 = (uint8_t)(t388 == ((uint8_t)8ULL));
                        if (!t389) {
                            goto gm_lb52;
                        }
                        gm_ts6oqppjh0ba3 t390;
                        GM_ST(uint8_t, (uint8_t *)&t390 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t390 + 0), ((uint8_t)8ULL));
                        gm_l40 = t390;
                        goto gm_lb50;
                    }
gm_lb52:;
                    {
                        gm_ts6oqppjh0ba3 t391;
                        GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t391 + 0), ((uint8_t)9ULL));
                        gm_l40 = t391;
                        goto gm_lb50;
                    }
gm_lb53:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 103, 30);
                }
gm_lb50:;
                gm_ts6oqppjh0ba3 t392 = gm_l40;
                gm_l25 = t392;
                gm_t88rcucg1b7uk t393 = gm_l6;
                gm_ts6oqppjh0ba3 t394 = gm_l25;
                void * t395 = gm_l2;
                void * t396 = gm_l14;
                gm_trl9d7m1k5lbd t397;
                GM_ST(uint16_t, (uint8_t *)&t397 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t397 + 4, 0);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t397 + 0), t394);
                GM_ST(void *, ((uint8_t *)&t397 + 8), t395);
                GM_ST(void *, ((uint8_t *)&t397 + 16), t396);
                gm_tl874t2atnlh3 t398 = gm_l4;
                gm_t2b6tm8c0vgo1 t399;
                memset(&t399, 0, sizeof t399);
                GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)30ULL));
                GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t399 + 8), t397);
                void * t400 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t393, &t399, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t400;
                goto gm_fret;
            }
gm_lb45:;
            {
                goto gm_lb21;
            }
gm_lb54:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 75, 5);
        }
gm_lb21:;
    }
    {
        void * t401 = gm_l0;
        gm_tl874t2atnlh3 t402 = gm_l4;
        gm_b16_8 t403 = gm_l9;
        void * t404 = gm_l0;
        gm_t88rcucg1b7uk t405 = gm_l8;
        gm_b16_8 t406 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t404, t405);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t407;
        GM_ST(gm_b16_8, ((uint8_t *)&t407 + 0), t403);
        GM_ST(gm_b16_8, ((uint8_t *)&t407 + 16), t406);
        gm_f_format_qu5usm7((void *)&gm_l41, (void *)NULL, cx, NULL, &t407);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t408 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
        if (!t408) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_src7u9cfdjh7s0n, 113, 20);
        gm_b16_8 t409 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
        gm_b16_8 t410 = GM_LD(gm_b16_8, ((uint8_t *)&t409));
        gm_b0_1 t411 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t401, t402, t410);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t412 = gm_l0;
        void * t413 = GM_LD(void *, ((uint8_t *)t412));
        gm_tl874t2atnlh3 t414 = gm_l4;
        void * t415 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t413, t414);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t415;
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
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_construction_fhm2mm5 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* attrs */
    void * gm_l2; /* inner */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* call */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* st */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_t88rcucg1b7uk gm_l11; /* t */
    void * gm_l12; /* tys */
    gm_tlcm4lvu3g1a8 gm_l13; /* p */
    gm_tlcm4lvu3g1a8 gm_l14; /* p */
    GM_STACK_CHECK(&F, 1144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l15;
    void * gm_l16;
    gm_t8cc1mtbrhlpt gm_l17;
    gm_tjpqvfd72o62u gm_l18;
    gm_tb7u19loo3j87 gm_l19;
    gm_tb7u19loo3j87 gm_l20;
    void * t13 = gm_l2;
    void * t14 = gm_f_construction_call_ktqtrpo(t13);
    gm_l15 = t14;
    uint8_t t15 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l15)) != 0);
    if (t15) {
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l15));
        gm_l16 = t16;
    } else {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, ((uint8_t *)t17));
        gm_tl874t2atnlh3 t19 = gm_l3;
        void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t20;
        goto gm_fret;
    }
    void * t21 = gm_l16;
    gm_l4 = t21;
    {
        {
            void * t22 = gm_l4;
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)19ULL));
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l5 = t25;
            gm_b16_8 t26 = gm_l1;
            gm_l6 = t26;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t27 = gm_l7;
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t29 = (t27 < t28);
                    if (!t29) {
                        goto gm_lb3;
                    }
                    {
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t32 = gm_l7;
                        {
                            {
                                memmove(((uint8_t *)&F.l8), (((uint8_t *)t30) + (uintptr_t)t32 * 72), 72);
                                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                gm_b16_8 t34;
                                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sqes10usq4q5e));
                                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(3));
                                uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                                if (t35) {
                                    void * t36 = gm_l0;
                                    void * t37 = GM_LD(void *, ((uint8_t *)t36));
                                    void * t38 = gm_l0;
                                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 8));
                                    void * t40 = gm_l2;
                                    void * t41 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t40);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tl874t2atnlh3 t42 = gm_l3;
                                    gm_t88rcucg1b7uk t43 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t37, t39, t41, t42);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l9 = t43;
                                    void * t44 = gm_l0;
                                    void * t45 = GM_LD(void *, ((uint8_t *)t44));
                                    gm_t88rcucg1b7uk t46 = gm_l9;
                                    gm_f_soa_info_pq4q43l((void *)&gm_l17, t45, GM_CONV(gm_csguankko4lmhd, t46));
                                    uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l17))[32] != 0);
                                    if (t47) {
                                        memmove(((uint8_t *)&gm_l18), ((uint8_t *)&gm_l17), 32);
                                    } else {
                                        void * t48 = gm_l0;
                                        void * t49 = GM_LD(void *, ((uint8_t *)t48));
                                        gm_tl874t2atnlh3 t50 = gm_l3;
                                        void * t51 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t49, t50);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t51;
                                        goto gm_fret;
                                    }
                                    memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l18), 32);
                                    void * t52 = gm_l0;
                                    gm_t88rcucg1b7uk t53 = gm_l9;
                                    gm_b16_8 t54 = gm_l5;
                                    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                                    gm_tl874t2atnlh3 t56 = gm_l3;
                                    void * t57 = gm_f_soa_construct_8nun9tl((void *)NULL, cx, NULL, t52, t53, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t55, t56);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t57;
                                    goto gm_fret;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 12, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t58 = gm_l7;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l7 = t61;
                }
            }
gm_lb3:;
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, ((uint8_t *)t62));
            void * t64 = gm_l0;
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t64) + 8));
            gm_b16_8 t66 = gm_l1;
            void * t67 = gm_l2;
            void * t68 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t69 = gm_l3;
            gm_t88rcucg1b7uk t70 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t63, t65, t66, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t70;
            void * t71 = gm_l0;
            void * t72 = GM_LD(void *, ((uint8_t *)t71));
            void * t73 = (void *)(((uint8_t *)t72) + 8);
            gm_l12 = t73;
            {
                {
                    void * t74 = gm_l12;
                    gm_t88rcucg1b7uk t75 = gm_l11;
                    gm_f_vector_parts_crrfopm((void *)&gm_l19, t74, GM_CONV(gm_csguankko4lmhd, t75));
                    uint8_t t76 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                    if (!t76) {
                        goto gm_lb9;
                    }
                    gm_tlcm4lvu3g1a8 t77 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l19));
                    gm_l13 = t77;
                    void * t78 = gm_l0;
                    gm_t88rcucg1b7uk t79 = gm_l11;
                    gm_tkn42u7na3t81 t80;
                    GM_ST(uint8_t, (uint8_t *)&t80 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t80 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t80), t79);
                    ((uint8_t *)&t80)[4] = 1;
                    uint64_t t81 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                    gm_tf5srh4mp4b3i t82;
                    GM_ST(uint8_t, (uint8_t *)&t82 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t82 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t82 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t82), t81);
                    ((uint8_t *)&t82)[8] = 1;
                    gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
                    gm_b16_8 t84 = gm_l5;
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                    gm_tl874t2atnlh3 t86 = gm_l3;
                    void * t87 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t78, t80, t82, t83, t85, t86);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t87;
                    goto gm_fret;
                }
gm_lb9:;
            }
gm_lb8:;
            {
                {
                    void * t88 = gm_l12;
                    gm_t88rcucg1b7uk t89 = gm_l11;
                    gm_f_math_vector_parts_49c7865((void *)&gm_l20, t88, GM_CONV(gm_csguankko4lmhd, t89));
                    uint8_t t90 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
                    if (!t90) {
                        goto gm_lb11;
                    }
                    gm_tlcm4lvu3g1a8 t91 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l20));
                    gm_l14 = t91;
                    void * t92 = gm_l0;
                    gm_t88rcucg1b7uk t93 = gm_l11;
                    gm_tkn42u7na3t81 t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94), t93);
                    ((uint8_t *)&t94)[4] = 1;
                    uint64_t t95 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                    gm_tf5srh4mp4b3i t96;
                    GM_ST(uint8_t, (uint8_t *)&t96 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t96 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t96 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t96), t95);
                    ((uint8_t *)&t96)[8] = 1;
                    gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t98 = gm_l5;
                    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                    gm_tl874t2atnlh3 t100 = gm_l3;
                    void * t101 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t92, t94, t96, t97, t99, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t101;
                    goto gm_fret;
                }
gm_lb11:;
            }
gm_lb10:;
            void * t102 = gm_l0;
            void * t103 = GM_LD(void *, ((uint8_t *)t102));
            gm_tl874t2atnlh3 t104 = gm_l3;
            void * t105 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t105;
            goto gm_fret;
        }
gm_lb2:;
        void * t106 = gm_l0;
        void * t107 = GM_LD(void *, ((uint8_t *)t106));
        gm_tl874t2atnlh3 t108 = gm_l3;
        void * t109 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t107, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t109;
        goto gm_fret;
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
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_conversion_32a9cuf F;
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
    gm_tn3r4rvlh0rsv gm_l12; /* <temporary array> */
    gm_b16_8 gm_l13; /* why */
    GM_STACK_CHECK(&F, 1555u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l14;
    gm_t53at80pfdus3 gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_b16_8 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_l4 = t16;
    void * t17 = gm_l0;
    void * t18 = (void *)(((uint8_t *)t17) + 96);
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = gm_l2;
    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t21));
    gm_t88rcucg1b7uk t23 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t23;
    {
        {
            void * t24 = gm_l4;
            gm_t88rcucg1b7uk t25 = gm_l1;
            uint8_t t26 = gm_f_is_vector_gkq6b88(t24, GM_CONV(gm_csguankko4lmhd, t25));
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) {
                goto gm_lb2;
            }
            void * t28 = gm_l4;
            gm_t88rcucg1b7uk t29 = gm_l5;
            uint8_t t30 = gm_f_is_vector_gkq6b88(t28, GM_CONV(gm_csguankko4lmhd, t29));
            uint8_t t31 = (uint8_t)!t30;
            if (!t31) {
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
            void * t32 = gm_l4;
            gm_t88rcucg1b7uk t33 = gm_l5;
            gm_f_kind_jcnt4hm((void *)&gm_l14, t32, GM_CONV(gm_csguankko4lmhd, t33));
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)11ULL));
            if (!t35) {
                goto gm_lb4;
            }
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
            gm_l6 = t36;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l14) + 8) + 8));
            gm_l7 = t37;
            {
                {
                    void * t38 = gm_l4;
                    gm_t88rcucg1b7uk t39 = gm_l1;
                    gm_f_kind_jcnt4hm((void *)&gm_l15, t38, GM_CONV(gm_csguankko4lmhd, t39));
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)11ULL));
                    if (!t41) {
                        goto gm_lb6;
                    }
                    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    gm_l8 = t42;
                    gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 8));
                    gm_l9 = t43;
                    void * t44 = gm_l4;
                    gm_t88rcucg1b7uk t45 = gm_l5;
                    uint8_t t46 = gm_f_is_vector_gkq6b88(t44, GM_CONV(gm_csguankko4lmhd, t45));
                    if (t46) {
                        void * t47 = gm_l4;
                        gm_t88rcucg1b7uk t48 = gm_l1;
                        uint8_t t49 = gm_f_is_vector_gkq6b88(t47, GM_CONV(gm_csguankko4lmhd, t48));
                        gm_l16 = t49;
                    } else {
                        gm_l16 = ((uint8_t)0);
                    }
                    uint8_t t50 = gm_l16;
                    gm_l10 = t50;
                    {
                        {
                            uint64_t t51 = gm_l6;
                            uint64_t t52 = gm_l8;
                            uint8_t t53 = ((t51 == t52));
                            if (!t53) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t54 = gm_l7;
                            gm_t88rcucg1b7uk t55 = gm_l9;
                            uint8_t t56 = (gm_eq9m6mvtmgde(((uint8_t *)&t54), ((uint8_t *)&t55)));
                            if (!t56) {
                                goto gm_lb8;
                            }
                            uint8_t t57 = gm_l10;
                            if (t57) {
                                gm_l18 = ((uint8_t)1);
                            } else {
                                void * t58 = gm_l4;
                                gm_t88rcucg1b7uk t59 = gm_l5;
                                uint8_t t60 = gm_f_is_positional_array_6sufsk8(t58, GM_CONV(gm_csguankko4lmhd, t59));
                                gm_l18 = t60;
                            }
                            uint8_t t61 = gm_l18;
                            if (t61) {
                                gm_l17 = ((uint8_t)1);
                            } else {
                                void * t62 = gm_l4;
                                gm_t88rcucg1b7uk t63 = gm_l1;
                                uint8_t t64 = gm_f_is_positional_array_6sufsk8(t62, GM_CONV(gm_csguankko4lmhd, t63));
                                gm_l17 = t64;
                            }
                            uint8_t t65 = gm_l17;
                            if (!t65) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t66 = gm_l1;
                            void * t67 = gm_l2;
                            gm_tn3r4rvlh0rsv t68;
                            GM_ST(void *, ((uint8_t *)&t68 + 0), t67);
                            gm_tvdg94bk7aa22 t69;
                            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)7ULL));
                            gm_l11 = t68;
                            void * t70 = (void *)((uint8_t *)&gm_l11);
                            uint64_t t71 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t72 = (uintptr_t)((uint64_t)1ULL);
                            if (t71 > t72 || t72 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t71, t72, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 325, 39);
                            uint64_t t73 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t74;
                            GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t70 + (uintptr_t)((uint64_t)0ULL) * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(t73));
                            gm_tl874t2atnlh3 t75 = gm_l3;
                            void * t76 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t69, t66, t74, t75);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t76;
                            goto gm_fret;
                        }
gm_lb8:;
                    }
gm_lb7:;
                    {
                        {
                            uint8_t t77 = gm_l10;
                            if (!t77) {
                                goto gm_lb10;
                            }
                            uint64_t t78 = gm_l6;
                            uint64_t t79 = gm_l8;
                            uint8_t t80 = ((t78 == t79));
                            if (!t80) {
                                goto gm_lb10;
                            }
                            void * t81 = gm_l0;
                            void * t82 = GM_LD(void *, ((uint8_t *)t81));
                            gm_t88rcucg1b7uk t83 = gm_l7;
                            gm_t88rcucg1b7uk t84 = gm_l9;
                            uint8_t t85 = gm_f_lossless_a54o69s(t82, GM_CONV(gm_csguankko4lmhd, t83), GM_CONV(gm_csguankko4lmhd, t84));
                            if (!t85) {
                                goto gm_lb10;
                            }
                            gm_t88rcucg1b7uk t86 = gm_l1;
                            void * t87 = gm_l2;
                            gm_tn3r4rvlh0rsv t88;
                            GM_ST(void *, ((uint8_t *)&t88 + 0), t87);
                            gm_tvdg94bk7aa22 t89;
                            GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t89 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)56ULL));
                            gm_l12 = t88;
                            void * t90 = (void *)((uint8_t *)&gm_l12);
                            uint64_t t91 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t92 = (uintptr_t)((uint64_t)1ULL);
                            if (t91 > t92 || t92 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t91, t92, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 327, 88);
                            uint64_t t93 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t94;
                            GM_ST(void *, (uint8_t *)&t94, (void *)(((uint8_t *)t90 + (uintptr_t)((uint64_t)0ULL) * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(t93));
                            gm_tl874t2atnlh3 t95 = gm_l3;
                            void * t96 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t89, t86, t94, t95);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t96;
                            goto gm_fret;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    {
                        {
                            uint8_t t97 = gm_l10;
                            if (!t97) {
                                goto gm_lb12;
                            }
                            uint64_t t98 = gm_l6;
                            uint64_t t99 = gm_l8;
                            uint8_t t100 = ((t98 == t99));
                            if (!t100) {
                                goto gm_lb12;
                            }
                            void * t101 = gm_l0;
                            gm_t88rcucg1b7uk t102 = gm_l7;
                            gm_b16_8 t103 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t101, t102);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t104 = gm_l0;
                            gm_t88rcucg1b7uk t105 = gm_l9;
                            gm_b16_8 t106 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t104, t105);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t107;
                            GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t103);
                            GM_ST(gm_b16_8, ((uint8_t *)&t107 + 16), t106);
                            gm_f_format_mrlevsk((void *)&gm_l20, (void *)NULL, cx, NULL, &t107);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src7u9cfdjh7s0n, 328, 40);
                            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                            gm_l19 = t110;
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_b16_8 t111;
                        GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(0));
                        gm_l19 = t111;
                    }
gm_lb11:;
                    gm_b16_8 t112 = gm_l19;
                    gm_l13 = t112;
                    void * t113 = gm_l0;
                    gm_tl874t2atnlh3 t114 = gm_l3;
                    void * t115 = gm_l0;
                    gm_t88rcucg1b7uk t116 = gm_l5;
                    gm_b16_8 t117 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t115, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t118 = gm_l0;
                    gm_t88rcucg1b7uk t119 = gm_l1;
                    gm_b16_8 t120 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t118, t119);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t121 = gm_l13;
                    gm_tfeab279c16rr t122;
                    GM_ST(gm_b16_8, ((uint8_t *)&t122 + 0), t117);
                    GM_ST(gm_b16_8, ((uint8_t *)&t122 + 16), t120);
                    GM_ST(gm_b16_8, ((uint8_t *)&t122 + 32), t121);
                    gm_f_format_d0lfl1k((void *)&gm_l21, (void *)NULL, cx, NULL, &t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t123 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                    if (!t123) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src7u9cfdjh7s0n, 329, 20);
                    gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                    gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                    gm_b0_1 t126 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t113, t114, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t127 = gm_l0;
                    void * t128 = GM_LD(void *, ((uint8_t *)t127));
                    gm_tl874t2atnlh3 t129 = gm_l3;
                    void * t130 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t128, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t130;
                    goto gm_fret;
                }
gm_lb6:;
                void * t131 = gm_l0;
                gm_tl874t2atnlh3 t132 = gm_l3;
                void * t133 = gm_l0;
                gm_t88rcucg1b7uk t134 = gm_l5;
                gm_b16_8 t135 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t133, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t136 = gm_l0;
                gm_t88rcucg1b7uk t137 = gm_l1;
                gm_b16_8 t138 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t136, t137);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t139;
                GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t135);
                GM_ST(gm_b16_8, ((uint8_t *)&t139 + 16), t138);
                gm_f_format_9rhpk5t((void *)&gm_l22, (void *)NULL, cx, NULL, &t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src7u9cfdjh7s0n, 319, 24);
                gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                gm_b0_1 t143 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t131, t132, t142);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t144 = gm_l0;
                void * t145 = GM_LD(void *, ((uint8_t *)t144));
                gm_tl874t2atnlh3 t146 = gm_l3;
                void * t147 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t145, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t147;
                goto gm_fret;
            }
gm_lb5:;
        }
gm_lb4:;
        void * t148 = gm_l0;
        gm_tl874t2atnlh3 t149 = gm_l3;
        void * t150 = gm_l0;
        gm_t88rcucg1b7uk t151 = gm_l5;
        gm_b16_8 t152 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t150, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t153 = gm_l0;
        gm_t88rcucg1b7uk t154 = gm_l1;
        gm_b16_8 t155 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t153, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t156;
        GM_ST(gm_b16_8, ((uint8_t *)&t156 + 0), t152);
        GM_ST(gm_b16_8, ((uint8_t *)&t156 + 16), t155);
        gm_f_format_9rhpk5t((void *)&gm_l23, (void *)NULL, cx, NULL, &t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t157 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t157) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src7u9cfdjh7s0n, 315, 24);
        gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
        gm_b0_1 t160 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t148, t149, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t161 = gm_l0;
        void * t162 = GM_LD(void *, ((uint8_t *)t161));
        gm_tl874t2atnlh3 t163 = gm_l3;
        void * t164 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t162, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t164;
        goto gm_fret;
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
inline __attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_load_ssdvomk F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tlcm4lvu3g1a8 gm_l5; /* parts */
    void * gm_l6; /* s */
    gm_tn3r4rvlh0rsv gm_l7; /* <temporary array> */
    GM_STACK_CHECK(&F, 491u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tb7u19loo3j87 gm_l8;
    gm_tlcm4lvu3g1a8 gm_l9;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_l4 = t4;
    void * t5 = gm_l4;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_vector_parts_crrfopm((void *)&gm_l8, t5, GM_CONV(gm_csguankko4lmhd, t6));
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t7) {
        gm_tlcm4lvu3g1a8 t8 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l8));
        gm_l9 = t8;
    } else {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, ((uint8_t *)t9));
        gm_tl874t2atnlh3 t11 = gm_l3;
        void * t12 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t12;
        goto gm_fret;
    }
    gm_tlcm4lvu3g1a8 t13 = gm_l9;
    gm_l5 = t13;
    void * t14 = gm_l0;
    gm_b16_8 t15 = gm_l2;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sqfrv3g5v9m2n));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
    gm_tl874t2atnlh3 t17 = gm_l3;
    uint8_t t18 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t14, t15, ((uint64_t)1ULL), t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t19 = (uint8_t)!t18;
    if (t19) {
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, ((uint8_t *)t20));
        gm_tl874t2atnlh3 t22 = gm_l3;
        void * t23 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t23;
        goto gm_fret;
    }
    void * t24 = gm_l0;
    gm_b16_8 t25 = gm_l2;
    void * t26 = gm_l4;
    gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_t88rcucg1b7uk t28 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t26, ((uint8_t)1), t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t29 = gm_l3;
    void * t30 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t24, t25, ((uint64_t)0ULL), t28, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t30;
    gm_t88rcucg1b7uk t31 = gm_l1;
    void * t32 = gm_l6;
    gm_tn3r4rvlh0rsv t33;
    GM_ST(void *, ((uint8_t *)&t33 + 0), t32);
    gm_tvdg94bk7aa22 t34;
    GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t34 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)58ULL));
    gm_l7 = t33;
    void * t35 = (void *)((uint8_t *)&gm_l7);
    uint64_t t36 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t37 = (uintptr_t)((uint64_t)1ULL);
    if (t36 > t37 || t37 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t36, t37, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 304, 34);
    uint64_t t38 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t35 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
    gm_tl874t2atnlh3 t40 = gm_l3;
    void * t41 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t34, t31, t39, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t41;
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
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_vector_method_hgn4sb1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* lane */
    uint8_t gm_l10; /* numeric */
    gm_tn3r4rvlh0rsv gm_l11; /* one */
    gm_tvdg94bk7aa22 gm_l12; /* op */
    void * gm_l13; /* y */
    gm_tvdg94bk7aa22 gm_l14; /* op */
    gm_t5fru0lp11n7t gm_l15; /* <temporary array> */
    void * gm_l17; /* x */
    gm_t88rcucg1b7uk gm_l18; /* xt */
    uint8_t gm_l19; /* same_lanes */
    gm_tlcm4lvu3g1a8 gm_l20; /* xp */
    void * gm_l21; /* y */
    gm_thmgeksj5pdth gm_l22; /* <temporary array> */
    void * gm_l23; /* y */
    gm_t5fru0lp11n7t gm_l24; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l25; /* f */
    gm_b16_8 gm_l26; /* msg */
    void * gm_l27; /* dst */
    gm_t5fru0lp11n7t gm_l28; /* <temporary array> */
    GM_STACK_CHECK(&F, 5411u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tb7u19loo3j87 gm_l29;
    gm_tlcm4lvu3g1a8 gm_l30;
    uint8_t gm_l31;
    gm_tvdg94bk7aa22 gm_l32;
    gm_tvdg94bk7aa22 gm_l33;
    gm_tb7u19loo3j87 gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tvdg94bk7aa22 gm_l36;
    uint8_t gm_l37;
    gm_tkn42u7na3t81 gm_l38;
    gm_t88rcucg1b7uk gm_l39;
    uint8_t gm_l40;
    uint8_t gm_l41;
    gm_tabb710d9mq2e gm_l42;
    gm_th2lqbo77pvsc gm_l43;
    gm_tabb710d9mq2e gm_l44;
    {
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, ((uint8_t *)t50));
        void * t52 = (void *)(((uint8_t *)t51) + 8);
        gm_l6 = t52;
        void * t53 = gm_l6;
        gm_t88rcucg1b7uk t54 = gm_l2;
        gm_f_vector_parts_crrfopm((void *)&gm_l29, t53, GM_CONV(gm_csguankko4lmhd, t54));
        uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l29))[16] != 0);
        if (t55) {
            gm_tlcm4lvu3g1a8 t56 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l29));
            gm_l30 = t56;
        } else {
            void * t57 = gm_l0;
            void * t58 = GM_LD(void *, ((uint8_t *)t57));
            gm_tl874t2atnlh3 t59 = gm_l5;
            void * t60 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t60;
            goto gm_fret;
        }
        gm_tlcm4lvu3g1a8 t61 = gm_l30;
        gm_l7 = t61;
        uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l8 = t62;
        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l9 = t63;
        void * t64 = gm_l6;
        gm_t88rcucg1b7uk t65 = gm_l9;
        uint8_t t66 = gm_f_is_integer_8d4f9ek(t64, GM_CONV(gm_csguankko4lmhd, t65));
        if (t66) {
            gm_l31 = ((uint8_t)1);
        } else {
            void * t67 = gm_l6;
            gm_t88rcucg1b7uk t68 = gm_l9;
            uint8_t t69 = gm_f_is_float_3jk89qu(t67, GM_CONV(gm_csguankko4lmhd, t68));
            gm_l31 = t69;
        }
        uint8_t t70 = gm_l31;
        gm_l10 = t70;
        void * t71 = gm_l1;
        gm_tn3r4rvlh0rsv t72;
        GM_ST(void *, ((uint8_t *)&t72 + 0), t71);
        gm_l11 = t72;
        {
            {
                {
                    {
                        {
                            gm_b16_8 t73 = gm_l3;
                            gm_b16_8 t74;
                            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sut92jpq3kk88));
                            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(10));
                            uint8_t t75 = (gm_eq0iqiubfm7e(((uint8_t *)&t73), ((uint8_t *)&t74)));
                            if (!t75) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            gm_b16_8 t76 = gm_l3;
                            gm_b16_8 t77;
                            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sbqoiitqtfv75));
                            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(10));
                            uint8_t t78 = (gm_eq0iqiubfm7e(((uint8_t *)&t76), ((uint8_t *)&t77)));
                            if (!t78) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            gm_b16_8 t79 = gm_l3;
                            gm_b16_8 t80;
                            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sf1qp7r4ktsd1));
                            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(10));
                            uint8_t t81 = (gm_eq0iqiubfm7e(((uint8_t *)&t79), ((uint8_t *)&t80)));
                            if (!t81) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t82 = gm_l0;
                    gm_b16_8 t83 = gm_l4;
                    gm_b16_8 t84 = gm_l3;
                    gm_tl874t2atnlh3 t85 = gm_l5;
                    uint8_t t86 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t82, t83, ((uint64_t)0ULL), t84, t85);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t87 = (uint8_t)!t86;
                    if (t87) {
                        void * t88 = gm_l0;
                        void * t89 = GM_LD(void *, ((uint8_t *)t88));
                        gm_tl874t2atnlh3 t90 = gm_l5;
                        void * t91 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t89, t90);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t91;
                        goto gm_fret;
                    }
                    uint8_t t92 = gm_l10;
                    uint8_t t93 = (uint8_t)!t92;
                    if (t93) {
                        void * t94 = gm_l0;
                        gm_b16_8 t95 = gm_l3;
                        gm_t88rcucg1b7uk t96 = gm_l2;
                        gm_b16_8 t97;
                        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sfmov9pb6t5lt));
                        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(13));
                        gm_tl874t2atnlh3 t98 = gm_l5;
                        void * t99 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t94, t95, t96, t97, t98);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t99;
                        goto gm_fret;
                    }
                    {
                        {
                            gm_b16_8 t100 = gm_l3;
                            gm_b16_8 t101;
                            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sut92jpq3kk88));
                            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(10));
                            uint8_t t102 = (gm_eq0iqiubfm7e(((uint8_t *)&t100), ((uint8_t *)&t101)));
                            if (!t102) {
                                goto gm_lb8;
                            }
                            gm_tvdg94bk7aa22 t103;
                            GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t103 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)46ULL));
                            gm_l32 = t103;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            gm_b16_8 t104 = gm_l3;
                            gm_b16_8 t105;
                            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sbqoiitqtfv75));
                            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(10));
                            uint8_t t106 = (gm_eq0iqiubfm7e(((uint8_t *)&t104), ((uint8_t *)&t105)));
                            if (!t106) {
                                goto gm_lb9;
                            }
                            gm_tvdg94bk7aa22 t107;
                            GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t107 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)47ULL));
                            gm_l32 = t107;
                            goto gm_lb7;
                        }
gm_lb9:;
                        {
                            gm_tvdg94bk7aa22 t108;
                            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t108 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)48ULL));
                            gm_l32 = t108;
                            goto gm_lb7;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 161, 31);
                    }
gm_lb7:;
                    gm_tvdg94bk7aa22 t109 = gm_l32;
                    gm_l12 = t109;
                    gm_tvdg94bk7aa22 t110 = gm_l12;
                    gm_t88rcucg1b7uk t111 = gm_l9;
                    void * t112 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t113 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t114 = (uintptr_t)((uint64_t)1ULL);
                    if (t113 > t114 || t114 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t113, t114, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 166, 38);
                    uint64_t t115 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t116;
                    GM_ST(void *, (uint8_t *)&t116, (void *)(((uint8_t *)t112 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(t115));
                    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                    gm_tl874t2atnlh3 t118 = gm_l5;
                    void * t119 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t110, t111, t117, t118);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t119;
                    goto gm_fret;
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t120 = gm_l3;
                            gm_b16_8 t121;
                            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sj7s3tnnvob4e));
                            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(8));
                            uint8_t t122 = (gm_eq0iqiubfm7e(((uint8_t *)&t120), ((uint8_t *)&t121)));
                            if (!t122) {
                                goto gm_lb13;
                            }
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            gm_b16_8 t123 = gm_l3;
                            gm_b16_8 t124;
                            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_s6lddausbiitu));
                            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(8));
                            uint8_t t125 = (gm_eq0iqiubfm7e(((uint8_t *)&t123), ((uint8_t *)&t124)));
                            if (!t125) {
                                goto gm_lb14;
                            }
                            goto gm_lb12;
                        }
gm_lb14:;
                        {
                            gm_b16_8 t126 = gm_l3;
                            gm_b16_8 t127;
                            GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sh1to1i0u67lu));
                            GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(8));
                            uint8_t t128 = (gm_eq0iqiubfm7e(((uint8_t *)&t126), ((uint8_t *)&t127)));
                            if (!t128) {
                                goto gm_lb15;
                            }
                            goto gm_lb12;
                        }
gm_lb15:;
                        {
                            gm_b16_8 t129 = gm_l3;
                            gm_b16_8 t130;
                            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sv8pauo1bhltl));
                            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(8));
                            uint8_t t131 = (gm_eq0iqiubfm7e(((uint8_t *)&t129), ((uint8_t *)&t130)));
                            if (!t131) {
                                goto gm_lb16;
                            }
                            goto gm_lb12;
                        }
gm_lb16:;
                        {
                            gm_b16_8 t132 = gm_l3;
                            gm_b16_8 t133;
                            GM_ST(void *, (uint8_t *)&t133, (void *)(gm_s1t08grtrn18m));
                            GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(8));
                            uint8_t t134 = (gm_eq0iqiubfm7e(((uint8_t *)&t132), ((uint8_t *)&t133)));
                            if (!t134) {
                                goto gm_lb17;
                            }
                            goto gm_lb12;
                        }
gm_lb17:;
                        {
                            gm_b16_8 t135 = gm_l3;
                            gm_b16_8 t136;
                            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_scqrlin5l437v));
                            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(8));
                            uint8_t t137 = (gm_eq0iqiubfm7e(((uint8_t *)&t135), ((uint8_t *)&t136)));
                            if (!t137) {
                                goto gm_lb18;
                            }
                            goto gm_lb12;
                        }
gm_lb18:;
                        goto gm_lb11;
                    }
gm_lb12:;
                    void * t138 = gm_l0;
                    gm_b16_8 t139 = gm_l4;
                    gm_b16_8 t140 = gm_l3;
                    gm_tl874t2atnlh3 t141 = gm_l5;
                    uint8_t t142 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t138, t139, ((uint64_t)1ULL), t140, t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t143 = (uint8_t)!t142;
                    if (t143) {
                        void * t144 = gm_l0;
                        void * t145 = GM_LD(void *, ((uint8_t *)t144));
                        gm_tl874t2atnlh3 t146 = gm_l5;
                        void * t147 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t145, t146);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t147;
                        goto gm_fret;
                    }
                    {
                        {
                            gm_b16_8 t148 = gm_l3;
                            gm_b16_8 t149;
                            GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sj7s3tnnvob4e));
                            GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(8));
                            gm_b16_8 t150;
                            GM_ST(void *, (uint8_t *)&t150, (void *)(gm_s6lddausbiitu));
                            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(8));
                            gm_t6j6smj7qoctu t151;
                            GM_ST(gm_b16_8, ((uint8_t *)&t151 + 0), t149);
                            GM_ST(gm_b16_8, ((uint8_t *)&t151 + 16), t150);
                            uint8_t t152 = 0;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t151) + 0, ((uint8_t *)&t148))) t152 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t151) + 16, ((uint8_t *)&t148))) t152 = 1;
                            uint8_t t153 = (uint8_t)(!t152);
                            if (!t153) {
                                goto gm_lb20;
                            }
                            uint8_t t154 = gm_l10;
                            uint8_t t155 = (uint8_t)!t154;
                            if (!t155) {
                                goto gm_lb20;
                            }
                            void * t156 = gm_l0;
                            gm_b16_8 t157 = gm_l3;
                            gm_t88rcucg1b7uk t158 = gm_l2;
                            gm_b16_8 t159;
                            GM_ST(void *, (uint8_t *)&t159, (void *)(gm_sfmov9pb6t5lt));
                            GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(13));
                            gm_tl874t2atnlh3 t160 = gm_l5;
                            void * t161 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t156, t157, t158, t159, t160);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t161;
                            goto gm_fret;
                        }
gm_lb20:;
                    }
gm_lb19:;
                    void * t162 = gm_l0;
                    gm_b16_8 t163 = gm_l4;
                    gm_t88rcucg1b7uk t164 = gm_l2;
                    gm_tl874t2atnlh3 t165 = gm_l5;
                    void * t166 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t162, t163, ((uint64_t)0ULL), t164, t165);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t166;
                    {
                        {
                            gm_b16_8 t167 = gm_l3;
                            gm_b16_8 t168;
                            GM_ST(void *, (uint8_t *)&t168, (void *)(gm_sj7s3tnnvob4e));
                            GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(8));
                            uint8_t t169 = (gm_eq0iqiubfm7e(((uint8_t *)&t167), ((uint8_t *)&t168)));
                            if (!t169) {
                                goto gm_lb22;
                            }
                            gm_tvdg94bk7aa22 t170;
                            GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t170 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)40ULL));
                            gm_l33 = t170;
                            goto gm_lb21;
                        }
gm_lb22:;
                        {
                            gm_b16_8 t171 = gm_l3;
                            gm_b16_8 t172;
                            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_s6lddausbiitu));
                            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(8));
                            uint8_t t173 = (gm_eq0iqiubfm7e(((uint8_t *)&t171), ((uint8_t *)&t172)));
                            if (!t173) {
                                goto gm_lb23;
                            }
                            gm_tvdg94bk7aa22 t174;
                            GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t174 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)41ULL));
                            gm_l33 = t174;
                            goto gm_lb21;
                        }
gm_lb23:;
                        {
                            gm_b16_8 t175 = gm_l3;
                            gm_b16_8 t176;
                            GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sh1to1i0u67lu));
                            GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(8));
                            uint8_t t177 = (gm_eq0iqiubfm7e(((uint8_t *)&t175), ((uint8_t *)&t176)));
                            if (!t177) {
                                goto gm_lb24;
                            }
                            gm_tvdg94bk7aa22 t178;
                            GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t178 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t178 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)42ULL));
                            gm_l33 = t178;
                            goto gm_lb21;
                        }
gm_lb24:;
                        {
                            gm_b16_8 t179 = gm_l3;
                            gm_b16_8 t180;
                            GM_ST(void *, (uint8_t *)&t180, (void *)(gm_sv8pauo1bhltl));
                            GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(8));
                            uint8_t t181 = (gm_eq0iqiubfm7e(((uint8_t *)&t179), ((uint8_t *)&t180)));
                            if (!t181) {
                                goto gm_lb25;
                            }
                            gm_tvdg94bk7aa22 t182;
                            GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t182 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)43ULL));
                            gm_l33 = t182;
                            goto gm_lb21;
                        }
gm_lb25:;
                        {
                            gm_b16_8 t183 = gm_l3;
                            gm_b16_8 t184;
                            GM_ST(void *, (uint8_t *)&t184, (void *)(gm_s1t08grtrn18m));
                            GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(8));
                            uint8_t t185 = (gm_eq0iqiubfm7e(((uint8_t *)&t183), ((uint8_t *)&t184)));
                            if (!t185) {
                                goto gm_lb26;
                            }
                            gm_tvdg94bk7aa22 t186;
                            GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t186 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)44ULL));
                            gm_l33 = t186;
                            goto gm_lb21;
                        }
gm_lb26:;
                        {
                            gm_tvdg94bk7aa22 t187;
                            GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t187 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)45ULL));
                            gm_l33 = t187;
                            goto gm_lb21;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 172, 31);
                    }
gm_lb21:;
                    gm_tvdg94bk7aa22 t188 = gm_l33;
                    gm_l14 = t188;
                    gm_tvdg94bk7aa22 t189 = gm_l14;
                    void * t190 = gm_l6;
                    uint64_t t191 = gm_l8;
                    gm_t88rcucg1b7uk t192 = gm_f_mask_ty_cj7ln0g((void *)NULL, cx, NULL, t190, t191);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t193 = gm_l1;
                    void * t194 = gm_l13;
                    gm_t5fru0lp11n7t t195;
                    GM_ST(void *, ((uint8_t *)&t195 + 0), t193);
                    GM_ST(void *, ((uint8_t *)&t195 + 8), t194);
                    gm_l15 = t195;
                    void * t196 = (void *)((uint8_t *)&gm_l15);
                    uint64_t t197 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t198 = (uintptr_t)((uint64_t)2ULL);
                    if (t197 > t198 || t198 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t197, t198, ((uint64_t)2ULL), gm_src7u9cfdjh7s0n, 180, 51);
                    uint64_t t199 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t200;
                    GM_ST(void *, (uint8_t *)&t200, (void *)(((uint8_t *)t196 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(t199));
                    gm_tl874t2atnlh3 t201 = gm_l5;
                    void * t202 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t189, t192, t200, t201);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t202;
                    goto gm_fret;
                }
gm_lb11:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t203 = gm_l3;
                            gm_b16_8 t204;
                            GM_ST(void *, (uint8_t *)&t204, (void *)(gm_s0nf1rg04f3m5));
                            GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(3));
                            uint8_t t205 = (gm_eq0iqiubfm7e(((uint8_t *)&t203), ((uint8_t *)&t204)));
                            if (!t205) {
                                goto gm_lb30;
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                        {
                            gm_b16_8 t206 = gm_l3;
                            gm_b16_8 t207;
                            GM_ST(void *, (uint8_t *)&t207, (void *)(gm_skftljk77e6mu));
                            GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(3));
                            uint8_t t208 = (gm_eq0iqiubfm7e(((uint8_t *)&t206), ((uint8_t *)&t207)));
                            if (!t208) {
                                goto gm_lb31;
                            }
                            goto gm_lb29;
                        }
gm_lb31:;
                        {
                            gm_b16_8 t209 = gm_l3;
                            gm_b16_8 t210;
                            GM_ST(void *, (uint8_t *)&t210, (void *)(gm_snc5ns3akncuq));
                            GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(5));
                            uint8_t t211 = (gm_eq0iqiubfm7e(((uint8_t *)&t209), ((uint8_t *)&t210)));
                            if (!t211) {
                                goto gm_lb32;
                            }
                            goto gm_lb29;
                        }
gm_lb32:;
                        goto gm_lb28;
                    }
gm_lb29:;
                    void * t212 = gm_l0;
                    gm_b16_8 t213 = gm_l4;
                    gm_b16_8 t214 = gm_l3;
                    gm_tl874t2atnlh3 t215 = gm_l5;
                    uint8_t t216 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t212, t213, ((uint64_t)0ULL), t214, t215);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t217 = (uint8_t)!t216;
                    if (t217) {
                        void * t218 = gm_l0;
                        void * t219 = GM_LD(void *, ((uint8_t *)t218));
                        gm_tl874t2atnlh3 t220 = gm_l5;
                        void * t221 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t219, t220);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t221;
                        goto gm_fret;
                    }
                    void * t222 = gm_l6;
                    gm_t88rcucg1b7uk t223 = gm_l9;
                    uint8_t t224 = gm_f_is_bool_ajhjgeg(t222, GM_CONV(gm_csguankko4lmhd, t223));
                    uint8_t t225 = (uint8_t)!t224;
                    if (t225) {
                        void * t226 = gm_l0;
                        gm_b16_8 t227 = gm_l3;
                        gm_t88rcucg1b7uk t228 = gm_l2;
                        gm_b16_8 t229;
                        GM_ST(void *, (uint8_t *)&t229, (void *)(gm_s8guqcjcce9oc));
                        GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(21));
                        gm_tl874t2atnlh3 t230 = gm_l5;
                        void * t231 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t226, t227, t228, t229, t230);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t231;
                        goto gm_fret;
                    }
                    gm_b16_8 t232 = gm_l3;
                    gm_b16_8 t233;
                    GM_ST(void *, (uint8_t *)&t233, (void *)(gm_s0nf1rg04f3m5));
                    GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(3));
                    uint8_t t234 = (gm_eq0iqiubfm7e(((uint8_t *)&t232), ((uint8_t *)&t233)));
                    if (t234) {
                        void * t235 = gm_l6;
                        gm_tvdg94bk7aa22 t236;
                        GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t236 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t236 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t236 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)49ULL));
                        gm_t88rcucg1b7uk t237 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t235);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t238 = (void *)((uint8_t *)&gm_l11);
                        uint64_t t239 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t240 = (uintptr_t)((uint64_t)1ULL);
                        if (t239 > t240 || t240 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t239, t240, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 185, 69);
                        uint64_t t241 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t242;
                        GM_ST(void *, (uint8_t *)&t242, (void *)(((uint8_t *)t238 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(t241));
                        gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
                        gm_tl874t2atnlh3 t244 = gm_l5;
                        void * t245 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t236, t237, t243, t244);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t245;
                        goto gm_fret;
                    }
                    gm_b16_8 t246 = gm_l3;
                    gm_b16_8 t247;
                    GM_ST(void *, (uint8_t *)&t247, (void *)(gm_skftljk77e6mu));
                    GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(3));
                    uint8_t t248 = (gm_eq0iqiubfm7e(((uint8_t *)&t246), ((uint8_t *)&t247)));
                    if (t248) {
                        void * t249 = gm_l6;
                        gm_tvdg94bk7aa22 t250;
                        GM_ST(uint8_t, (uint8_t *)&t250 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t250 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t250 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t250 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t250 + 0), ((uint8_t)50ULL));
                        gm_t88rcucg1b7uk t251 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t249);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t252 = (void *)((uint8_t *)&gm_l11);
                        uint64_t t253 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t254 = (uintptr_t)((uint64_t)1ULL);
                        if (t253 > t254 || t254 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t253, t254, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 186, 69);
                        uint64_t t255 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t256;
                        GM_ST(void *, (uint8_t *)&t256, (void *)(((uint8_t *)t252 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(t255));
                        gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                        gm_tl874t2atnlh3 t258 = gm_l5;
                        void * t259 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t250, t251, t257, t258);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t259;
                        goto gm_fret;
                    }
                    void * t260 = gm_l6;
                    gm_tvdg94bk7aa22 t261;
                    GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t261 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)51ULL));
                    gm_t88rcucg1b7uk t262 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t260);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t263 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t264 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t265 = (uintptr_t)((uint64_t)1ULL);
                    if (t264 > t265 || t265 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t264, t265, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 187, 56);
                    uint64_t t266 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t267;
                    GM_ST(void *, (uint8_t *)&t267, (void *)(((uint8_t *)t263 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(t266));
                    gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                    gm_tl874t2atnlh3 t269 = gm_l5;
                    void * t270 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t261, t262, t268, t269);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t270;
                    goto gm_fret;
                }
gm_lb28:;
            }
            {
                {
                    gm_b16_8 t271 = gm_l3;
                    gm_b16_8 t272;
                    GM_ST(void *, (uint8_t *)&t272, (void *)(gm_svgoe41piv341));
                    GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(6));
                    uint8_t t273 = (gm_eq0iqiubfm7e(((uint8_t *)&t271), ((uint8_t *)&t272)));
                    if (!t273) {
                        goto gm_lb33;
                    }
                    void * t274 = gm_l0;
                    gm_b16_8 t275 = gm_l4;
                    gm_b16_8 t276 = gm_l3;
                    gm_tl874t2atnlh3 t277 = gm_l5;
                    uint8_t t278 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t274, t275, ((uint64_t)2ULL), t276, t277);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t279 = (uint8_t)!t278;
                    if (t279) {
                        void * t280 = gm_l0;
                        void * t281 = GM_LD(void *, ((uint8_t *)t280));
                        gm_tl874t2atnlh3 t282 = gm_l5;
                        void * t283 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t281, t282);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t283;
                        goto gm_fret;
                    }
                    void * t284 = gm_l6;
                    gm_t88rcucg1b7uk t285 = gm_l9;
                    uint8_t t286 = gm_f_is_bool_ajhjgeg(t284, GM_CONV(gm_csguankko4lmhd, t285));
                    uint8_t t287 = (uint8_t)!t286;
                    if (t287) {
                        void * t288 = gm_l0;
                        gm_b16_8 t289 = gm_l3;
                        gm_t88rcucg1b7uk t290 = gm_l2;
                        gm_b16_8 t291;
                        GM_ST(void *, (uint8_t *)&t291, (void *)(gm_s8guqcjcce9oc));
                        GM_ST(uintptr_t, (uint8_t *)&t291 + 8, (uintptr_t)(21));
                        gm_tl874t2atnlh3 t292 = gm_l5;
                        void * t293 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t288, t289, t290, t291, t292);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t293;
                        goto gm_fret;
                    }
                    {
                        {
                            void * t294 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t295 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t295)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t295, gm_src7u9cfdjh7s0n, 192, 32);
                            uint8_t t296 = GM_LD(uint8_t, ((((uint8_t *)t294) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                            uint8_t t297 = (uint8_t)(t296 == ((uint8_t)0ULL));
                            if (!t297) {
                                goto gm_lb35;
                            }
                            memmove(((uint8_t *)&F.l16), ((((uint8_t *)t294) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                            void * t298 = gm_l0;
                            void * t299 = (void *)((uint8_t *)&F.l16);
                            gm_tkn42u7na3t81 t300;
                            memset(&t300, 0, sizeof t300);
                            void * t301 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t298, t299, t300);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l17 = t301;
                            void * t302 = gm_l0;
                            void * t303 = (void *)(((uint8_t *)t302) + 96);
                            void * t304 = gm_l0;
                            void * t305 = GM_LD(void *, ((uint8_t *)t304));
                            void * t306 = gm_l17;
                            gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t306));
                            gm_t88rcucg1b7uk t308 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t303, t305, t307);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l18 = t308;
                            void * t309 = gm_l6;
                            gm_t88rcucg1b7uk t310 = gm_l18;
                            uint8_t t311 = gm_f_is_error_8k27gd1(t309, GM_CONV(gm_csguankko4lmhd, t310));
                            if (t311) {
                                void * t312 = gm_l0;
                                void * t313 = GM_LD(void *, ((uint8_t *)t312));
                                gm_tl874t2atnlh3 t314 = gm_l5;
                                void * t315 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t313, t314);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t315;
                                goto gm_fret;
                            }
                            gm_l19 = ((uint8_t)0);
                            {
                                {
                                    void * t316 = gm_l6;
                                    gm_t88rcucg1b7uk t317 = gm_l18;
                                    gm_f_vector_parts_crrfopm((void *)&gm_l34, t316, GM_CONV(gm_csguankko4lmhd, t317));
                                    uint8_t t318 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
                                    if (!t318) {
                                        goto gm_lb37;
                                    }
                                    gm_tlcm4lvu3g1a8 t319 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l34));
                                    gm_l20 = t319;
                                    uint64_t t320 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                    uint64_t t321 = gm_l8;
                                    uint8_t t322 = ((t320 == t321));
                                    gm_l19 = t322;
                                    goto gm_lb36;
                                }
gm_lb37:;
                            }
gm_lb36:;
                            uint8_t t323 = gm_l19;
                            uint8_t t324 = (uint8_t)!t323;
                            if (t324) {
                                void * t325 = gm_l0;
                                void * t326 = gm_l17;
                                gm_tl874t2atnlh3 t327 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t326) + 72));
                                uint64_t t328 = gm_l8;
                                void * t329 = gm_l0;
                                gm_t88rcucg1b7uk t330 = gm_l2;
                                gm_b16_8 t331 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t329, t330);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t332 = gm_l0;
                                gm_t88rcucg1b7uk t333 = gm_l18;
                                gm_b16_8 t334 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t332, t333);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t7neromnu8t62 t335;
                                GM_ST(uint64_t, ((uint8_t *)&t335 + 0), t328);
                                GM_ST(gm_b16_8, ((uint8_t *)&t335 + 8), t331);
                                GM_ST(gm_b16_8, ((uint8_t *)&t335 + 24), t334);
                                gm_f_format_4nuejdf((void *)&gm_l35, (void *)NULL, cx, NULL, &t335);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t336 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                if (!t336) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src7u9cfdjh7s0n, 199, 34);
                                gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                gm_b16_8 t338 = GM_LD(gm_b16_8, ((uint8_t *)&t337));
                                gm_b0_1 t339 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t325, t327, t338);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t340 = gm_l0;
                                void * t341 = GM_LD(void *, ((uint8_t *)t340));
                                gm_tl874t2atnlh3 t342 = gm_l5;
                                void * t343 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t341, t342);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t343;
                                goto gm_fret;
                            }
                            void * t344 = gm_l0;
                            gm_b16_8 t345 = gm_l4;
                            gm_t88rcucg1b7uk t346 = gm_l18;
                            gm_tl874t2atnlh3 t347 = gm_l5;
                            void * t348 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t344, t345, ((uint64_t)1ULL), t346, t347);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l21 = t348;
                            gm_t88rcucg1b7uk t349 = gm_l18;
                            void * t350 = gm_l1;
                            void * t351 = gm_l17;
                            void * t352 = gm_l21;
                            gm_thmgeksj5pdth t353;
                            GM_ST(void *, ((uint8_t *)&t353 + 0), t350);
                            GM_ST(void *, ((uint8_t *)&t353 + 8), t351);
                            GM_ST(void *, ((uint8_t *)&t353 + 16), t352);
                            gm_tvdg94bk7aa22 t354;
                            GM_ST(uint8_t, (uint8_t *)&t354 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t354 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t354 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t354 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t354 + 0), ((uint8_t)52ULL));
                            gm_l22 = t353;
                            void * t355 = (void *)((uint8_t *)&gm_l22);
                            uint64_t t356 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t357 = (uintptr_t)((uint64_t)3ULL);
                            if (t356 > t357 || t357 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t356, t357, ((uint64_t)3ULL), gm_src7u9cfdjh7s0n, 203, 41);
                            uint64_t t358 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t359;
                            GM_ST(void *, (uint8_t *)&t359, (void *)(((uint8_t *)t355 + (uintptr_t)((uint64_t)0ULL) * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t359 + 8, (uintptr_t)(t358));
                            gm_tl874t2atnlh3 t360 = gm_l5;
                            void * t361 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t354, t349, t359, t360);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t361;
                            goto gm_fret;
                        }
gm_lb35:;
                        void * t362 = gm_l0;
                        void * t363 = GM_LD(void *, ((uint8_t *)t362));
                        gm_tl874t2atnlh3 t364 = gm_l5;
                        void * t365 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t363, t364);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t365;
                        goto gm_fret;
                    }
gm_lb34:;
                }
gm_lb33:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t366 = gm_l3;
                            gm_b16_8 t367;
                            GM_ST(void *, (uint8_t *)&t367, (void *)(gm_sm3o0uerjumpm));
                            GM_ST(uintptr_t, (uint8_t *)&t367 + 8, (uintptr_t)(3));
                            uint8_t t368 = (gm_eq0iqiubfm7e(((uint8_t *)&t366), ((uint8_t *)&t367)));
                            if (!t368) {
                                goto gm_lb40;
                            }
                            goto gm_lb39;
                        }
gm_lb40:;
                        {
                            gm_b16_8 t369 = gm_l3;
                            gm_b16_8 t370;
                            GM_ST(void *, (uint8_t *)&t370, (void *)(gm_s2mtoab140jp6));
                            GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(3));
                            uint8_t t371 = (gm_eq0iqiubfm7e(((uint8_t *)&t369), ((uint8_t *)&t370)));
                            if (!t371) {
                                goto gm_lb41;
                            }
                            goto gm_lb39;
                        }
gm_lb41:;
                        goto gm_lb38;
                    }
gm_lb39:;
                    void * t372 = gm_l0;
                    gm_b16_8 t373 = gm_l4;
                    gm_b16_8 t374 = gm_l3;
                    gm_tl874t2atnlh3 t375 = gm_l5;
                    uint8_t t376 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t372, t373, ((uint64_t)1ULL), t374, t375);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t377 = (uint8_t)!t376;
                    if (t377) {
                        void * t378 = gm_l0;
                        void * t379 = GM_LD(void *, ((uint8_t *)t378));
                        gm_tl874t2atnlh3 t380 = gm_l5;
                        void * t381 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t379, t380);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t381;
                        goto gm_fret;
                    }
                    uint8_t t382 = gm_l10;
                    uint8_t t383 = (uint8_t)!t382;
                    if (t383) {
                        void * t384 = gm_l0;
                        gm_b16_8 t385 = gm_l3;
                        gm_t88rcucg1b7uk t386 = gm_l2;
                        gm_b16_8 t387;
                        GM_ST(void *, (uint8_t *)&t387, (void *)(gm_sfmov9pb6t5lt));
                        GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(13));
                        gm_tl874t2atnlh3 t388 = gm_l5;
                        void * t389 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t384, t385, t386, t387, t388);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t389;
                        goto gm_fret;
                    }
                    void * t390 = gm_l0;
                    gm_b16_8 t391 = gm_l4;
                    gm_t88rcucg1b7uk t392 = gm_l2;
                    gm_tl874t2atnlh3 t393 = gm_l5;
                    void * t394 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t390, t391, ((uint64_t)0ULL), t392, t393);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t394;
                    gm_b16_8 t395 = gm_l3;
                    gm_b16_8 t396;
                    GM_ST(void *, (uint8_t *)&t396, (void *)(gm_sm3o0uerjumpm));
                    GM_ST(uintptr_t, (uint8_t *)&t396 + 8, (uintptr_t)(3));
                    uint8_t t397 = (gm_eq0iqiubfm7e(((uint8_t *)&t395), ((uint8_t *)&t396)));
                    if (t397) {
                        gm_tvdg94bk7aa22 t398;
                        GM_ST(uint8_t, (uint8_t *)&t398 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t398 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t398 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t398 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t398 + 0), ((uint8_t)53ULL));
                        gm_l36 = t398;
                    } else {
                        gm_tvdg94bk7aa22 t399;
                        GM_ST(uint8_t, (uint8_t *)&t399 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t399 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t399 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t399 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)54ULL));
                        gm_l36 = t399;
                    }
                    gm_tvdg94bk7aa22 t400 = gm_l36;
                    gm_t88rcucg1b7uk t401 = gm_l2;
                    void * t402 = gm_l1;
                    void * t403 = gm_l23;
                    gm_t5fru0lp11n7t t404;
                    GM_ST(void *, ((uint8_t *)&t404 + 0), t402);
                    GM_ST(void *, ((uint8_t *)&t404 + 8), t403);
                    gm_l24 = t404;
                    void * t405 = (void *)((uint8_t *)&gm_l24);
                    uint64_t t406 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t407 = (uintptr_t)((uint64_t)2ULL);
                    if (t406 > t407 || t407 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t406, t407, ((uint64_t)2ULL), gm_src7u9cfdjh7s0n, 209, 100);
                    uint64_t t408 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t409;
                    GM_ST(void *, (uint8_t *)&t409, (void *)(((uint8_t *)t405 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(t408));
                    gm_tl874t2atnlh3 t410 = gm_l5;
                    void * t411 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t400, t401, t409, t410);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t411;
                    goto gm_fret;
                }
gm_lb38:;
                {
                    gm_b16_8 t412 = gm_l3;
                    gm_b16_8 t413;
                    GM_ST(void *, (uint8_t *)&t413, (void *)(gm_sbl3pbs9195ho));
                    GM_ST(uintptr_t, (uint8_t *)&t413 + 8, (uintptr_t)(3));
                    uint8_t t414 = (gm_eq0iqiubfm7e(((uint8_t *)&t412), ((uint8_t *)&t413)));
                    if (!t414) {
                        goto gm_lb42;
                    }
                    void * t415 = gm_l0;
                    gm_b16_8 t416 = gm_l4;
                    gm_b16_8 t417 = gm_l3;
                    gm_tl874t2atnlh3 t418 = gm_l5;
                    uint8_t t419 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t415, t416, ((uint64_t)0ULL), t417, t418);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t420 = (uint8_t)!t419;
                    if (t420) {
                        void * t421 = gm_l0;
                        void * t422 = GM_LD(void *, ((uint8_t *)t421));
                        gm_tl874t2atnlh3 t423 = gm_l5;
                        void * t424 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t422, t423);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t424;
                        goto gm_fret;
                    }
                    void * t425 = gm_l6;
                    gm_t88rcucg1b7uk t426 = gm_l9;
                    uint8_t t427 = gm_f_is_signed_integer_s66djm7(t425, GM_CONV(gm_csguankko4lmhd, t426));
                    if (t427) {
                        gm_l37 = ((uint8_t)1);
                    } else {
                        void * t428 = gm_l6;
                        gm_t88rcucg1b7uk t429 = gm_l9;
                        uint8_t t430 = gm_f_is_float_3jk89qu(t428, GM_CONV(gm_csguankko4lmhd, t429));
                        gm_l37 = t430;
                    }
                    uint8_t t431 = gm_l37;
                    uint8_t t432 = (uint8_t)!t431;
                    if (t432) {
                        void * t433 = gm_l0;
                        gm_b16_8 t434 = gm_l3;
                        gm_t88rcucg1b7uk t435 = gm_l2;
                        gm_b16_8 t436;
                        GM_ST(void *, (uint8_t *)&t436, (void *)(gm_salin070nim24));
                        GM_ST(uintptr_t, (uint8_t *)&t436 + 8, (uintptr_t)(29));
                        gm_tl874t2atnlh3 t437 = gm_l5;
                        void * t438 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t433, t434, t435, t436, t437);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t438;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t439 = gm_l2;
                    void * t440 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t441 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t442 = (uintptr_t)((uint64_t)1ULL);
                    if (t441 > t442 || t442 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t441, t442, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 214, 42);
                    uint64_t t443 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t444;
                    GM_ST(void *, (uint8_t *)&t444, (void *)(((uint8_t *)t440 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t444 + 8, (uintptr_t)(t443));
                    gm_b16_8 t445 = GM_LD(gm_b16_8, ((uint8_t *)&t444));
                    gm_tl874t2atnlh3 t446 = gm_l5;
                    gm_tvdg94bk7aa22 t447;
                    GM_ST(uint8_t, (uint8_t *)&t447 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t447 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t447 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t447 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t447 + 0), ((uint8_t)55ULL));
                    void * t448 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t447, t439, t445, t446);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t448;
                    goto gm_fret;
                }
gm_lb42:;
            }
            {
                {
                    gm_b16_8 t449 = gm_l3;
                    gm_b16_8 t450;
                    GM_ST(void *, (uint8_t *)&t450, (void *)(gm_sfrjcsmisriv1));
                    GM_ST(uintptr_t, (uint8_t *)&t450 + 8, (uintptr_t)(2));
                    uint8_t t451 = (gm_eq0iqiubfm7e(((uint8_t *)&t449), ((uint8_t *)&t450)));
                    if (!t451) {
                        goto gm_lb43;
                    }
                    void * t452 = gm_l0;
                    gm_b16_8 t453 = gm_l4;
                    gm_b16_8 t454 = gm_l3;
                    gm_tl874t2atnlh3 t455 = gm_l5;
                    uint8_t t456 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t452, t453, ((uint64_t)1ULL), t454, t455);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t457 = (uint8_t)!t456;
                    if (t457) {
                        void * t458 = gm_l0;
                        void * t459 = GM_LD(void *, ((uint8_t *)t458));
                        gm_tl874t2atnlh3 t460 = gm_l5;
                        void * t461 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t459, t460);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t461;
                        goto gm_fret;
                    }
                    void * t462 = gm_l0;
                    gm_b16_8 t463 = gm_l4;
                    gm_tl874t2atnlh3 t464 = gm_l5;
                    gm_tkn42u7na3t81 t465 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t462, t463, ((uint64_t)0ULL), t464);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l38 = t465;
                    uint8_t t466 = (uint8_t)((((uint8_t *)&gm_l38))[4] != 0);
                    if (t466) {
                        gm_t88rcucg1b7uk t467 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l38));
                        gm_l39 = t467;
                    } else {
                        void * t468 = gm_l0;
                        void * t469 = GM_LD(void *, ((uint8_t *)t468));
                        gm_tl874t2atnlh3 t470 = gm_l5;
                        void * t471 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t469, t470);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t471;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t472 = gm_l39;
                    gm_l25 = t472;
                    void * t473 = gm_l6;
                    gm_t88rcucg1b7uk t474 = gm_l25;
                    uint8_t t475 = gm_f_is_float_3jk89qu(t473, GM_CONV(gm_csguankko4lmhd, t474));
                    uint8_t t476 = (uint8_t)!t475;
                    if (t476) {
                        gm_l41 = ((uint8_t)1);
                    } else {
                        void * t477 = gm_l6;
                        gm_t88rcucg1b7uk t478 = gm_l25;
                        uint8_t t479 = gm_f_lane_type_ok_rp8uq1p(t477, GM_CONV(gm_csguankko4lmhd, t478));
                        uint8_t t480 = (uint8_t)!t479;
                        gm_l41 = t480;
                    }
                    uint8_t t481 = gm_l41;
                    if (t481) {
                        gm_l40 = ((uint8_t)1);
                    } else {
                        uint8_t t482 = gm_l10;
                        uint8_t t483 = (uint8_t)!t482;
                        gm_l40 = t483;
                    }
                    uint8_t t484 = gm_l40;
                    if (t484) {
                        void * t485 = gm_l0;
                        gm_tl874t2atnlh3 t486 = gm_l5;
                        void * t487 = gm_l0;
                        gm_t88rcucg1b7uk t488 = gm_l9;
                        gm_b16_8 t489 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t487, t488);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t490 = gm_l0;
                        gm_t88rcucg1b7uk t491 = gm_l25;
                        gm_b16_8 t492 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t490, t491);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t493;
                        GM_ST(gm_b16_8, ((uint8_t *)&t493 + 0), t489);
                        GM_ST(gm_b16_8, ((uint8_t *)&t493 + 16), t492);
                        gm_f_format_m1f4vhf((void *)&gm_l42, (void *)NULL, cx, NULL, &t493);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t494 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                        if (!t494) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_src7u9cfdjh7s0n, 220, 32);
                        gm_b16_8 t495 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                        gm_b16_8 t496 = GM_LD(gm_b16_8, ((uint8_t *)&t495));
                        gm_b0_1 t497 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t485, t486, t496);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t498 = gm_l0;
                        void * t499 = GM_LD(void *, ((uint8_t *)t498));
                        gm_tl874t2atnlh3 t500 = gm_l5;
                        void * t501 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t499, t500);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t501;
                        goto gm_fret;
                    }
                    {
                        {
                            void * t502 = gm_l0;
                            void * t503 = GM_LD(void *, ((uint8_t *)t502));
                            uint64_t t504 = gm_l8;
                            gm_t88rcucg1b7uk t505 = gm_l25;
                            gm_f_vector_shape_error_3invhhe((void *)&gm_l43, (void *)NULL, cx, NULL, t503, t504, t505);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t506 = (uint8_t)((((uint8_t *)&gm_l43))[16] != 0);
                            if (!t506) {
                                goto gm_lb45;
                            }
                            gm_b16_8 t507 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l43));
                            gm_l26 = t507;
                            void * t508 = gm_l0;
                            gm_tl874t2atnlh3 t509 = gm_l5;
                            gm_b16_8 t510 = gm_l26;
                            gm_b0_1 t511 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t508, t509, t510);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t512 = gm_l0;
                            void * t513 = GM_LD(void *, ((uint8_t *)t512));
                            gm_tl874t2atnlh3 t514 = gm_l5;
                            void * t515 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t513, t514);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t515;
                            goto gm_fret;
                        }
gm_lb45:;
                    }
gm_lb44:;
                    void * t516 = gm_l6;
                    uint64_t t517 = gm_l8;
                    gm_t88rcucg1b7uk t518 = gm_l25;
                    gm_tvdg94bk7aa22 t519;
                    GM_ST(uint8_t, (uint8_t *)&t519 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t519 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t519 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t519 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t519 + 0), ((uint8_t)56ULL));
                    gm_t88rcucg1b7uk t520 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t516, t517, t518);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t521 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t522 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t523 = (uintptr_t)((uint64_t)1ULL);
                    if (t522 > t523 || t523 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t522, t523, ((uint64_t)1ULL), gm_src7u9cfdjh7s0n, 227, 60);
                    uint64_t t524 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t525;
                    GM_ST(void *, (uint8_t *)&t525, (void *)(((uint8_t *)t521 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t525 + 8, (uintptr_t)(t524));
                    gm_b16_8 t526 = GM_LD(gm_b16_8, ((uint8_t *)&t525));
                    gm_tl874t2atnlh3 t527 = gm_l5;
                    void * t528 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t519, t520, t526, t527);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t528;
                    goto gm_fret;
                }
gm_lb43:;
            }
            {
                {
                    gm_b16_8 t529 = gm_l3;
                    gm_b16_8 t530;
                    GM_ST(void *, (uint8_t *)&t530, (void *)(gm_salpeh0v5rqdf));
                    GM_ST(uintptr_t, (uint8_t *)&t530 + 8, (uintptr_t)(7));
                    uint8_t t531 = (gm_eq0iqiubfm7e(((uint8_t *)&t529), ((uint8_t *)&t530)));
                    if (!t531) {
                        goto gm_lb46;
                    }
                    void * t532 = gm_l0;
                    void * t533 = gm_l1;
                    gm_t88rcucg1b7uk t534 = gm_l2;
                    gm_b16_8 t535 = gm_l4;
                    gm_tl874t2atnlh3 t536 = gm_l5;
                    void * t537 = gm_f_vector_shuffle_5ime7a7((void *)NULL, cx, NULL, t532, t533, t534, t535, t536);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t537;
                    goto gm_fret;
                }
gm_lb46:;
                {
                    gm_b16_8 t538 = gm_l3;
                    gm_b16_8 t539;
                    GM_ST(void *, (uint8_t *)&t539, (void *)(gm_s0ugop4l95o1j));
                    GM_ST(uintptr_t, (uint8_t *)&t539 + 8, (uintptr_t)(5));
                    uint8_t t540 = (gm_eq0iqiubfm7e(((uint8_t *)&t538), ((uint8_t *)&t539)));
                    if (!t540) {
                        goto gm_lb47;
                    }
                    void * t541 = gm_l0;
                    gm_b16_8 t542 = gm_l4;
                    gm_b16_8 t543 = gm_l3;
                    gm_tl874t2atnlh3 t544 = gm_l5;
                    uint8_t t545 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t541, t542, ((uint64_t)1ULL), t543, t544);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t546 = (uint8_t)!t545;
                    if (t546) {
                        void * t547 = gm_l0;
                        void * t548 = GM_LD(void *, ((uint8_t *)t547));
                        gm_tl874t2atnlh3 t549 = gm_l5;
                        void * t550 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t548, t549);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t550;
                        goto gm_fret;
                    }
                    void * t551 = gm_l0;
                    gm_b16_8 t552 = gm_l4;
                    void * t553 = gm_l6;
                    gm_t88rcucg1b7uk t554 = gm_l9;
                    gm_t88rcucg1b7uk t555 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t553, ((uint8_t)0), t554);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl874t2atnlh3 t556 = gm_l5;
                    void * t557 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t551, t552, ((uint64_t)0ULL), t555, t556);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t557;
                    void * t558 = gm_l6;
                    gm_tvdg94bk7aa22 t559;
                    GM_ST(uint8_t, (uint8_t *)&t559 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t559 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t559 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t559 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t559 + 0), ((uint8_t)59ULL));
                    gm_t88rcucg1b7uk t560 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t558);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t561 = gm_l1;
                    void * t562 = gm_l27;
                    gm_t5fru0lp11n7t t563;
                    GM_ST(void *, ((uint8_t *)&t563 + 0), t561);
                    GM_ST(void *, ((uint8_t *)&t563 + 8), t562);
                    gm_l28 = t563;
                    void * t564 = (void *)((uint8_t *)&gm_l28);
                    uint64_t t565 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t566 = (uintptr_t)((uint64_t)2ULL);
                    if (t565 > t566 || t566 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t565, t566, ((uint64_t)2ULL), gm_src7u9cfdjh7s0n, 233, 51);
                    uint64_t t567 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t568;
                    GM_ST(void *, (uint8_t *)&t568, (void *)(((uint8_t *)t564 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t568 + 8, (uintptr_t)(t567));
                    gm_tl874t2atnlh3 t569 = gm_l5;
                    void * t570 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t559, t560, t568, t569);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t570;
                    goto gm_fret;
                }
gm_lb47:;
                {
                    goto gm_lb1;
                }
gm_lb48:;
                gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 157, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t571 = gm_l0;
        gm_tl874t2atnlh3 t572 = gm_l5;
        gm_b16_8 t573 = gm_l3;
        gm_tnpifv20irnph t574;
        GM_ST(gm_b16_8, ((uint8_t *)&t574 + 0), t573);
        gm_f_format_gsgjkse((void *)&gm_l44, (void *)NULL, cx, NULL, t574);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t575 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
        if (!t575) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_src7u9cfdjh7s0n, 237, 20);
        gm_b16_8 t576 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
        gm_b16_8 t577 = GM_LD(gm_b16_8, ((uint8_t *)&t576));
        gm_b0_1 t578 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t571, t572, t577);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t579 = gm_l0;
        void * t580 = GM_LD(void *, ((uint8_t *)t579));
        gm_tl874t2atnlh3 t581 = gm_l5;
        void * t582 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t580, t581);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t582;
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
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_shuffle_5ime7a7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    uint64_t gm_l7; /* len */
    gm_t88rcucg1b7uk gm_l8; /* usize_t */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l14; /* v */
    gm_tf5srh4mp4b3i gm_l15; /* k */
    uint8_t gm_l16; /* int_ok */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_tl874t2atnlh3 gm_l19; /* at */
    void * gm_l20; /* cell */
    unsigned __int128 gm_l21; /* n */
    uint64_t gm_l22; /* x */
    uint64_t gm_l24; /* m */
    gm_b16_8 gm_l25; /* msg */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    GM_STACK_CHECK(&F, 2530u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tb7u19loo3j87 gm_l30;
    gm_tlcm4lvu3g1a8 gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    gm_t9qkh0rsooc8d gm_l34;
    gm_tabb710d9mq2e gm_l35;
    void * gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_th2lqbo77pvsc gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    {
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, ((uint8_t *)t34));
        void * t36 = (void *)(((uint8_t *)t35) + 8);
        gm_l5 = t36;
        void * t37 = gm_l5;
        gm_t88rcucg1b7uk t38 = gm_l2;
        gm_f_vector_parts_crrfopm((void *)&gm_l30, t37, GM_CONV(gm_csguankko4lmhd, t38));
        uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l30))[16] != 0);
        if (t39) {
            gm_tlcm4lvu3g1a8 t40 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l30));
            gm_l31 = t40;
        } else {
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, ((uint8_t *)t41));
            gm_tl874t2atnlh3 t43 = gm_l4;
            void * t44 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t44;
            goto gm_fret;
        }
        gm_tlcm4lvu3g1a8 t45 = gm_l31;
        gm_l6 = t45;
        uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l7 = t46;
        void * t47 = gm_l5;
        gm_t88rcucg1b7uk t48 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t48;
        gm_trqfvmjtt4sij t49;
        gm_f_list_vp1djv1_2((void *)&t49, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t49;
        gm_b16_8 t50 = gm_l3;
        gm_l10 = t50;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t51 = gm_l11;
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t53 = (t51 < t52);
                    if (!t53) {
                        goto gm_lb1;
                    }
                    {
                        void * t54 = GM_LD(void *, ((uint8_t *)&gm_l10));
                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t t56 = gm_l11;
                        {
                            {
                                memmove(((uint8_t *)&F.l12), (((uint8_t *)t54) + (uintptr_t)t56 * 144), 144);
                                {
                                    {
                                        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                        uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
                                        if (!t58) {
                                            goto gm_lb7;
                                        }
                                        memmove(((uint8_t *)&F.l13), (((uint8_t *)&F.l12) + 8), 88);
                                        void * t59 = gm_l0;
                                        void * t60 = (void *)((uint8_t *)&F.l13);
                                        gm_t88rcucg1b7uk t61 = gm_l8;
                                        gm_tkn42u7na3t81 t62;
                                        GM_ST(uint8_t, (uint8_t *)&t62 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t62 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t62), t61);
                                        ((uint8_t *)&t62)[4] = 1;
                                        void * t63 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t59, t60, t62);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l14 = t63;
                                        gm_tf5srh4mp4b3i t64;
                                        memset(&t64, 0, sizeof t64);
                                        gm_l15 = t64;
                                        gm_l16 = ((uint8_t)0);
                                        void * t65 = gm_l14;
                                        {
                                            {
                                                uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
                                                uint8_t t67 = (uint8_t)(t66 == ((uint8_t)0ULL));
                                                if (!t67) {
                                                    goto gm_lb9;
                                                }
                                                gm_l32 = ((uint8_t)1);
                                                goto gm_lb8;
                                            }
gm_lb9:;
                                            {
                                                gm_l32 = ((uint8_t)0);
                                                goto gm_lb8;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 257, 12);
                                        }
gm_lb8:;
                                        uint8_t t68 = gm_l32;
                                        if (t68) {
                                            void * t69 = gm_l0;
                                            void * t70 = (void *)(((uint8_t *)t69) + 96);
                                            void * t71 = gm_l5;
                                            void * t72 = gm_l14;
                                            gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t72));
                                            gm_t88rcucg1b7uk t74 = gm_l8;
                                            gm_t9qkh0rsooc8d t75 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t70, t71, t73, t74);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l34 = t75;
                                            {
                                                {
                                                    uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                    if (t76) {
                                                        goto gm_lb12;
                                                    }
                                                    gm_l33 = ((uint8_t)1);
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                {
                                                    gm_l33 = ((uint8_t)0);
                                                    goto gm_lb11;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 257, 46);
                                            }
gm_lb11:;
                                            uint8_t t77 = gm_l33;
                                            uint8_t t78 = (uint8_t)!t77;
                                            gm_l16 = t78;
                                        }
                                        uint8_t t79 = gm_l16;
                                        if (t79) {
                                            {
                                                {
                                                    void * t80 = gm_l14;
                                                    uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t80) + 8) + 0));
                                                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)0ULL));
                                                    if (!t82) {
                                                        goto gm_lb15;
                                                    }
                                                    memmove(((uint8_t *)&F.l17), ((((uint8_t *)t80) + 8) + 8), 48);
                                                    gm_tf5srh4mp4b3i t83 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
                                                    gm_l15 = t83;
                                                    goto gm_lb14;
                                                }
gm_lb15:;
                                                void * t84 = gm_l0;
                                                void * t85 = GM_LD(void *, ((uint8_t *)t84));
                                                gm_tl874t2atnlh3 t86 = gm_l4;
                                                void * t87 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t85, t86);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t87;
                                                goto gm_fret;
                                            }
gm_lb14:;
                                        } else {
                                            void * t88 = gm_l5;
                                            void * t89 = gm_l14;
                                            gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t89));
                                            uint8_t t91 = gm_f_is_error_8k27gd1(t88, GM_CONV(gm_csguankko4lmhd, t90));
                                            if (t91) {
                                                void * t92 = gm_l0;
                                                void * t93 = GM_LD(void *, ((uint8_t *)t92));
                                                gm_tl874t2atnlh3 t94 = gm_l4;
                                                void * t95 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t93, t94);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t95;
                                                goto gm_fret;
                                            } else {
                                                void * t96 = gm_l0;
                                                void * t97 = (void *)(((uint8_t *)t96) + 96);
                                                void * t98 = gm_l0;
                                                void * t99 = GM_LD(void *, ((uint8_t *)t98));
                                                void * t100 = gm_l14;
                                                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t100));
                                                gm_t88rcucg1b7uk t102 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t97, t99, t101);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_t88rcucg1b7uk t103 = gm_l8;
                                                uint8_t t104 = (!gm_eq9m6mvtmgde(((uint8_t *)&t102), ((uint8_t *)&t103)));
                                                if (t104) {
                                                    void * t105 = gm_l0;
                                                    void * t106 = (void *)(((uint8_t *)t105) + 96);
                                                    void * t107 = gm_l0;
                                                    void * t108 = GM_LD(void *, ((uint8_t *)t107));
                                                    void * t109 = gm_l14;
                                                    gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t109));
                                                    gm_t88rcucg1b7uk t111 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t106, t108, t110);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l18 = t111;
                                                    void * t112 = gm_l0;
                                                    void * t113 = gm_l14;
                                                    gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t113) + 72));
                                                    void * t115 = gm_l0;
                                                    gm_t88rcucg1b7uk t116 = gm_l18;
                                                    gm_b16_8 t117 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t115, t116);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tnpifv20irnph t118;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t117);
                                                    gm_f_format_jrngvku((void *)&gm_l35, (void *)NULL, cx, NULL, t118);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                    if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src7u9cfdjh7s0n, 265, 30);
                                                    gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                                    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                                    gm_b0_1 t122 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t112, t114, t121);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t123 = gm_l0;
                                                    void * t124 = GM_LD(void *, ((uint8_t *)t123));
                                                    gm_tl874t2atnlh3 t125 = gm_l4;
                                                    void * t126 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t124, t125);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.ret = t126;
                                                    goto gm_fret;
                                                } else {
                                                    {
                                                        {
                                                            void * t127 = gm_l14;
                                                            uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t127) + 8) + 0));
                                                            uint8_t t129 = (uint8_t)(t128 == ((uint8_t)14ULL));
                                                            if (!t129) {
                                                                goto gm_lb17;
                                                            }
                                                            gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t127) + 8) + 8) + 16));
                                                            gm_l19 = t130;
                                                            {
                                                                {
                                                                    void * t131 = gm_l0;
                                                                    void * t132 = GM_LD(void *, ((uint8_t *)t131));
                                                                    gm_tl874t2atnlh3 t133 = gm_l19;
                                                                    void * t134 = gm_l14;
                                                                    gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t134));
                                                                    void * t136 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t132, t133, t135);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l36 = t136;
                                                                    uint8_t t137 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l36)) != 0);
                                                                    if (!t137) {
                                                                        goto gm_lb19;
                                                                    }
                                                                    void * t138 = GM_LD(void *, ((uint8_t *)&gm_l36));
                                                                    gm_l20 = t138;
                                                                    {
                                                                        {
                                                                            void * t139 = gm_l20;
                                                                            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)t139) + 0));
                                                                            uint8_t t141 = (uint8_t)(t140 == ((uint8_t)0ULL));
                                                                            if (!t141) {
                                                                                goto gm_lb21;
                                                                            }
                                                                            unsigned __int128 t142 = GM_LD(unsigned __int128, (((uint8_t *)t139) + 16));
                                                                            gm_l21 = t142;
                                                                            unsigned __int128 t143 = gm_l21;
                                                                            gm_tf5srh4mp4b3i t144;
                                                                            uint64_t t145 = (uint64_t)t143;
                                                                            gm_tf5srh4mp4b3i t146;
                                                                            GM_ST(uint8_t, (uint8_t *)&t146 + 9, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&t146 + 10, 0);
                                                                            GM_ST(uint32_t, (uint8_t *)&t146 + 12, 0);
                                                                            GM_ST(uint64_t, ((uint8_t *)&t146), t145);
                                                                            ((uint8_t *)&t146)[8] = 1;
                                                                            gm_tf5srh4mp4b3i t147;
                                                                            memset(&t147, 0, sizeof t147);
                                                                            t144 = ((unsigned __int128)(t143) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t146 : t147;
                                                                            gm_l15 = t144;
                                                                            goto gm_lb20;
                                                                        }
gm_lb21:;
                                                                    }
gm_lb20:;
                                                                    goto gm_lb18;
                                                                }
gm_lb19:;
                                                            }
gm_lb18:;
                                                            goto gm_lb16;
                                                        }
gm_lb17:;
                                                    }
gm_lb16:;
                                                }
                                            }
                                        }
                                        {
                                            {
                                                uint8_t t148 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                                if (!t148) {
                                                    goto gm_lb23;
                                                }
                                                uint64_t t149 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                                gm_l22 = t149;
                                                uint64_t t150 = gm_l22;
                                                uint64_t t151 = gm_l7;
                                                uint8_t t152 = (t150 >= t151);
                                                if (t152) {
                                                    void * t153 = gm_l0;
                                                    void * t154 = gm_l14;
                                                    gm_tl874t2atnlh3 t155 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t154) + 72));
                                                    uint64_t t156 = gm_l22;
                                                    void * t157 = gm_l0;
                                                    gm_t88rcucg1b7uk t158 = gm_l2;
                                                    gm_b16_8 t159 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t157, t158);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint64_t t160 = gm_l7;
                                                    uint64_t r161; bool o162 = __builtin_sub_overflow(t160, ((uint64_t)1ULL), &r161);
                                                    if (o162) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 274, 111);
                                                    uint64_t t163 = r161;
                                                    gm_tob1snuii5bme t164;
                                                    GM_ST(uint64_t, ((uint8_t *)&t164 + 0), t156);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t164 + 8), t159);
                                                    GM_ST(uint64_t, ((uint8_t *)&t164 + 24), t163);
                                                    gm_f_format_jm4dd7q((void *)&gm_l37, (void *)NULL, cx, NULL, &t164);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                    if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src7u9cfdjh7s0n, 274, 34);
                                                    gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                                    gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                                                    gm_b0_1 t168 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t153, t155, t167);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t169 = gm_l0;
                                                    void * t170 = GM_LD(void *, ((uint8_t *)t169));
                                                    gm_tl874t2atnlh3 t171 = gm_l4;
                                                    void * t172 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t170, t171);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.ret = t172;
                                                    goto gm_fret;
                                                }
                                                void * t173 = (void *)((uint8_t *)&F.l9);
                                                gm_t88rcucg1b7uk t174 = gm_l8;
                                                uint64_t t175 = gm_l22;
                                                gm_tbci5ospk5nnn t176;
                                                gm_f_from_u64_ud13q5v((void *)&t176, (void *)NULL, cx, NULL, t175);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t177 = gm_l14;
                                                gm_tl874t2atnlh3 t178 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t177) + 72));
                                                gm_t2b6tm8c0vgo1 t179;
                                                GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t179 + 56, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t179 + 8), t176);
                                                void * t180 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t174, &t179, t178);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tftb1odc739t5 t181 = gm_f_push_d943vgp_2(t173, &(*(gm_tcgrjq9toscke *)((uint8_t *)t180)));
                                                gm_l38 = t181;
                                                uint8_t t182 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                if (!t182) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src7u9cfdjh7s0n, 277, 13);
                                                goto gm_lb22;
                                            }
gm_lb23:;
                                            {
                                                {
                                                    void * t183 = gm_l14;
                                                    uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t183) + 8) + 0));
                                                    uint8_t t185 = (uint8_t)(t184 == ((uint8_t)0ULL));
                                                    if (!t185) {
                                                        goto gm_lb25;
                                                    }
                                                    memmove(((uint8_t *)&F.l23), ((((uint8_t *)t183) + 8) + 8), 48);
                                                    void * t186 = gm_l0;
                                                    void * t187 = gm_l14;
                                                    gm_tl874t2atnlh3 t188 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t187) + 72));
                                                    gm_b16_8 t189 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t190 = gm_l0;
                                                    gm_t88rcucg1b7uk t191 = gm_l2;
                                                    gm_b16_8 t192 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t190, t191);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint64_t t193 = gm_l7;
                                                    uint64_t r194; bool o195 = __builtin_sub_overflow(t193, ((uint64_t)1ULL), &r194);
                                                    if (o195) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 280, 124);
                                                    uint64_t t196 = r194;
                                                    gm_tftksr4op114e t197;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t189);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t197 + 16), t192);
                                                    GM_ST(uint64_t, ((uint8_t *)&t197 + 32), t196);
                                                    gm_f_format_8eujoej((void *)&gm_l39, (void *)NULL, cx, NULL, &t197);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                                    if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src7u9cfdjh7s0n, 280, 34);
                                                    gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                                                    gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                                                    gm_b0_1 t201 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t186, t188, t200);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t202 = gm_l0;
                                                    void * t203 = GM_LD(void *, ((uint8_t *)t202));
                                                    gm_tl874t2atnlh3 t204 = gm_l4;
                                                    void * t205 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t203, t204);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.ret = t205;
                                                    goto gm_fret;
                                                }
gm_lb25:;
                                            }
gm_lb24:;
                                            void * t206 = gm_l0;
                                            void * t207 = gm_l14;
                                            gm_tl874t2atnlh3 t208 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t207) + 72));
                                            gm_b16_8 t209;
                                            GM_ST(void *, (uint8_t *)&t209, (void *)(gm_skdalh0o0hret));
                                            GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(58));
                                            gm_b0_1 t210 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t206, t208, t209);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t211 = gm_l0;
                                            void * t212 = GM_LD(void *, ((uint8_t *)t211));
                                            gm_tl874t2atnlh3 t213 = gm_l4;
                                            void * t214 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t212, t213);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.ret = t214;
                                            goto gm_fret;
                                        }
gm_lb22:;
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    void * t215 = gm_l0;
                                    gm_tl874t2atnlh3 t216 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                                    gm_b16_8 t217;
                                    GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sioam4mlrjuar));
                                    GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(51));
                                    gm_b0_1 t218 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t215, t216, t217);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t219 = gm_l0;
                                    void * t220 = GM_LD(void *, ((uint8_t *)t219));
                                    gm_tl874t2atnlh3 t221 = gm_l4;
                                    void * t222 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t220, t221);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t222;
                                    goto gm_fret;
                                }
gm_lb6:;
                                goto gm_lb4;
                            }
gm_lb5:;
                            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 249, 5);
                        }
gm_lb4:;
                    }
gm_lb3:;
                }
                {
                    uint64_t t223 = gm_l11;
                    uint64_t r224; bool o225 = __builtin_add_overflow(t223, ((uint64_t)1ULL), &r224);
                    uint64_t t226 = r224;
                    gm_l11 = t226;
                }
            }
        }
gm_lb1:;
    }
    {
        uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
        uint64_t t228 = (uint64_t)t227;
        gm_l24 = t228;
        {
            {
                void * t229 = gm_l0;
                void * t230 = GM_LD(void *, ((uint8_t *)t229));
                uint64_t t231 = gm_l24;
                gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
                gm_f_vector_shape_error_3invhhe((void *)&gm_l40, (void *)NULL, cx, NULL, t230, t231, t232);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t233 = (uint8_t)((((uint8_t *)&gm_l40))[16] != 0);
                if (!t233) {
                    goto gm_lb27;
                }
                gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l40));
                gm_l25 = t234;
                void * t235 = gm_l0;
                gm_tl874t2atnlh3 t236 = gm_l4;
                gm_b16_8 t237 = gm_l25;
                gm_tnpifv20irnph t238;
                GM_ST(gm_b16_8, ((uint8_t *)&t238 + 0), t237);
                gm_f_format_7cgva50((void *)&gm_l41, (void *)NULL, cx, NULL, t238);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t239 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                if (!t239) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_src7u9cfdjh7s0n, 289, 24);
                gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
                gm_b0_1 t242 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t235, t236, t241);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t243 = gm_l0;
                void * t244 = GM_LD(void *, ((uint8_t *)t243));
                gm_tl874t2atnlh3 t245 = gm_l4;
                void * t246 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t244, t245);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t246;
                goto gm_fret;
            }
gm_lb27:;
        }
gm_lb26:;
        gm_trqfvmjtt4sij t247;
        gm_f_list_vp1djv1_2((void *)&t247, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l26 = t247;
        void * t248 = (void *)((uint8_t *)&F.l26);
        void * t249 = gm_l1;
        gm_tftb1odc739t5 t250 = gm_f_push_d943vgp_2(t248, &(*(gm_tcgrjq9toscke *)((uint8_t *)t249)));
        gm_l42 = t250;
        uint8_t t251 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
        if (!t251) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src7u9cfdjh7s0n, 293, 5);
        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_l27 = t252;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t253 = gm_l28;
                uint64_t t254 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t255 = (t253 < t254);
                if (!t255) {
                    goto gm_lb28;
                }
                {
                    void * t256 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t258 = gm_l28;
                    {
                        {
                            memmove(((uint8_t *)&F.l29), (((uint8_t *)t256) + (uintptr_t)t258 * 88), 88);
                            void * t259 = (void *)((uint8_t *)&F.l26);
                            gm_tftb1odc739t5 t260 = gm_f_push_d943vgp_2(t259, &(*(gm_tcgrjq9toscke *)((uint8_t *)&F.l29)));
                            gm_l43 = t260;
                            uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                            if (!t261) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_src7u9cfdjh7s0n, 294, 26);
                            goto gm_lb31;
                        }
gm_lb32:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 294, 5);
                    }
gm_lb31:;
                }
gm_lb30:;
                uint64_t t262 = gm_l28;
                uint64_t r263; bool o264 = __builtin_add_overflow(t262, ((uint64_t)1ULL), &r263);
                uint64_t t265 = r263;
                gm_l28 = t265;
            }
        }
gm_lb28:;
    }
    {
        void * t266 = gm_l5;
        uint64_t t267 = gm_l24;
        gm_t88rcucg1b7uk t268 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_t88rcucg1b7uk t269 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t266, t267, t268);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
        gm_tvdg94bk7aa22 t271;
        GM_ST(uint8_t, (uint8_t *)&t271 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t271 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t271 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t271 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t271 + 0), ((uint8_t)57ULL));
        gm_tt8o5kebou62r t272;
        GM_ST(uint32_t, (uint8_t *)&t272 + 12, 0);
        GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t272 + 0), t271);
        GM_ST(gm_b16_8, ((uint8_t *)&t272 + 16), t270);
        gm_tl874t2atnlh3 t273 = gm_l4;
        gm_t2b6tm8c0vgo1 t274;
        GM_ST(uint8_t, (uint8_t *)&t274 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t274 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t274 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t274 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t274 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t274 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t274 + 8), t272);
        void * t275 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t269, &t274, t273);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t275;
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
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_unary_2mbe97t F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* v */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    gm_t88rcucg1b7uk gm_l7; /* lane */
    gm_b16_8 gm_l8; /* hint */
    gm_b16_8 gm_l9; /* s */
    GM_STACK_CHECK(&F, 1103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tb7u19loo3j87 gm_l10;
    gm_tlcm4lvu3g1a8 gm_l11;
    uint8_t gm_l12;
    gm_b16_8 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_b16_8 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_l4 = t9;
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 96);
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    void * t14 = gm_l2;
    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t14));
    gm_t88rcucg1b7uk t16 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t11, t13, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t16;
    void * t17 = gm_l4;
    gm_t88rcucg1b7uk t18 = gm_l5;
    gm_f_vector_parts_crrfopm((void *)&gm_l10, t17, GM_CONV(gm_csguankko4lmhd, t18));
    uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
    if (t19) {
        gm_tlcm4lvu3g1a8 t20 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l10));
        gm_l11 = t20;
    } else {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, ((uint8_t *)t21));
        gm_tl874t2atnlh3 t23 = gm_l3;
        void * t24 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t24;
        goto gm_fret;
    }
    gm_tlcm4lvu3g1a8 t25 = gm_l11;
    gm_l6 = t25;
    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
    gm_l7 = t26;
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
            if (!t28) {
                goto gm_lb2;
            }
            void * t29 = gm_l4;
            gm_t88rcucg1b7uk t30 = gm_l7;
            uint8_t t31 = gm_f_is_signed_integer_s66djm7(t29, GM_CONV(gm_csguankko4lmhd, t30));
            if (t31) {
                gm_l12 = ((uint8_t)1);
            } else {
                void * t32 = gm_l4;
                gm_t88rcucg1b7uk t33 = gm_l7;
                uint8_t t34 = gm_f_is_float_3jk89qu(t32, GM_CONV(gm_csguankko4lmhd, t33));
                gm_l12 = t34;
            }
            uint8_t t35 = gm_l12;
            if (t35) {
                gm_t88rcucg1b7uk t36 = gm_l5;
                void * t37 = gm_l2;
                gm_t9qkh0rsooc8d t38;
                GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
                gm_t8ghgl2jj0qha t39;
                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 0), t38);
                GM_ST(void *, ((uint8_t *)&t39 + 8), t37);
                gm_tl874t2atnlh3 t40 = gm_l3;
                gm_t2b6tm8c0vgo1 t41;
                memset(&t41, 0, sizeof t41);
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t41 + 8), t39);
                void * t42 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t36, &t41, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t42;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)1ULL));
            if (!t44) {
                goto gm_lb3;
            }
            void * t45 = gm_l4;
            gm_t88rcucg1b7uk t46 = gm_l7;
            uint8_t t47 = gm_f_is_float_3jk89qu(t45, GM_CONV(gm_csguankko4lmhd, t46));
            uint8_t t48 = (uint8_t)!t47;
            if (t48) {
                gm_t88rcucg1b7uk t49 = gm_l5;
                void * t50 = gm_l2;
                gm_t9qkh0rsooc8d t51;
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)1ULL));
                gm_t8ghgl2jj0qha t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 0), t51);
                GM_ST(void *, ((uint8_t *)&t52 + 8), t50);
                gm_tl874t2atnlh3 t53 = gm_l3;
                gm_t2b6tm8c0vgo1 t54;
                memset(&t54, 0, sizeof t54);
                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t54 + 8), t52);
                void * t55 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &t54, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t55;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)5ULL));
            if (!t57) {
                goto gm_lb4;
            }
            void * t58 = gm_l4;
            gm_t88rcucg1b7uk t59 = gm_l7;
            uint8_t t60 = gm_f_is_bool_ajhjgeg(t58, GM_CONV(gm_csguankko4lmhd, t59));
            if (t60) {
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sajso5sujsruj));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(46));
                gm_l13 = t61;
            } else {
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(0));
                gm_l13 = t62;
            }
            gm_b16_8 t63 = gm_l13;
            gm_l8 = t63;
            void * t64 = gm_l0;
            gm_tl874t2atnlh3 t65 = gm_l3;
            void * t66 = gm_l0;
            gm_t88rcucg1b7uk t67 = gm_l5;
            gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t69 = gm_l8;
            gm_t6j6smj7qoctu t70;
            GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t68);
            GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
            gm_f_format_s7ci5ul((void *)&gm_l14, (void *)NULL, cx, NULL, &t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src7u9cfdjh7s0n, 139, 28);
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t65, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, ((uint8_t *)t75));
            gm_tl874t2atnlh3 t77 = gm_l3;
            void * t78 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t78;
            goto gm_fret;
        }
gm_lb4:;
        {
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 134, 5);
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t79 = gm_l1;
    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t79) + 0));
    uint8_t t81 = (uint8_t)((t80 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t82;
    GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)0ULL));
    if (t81) {
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(1));
        gm_l15 = t83;
    } else {
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sa831sfid0hf3));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(1));
        gm_l15 = t84;
    }
    gm_b16_8 t85 = gm_l15;
    gm_l9 = t85;
    void * t86 = gm_l0;
    gm_tl874t2atnlh3 t87 = gm_l3;
    gm_b16_8 t88 = gm_l9;
    void * t89 = gm_l0;
    gm_t88rcucg1b7uk t90 = gm_l5;
    gm_b16_8 t91 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t89, t90);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6j6smj7qoctu t92;
    GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t88);
    GM_ST(gm_b16_8, ((uint8_t *)&t92 + 16), t91);
    gm_f_format_ul5ufam((void *)&gm_l16, (void *)NULL, cx, NULL, &t92);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src7u9cfdjh7s0n, 145, 20);
    gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
    gm_b0_1 t96 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t87, t95);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t97 = gm_l0;
    void * t98 = GM_LD(void *, ((uint8_t *)t97));
    gm_tl874t2atnlh3 t99 = gm_l3;
    void * t100 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t98, t99);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t100;
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
