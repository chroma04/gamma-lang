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
typedef struct gm_cs2rc1d61u3qan { uint64_t f0; uint64_t f1; int32_t f2; _Bool f3; } gm_cs2rc1d61u3qan;
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_cseqep6akjq2jh { uint64_t f0; int32_t f1; } gm_cseqep6akjq2jh;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint64_t f0; int32_t f1; uint8_t pe[4]; } gm_t43v1gep754th;
typedef struct { uint32_t a[40]; } gm_t602t587brq3n;
typedef struct { uint64_t f0; uint64_t f1; int32_t f2; uint8_t f3; uint8_t pe[3]; } gm_t88486mk7vrvo;
typedef struct { uint8_t a[1100]; } gm_t91rshbm1iu79;
typedef struct { uint32_t a[37]; } gm_tss0v68n9gqca;
_Static_assert(sizeof(gm_cs2rc1d61u3qan) == 24 && _Alignof(gm_cs2rc1d61u3qan) == 8 && offsetof(gm_cs2rc1d61u3qan, f0) == 0 && offsetof(gm_cs2rc1d61u3qan, f1) == 8 && offsetof(gm_cs2rc1d61u3qan, f2) == 16 && offsetof(gm_cs2rc1d61u3qan, f3) == 20, "layout of `SciDigits`");
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseqep6akjq2jh) == 16 && _Alignof(gm_cseqep6akjq2jh) == 8 && offsetof(gm_cseqep6akjq2jh, f0) == 0 && offsetof(gm_cseqep6akjq2jh, f1) == 8, "layout of `Parts`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t43v1gep754th) == 16 && _Alignof(gm_t43v1gep754th) == 8 && offsetof(gm_t43v1gep754th, f0) == 0 && offsetof(gm_t43v1gep754th, f1) == 8, "layout of `(n: usize, exp: i32)`");
_Static_assert(sizeof(gm_t602t587brq3n) == 160 && _Alignof(gm_t602t587brq3n) == 4 && offsetof(gm_t602t587brq3n, a) == 0, "layout of `[40]u32`");
_Static_assert(sizeof(gm_t88486mk7vrvo) == 24 && _Alignof(gm_t88486mk7vrvo) == 8 && offsetof(gm_t88486mk7vrvo, f0) == 0 && offsetof(gm_t88486mk7vrvo, f1) == 8 && offsetof(gm_t88486mk7vrvo, f2) == 16 && offsetof(gm_t88486mk7vrvo, f3) == 20, "layout of `SciDigits`");
_Static_assert(sizeof(gm_t91rshbm1iu79) == 1100 && _Alignof(gm_t91rshbm1iu79) == 1 && offsetof(gm_t91rshbm1iu79, a) == 0, "layout of `[1100]u8`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tss0v68n9gqca) == 148 && _Alignof(gm_tss0v68n9gqca) == 4 && offsetof(gm_tss0v68n9gqca, a) == 0, "layout of `[37]u32`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
struct gm_fr_check_base_ulm9qq6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_decompose_c1606vm {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_digit_at_gkhhkiq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_digit_char_ie4khce {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_expand_7rpkqt1 {
    gm_frame h;
    gm_t602t587brq3n l5; /* limbs */
    gm_tss0v68n9gqca l24; /* limbs */
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_fixed_buf_len_gor7a4d {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fixed_rppim5r {
    gm_frame h;
    gm_t91rshbm1iu79 l4; /* d */
    gm_b16_8 ret;
};
struct gm_fr_put_u64_ant0p2p {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_round_digits_4thelh5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sci_buf_len_tedn1r4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_sci_digit_sqvlt0a {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sci_digits_0sasrt8 {
    gm_frame h;
    gm_t88486mk7vrvo ret;
};
struct gm_fr_sci_oikoiqs {
    gm_frame h;
    gm_t91rshbm1iu79 l4; /* d */
    gm_b16_8 ret;
};
struct gm_fr_special_meo3u57 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_to_front_b98pl8o {
    gm_frame h;
    gm_b16_8 ret;
};
inline __attribute__((aligned(4))) void gm_f_check_base_ulm9qq6(uint32_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cseqep6akjq2jh gm_f_decompose_c1606vm(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_digit_at_gkhhkiq(gm_b16_8 a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_digit_char_ie4khce(uint8_t a0, uint8_t a1);
__attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_expand_7rpkqt1(uint64_t a0, int32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fixed_rppim5r(double a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_put_u64_ant0p2p(uint64_t a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_round_digits_4thelh5(gm_b16_8 a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0);
__attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a(const void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a_c(gm_cs2rc1d61u3qan a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_sci_digits_0sasrt8(void *gm_out, double a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_cs2rc1d61u3qan gm_f_sci_digits_0sasrt8_c(double a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_sci_oikoiqs(double a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_special_meo3u57(void *gm_out, double a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_special_meo3u57_c(double a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_to_front_b98pl8o(gm_b16_8 a0, uint64_t a1, uint64_t a2);
extern const uint8_t gm_sa954a25u62up[46];
extern const uint8_t gm_scdb74to7b5sl[39];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_sl2sr5fl46c73[48];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcrsgf2gf2b1dq[];
inline __attribute__((aligned(4))) void gm_f_check_base_ulm9qq6(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_check_base_ulm9qq6 F;
    uint32_t gm_l0; /* base */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 < ((uint32_t)2ULL));
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint32_t t4 = gm_l0;
        uint8_t t5 = (t4 > ((uint32_t)36ULL));
        gm_l1 = t5;
    }
    uint8_t t6 = gm_l1;
    if (t6) {
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_scdb74to7b5sl));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(38));
        gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t7) + 8), gm_srcrsgf2gf2b1dq, 94, 32);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t4 = ((t2 == t3));
        if (t4) {
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            { gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5); }
        }
    }
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_digit_at_gkhhkiq(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digit_at_gkhhkiq F;
    gm_b16_8 gm_l0; /* d */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t2 = gm_l2;
    uint64_t t3 = gm_l1;
    uint8_t t4 = (t2 < t3);
    if (t4) {
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t7 = gm_l2;
        if ((uintptr_t)t7 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t7, t6, gm_srcrsgf2gf2b1dq, 432, 23);
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)t7 * 1));
        F.ret = t8;
        goto gm_fret;
    }
    F.ret = ((uint8_t)0ULL);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_digit_char_ie4khce(uint8_t a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digit_char_ie4khce F;
    uint8_t gm_l0; /* d */
    uint8_t gm_l1; /* upper */
    GM_STACK_CHECK_SHALLOW(&F, 14u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 < ((uint8_t)10ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t r5; bool o6 = __builtin_add_overflow(((uint8_t)48ULL), t4, &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 39, 24);
        uint8_t t7 = r5;
        F.ret = t7;
        goto gm_fret;
    }
    uint8_t t8 = gm_l1;
    if (t8) {
        gm_l2 = ((uint8_t)65ULL);
    } else {
        gm_l2 = ((uint8_t)97ULL);
    }
    uint8_t t9 = gm_l2;
    uint8_t t10 = gm_l0;
    uint8_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint8_t)10ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 40, 47);
    uint8_t t13 = r11;
    uint8_t r14; bool o15 = __builtin_add_overflow(t9, t13, &r14);
    if (o15) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 40, 12);
    uint8_t t16 = r14;
    F.ret = t16;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_buf_len_gor7a4d F;
    uint32_t gm_l0; /* decimals */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)312ULL), &r4);
    if (o5) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 447, 52);
    uint64_t t6 = r4;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_buf_len_tedn1r4 F;
    uint32_t gm_l0; /* digits */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 451, 48);
    uint64_t t6 = r4;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_check_base_ulm9qq6(uint32_t a0);
inline __attribute__((aligned(4))) gm_cseqep6akjq2jh gm_f_decompose_c1606vm(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decompose_c1606vm F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* bits */
    uint64_t gm_l2; /* be */
    uint64_t gm_l3; /* frac */
    GM_STACK_CHECK_SHALLOW(&F, 209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t2 = gm_l0;
    uint64_t t3;
    memset(&t3, 0, sizeof t3); memcpy(&t3, ((uint8_t *)&t2), sizeof t3 < 8 ? sizeof t3 : 8);
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    unsigned __int128 n5 = (unsigned __int128)((uint64_t)52ULL);
    if (n5 >= 64) gm_trap_shift((uint64_t)n5, "u64", gm_srcrsgf2gf2b1dq, 385, 12);
    uint64_t t6 = (uint64_t)(t4 >> (int)n5);
    uint64_t t7 = (uint64_t)(t6 & ((uint64_t)2047ULL));
    gm_l2 = t7;
    uint64_t t8 = gm_l1;
    uint64_t t9 = (uint64_t)(t8 & ((uint64_t)4503599627370495ULL));
    gm_l3 = t9;
    uint64_t t10 = gm_l2;
    uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
    if (t11) {
        uint64_t t12 = gm_l3;
        gm_t43v1gep754th t13;
        GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t13 + 0), t12);
        GM_ST(int32_t, ((uint8_t *)&t13 + 8), ((int32_t)-1074LL));
        F.ret = t13;
        goto gm_fret;
    }
    uint64_t t14 = gm_l3;
    unsigned __int128 n15 = (unsigned __int128)((uint64_t)52ULL);
    if (n15 >= 64) gm_trap_shift((uint64_t)n15, "u64", gm_srcrsgf2gf2b1dq, 388, 29);
    uint64_t r16 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n15);
    bool o17 = (uint64_t)(r16 >> (int)n15) != ((uint64_t)1ULL);
    if (o17) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 388, 29);
    uint64_t t18 = r16;
    uint64_t t19 = (uint64_t)(t14 | t18);
    uint64_t t20 = gm_l2;
    int32_t t21 = (int32_t)t20;
    int32_t r22; bool o23 = __builtin_sub_overflow(t21, ((int32_t)1075LL), &r22);
    if (o23) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 388, 42);
    int32_t t24 = r22;
    gm_t43v1gep754th t25;
    GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t19);
    GM_ST(int32_t, ((uint8_t *)&t25 + 8), t24);
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseqep6akjq2jh, F.ret);
gm_fprop:
    return GM_CONV(gm_cseqep6akjq2jh, F.ret);
}
extern __attribute__((aligned(4))) uint8_t gm_f_digit_at_gkhhkiq(gm_b16_8 a0, uint64_t a1, uint64_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_digit_char_ie4khce(uint8_t a0, uint8_t a1);
__attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_expand_7rpkqt1(uint64_t a0, int32_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_expand_7rpkqt1 F;
    uint64_t gm_l0; /* m0 */
    int32_t gm_l1; /* e0 */
    gm_b16_8 gm_l2; /* d */
    uint64_t gm_l3; /* m */
    int32_t gm_l4; /* e */
    uint64_t gm_l6; /* q */
    uint32_t gm_l7; /* s */
    uint64_t gm_l8; /* w0 */
    uint64_t gm_l9; /* w1 */
    uint64_t gm_l10; /* top */
    uint64_t gm_l11; /* count */
    uint64_t gm_l12; /* rem */
    uint64_t gm_l13; /* i */
    uint64_t gm_l14; /* cur */
    uint64_t gm_l15; /* k */
    uint64_t gm_l16; /* i */
    uint64_t gm_l17; /* j */
    uint8_t gm_l18; /* c */
    uint32_t gm_l19; /* k */
    uint64_t gm_l20; /* point */
    uint64_t gm_l21; /* n */
    uint64_t gm_l22; /* q */
    uint32_t gm_l23; /* s */
    uint64_t gm_l25; /* frac */
    uint64_t gm_l26; /* lowest */
    uint64_t gm_l27; /* carry */
    uint64_t gm_l28; /* i */
    uint64_t gm_l29; /* cur */
    uint64_t gm_l30; /* window */
    uint8_t gm_l31; /* any */
    GM_STACK_CHECK_SHALLOW(&F, 2008u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l32;
    uint64_t gm_l33;
    {
        uint64_t t29 = gm_l0;
        gm_l3 = t29;
        int32_t t30 = gm_l1;
        gm_l4 = t30;
        uint64_t t31 = gm_l3;
        uint8_t t32 = ((t31 == ((uint64_t)0ULL)));
        if (t32) {
            gm_t2lqu8jsj511d t33;
            GM_ST(uint64_t, ((uint8_t *)&t33 + 0), ((uint64_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t33 + 8), ((uint64_t)0ULL));
            F.ret = t33;
            goto gm_fret;
        }
        {
            for (;;) {
                int32_t t34 = gm_l4;
                uint8_t t35 = (t34 < ((int32_t)0LL));
                if (!t35) {
                    goto gm_lb1;
                }
                uint64_t t36 = gm_l3;
                if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 290, 21);
                uint64_t t37 = (uint64_t)(t36 % ((uint64_t)2ULL));
                uint8_t t38 = ((t37 == ((uint64_t)0ULL)));
                if (!t38) {
                    goto gm_lb1;
                }
                {
                    uint64_t t39 = gm_l3;
                    if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 291, 9);
                    uint64_t t40 = (uint64_t)(t39 / ((uint64_t)2ULL));
                    gm_l3 = t40;
                    int32_t t41 = gm_l4;
                    int32_t r42; bool o43 = __builtin_add_overflow(t41, ((int32_t)1LL), &r42);
                    if (o43) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 292, 9);
                    int32_t t44 = r42;
                    gm_l4 = t44;
                }
gm_lb3:;
            }
        }
gm_lb1:;
        int32_t t45 = gm_l4;
        uint8_t t46 = (t45 >= ((int32_t)0LL));
        if (t46) {
            gm_t602t587brq3n t47;
            for (size_t i48 = 0; i48 < 40; i48++) GM_ST(uint32_t, (uint8_t *)&t47 + i48 * 4, ((uint32_t)0ULL));
            F.l5 = t47;
            int32_t t49 = gm_l4;
            if (((int32_t)32LL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 297, 15);
            if (t49 == ((int32_t)-2147483648LL) && ((int32_t)32LL) == (int32_t)-1) gm_trap("integer overflow in `/`", gm_srcrsgf2gf2b1dq, 297, 15);
            int32_t t50 = (int32_t)(t49 / ((int32_t)32LL));
            uint64_t t51 = (uint64_t)t50;
            gm_l6 = t51;
            int32_t t52 = gm_l4;
            if (((int32_t)32LL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 298, 15);
            if (t52 == ((int32_t)-2147483648LL) && ((int32_t)32LL) == (int32_t)-1) gm_trap("integer overflow in `%`", gm_srcrsgf2gf2b1dq, 298, 15);
            int32_t t53 = (int32_t)(t52 % ((int32_t)32LL));
            uint32_t t54 = (uint32_t)t53;
            gm_l7 = t54;
            uint64_t t55 = gm_l3;
            uint64_t t56 = (uint64_t)(t55 & ((uint64_t)4294967295ULL));
            uint32_t t57 = gm_l7;
            unsigned __int128 n58 = (unsigned __int128)t57;
            if (n58 >= 64) gm_trap_shift((uint64_t)n58, "u64", gm_srcrsgf2gf2b1dq, 299, 15);
            uint64_t r59 = (uint64_t)((uint64_t)t56 << (int)n58);
            bool o60 = (uint64_t)(r59 >> (int)n58) != t56;
            if (o60) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 299, 15);
            uint64_t t61 = r59;
            gm_l8 = t61;
            uint64_t t62 = gm_l6;
            if ((uintptr_t)t62 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t62, ((uint64_t)40ULL), gm_srcrsgf2gf2b1dq, 300, 9);
            uint64_t t63 = gm_l8;
            uint64_t t64 = (uint64_t)(t63 & ((uint64_t)4294967295ULL));
            uint32_t t65 = (uint32_t)t64;
            GM_ST(uint32_t, (((uint8_t *)&F.l5) + (uintptr_t)t62 * 4), t65);
            uint64_t t66 = gm_l3;
            unsigned __int128 n67 = (unsigned __int128)((uint64_t)32ULL);
            if (n67 >= 64) gm_trap_shift((uint64_t)n67, "u64", gm_srcrsgf2gf2b1dq, 301, 17);
            uint64_t t68 = (uint64_t)(t66 >> (int)n67);
            uint32_t t69 = gm_l7;
            unsigned __int128 n70 = (unsigned __int128)t69;
            if (n70 >= 64) gm_trap_shift((uint64_t)n70, "u64", gm_srcrsgf2gf2b1dq, 301, 16);
            uint64_t r71 = (uint64_t)((uint64_t)t68 << (int)n70);
            bool o72 = (uint64_t)(r71 >> (int)n70) != t68;
            if (o72) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 301, 16);
            uint64_t t73 = r71;
            uint64_t t74 = gm_l8;
            unsigned __int128 n75 = (unsigned __int128)((uint64_t)32ULL);
            if (n75 >= 64) gm_trap_shift((uint64_t)n75, "u64", gm_srcrsgf2gf2b1dq, 301, 35);
            uint64_t t76 = (uint64_t)(t74 >> (int)n75);
            uint64_t r77; bool o78 = __builtin_add_overflow(t73, t76, &r77);
            if (o78) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 301, 15);
            uint64_t t79 = r77;
            gm_l9 = t79;
            uint64_t t80 = gm_l6;
            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
            if (o82) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 302, 15);
            uint64_t t83 = r81;
            if ((uintptr_t)t83 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t83, ((uint64_t)40ULL), gm_srcrsgf2gf2b1dq, 302, 9);
            uint64_t t84 = gm_l9;
            uint64_t t85 = (uint64_t)(t84 & ((uint64_t)4294967295ULL));
            uint32_t t86 = (uint32_t)t85;
            GM_ST(uint32_t, (((uint8_t *)&F.l5) + (uintptr_t)t83 * 4), t86);
            uint64_t t87 = gm_l6;
            uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)2ULL), &r88);
            if (o89) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 303, 15);
            uint64_t t90 = r88;
            if ((uintptr_t)t90 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t90, ((uint64_t)40ULL), gm_srcrsgf2gf2b1dq, 303, 9);
            uint64_t t91 = gm_l9;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)32ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcrsgf2gf2b1dq, 303, 25);
            uint64_t t93 = (uint64_t)(t91 >> (int)n92);
            uint32_t t94 = (uint32_t)t93;
            GM_ST(uint32_t, (((uint8_t *)&F.l5) + (uintptr_t)t90 * 4), t94);
            uint64_t t95 = gm_l6;
            uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)3ULL), &r96);
            if (o97) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 304, 16);
            uint64_t t98 = r96;
            gm_l10 = t98;
            gm_l11 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t99 = gm_l10;
                    uint8_t t100 = (t99 > ((uint64_t)0ULL));
                    if (!t100) {
                        goto gm_lb4;
                    }
                    {
                        gm_l12 = ((uint64_t)0ULL);
                        uint64_t t101 = gm_l10;
                        gm_l13 = t101;
                        {
                            for (;;) {
                                uint64_t t102 = gm_l13;
                                uint8_t t103 = (t102 > ((uint64_t)0ULL));
                                if (!t103) {
                                    goto gm_lb7;
                                }
                                {
                                    uint64_t t104 = gm_l13;
                                    uint64_t r105; bool o106 = __builtin_sub_overflow(t104, ((uint64_t)1ULL), &r105);
                                    if (o106) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 310, 17);
                                    uint64_t t107 = r105;
                                    gm_l13 = t107;
                                    uint64_t t108 = gm_l12;
                                    unsigned __int128 n109 = (unsigned __int128)((uint64_t)32ULL);
                                    if (n109 >= 64) gm_trap_shift((uint64_t)n109, "u64", gm_srcrsgf2gf2b1dq, 311, 25);
                                    uint64_t r110 = (uint64_t)((uint64_t)t108 << (int)n109);
                                    bool o111 = (uint64_t)(r110 >> (int)n109) != t108;
                                    if (o111) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 311, 25);
                                    uint64_t t112 = r110;
                                    uint64_t t113 = gm_l13;
                                    if ((uintptr_t)t113 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t113, ((uint64_t)40ULL), gm_srcrsgf2gf2b1dq, 311, 42);
                                    uint32_t t114 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + (uintptr_t)t113 * 4));
                                    uint64_t t115 = (uint64_t)t114;
                                    uint64_t t116 = (uint64_t)(t112 | t115);
                                    gm_l14 = t116;
                                    uint64_t t117 = gm_l13;
                                    if ((uintptr_t)t117 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t117, ((uint64_t)40ULL), gm_srcrsgf2gf2b1dq, 312, 17);
                                    uint64_t t118 = gm_l14;
                                    if (((uint64_t)1000000000ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 312, 29);
                                    uint64_t t119 = (uint64_t)(t118 / ((uint64_t)1000000000ULL));
                                    uint32_t t120 = (uint32_t)t119;
                                    GM_ST(uint32_t, (((uint8_t *)&F.l5) + (uintptr_t)t117 * 4), t120);
                                    uint64_t t121 = gm_l14;
                                    if (((uint64_t)1000000000ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 313, 23);
                                    uint64_t t122 = (uint64_t)(t121 % ((uint64_t)1000000000ULL));
                                    gm_l12 = t122;
                                }
gm_lb9:;
                            }
                        }
gm_lb7:;
                        {
                            for (;;) {
                                uint64_t t123 = gm_l10;
                                uint8_t t124 = (t123 > ((uint64_t)0ULL));
                                if (!t124) {
                                    goto gm_lb10;
                                }
                                uint64_t t125 = gm_l10;
                                uint64_t r126; bool o127 = __builtin_sub_overflow(t125, ((uint64_t)1ULL), &r126);
                                if (o127) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 315, 37);
                                uint64_t t128 = r126;
                                if ((uintptr_t)t128 >= (uintptr_t)(((uint64_t)40ULL))) gm_trap_index((uintptr_t)t128, ((uint64_t)40ULL), gm_srcrsgf2gf2b1dq, 315, 31);
                                uint32_t t129 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + (uintptr_t)t128 * 4));
                                uint8_t t130 = ((t129 == ((uint32_t)0ULL)));
                                if (!t130) {
                                    goto gm_lb10;
                                }
                                {
                                    uint64_t t131 = gm_l10;
                                    uint64_t r132; bool o133 = __builtin_sub_overflow(t131, ((uint64_t)1ULL), &r132);
                                    if (o133) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 315, 53);
                                    uint64_t t134 = r132;
                                    gm_l10 = t134;
                                }
gm_lb12:;
                            }
                        }
gm_lb10:;
                        gm_l15 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t135 = gm_l15;
                                uint8_t t136 = (t135 < ((uint64_t)9ULL));
                                if (!t136) {
                                    goto gm_lb13;
                                }
                                uint64_t t137 = gm_l10;
                                uint8_t t138 = (t137 > ((uint64_t)0ULL));
                                if (t138) {
                                    gm_l32 = ((uint8_t)1);
                                } else {
                                    uint64_t t139 = gm_l12;
                                    uint8_t t140 = (!(t139 == ((uint64_t)0ULL)));
                                    gm_l32 = t140;
                                }
                                uint8_t t141 = gm_l32;
                                if (!t141) {
                                    goto gm_lb13;
                                }
                                {
                                    void * t142 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                    uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                    uint64_t t144 = gm_l11;
                                    if ((uintptr_t)t144 >= (uintptr_t)(t143)) gm_trap_index((uintptr_t)t144, t143, gm_srcrsgf2gf2b1dq, 319, 17);
                                    uint64_t t145 = gm_l12;
                                    if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 319, 29);
                                    uint64_t t146 = (uint64_t)(t145 % ((uint64_t)10ULL));
                                    uint8_t t147 = (uint8_t)t146;
                                    GM_ST(uint8_t, (((uint8_t *)t142) + (uintptr_t)t144 * 1), t147);
                                    uint64_t t148 = gm_l12;
                                    if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 320, 17);
                                    uint64_t t149 = (uint64_t)(t148 / ((uint64_t)10ULL));
                                    gm_l12 = t149;
                                    uint64_t t150 = gm_l11;
                                    uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                                    if (o152) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 321, 17);
                                    uint64_t t153 = r151;
                                    gm_l11 = t153;
                                    uint64_t t154 = gm_l15;
                                    uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                                    if (o156) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 322, 17);
                                    uint64_t t157 = r155;
                                    gm_l15 = t157;
                                }
gm_lb15:;
                            }
                        }
gm_lb13:;
                    }
gm_lb6:;
                }
            }
gm_lb4:;
            gm_l16 = ((uint64_t)0ULL);
            uint64_t t158 = gm_l11;
            gm_l17 = t158;
            {
                for (;;) {
                    uint64_t t159 = gm_l16;
                    uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                    if (o161) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 327, 15);
                    uint64_t t162 = r160;
                    uint64_t t163 = gm_l17;
                    uint8_t t164 = (t162 < t163);
                    if (!t164) {
                        goto gm_lb16;
                    }
                    {
                        uint64_t t165 = gm_l17;
                        uint64_t r166; bool o167 = __builtin_sub_overflow(t165, ((uint64_t)1ULL), &r166);
                        if (o167) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 328, 13);
                        uint64_t t168 = r166;
                        gm_l17 = t168;
                        void * t169 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t171 = gm_l16;
                        if ((uintptr_t)t171 >= (uintptr_t)(t170)) gm_trap_index((uintptr_t)t171, t170, gm_srcrsgf2gf2b1dq, 329, 18);
                        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)t169) + (uintptr_t)t171 * 1));
                        gm_l18 = t172;
                        void * t173 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t175 = gm_l16;
                        if ((uintptr_t)t175 >= (uintptr_t)(t174)) gm_trap_index((uintptr_t)t175, t174, gm_srcrsgf2gf2b1dq, 330, 13);
                        void * t176 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t178 = gm_l17;
                        if ((uintptr_t)t178 >= (uintptr_t)(t177)) gm_trap_index((uintptr_t)t178, t177, gm_srcrsgf2gf2b1dq, 330, 20);
                        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)t176) + (uintptr_t)t178 * 1));
                        GM_ST(uint8_t, (((uint8_t *)t173) + (uintptr_t)t175 * 1), t179);
                        void * t180 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t182 = gm_l17;
                        if ((uintptr_t)t182 >= (uintptr_t)(t181)) gm_trap_index((uintptr_t)t182, t181, gm_srcrsgf2gf2b1dq, 331, 13);
                        uint8_t t183 = gm_l18;
                        GM_ST(uint8_t, (((uint8_t *)t180) + (uintptr_t)t182 * 1), t183);
                        uint64_t t184 = gm_l16;
                        uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                        if (o186) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 332, 13);
                        uint64_t t187 = r185;
                        gm_l16 = t187;
                    }
gm_lb18:;
                }
            }
gm_lb16:;
            uint64_t t188 = gm_l11;
            uint64_t t189 = gm_l11;
            gm_t2lqu8jsj511d t190;
            GM_ST(uint64_t, ((uint8_t *)&t190 + 0), t188);
            GM_ST(uint64_t, ((uint8_t *)&t190 + 8), t189);
            F.ret = t190;
            goto gm_fret;
        }
    }
    {
        int32_t t191 = gm_l4;
        int32_t r192; bool o193 = __builtin_sub_overflow(((int32_t)0LL), t191, &r192);
        if (o193) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 337, 11);
        int32_t t194 = r192;
        uint32_t t195 = (uint32_t)t194;
        gm_l19 = t195;
        gm_l20 = ((uint64_t)0ULL);
        uint32_t t196 = gm_l19;
        uint8_t t197 = (t196 < ((uint32_t)64ULL));
        if (t197) {
            uint64_t t198 = gm_l3;
            uint32_t t199 = gm_l19;
            unsigned __int128 n200 = (unsigned __int128)t199;
            if (n200 >= 64) gm_trap_shift((uint64_t)n200, "u64", gm_srcrsgf2gf2b1dq, 339, 33);
            uint64_t t201 = (uint64_t)(t198 >> (int)n200);
            gm_b16_8 t202 = gm_l2;
            uint64_t t203 = gm_f_put_u64_ant0p2p(t201, t202, ((uint64_t)0ULL));
            gm_l20 = t203;
        }
        uint64_t t204 = gm_l20;
        gm_l21 = t204;
        uint32_t t205 = gm_l19;
        if (((uint32_t)32ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 343, 11);
        uint32_t t206 = (uint32_t)(t205 / ((uint32_t)32ULL));
        uint64_t t207 = (uint64_t)t206;
        gm_l22 = t207;
        uint32_t t208 = gm_l19;
        if (((uint32_t)32ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 344, 10);
        uint32_t t209 = (uint32_t)(t208 % ((uint32_t)32ULL));
        gm_l23 = t209;
        gm_tss0v68n9gqca t210;
        for (size_t i211 = 0; i211 < 37; i211++) GM_ST(uint32_t, (uint8_t *)&t210 + i211 * 4, ((uint32_t)0ULL));
        F.l24 = t210;
        uint32_t t212 = gm_l19;
        uint8_t t213 = (t212 >= ((uint32_t)64ULL));
        if (t213) {
            uint64_t t214 = gm_l3;
            gm_l33 = t214;
        } else {
            uint64_t t215 = gm_l3;
            uint32_t t216 = gm_l19;
            unsigned __int128 n217 = (unsigned __int128)t216;
            if (n217 >= 64) gm_trap_shift((uint64_t)n217, "u64", gm_srcrsgf2gf2b1dq, 346, 43);
            uint64_t r218 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n217);
            bool o219 = (uint64_t)(r218 >> (int)n217) != ((uint64_t)1ULL);
            if (o219) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 346, 43);
            uint64_t t220 = r218;
            uint64_t r221; bool o222 = __builtin_sub_overflow(t220, ((uint64_t)1ULL), &r221);
            if (o222) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 346, 42);
            uint64_t t223 = r221;
            uint64_t t224 = (uint64_t)(t215 & t223);
            gm_l33 = t224;
        }
        uint64_t t225 = gm_l33;
        gm_l25 = t225;
        uint64_t t226 = gm_l25;
        uint64_t t227 = (uint64_t)(t226 & ((uint64_t)4294967295ULL));
        uint32_t t228 = (uint32_t)t227;
        GM_ST(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)((uint64_t)0ULL) * 4), t228);
        uint64_t t229 = gm_l25;
        unsigned __int128 n230 = (unsigned __int128)((uint64_t)32ULL);
        if (n230 >= 64) gm_trap_shift((uint64_t)n230, "u64", gm_srcrsgf2gf2b1dq, 348, 17);
        uint64_t t231 = (uint64_t)(t229 >> (int)n230);
        uint32_t t232 = (uint32_t)t231;
        GM_ST(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)((uint64_t)1ULL) * 4), t232);
        gm_l26 = ((uint64_t)0ULL);
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb19;
                }
                {
                    gm_l27 = ((uint64_t)0ULL);
                    uint64_t t233 = gm_l26;
                    gm_l28 = t233;
                    {
                        for (;;) {
                            uint64_t t234 = gm_l28;
                            uint64_t t235 = gm_l22;
                            uint8_t t236 = (t234 <= t235);
                            if (!t236) {
                                goto gm_lb22;
                            }
                            {
                                uint64_t t237 = gm_l28;
                                if ((uintptr_t)t237 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t237, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 354, 24);
                                uint32_t t238 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t237 * 4));
                                uint64_t t239 = (uint64_t)t238;
                                uint64_t r240; bool o241 = __builtin_mul_overflow(t239, ((uint64_t)10ULL), &r240);
                                if (o241) gm_trap("integer overflow in `*`", gm_srcrsgf2gf2b1dq, 354, 20);
                                uint64_t t242 = r240;
                                uint64_t t243 = gm_l27;
                                uint64_t r244; bool o245 = __builtin_add_overflow(t242, t243, &r244);
                                if (o245) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 354, 20);
                                uint64_t t246 = r244;
                                gm_l29 = t246;
                                uint64_t t247 = gm_l28;
                                if ((uintptr_t)t247 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t247, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 355, 13);
                                uint64_t t248 = gm_l29;
                                uint64_t t249 = (uint64_t)(t248 & ((uint64_t)4294967295ULL));
                                uint32_t t250 = (uint32_t)t249;
                                GM_ST(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t247 * 4), t250);
                                uint64_t t251 = gm_l29;
                                unsigned __int128 n252 = (unsigned __int128)((uint64_t)32ULL);
                                if (n252 >= 64) gm_trap_shift((uint64_t)n252, "u64", gm_srcrsgf2gf2b1dq, 356, 21);
                                uint64_t t253 = (uint64_t)(t251 >> (int)n252);
                                gm_l27 = t253;
                                uint64_t t254 = gm_l28;
                                uint64_t r255; bool o256 = __builtin_add_overflow(t254, ((uint64_t)1ULL), &r255);
                                if (o256) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 357, 13);
                                uint64_t t257 = r255;
                                gm_l28 = t257;
                            }
gm_lb24:;
                        }
                    }
gm_lb22:;
                    uint64_t t258 = gm_l22;
                    uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                    if (o260) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 359, 15);
                    uint64_t t261 = r259;
                    if ((uintptr_t)t261 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t261, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 359, 9);
                    uint64_t t262 = gm_l27;
                    uint32_t t263 = (uint32_t)t262;
                    GM_ST(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t261 * 4), t263);
                    uint64_t t264 = gm_l22;
                    if ((uintptr_t)t264 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t264, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 360, 23);
                    uint32_t t265 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t264 * 4));
                    uint64_t t266 = (uint64_t)t265;
                    uint64_t t267 = gm_l22;
                    uint64_t r268; bool o269 = __builtin_add_overflow(t267, ((uint64_t)1ULL), &r268);
                    if (o269) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 360, 46);
                    uint64_t t270 = r268;
                    if ((uintptr_t)t270 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t270, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 360, 40);
                    uint32_t t271 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t270 * 4));
                    uint64_t t272 = (uint64_t)t271;
                    unsigned __int128 n273 = (unsigned __int128)((uint64_t)32ULL);
                    if (n273 >= 64) gm_trap_shift((uint64_t)n273, "u64", gm_srcrsgf2gf2b1dq, 360, 36);
                    uint64_t r274 = (uint64_t)((uint64_t)t272 << (int)n273);
                    bool o275 = (uint64_t)(r274 >> (int)n273) != t272;
                    if (o275) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 360, 36);
                    uint64_t t276 = r274;
                    uint64_t t277 = (uint64_t)(t266 | t276);
                    gm_l30 = t277;
                    void * t278 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t280 = gm_l21;
                    if ((uintptr_t)t280 >= (uintptr_t)(t279)) gm_trap_index((uintptr_t)t280, t279, gm_srcrsgf2gf2b1dq, 361, 9);
                    uint64_t t281 = gm_l30;
                    uint32_t t282 = gm_l23;
                    unsigned __int128 n283 = (unsigned __int128)t282;
                    if (n283 >= 64) gm_trap_shift((uint64_t)n283, "u64", gm_srcrsgf2gf2b1dq, 361, 17);
                    uint64_t t284 = (uint64_t)(t281 >> (int)n283);
                    uint8_t t285 = (uint8_t)t284;
                    GM_ST(uint8_t, (((uint8_t *)t278) + (uintptr_t)t280 * 1), t285);
                    uint64_t t286 = gm_l21;
                    uint64_t r287; bool o288 = __builtin_add_overflow(t286, ((uint64_t)1ULL), &r287);
                    if (o288) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 362, 9);
                    uint64_t t289 = r287;
                    gm_l21 = t289;
                    uint64_t t290 = gm_l22;
                    if ((uintptr_t)t290 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t290, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 363, 9);
                    uint64_t t291 = gm_l22;
                    if ((uintptr_t)t291 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t291, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 363, 20);
                    uint32_t t292 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t291 * 4));
                    uint32_t t293 = gm_l23;
                    unsigned __int128 n294 = (unsigned __int128)t293;
                    if (n294 >= 32) gm_trap_shift((uint64_t)n294, "u32", gm_srcrsgf2gf2b1dq, 363, 33);
                    uint32_t r295 = (uint32_t)((uint32_t)((uint32_t)1ULL) << (int)n294);
                    bool o296 = (uint32_t)(r295 >> (int)n294) != ((uint32_t)1ULL);
                    if (o296) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 363, 33);
                    uint32_t t297 = r295;
                    uint32_t r298; bool o299 = __builtin_sub_overflow(t297, ((uint32_t)1ULL), &r298);
                    if (o299) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 363, 32);
                    uint32_t t300 = r298;
                    uint32_t t301 = (uint32_t)(t292 & t300);
                    GM_ST(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t290 * 4), t301);
                    uint64_t t302 = gm_l22;
                    uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                    if (o304) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 364, 15);
                    uint64_t t305 = r303;
                    if ((uintptr_t)t305 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t305, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 364, 9);
                    GM_ST(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t305 * 4), ((uint32_t)0ULL));
                    gm_l31 = ((uint8_t)0);
                    uint64_t t306 = gm_l26;
                    gm_l28 = t306;
                    {
                        for (;;) {
                            uint64_t t307 = gm_l28;
                            uint64_t t308 = gm_l22;
                            uint8_t t309 = (t307 <= t308);
                            if (!t309) {
                                goto gm_lb25;
                            }
                            {
                                uint64_t t310 = gm_l28;
                                if ((uintptr_t)t310 >= (uintptr_t)(((uint64_t)37ULL))) gm_trap_index((uintptr_t)t310, ((uint64_t)37ULL), gm_srcrsgf2gf2b1dq, 369, 16);
                                uint32_t t311 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + (uintptr_t)t310 * 4));
                                uint8_t t312 = (!(t311 == ((uint32_t)0ULL)));
                                if (t312) {
                                    gm_l31 = ((uint8_t)1);
                                    goto gm_lb25;
                                }
                                uint64_t t313 = gm_l28;
                                uint64_t r314; bool o315 = __builtin_add_overflow(t313, ((uint64_t)1ULL), &r314);
                                if (o315) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 373, 13);
                                uint64_t t316 = r314;
                                gm_l28 = t316;
                            }
gm_lb27:;
                        }
                    }
gm_lb25:;
                    uint8_t t317 = gm_l31;
                    uint8_t t318 = (uint8_t)!t317;
                    if (t318) {
                        goto gm_lb19;
                    }
                    uint64_t t319 = gm_l21;
                    uint64_t t320 = gm_l20;
                    uint64_t r321; bool o322 = __builtin_sub_overflow(t319, t320, &r321);
                    if (o322) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 377, 19);
                    uint64_t t323 = r321;
                    if (((uint64_t)32ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 377, 18);
                    uint64_t t324 = (uint64_t)(t323 / ((uint64_t)32ULL));
                    gm_l26 = t324;
                }
gm_lb21:;
            }
        }
gm_lb19:;
    }
    {
        uint64_t t325 = gm_l21;
        uint64_t t326 = gm_l20;
        gm_t2lqu8jsj511d t327;
        GM_ST(uint64_t, ((uint8_t *)&t327 + 0), t325);
        GM_ST(uint64_t, ((uint8_t *)&t327 + 8), t326);
        F.ret = t327;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
extern __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fixed_rppim5r(double a0, uint32_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_rppim5r F;
    double gm_l0; /* x */
    uint32_t gm_l1; /* decimals */
    gm_b16_8 gm_l2; /* buf */
    gm_b16_8 gm_l3; /* sp */
    gm_t43v1gep754th gm_l5; /* parts */
    gm_t2lqu8jsj511d gm_l6; /* ex */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* point */
    uint64_t gm_l9; /* places */
    uint8_t gm_l10; /* carry */
    uint8_t gm_l11; /* neg */
    uint64_t gm_l12; /* int_digits */
    uint64_t gm_l13; /* total */
    uint64_t gm_l14; /* out */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 2318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l21;
    uint64_t gm_l22;
    uint64_t gm_l23;
    {
        {
            double t16 = gm_l0;
            gm_b16_8 t17 = gm_l2;
            gm_f_special_meo3u57((void *)&gm_l21, t16, t17);
            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
            if (!t18) {
                goto gm_lb2;
            }
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
            gm_l3 = t19;
            gm_b16_8 t20 = gm_l3;
            F.ret = t20;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_t91rshbm1iu79 t21;
    for (size_t i22 = 0; i22 < 1100; i22++) GM_ST(uint8_t, (uint8_t *)&t21 + i22 * 1, ((uint8_t)0ULL));
    F.l4 = t21;
    double t23 = gm_l0;
    gm_t43v1gep754th t24 = GM_CONV(gm_t43v1gep754th, gm_f_decompose_c1606vm(t23));
    gm_l5 = t24;
    uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    int32_t t26 = GM_LD(int32_t, (((uint8_t *)&gm_l5) + 8));
    void * t27 = (void *)((uint8_t *)&F.l4);
    uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t29 = (uintptr_t)((uint64_t)1100ULL);
    if (t28 > t29 || t29 > (uintptr_t)((uint64_t)1100ULL)) gm_trap_slice(t28, t29, ((uint64_t)1100ULL), gm_srcrsgf2gf2b1dq, 469, 36);
    uint64_t t30 = ((uintptr_t)((uint64_t)1100ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t27 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30));
    gm_t2lqu8jsj511d t32 = GM_CONV(gm_t2lqu8jsj511d, gm_f_expand_7rpkqt1(t25, t26, t31));
    gm_l6 = t32;
    uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
    gm_l7 = t33;
    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
    gm_l8 = t34;
    uint32_t t35 = gm_l1;
    uint64_t t36 = (uint64_t)t35;
    gm_l9 = t36;
    void * t37 = (void *)((uint8_t *)&F.l4);
    uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t39 = (uintptr_t)((uint64_t)1100ULL);
    if (t38 > t39 || t39 > (uintptr_t)((uint64_t)1100ULL)) gm_trap_slice(t38, t39, ((uint64_t)1100ULL), gm_srcrsgf2gf2b1dq, 473, 27);
    uint64_t t40 = ((uintptr_t)((uint64_t)1100ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40));
    uint64_t t42 = gm_l7;
    uint64_t t43 = gm_l8;
    uint64_t t44 = gm_l9;
    uint64_t r45; bool o46 = __builtin_add_overflow(t43, t44, &r45);
    if (o46) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 473, 37);
    uint64_t t47 = r45;
    uint8_t t48 = gm_f_round_digits_4thelh5(t41, t42, t47);
    gm_l10 = t48;
    double t49 = gm_l0;
    uint8_t t50 = (uint8_t)(__builtin_signbit(t49) != 0);
    gm_l11 = t50;
    uint8_t t51 = gm_l10;
    if (t51) {
        uint64_t t52 = gm_l8;
        uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
        if (o54) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 475, 30);
        uint64_t t55 = r53;
        gm_l22 = t55;
    } else {
        uint64_t t56 = gm_l8;
        uint8_t t57 = ((t56 == ((uint64_t)0ULL)));
        if (t57) {
            gm_l23 = ((uint64_t)1ULL);
        } else {
            uint64_t t58 = gm_l8;
            gm_l23 = t58;
        }
        uint64_t t59 = gm_l23;
        gm_l22 = t59;
    }
    uint64_t t60 = gm_l22;
    gm_l12 = t60;
    uint64_t t61 = gm_l12;
    gm_l13 = t61;
    uint8_t t62 = gm_l11;
    if (t62) {
        uint64_t t63 = gm_l13;
        uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
        if (o65) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 477, 14);
        uint64_t t66 = r64;
        gm_l13 = t66;
    }
    uint64_t t67 = gm_l9;
    uint8_t t68 = (t67 > ((uint64_t)0ULL));
    if (t68) {
        uint64_t t69 = gm_l13;
        uint64_t t70 = gm_l9;
        uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
        if (o72) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 478, 30);
        uint64_t t73 = r71;
        uint64_t r74; bool o75 = __builtin_add_overflow(t69, t73, &r74);
        if (o75) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 478, 21);
        uint64_t t76 = r74;
        gm_l13 = t76;
    }
    uint64_t t77 = gm_l13;
    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t79 = (t77 > t78);
    if (t79) {
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sa954a25u62up));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(45));
        gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t80)), GM_LD(uintptr_t, ((uint8_t *)&t80) + 8), gm_srcrsgf2gf2b1dq, 479, 26);
    }
    gm_l14 = ((uint64_t)0ULL);
    uint8_t t81 = gm_l11;
    if (t81) {
        void * t82 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t84 = gm_l14;
        if ((uintptr_t)t84 >= (uintptr_t)(t83)) gm_trap_index((uintptr_t)t84, t83, gm_srcrsgf2gf2b1dq, 482, 9);
        GM_ST(uint8_t, (((uint8_t *)t82) + (uintptr_t)t84 * 1), ((uint8_t)45ULL));
        uint64_t t85 = gm_l14;
        uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
        if (o87) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 483, 9);
        uint64_t t88 = r86;
        gm_l14 = t88;
    }
    uint8_t t89 = gm_l10;
    if (t89) {
        void * t90 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t92 = gm_l14;
        if ((uintptr_t)t92 >= (uintptr_t)(t91)) gm_trap_index((uintptr_t)t92, t91, gm_srcrsgf2gf2b1dq, 486, 9);
        GM_ST(uint8_t, (((uint8_t *)t90) + (uintptr_t)t92 * 1), ((uint8_t)49ULL));
        uint64_t t93 = gm_l14;
        uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
        if (o95) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 487, 9);
        uint64_t t96 = r94;
        gm_l14 = t96;
    }
    {
        {
            uint64_t t97 = gm_l8;
            uint8_t t98 = ((t97 == ((uint64_t)0ULL)));
            if (!t98) {
                goto gm_lb4;
            }
            uint8_t t99 = gm_l10;
            uint8_t t100 = (uint8_t)!t99;
            if (!t100) {
                goto gm_lb4;
            }
            void * t101 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t103 = gm_l14;
            if ((uintptr_t)t103 >= (uintptr_t)(t102)) gm_trap_index((uintptr_t)t103, t102, gm_srcrsgf2gf2b1dq, 490, 9);
            GM_ST(uint8_t, (((uint8_t *)t101) + (uintptr_t)t103 * 1), ((uint8_t)48ULL));
            uint64_t t104 = gm_l14;
            uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
            if (o106) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 491, 9);
            uint64_t t107 = r105;
            gm_l14 = t107;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_l16 = ((uint64_t)0ULL);
    uint64_t t108 = gm_l8;
    gm_l15 = t108;
    {
        for (;;) {
            uint64_t t109 = gm_l16;
            uint64_t t110 = gm_l15;
            uint8_t t111 = (t109 < t110);
            if (!t111) {
                goto gm_lb5;
            }
            {
                {
                    {
                        uint64_t t112 = gm_l16;
                        gm_l17 = t112;
                        void * t113 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t115 = gm_l14;
                        if ((uintptr_t)t115 >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)t115, t114, gm_srcrsgf2gf2b1dq, 494, 9);
                        uint64_t t116 = gm_l17;
                        if ((uintptr_t)t116 >= (uintptr_t)(((uint64_t)1100ULL))) gm_trap_index((uintptr_t)t116, ((uint64_t)1100ULL), gm_srcrsgf2gf2b1dq, 494, 26);
                        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + (uintptr_t)t116 * 1));
                        uint8_t r118; bool o119 = __builtin_add_overflow(((uint8_t)48ULL), t117, &r118);
                        if (o119) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 494, 20);
                        uint8_t t120 = r118;
                        GM_ST(uint8_t, (((uint8_t *)t113) + (uintptr_t)t115 * 1), t120);
                        uint64_t t121 = gm_l14;
                        uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                        if (o123) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 495, 9);
                        uint64_t t124 = r122;
                        gm_l14 = t124;
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrsgf2gf2b1dq, 493, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t125 = gm_l16;
            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
            uint64_t t128 = r126;
            gm_l16 = t128;
        }
    }
gm_lb5:;
    uint64_t t129 = gm_l9;
    uint8_t t130 = (t129 > ((uint64_t)0ULL));
    if (t130) {
        void * t131 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t133 = gm_l14;
        if ((uintptr_t)t133 >= (uintptr_t)(t132)) gm_trap_index((uintptr_t)t133, t132, gm_srcrsgf2gf2b1dq, 498, 9);
        GM_ST(uint8_t, (((uint8_t *)t131) + (uintptr_t)t133 * 1), ((uint8_t)46ULL));
        uint64_t t134 = gm_l14;
        uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
        if (o136) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 499, 9);
        uint64_t t137 = r135;
        gm_l14 = t137;
        gm_l19 = ((uint64_t)0ULL);
        uint64_t t138 = gm_l9;
        gm_l18 = t138;
        {
            for (;;) {
                uint64_t t139 = gm_l19;
                uint64_t t140 = gm_l18;
                uint8_t t141 = (t139 < t140);
                if (!t141) {
                    goto gm_lb10;
                }
                {
                    {
                        {
                            uint64_t t142 = gm_l19;
                            gm_l20 = t142;
                            void * t143 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t145 = gm_l14;
                            if ((uintptr_t)t145 >= (uintptr_t)(t144)) gm_trap_index((uintptr_t)t145, t144, gm_srcrsgf2gf2b1dq, 501, 13);
                            void * t146 = (void *)((uint8_t *)&F.l4);
                            uint64_t t147 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t148 = (uintptr_t)((uint64_t)1100ULL);
                            if (t147 > t148 || t148 > (uintptr_t)((uint64_t)1100ULL)) gm_trap_slice(t147, t148, ((uint64_t)1100ULL), gm_srcrsgf2gf2b1dq, 501, 39);
                            uint64_t t149 = ((uintptr_t)((uint64_t)1100ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t150;
                            GM_ST(void *, (uint8_t *)&t150, (void *)(((uint8_t *)t146 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(t149));
                            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                            uint64_t t152 = gm_l7;
                            uint64_t t153 = gm_l8;
                            uint64_t t154 = gm_l20;
                            uint64_t r155; bool o156 = __builtin_add_overflow(t153, t154, &r155);
                            if (o156) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 501, 49);
                            uint64_t t157 = r155;
                            uint8_t t158 = gm_f_digit_at_gkhhkiq(t151, t152, t157);
                            uint8_t r159; bool o160 = __builtin_add_overflow(((uint8_t)48ULL), t158, &r159);
                            if (o160) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 501, 24);
                            uint8_t t161 = r159;
                            GM_ST(uint8_t, (((uint8_t *)t143) + (uintptr_t)t145 * 1), t161);
                            uint64_t t162 = gm_l14;
                            uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                            if (o164) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 502, 13);
                            uint64_t t165 = r163;
                            gm_l14 = t165;
                            goto gm_lb13;
                        }
gm_lb14:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrsgf2gf2b1dq, 500, 9);
                    }
gm_lb13:;
                }
gm_lb12:;
                uint64_t t166 = gm_l19;
                uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                uint64_t t169 = r167;
                gm_l19 = t169;
            }
        }
gm_lb10:;
    }
    void * t170 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t172 = gm_l14;
    uint64_t t173 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t174 = (uintptr_t)t172;
    if (t173 > t174 || t174 > (uintptr_t)t171) gm_trap_slice(t173, t174, t171, gm_srcrsgf2gf2b1dq, 505, 12);
    uint64_t t175 = ((uintptr_t)t172) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t176;
    GM_ST(void *, (uint8_t *)&t176, (void *)(((uint8_t *)t170 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(t175));
    gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
    F.ret = t177;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_put_u64_ant0p2p(uint64_t a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_ant0p2p F;
    uint64_t gm_l0; /* v */
    gm_b16_8 gm_l1; /* d */
    uint64_t gm_l2; /* at */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* t */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* j */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t8 = gm_l0;
    gm_l4 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint8_t t10 = (!(t9 == ((uint64_t)0ULL)));
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t13 = gm_l2;
                uint64_t t14 = gm_l3;
                uint64_t r15; bool o16 = __builtin_add_overflow(t13, t14, &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 267, 11);
                uint64_t t17 = r15;
                if ((uintptr_t)t17 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t17, t12, gm_srcrsgf2gf2b1dq, 267, 9);
                uint64_t t18 = gm_l4;
                if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 267, 22);
                uint64_t t19 = (uint64_t)(t18 % ((uint64_t)10ULL));
                uint8_t t20 = (uint8_t)t19;
                GM_ST(uint8_t, (((uint8_t *)t11) + (uintptr_t)t17 * 1), t20);
                uint64_t t21 = gm_l4;
                if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 268, 9);
                uint64_t t22 = (uint64_t)(t21 / ((uint64_t)10ULL));
                gm_l4 = t22;
                uint64_t t23 = gm_l3;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 269, 9);
                uint64_t t26 = r24;
                gm_l3 = t26;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t27 = gm_l3;
    gm_l6 = t27;
    {
        for (;;) {
            uint64_t t28 = gm_l5;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 273, 11);
            uint64_t t31 = r29;
            uint64_t t32 = gm_l6;
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb4;
            }
            {
                uint64_t t34 = gm_l6;
                uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 274, 9);
                uint64_t t37 = r35;
                gm_l6 = t37;
                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t40 = gm_l2;
                uint64_t t41 = gm_l5;
                uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
                if (o43) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 275, 16);
                uint64_t t44 = r42;
                if ((uintptr_t)t44 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t44, t39, gm_srcrsgf2gf2b1dq, 275, 14);
                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t38) + (uintptr_t)t44 * 1));
                gm_l7 = t45;
                void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t48 = gm_l2;
                uint64_t t49 = gm_l5;
                uint64_t r50; bool o51 = __builtin_add_overflow(t48, t49, &r50);
                if (o51) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 276, 11);
                uint64_t t52 = r50;
                if ((uintptr_t)t52 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t52, t47, gm_srcrsgf2gf2b1dq, 276, 9);
                void * t53 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t55 = gm_l2;
                uint64_t t56 = gm_l6;
                uint64_t r57; bool o58 = __builtin_add_overflow(t55, t56, &r57);
                if (o58) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 276, 23);
                uint64_t t59 = r57;
                if ((uintptr_t)t59 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t59, t54, gm_srcrsgf2gf2b1dq, 276, 21);
                uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)t53) + (uintptr_t)t59 * 1));
                GM_ST(uint8_t, (((uint8_t *)t46) + (uintptr_t)t52 * 1), t60);
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t63 = gm_l2;
                uint64_t t64 = gm_l6;
                uint64_t r65; bool o66 = __builtin_add_overflow(t63, t64, &r65);
                if (o66) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 277, 11);
                uint64_t t67 = r65;
                if ((uintptr_t)t67 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t67, t62, gm_srcrsgf2gf2b1dq, 277, 9);
                uint8_t t68 = gm_l7;
                GM_ST(uint8_t, (((uint8_t *)t61) + (uintptr_t)t67 * 1), t68);
                uint64_t t69 = gm_l5;
                uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                if (o71) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 278, 9);
                uint64_t t72 = r70;
                gm_l5 = t72;
            }
gm_lb6:;
        }
    }
gm_lb4:;
    uint64_t t73 = gm_l3;
    F.ret = t73;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_round_digits_4thelh5(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_round_digits_4thelh5 F;
    gm_b16_8 gm_l0; /* d */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* keep */
    uint8_t gm_l3; /* up */
    uint8_t gm_l4; /* next */
    uint8_t gm_l5; /* rest */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 362u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t8 = gm_l2;
    uint64_t t9 = gm_l1;
    uint8_t t10 = (t8 >= t9);
    if (t10) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_l3 = ((uint8_t)0);
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t13 = gm_l2;
    if ((uintptr_t)t13 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t13, t12, gm_srcrsgf2gf2b1dq, 397, 13);
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t13 * 1));
    gm_l4 = t14;
    uint8_t t15 = gm_l4;
    uint8_t t16 = (t15 > ((uint8_t)5ULL));
    if (t16) {
        gm_l3 = ((uint8_t)1);
    } else {
        uint8_t t17 = gm_l4;
        uint8_t t18 = ((t17 == ((uint8_t)5ULL)));
        if (t18) {
            gm_l5 = ((uint8_t)0);
            uint64_t t19 = gm_l2;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            if (o21) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 402, 14);
            uint64_t t22 = r20;
            gm_l6 = t22;
            {
                for (;;) {
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = gm_l1;
                    uint8_t t25 = (t23 < t24);
                    if (!t25) {
                        goto gm_lb1;
                    }
                    {
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t28 = gm_l6;
                        if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_srcrsgf2gf2b1dq, 404, 16);
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)t28 * 1));
                        uint8_t t30 = (!(t29 == ((uint8_t)0ULL)));
                        if (t30) {
                            gm_l5 = ((uint8_t)1);
                            goto gm_lb1;
                        }
                        uint64_t t31 = gm_l6;
                        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                        if (o33) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 408, 13);
                        uint64_t t34 = r32;
                        gm_l6 = t34;
                    }
gm_lb3:;
                }
            }
gm_lb1:;
            uint8_t t35 = gm_l5;
            if (t35) {
                gm_l3 = ((uint8_t)1);
            } else {
                uint64_t t36 = gm_l2;
                uint8_t t37 = (t36 > ((uint64_t)0ULL));
                if (t37) {
                    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t40 = gm_l2;
                    uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
                    if (o42) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 413, 20);
                    uint64_t t43 = r41;
                    if ((uintptr_t)t43 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t43, t39, gm_srcrsgf2gf2b1dq, 413, 18);
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t38) + (uintptr_t)t43 * 1));
                    if (((uint8_t)2ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 413, 18);
                    uint8_t t45 = (uint8_t)(t44 % ((uint8_t)2ULL));
                    uint8_t t46 = ((t45 == ((uint8_t)1ULL)));
                    gm_l3 = t46;
                }
            }
        }
    }
    uint8_t t47 = gm_l3;
    uint8_t t48 = (uint8_t)!t47;
    if (t48) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t49 = gm_l2;
    gm_l7 = t49;
    {
        for (;;) {
            uint64_t t50 = gm_l7;
            uint8_t t51 = (t50 > ((uint64_t)0ULL));
            if (!t51) {
                goto gm_lb4;
            }
            {
                uint64_t t52 = gm_l7;
                uint64_t r53; bool o54 = __builtin_sub_overflow(t52, ((uint64_t)1ULL), &r53);
                if (o54) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 419, 9);
                uint64_t t55 = r53;
                gm_l7 = t55;
                void * t56 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t58 = gm_l7;
                if ((uintptr_t)t58 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t58, t57, gm_srcrsgf2gf2b1dq, 420, 12);
                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t56) + (uintptr_t)t58 * 1));
                uint8_t t60 = ((t59 == ((uint8_t)9ULL)));
                if (t60) {
                    void * t61 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t63 = gm_l7;
                    if ((uintptr_t)t63 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t63, t62, gm_srcrsgf2gf2b1dq, 421, 13);
                    GM_ST(uint8_t, (((uint8_t *)t61) + (uintptr_t)t63 * 1), ((uint8_t)0ULL));
                } else {
                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t66 = gm_l7;
                    if ((uintptr_t)t66 >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)t66, t65, gm_srcrsgf2gf2b1dq, 423, 13);
                    void * t67 = (void *)(((uint8_t *)t64) + (uintptr_t)t66 * 1);
                    gm_l8 = t67;
                    void * t68 = gm_l8;
                    void * t69 = gm_l8;
                    uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)t69));
                    uint8_t r71; bool o72 = __builtin_add_overflow(t70, ((uint8_t)1ULL), &r71);
                    if (o72) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 423, 13);
                    uint8_t t73 = r71;
                    GM_ST(uint8_t, ((uint8_t *)t68), t73);
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
            }
gm_lb6:;
        }
    }
gm_lb4:;
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a(const void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_digit_sqvlt0a F;
    gm_t88486mk7vrvo gm_l0; /* sd */
    gm_b16_8 gm_l1; /* d */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l3;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 20));
    if (t2) {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (t4) {
            gm_l3 = ((uint8_t)1ULL);
        } else {
            gm_l3 = ((uint8_t)0ULL);
        }
        uint8_t t5 = gm_l3;
        F.ret = t5;
        goto gm_fret;
    }
    void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)a0));
    uint64_t t9 = (uintptr_t)t8;
    uint64_t t10 = (uintptr_t)t7;
    if (t9 > t10 || t10 > (uintptr_t)t7) gm_trap_slice(t9, t10, t7, gm_srcrsgf2gf2b1dq, 535, 21);
    uint64_t t11 = ((uintptr_t)t7) - (uintptr_t)t8;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t6 + (uintptr_t)t8 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(t11));
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
    uint64_t t14 = gm_l2;
    uint8_t t15 = gm_f_digit_at_gkhhkiq(t12, t13, t14);
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a_c(gm_cs2rc1d61u3qan a0, gm_b16_8 a1, uint64_t a2) { return gm_f_sci_digit_sqvlt0a(&a0, a1, a2); }
inline __attribute__((aligned(4))) void gm_f_sci_digits_0sasrt8(void *gm_out, double a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_digits_0sasrt8 F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* sig */
    gm_b16_8 gm_l2; /* d */
    gm_t43v1gep754th gm_l3; /* parts */
    gm_t2lqu8jsj511d gm_l4; /* ex */
    gm_t88486mk7vrvo gm_l5; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    double t5 = gm_l0;
    gm_t43v1gep754th t6 = GM_CONV(gm_t43v1gep754th, gm_f_decompose_c1606vm(t5));
    gm_l3 = t6;
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    int32_t t8 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t9 = gm_l2;
    gm_t2lqu8jsj511d t10 = GM_CONV(gm_t2lqu8jsj511d, gm_f_expand_7rpkqt1(t7, t8, t9));
    gm_l4 = t10;
    gm_t88486mk7vrvo t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 21, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 22, 0);
    GM_ST(uint64_t, ((uint8_t *)&t11 + 0), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t11 + 8), ((uint64_t)0ULL));
    GM_ST(int32_t, ((uint8_t *)&t11 + 16), ((int32_t)0LL));
    GM_ST(uint8_t, ((uint8_t *)&t11 + 20), ((uint8_t)0));
    gm_l5 = t11;
    uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
    if (t13) {
        memmove(gm_out, ((uint8_t *)&gm_l5), 24);
        goto gm_fdone;
    }
    {
        for (;;) {
            void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
            if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcrsgf2gf2b1dq, 524, 11);
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t14) + (uintptr_t)t16 * 1));
            uint8_t t18 = ((t17 == ((uint8_t)0ULL)));
            if (!t18) {
                goto gm_lb1;
            }
            {
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 524, 29);
                uint64_t t22 = r20;
                GM_ST(uint64_t, ((uint8_t *)&gm_l5), t22);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 525, 15);
    uint64_t t27 = r25;
    GM_ST(uint64_t, (((uint8_t *)&gm_l5) + 8), t27);
    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    int32_t t29 = (int32_t)t28;
    int32_t r30; bool o31 = __builtin_sub_overflow(t29, ((int32_t)1LL), &r30);
    if (o31) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 526, 13);
    int32_t t32 = r30;
    uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    int32_t t34 = (int32_t)t33;
    int32_t r35; bool o36 = __builtin_sub_overflow(t32, t34, &r35);
    if (o36) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 526, 13);
    int32_t t37 = r35;
    GM_ST(int32_t, (((uint8_t *)&gm_l5) + 16), t37);
    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint64_t t42 = (uintptr_t)t40;
    uint64_t t43 = (uintptr_t)t41;
    if (t42 > t43 || t43 > (uintptr_t)t39) gm_trap_slice(t42, t43, t39, gm_srcrsgf2gf2b1dq, 527, 28);
    uint64_t t44 = ((uintptr_t)t41) - (uintptr_t)t40;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t38 + (uintptr_t)t40 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(t44));
    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t47 = gm_l1;
    uint8_t t48 = gm_f_round_digits_4thelh5(t45, t46, t47);
    GM_ST(uint8_t, (((uint8_t *)&gm_l5) + 20), t48);
    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 20));
    if (t49) {
        int32_t t50 = GM_LD(int32_t, (((uint8_t *)&gm_l5) + 16));
        int32_t r51; bool o52 = __builtin_add_overflow(t50, ((int32_t)1LL), &r51);
        if (o52) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 528, 18);
        int32_t t53 = r51;
        GM_ST(int32_t, (((uint8_t *)&gm_l5) + 16), t53);
    }
    memmove(gm_out, ((uint8_t *)&gm_l5), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_cs2rc1d61u3qan gm_f_sci_digits_0sasrt8_c(double a0, uint64_t a1, gm_b16_8 a2) { gm_t88486mk7vrvo gm_r; gm_f_sci_digits_0sasrt8(&gm_r, a0, a1, a2); return GM_CONV(gm_cs2rc1d61u3qan, gm_r); }
__attribute__((aligned(4))) gm_b16_8 gm_f_sci_oikoiqs(double a0, uint32_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_oikoiqs F;
    double gm_l0; /* x */
    uint32_t gm_l1; /* digits */
    gm_b16_8 gm_l2; /* buf */
    gm_b16_8 gm_l3; /* sp */
    uint64_t gm_l5; /* places */
    gm_t88486mk7vrvo gm_l6; /* sd */
    uint8_t gm_l7; /* neg */
    uint32_t gm_l8; /* ae */
    uint64_t gm_l9; /* exp_digits */
    uint64_t gm_l10; /* total */
    uint64_t gm_l11; /* out */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint64_t gm_l15; /* k */
    uint32_t gm_l16; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 2257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l17;
    int32_t gm_l18;
    uint64_t gm_l19;
    uint8_t gm_l20;
    {
        {
            double t12 = gm_l0;
            gm_b16_8 t13 = gm_l2;
            gm_f_special_meo3u57((void *)&gm_l17, t12, t13);
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
            if (!t14) {
                goto gm_lb2;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
            gm_l3 = t15;
            gm_b16_8 t16 = gm_l3;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_t91rshbm1iu79 t17;
    for (size_t i18 = 0; i18 < 1100; i18++) GM_ST(uint8_t, (uint8_t *)&t17 + i18 * 1, ((uint8_t)0ULL));
    F.l4 = t17;
    uint32_t t19 = gm_l1;
    uint64_t t20 = (uint64_t)t19;
    gm_l5 = t20;
    double t21 = gm_l0;
    uint64_t t22 = gm_l5;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    if (o24) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 547, 25);
    uint64_t t25 = r23;
    void * t26 = (void *)((uint8_t *)&F.l4);
    uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t28 = (uintptr_t)((uint64_t)1100ULL);
    if (t27 > t28 || t28 > (uintptr_t)((uint64_t)1100ULL)) gm_trap_slice(t27, t28, ((uint64_t)1100ULL), gm_srcrsgf2gf2b1dq, 547, 37);
    uint64_t t29 = ((uintptr_t)((uint64_t)1100ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t26 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29));
    gm_f_sci_digits_0sasrt8((void *)&gm_l6, t21, t25, t30);
    double t31 = gm_l0;
    uint8_t t32 = (uint8_t)(__builtin_signbit(t31) != 0);
    gm_l7 = t32;
    int32_t t33 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
    uint8_t t34 = (t33 < ((int32_t)0LL));
    if (t34) {
        int32_t t35 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
        int32_t r36; bool o37 = __builtin_sub_overflow(((int32_t)0LL), t35, &r36);
        if (o37) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 549, 28);
        int32_t t38 = r36;
        gm_l18 = t38;
    } else {
        int32_t t39 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
        gm_l18 = t39;
    }
    int32_t t40 = gm_l18;
    uint32_t t41 = (uint32_t)t40;
    gm_l8 = t41;
    uint32_t t42 = gm_l8;
    uint8_t t43 = (t42 >= ((uint32_t)100ULL));
    if (t43) {
        gm_l19 = ((uint64_t)3ULL);
    } else {
        gm_l19 = ((uint64_t)2ULL);
    }
    uint64_t t44 = gm_l19;
    gm_l9 = t44;
    uint64_t r45; bool o46 = __builtin_add_overflow(((uint64_t)1ULL), ((uint64_t)2ULL), &r45);
    if (o46) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 551, 14);
    uint64_t t47 = r45;
    uint64_t t48 = gm_l9;
    uint64_t r49; bool o50 = __builtin_add_overflow(t47, t48, &r49);
    if (o50) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 551, 14);
    uint64_t t51 = r49;
    gm_l10 = t51;
    uint8_t t52 = gm_l7;
    if (t52) {
        uint64_t t53 = gm_l10;
        uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
        if (o55) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 552, 14);
        uint64_t t56 = r54;
        gm_l10 = t56;
    }
    uint64_t t57 = gm_l5;
    uint8_t t58 = (t57 > ((uint64_t)0ULL));
    if (t58) {
        uint64_t t59 = gm_l10;
        uint64_t t60 = gm_l5;
        uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
        if (o62) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 553, 30);
        uint64_t t63 = r61;
        uint64_t r64; bool o65 = __builtin_add_overflow(t59, t63, &r64);
        if (o65) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 553, 21);
        uint64_t t66 = r64;
        gm_l10 = t66;
    }
    uint64_t t67 = gm_l10;
    uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t69 = (t67 > t68);
    if (t69) {
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sa954a25u62up));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(45));
        gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t70)), GM_LD(uintptr_t, ((uint8_t *)&t70) + 8), gm_srcrsgf2gf2b1dq, 554, 26);
    }
    gm_l11 = ((uint64_t)0ULL);
    uint8_t t71 = gm_l7;
    if (t71) {
        void * t72 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t74 = gm_l11;
        if ((uintptr_t)t74 >= (uintptr_t)(t73)) gm_trap_index((uintptr_t)t74, t73, gm_srcrsgf2gf2b1dq, 557, 9);
        GM_ST(uint8_t, (((uint8_t *)t72) + (uintptr_t)t74 * 1), ((uint8_t)45ULL));
        uint64_t t75 = gm_l11;
        uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
        if (o77) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 558, 9);
        uint64_t t78 = r76;
        gm_l11 = t78;
    }
    void * t79 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t81 = gm_l11;
    if ((uintptr_t)t81 >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)t81, t80, gm_srcrsgf2gf2b1dq, 560, 5);
    void * t82 = (void *)((uint8_t *)&F.l4);
    uint64_t t83 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t84 = (uintptr_t)((uint64_t)1100ULL);
    if (t83 > t84 || t84 > (uintptr_t)((uint64_t)1100ULL)) gm_trap_slice(t83, t84, ((uint64_t)1100ULL), gm_srcrsgf2gf2b1dq, 560, 36);
    uint64_t t85 = ((uintptr_t)((uint64_t)1100ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t86;
    GM_ST(void *, (uint8_t *)&t86, (void *)(((uint8_t *)t82 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(t85));
    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
    uint8_t t88 = gm_f_sci_digit_sqvlt0a(&(*(gm_t88486mk7vrvo *)((uint8_t *)&gm_l6)), t87, ((uint64_t)0ULL));
    uint8_t r89; bool o90 = __builtin_add_overflow(((uint8_t)48ULL), t88, &r89);
    if (o90) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 560, 16);
    uint8_t t91 = r89;
    GM_ST(uint8_t, (((uint8_t *)t79) + (uintptr_t)t81 * 1), t91);
    uint64_t t92 = gm_l11;
    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
    if (o94) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 561, 5);
    uint64_t t95 = r93;
    gm_l11 = t95;
    uint64_t t96 = gm_l5;
    uint8_t t97 = (t96 > ((uint64_t)0ULL));
    if (t97) {
        void * t98 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t100 = gm_l11;
        if ((uintptr_t)t100 >= (uintptr_t)(t99)) gm_trap_index((uintptr_t)t100, t99, gm_srcrsgf2gf2b1dq, 563, 9);
        GM_ST(uint8_t, (((uint8_t *)t98) + (uintptr_t)t100 * 1), ((uint8_t)46ULL));
        uint64_t t101 = gm_l11;
        uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
        if (o103) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 564, 9);
        uint64_t t104 = r102;
        gm_l11 = t104;
        gm_l13 = ((uint64_t)1ULL);
        uint64_t t105 = gm_l5;
        uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
        if (o107) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 565, 21);
        uint64_t t108 = r106;
        gm_l12 = t108;
        {
            for (;;) {
                uint64_t t109 = gm_l13;
                uint64_t t110 = gm_l12;
                uint8_t t111 = (t109 < t110);
                if (!t111) {
                    goto gm_lb3;
                }
                {
                    {
                        {
                            uint64_t t112 = gm_l13;
                            gm_l14 = t112;
                            void * t113 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t115 = gm_l11;
                            if ((uintptr_t)t115 >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)t115, t114, gm_srcrsgf2gf2b1dq, 566, 13);
                            void * t116 = (void *)((uint8_t *)&F.l4);
                            uint64_t t117 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t118 = (uintptr_t)((uint64_t)1100ULL);
                            if (t117 > t118 || t118 > (uintptr_t)((uint64_t)1100ULL)) gm_trap_slice(t117, t118, ((uint64_t)1100ULL), gm_srcrsgf2gf2b1dq, 566, 44);
                            uint64_t t119 = ((uintptr_t)((uint64_t)1100ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t120;
                            GM_ST(void *, (uint8_t *)&t120, (void *)(((uint8_t *)t116 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(t119));
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                            uint64_t t122 = gm_l14;
                            uint8_t t123 = gm_f_sci_digit_sqvlt0a(&(*(gm_t88486mk7vrvo *)((uint8_t *)&gm_l6)), t121, t122);
                            uint8_t r124; bool o125 = __builtin_add_overflow(((uint8_t)48ULL), t123, &r124);
                            if (o125) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 566, 24);
                            uint8_t t126 = r124;
                            GM_ST(uint8_t, (((uint8_t *)t113) + (uintptr_t)t115 * 1), t126);
                            uint64_t t127 = gm_l11;
                            uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                            if (o129) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 567, 13);
                            uint64_t t130 = r128;
                            gm_l11 = t130;
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrsgf2gf2b1dq, 565, 9);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t131 = gm_l13;
                uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                uint64_t t134 = r132;
                gm_l13 = t134;
            }
        }
gm_lb3:;
    }
    void * t135 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t137 = gm_l11;
    if ((uintptr_t)t137 >= (uintptr_t)(t136)) gm_trap_index((uintptr_t)t137, t136, gm_srcrsgf2gf2b1dq, 570, 5);
    GM_ST(uint8_t, (((uint8_t *)t135) + (uintptr_t)t137 * 1), ((uint8_t)101ULL));
    void * t138 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t140 = gm_l11;
    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
    if (o142) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 571, 9);
    uint64_t t143 = r141;
    if ((uintptr_t)t143 >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)t143, t139, gm_srcrsgf2gf2b1dq, 571, 5);
    int32_t t144 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
    uint8_t t145 = (t144 < ((int32_t)0LL));
    if (t145) {
        gm_l20 = ((uint8_t)45ULL);
    } else {
        gm_l20 = ((uint8_t)43ULL);
    }
    uint8_t t146 = gm_l20;
    GM_ST(uint8_t, (((uint8_t *)t138) + (uintptr_t)t143 * 1), t146);
    uint64_t t147 = gm_l11;
    uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)2ULL), &r148);
    if (o149) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 572, 5);
    uint64_t t150 = r148;
    gm_l11 = t150;
    uint64_t t151 = gm_l9;
    gm_l15 = t151;
    uint32_t t152 = gm_l8;
    gm_l16 = t152;
    {
        for (;;) {
            uint64_t t153 = gm_l15;
            uint8_t t154 = (t153 > ((uint64_t)0ULL));
            if (!t154) {
                goto gm_lb8;
            }
            {
                uint64_t t155 = gm_l15;
                uint64_t r156; bool o157 = __builtin_sub_overflow(t155, ((uint64_t)1ULL), &r156);
                if (o157) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 576, 9);
                uint64_t t158 = r156;
                gm_l15 = t158;
                void * t159 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t161 = gm_l11;
                uint64_t t162 = gm_l15;
                uint64_t r163; bool o164 = __builtin_add_overflow(t161, t162, &r163);
                if (o164) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 577, 13);
                uint64_t t165 = r163;
                if ((uintptr_t)t165 >= (uintptr_t)(t160)) gm_trap_index((uintptr_t)t165, t160, gm_srcrsgf2gf2b1dq, 577, 9);
                uint32_t t166 = gm_l16;
                if (((uint32_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 577, 31);
                uint32_t t167 = (uint32_t)(t166 % ((uint32_t)10ULL));
                uint8_t t168 = (uint8_t)t167;
                uint8_t r169; bool o170 = __builtin_add_overflow(((uint8_t)48ULL), t168, &r169);
                if (o170) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 577, 24);
                uint8_t t171 = r169;
                GM_ST(uint8_t, (((uint8_t *)t159) + (uintptr_t)t165 * 1), t171);
                uint32_t t172 = gm_l16;
                if (((uint32_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 578, 9);
                uint32_t t173 = (uint32_t)(t172 / ((uint32_t)10ULL));
                gm_l16 = t173;
            }
gm_lb10:;
        }
    }
gm_lb8:;
    uint64_t t174 = gm_l11;
    uint64_t t175 = gm_l9;
    uint64_t r176; bool o177 = __builtin_add_overflow(t174, t175, &r176);
    if (o177) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 580, 5);
    uint64_t t178 = r176;
    gm_l11 = t178;
    void * t179 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t181 = gm_l11;
    uint64_t t182 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t183 = (uintptr_t)t181;
    if (t182 > t183 || t183 > (uintptr_t)t180) gm_trap_slice(t182, t183, t180, gm_srcrsgf2gf2b1dq, 581, 12);
    uint64_t t184 = ((uintptr_t)t181) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t185;
    GM_ST(void *, (uint8_t *)&t185, (void *)(((uint8_t *)t179 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(t184));
    gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
    F.ret = t186;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_special_meo3u57(void *gm_out, double a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_special_meo3u57 F;
    double gm_l0; /* x */
    gm_b16_8 gm_l1; /* buf */
    gm_b16_8 gm_l2; /* text */
    GM_STACK_CHECK_SHALLOW(&F, 477u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l3;
    gm_b16_8 gm_l4;
    gm_b16_8 gm_l5;
    double t2 = gm_l0;
    uint8_t t3 = (uint8_t)__builtin_isnan(t2);
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sfa86erpod86s));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        gm_l3 = t4;
    } else {
        double t5 = gm_l0;
        uint8_t t6 = (uint8_t)__builtin_isinf(t5);
        if (t6) {
            double t7 = gm_l0;
            uint8_t t8 = (uint8_t)(__builtin_signbit(t7) != 0);
            if (t8) {
                gm_b16_8 t9;
                GM_ST(void *, (uint8_t *)&t9, (void *)(gm_segch6a5dme9u));
                GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(4));
                gm_l5 = t9;
            } else {
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sns75reqq32d2));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(3));
                gm_l5 = t10;
            }
            gm_b16_8 t11 = gm_l5;
            gm_l4 = t11;
        } else {
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
            gm_l4 = t12;
        }
        gm_b16_8 t13 = gm_l4;
        gm_l3 = t13;
    }
    gm_b16_8 t14 = gm_l3;
    gm_l2 = t14;
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
    if (t16) {
        gm_th2lqbo77pvsc t17;
        memset(&t17, 0, sizeof t17);
        memmove(gm_out, ((uint8_t *)&t17), 24);
        goto gm_fdone;
    }
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t20 = (t18 > t19);
    if (t20) {
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sa954a25u62up));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(45));
        gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t21)), GM_LD(uintptr_t, ((uint8_t *)&t21) + 8), gm_srcrsgf2gf2b1dq, 440, 29);
    }
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t26 = (uintptr_t)t24;
    if (t25 > t26 || t26 > (uintptr_t)t23) gm_trap_slice(t25, t26, t23, gm_srcrsgf2gf2b1dq, 441, 5);
    uint64_t t27 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t22 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
    gm_b16_8 t29 = gm_l2;
    gm_f_copy_from_f7j6p05(t28, t29);
    void * t30 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t34 = (uintptr_t)t32;
    if (t33 > t34 || t34 > (uintptr_t)t31) gm_trap_slice(t33, t34, t31, gm_srcrsgf2gf2b1dq, 442, 12);
    uint64_t t35 = ((uintptr_t)t32) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t30 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35));
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
    gm_th2lqbo77pvsc t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t38 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t38 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t38), t37);
    ((uint8_t *)&t38)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t38), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_special_meo3u57_c(double a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_special_meo3u57(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_front_b98pl8o(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_front_b98pl8o F;
    gm_b16_8 gm_l0; /* buf */
    uint64_t gm_l1; /* at */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 401u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t2 = gm_l1;
    uint64_t t3 = gm_l2;
    uint64_t r4; bool o5 = __builtin_add_overflow(t2, t3, &r4);
    if (o5) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 88, 8);
    uint64_t t6 = r4;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t8 = (t6 > t7);
    if (t8) {
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sl2sr5fl46c73));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(47));
        gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t9)), GM_LD(uintptr_t, ((uint8_t *)&t9) + 8), gm_srcrsgf2gf2b1dq, 88, 27);
    }
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t12 = gm_l1;
    uint64_t t13 = gm_l1;
    uint64_t t14 = gm_l2;
    uint64_t r15; bool o16 = __builtin_add_overflow(t13, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 89, 13);
    uint64_t t17 = r15;
    uint64_t t18 = (uintptr_t)t12;
    uint64_t t19 = (uintptr_t)t17;
    if (t18 > t19 || t19 > (uintptr_t)t11) gm_trap_slice(t18, t19, t11, gm_srcrsgf2gf2b1dq, 89, 5);
    uint64_t t20 = ((uintptr_t)t17) - (uintptr_t)t12;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t10 + (uintptr_t)t12 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20));
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t25 = gm_l2;
    uint64_t r26; bool o27 = __builtin_sub_overflow(t24, t25, &r26);
    if (o27) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 89, 35);
    uint64_t t28 = r26;
    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t30 = (uintptr_t)t28;
    uint64_t t31 = (uintptr_t)t29;
    if (t30 > t31 || t31 > (uintptr_t)t23) gm_trap_slice(t30, t31, t23, gm_srcrsgf2gf2b1dq, 89, 31);
    uint64_t t32 = ((uintptr_t)t29) - (uintptr_t)t28;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t22 + (uintptr_t)t28 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32));
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
    gm_f_copy_from_f7j6p05(t21, t34);
    void * t35 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t37 = gm_l1;
    uint64_t t38 = gm_l2;
    uint64_t r39; bool o40 = __builtin_add_overflow(t37, t38, &r39);
    if (o40) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 90, 19);
    uint64_t t41 = r39;
    uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t43 = (uintptr_t)t41;
    if (t42 > t43 || t43 > (uintptr_t)t36) gm_trap_slice(t42, t43, t36, gm_srcrsgf2gf2b1dq, 90, 12);
    uint64_t t44 = ((uintptr_t)t41) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t35 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(t44));
    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
    F.ret = t46;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
