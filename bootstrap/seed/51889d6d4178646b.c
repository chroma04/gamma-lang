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
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
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
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
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
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
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
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
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
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_trcmmd12cefb1 f0; void * f1; } gm_tdevt6kjpn5ja;
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tdevt6kjpn5ja) == 40 && _Alignof(gm_tdevt6kjpn5ja) == 8 && offsetof(gm_tdevt6kjpn5ja, f0) == 0 && offsetof(gm_tdevt6kjpn5ja, f1) == 32, "layout of `(hold: Stmt, local: ^Expr)`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
struct gm_fr_array_as_slice_gvflfg2 {
    gm_frame h;
    gm_tdevt6kjpn5ja l8; /* held_by */
    void * ret;
};
struct gm_fr_auto_deref_array_g2mtf4v {
    gm_frame h;
    void * ret;
};
struct gm_fr_const_temp_pjvp4uv {
    gm_frame h;
    void * ret;
};
struct gm_fr_empty_slice_d914i5u {
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
struct gm_fr_format_5v26vgi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6hur18u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_amt7kei {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pkmkakl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uc7ef2o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_held_4aoq406 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stmts */
    void * ret;
};
struct gm_fr_hold_temp_array_rb5nkro {
    gm_frame h;
    gm_tdevt6kjpn5ja ret;
};
struct gm_fr_index_uh6hfri {
    gm_frame h;
    void * ret;
};
struct gm_fr_index_value_ufcur8j {
    gm_frame h;
    gm_trqfvmjtt4sij l18; /* stmts */
    void * ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_held_temp_d00dm1p {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
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
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
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
struct gm_fr_slice_expr_iamf9el {
    gm_frame h;
    gm_tdevt6kjpn5ja l7; /* held_by */
    void * ret;
};
struct gm_fr_slice_of_84iab6u {
    gm_frame h;
    void * ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_auto_deref_array_g2mtf4v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_const_temp_pjvp4uv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) void * gm_f_enum_array_index_kma2hbd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, void * a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) void gm_f_format_5v26vgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_6hur18u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_amt7kei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pkmkakl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_uc7ef2o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void * gm_f_held_4aoq406(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_hold_temp_array_rb5nkro(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void * gm_f_index_uh6hfri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_index_value_ufcur8j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_held_temp_d00dm1p(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_shallow_8ur803a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_slice_expr_iamf9el(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint8_t a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_slice_of_84iab6u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2, void * a3, void * a4, uint8_t a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_element_04p1d8c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_slice_4dujfr9(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, uint8_t a5, gm_tl874t2atnlh3 a6);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_cs1qbc2gaqs[16] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0bc9s1qderg8[70];
extern const uint8_t gm_s1ctgkvj02ag0[34];
extern const uint8_t gm_s1h7h9updp99l[54];
extern const uint8_t gm_s7ed59qo09cfg[41];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_skr3v6bd9t3ir[59];
extern const uint8_t gm_smorejbk97kqm[65];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3a4t4bi1ba85[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svn9drpksr8pl[34];
extern const gm_tmpl gm_tpl3sv6aqbksrr5;
extern const gm_tmpl gm_tpl60bi48lvp2qn;
extern const gm_tmpl gm_tplcjeg5mjcrb8d;
extern const gm_tmpl gm_tplf28d52hmmd2n;
extern const gm_tmpl gm_tplubod389iot6h;
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) void gm_f_format_5v26vgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5v26vgi F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1ctgkvj02ag0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3sv6aqbksrr5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6hur18u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6hur18u F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svn9drpksr8pl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl60bi48lvp2qn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_amt7kei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_amt7kei F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1h7h9updp99l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf28d52hmmd2n), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pkmkakl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pkmkakl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skr3v6bd9t3ir));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplubod389iot6h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uc7ef2o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uc7ef2o F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smorejbk97kqm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcjeg5mjcrb8d), t3);
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
inline __attribute__((aligned(4))) void * gm_f_held_4aoq406(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2) {
    struct gm_fr_held_4aoq406 F;
    void * gm_l0; /* s */
    gm_trcmmd12cefb1 gm_l1; /* hold */
    void * gm_l2; /* sl */
    GM_STACK_CHECK(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    gm_trqfvmjtt4sij t2;
    gm_f_list_ir0h3tc_2((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t2;
    void * t3 = (void *)((uint8_t *)&F.l3);
    gm_tftb1odc739t5 t4 = gm_f_push_d6c76ud_2(t3, &(*(gm_trcmmd12cefb1 *)((uint8_t *)a1)));
    gm_l4 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src3a4t4bi1ba85, 116, 5);
    void * t6 = gm_l2;
    gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t6));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    void * t9 = gm_l2;
    void * t10 = gm_l2;
    gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 72));
    void * t12 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t7, t8, t9, t11);
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
inline __attribute__((aligned(4))) void * gm_f_index_uh6hfri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_index_uh6hfri F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    void * gm_l2; /* i */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t5;
    void * t6 = gm_l0;
    void * t7 = gm_l4;
    void * t8 = gm_l2;
    gm_tl874t2atnlh3 t9 = gm_l3;
    void * t10 = gm_f_index_value_ufcur8j((void *)NULL, cx, NULL, t6, t7, t8, t9);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
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
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
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
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_array_as_slice_gvflfg2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* target0 */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* target */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    uint8_t gm_l6; /* c */
    gm_tl874t2atnlh3 gm_l7; /* span */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    gm_tqs5p4n2tq19v gm_l12;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_l3 = t9;
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 96);
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    gm_t88rcucg1b7uk t14 = gm_l2;
    gm_t88rcucg1b7uk t15 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t11, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t15;
    void * t16 = gm_l0;
    void * t17 = (void *)(((uint8_t *)t16) + 96);
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, ((uint8_t *)t18));
    void * t20 = gm_l1;
    gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t20));
    gm_t88rcucg1b7uk t22 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t17, t19, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t22;
    {
        {
            void * t23 = gm_l3;
            gm_t88rcucg1b7uk t24 = gm_l4;
            gm_f_kind_jcnt4hm((void *)&gm_l9, t23, GM_CONV(gm_csguankko4lmhd, t24));
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)10ULL));
            if (!t26) {
                goto gm_lb2;
            }
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 8));
            gm_l6 = t27;
            uint8_t t28 = gm_l6;
            uint8_t t29 = (uint8_t)!t28;
            if (t29) {
                gm_l10 = ((uint8_t)1);
            } else {
                void * t30 = gm_l3;
                gm_t88rcucg1b7uk t31 = gm_l5;
                uint8_t t32 = gm_f_is_positional_array_6sufsk8(t30, GM_CONV(gm_csguankko4lmhd, t31));
                uint8_t t33 = (uint8_t)!t32;
                gm_l10 = t33;
            }
            uint8_t t34 = gm_l10;
            if (t34) {
                void * t35 = gm_l1;
                F.ret = t35;
                goto gm_fret;
            }
            void * t36 = gm_l1;
            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
            gm_l7 = t37;
            void * t38 = gm_l0;
            void * t39 = gm_l1;
            gm_tqs5p4n2tq19v t40 = gm_f_place_mutability_0tlm4g8(t38, t39);
            gm_l12 = t40;
            {
                {
                    uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l12))[1] != 0);
                    if (t41) {
                        goto gm_lb4;
                    }
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 87, 8);
            }
gm_lb3:;
            uint8_t t42 = gm_l11;
            if (t42) {
                void * t43 = gm_l0;
                void * t44 = gm_l1;
                gm_tl874t2atnlh3 t45 = gm_l7;
                gm_tl874t2atnlh3 t46 = gm_l7;
                void * t47 = gm_f_slice_of_84iab6u((void *)NULL, cx, NULL, t43, t44, t45, ((void *)0), ((void *)0), ((uint8_t)0), t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t47;
                goto gm_fret;
            }
            void * t48 = gm_l0;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t48) + 1240));
            if (t49) {
                void * t50 = gm_l0;
                void * t51 = gm_l0;
                void * t52 = gm_l1;
                void * t53 = gm_f_const_temp_pjvp4uv((void *)NULL, cx, NULL, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl874t2atnlh3 t54 = gm_l7;
                gm_tl874t2atnlh3 t55 = gm_l7;
                void * t56 = gm_f_slice_of_84iab6u((void *)NULL, cx, NULL, t50, t53, t54, ((void *)0), ((void *)0), ((uint8_t)0), t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t56;
                goto gm_fret;
            }
            void * t57 = gm_l0;
            void * t58 = gm_l1;
            gm_f_hold_temp_array_rb5nkro((void *)&F.l8, (void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t59 = gm_l0;
            void * t60 = GM_LD(void *, ((uint8_t *)t59));
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
            gm_tl874t2atnlh3 t63 = gm_l7;
            gm_tl874t2atnlh3 t64 = gm_l7;
            gm_trcmmd12cefb1 t65 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)&F.l8));
            void * t66 = gm_f_slice_of_84iab6u((void *)NULL, cx, NULL, t61, t62, t63, ((void *)0), ((void *)0), ((uint8_t)0), t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t67 = gm_f_held_4aoq406((void *)NULL, cx, NULL, t60, &t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t67;
            goto gm_fret;
        }
gm_lb2:;
        void * t68 = gm_l1;
        F.ret = t68;
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
inline __attribute__((aligned(4))) void * gm_f_auto_deref_array_g2mtf4v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_auto_deref_array_g2mtf4v F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l3;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    {
        {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, ((uint8_t *)t8));
            void * t10 = (void *)(((uint8_t *)t9) + 8);
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 96);
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, ((uint8_t *)t13));
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            void * t16 = gm_l1;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t16));
            gm_t88rcucg1b7uk t18 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t12, t15, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_kind_jcnt4hm((void *)&gm_l3, t10, GM_CONV(gm_csguankko4lmhd, t18));
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)8ULL));
            if (!t20) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l3) + 8) + 4));
            gm_l2 = t21;
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, ((uint8_t *)t22));
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t24, GM_CONV(gm_csguankko4lmhd, t25));
            {
                {
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)11ULL));
                    if (!t27) {
                        goto gm_lb4;
                    }
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)20ULL));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 51, 81);
            }
gm_lb3:;
            uint8_t t30 = gm_l4;
            if (!t30) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t31 = gm_l2;
            void * t32 = gm_l1;
            void * t33 = gm_l1;
            gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
            gm_t2b6tm8c0vgo1 t35;
            memset(&t35, 0, sizeof t35);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t35 + 8), t32);
            void * t36 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t31, &t35, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t36;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t37 = gm_l1;
    F.ret = t37;
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
inline __attribute__((aligned(4))) void * gm_f_const_temp_pjvp4uv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_const_temp_pjvp4uv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    void * gm_l2; /* p */
    GM_STACK_CHECK(&F, 459u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    uint8_t gm_l4;
    gm_tqs5p4n2tq19v gm_l5;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    void * t9 = gm_l0;
    void * t10 = gm_l1;
    gm_tqs5p4n2tq19v t11 = gm_f_place_mutability_0tlm4g8(t9, t10);
    gm_l5 = t11;
    {
        {
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
            if (t12) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 96, 8);
    }
gm_lb1:;
    uint8_t t13 = gm_l4;
    if (t13) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, ((uint8_t *)t14));
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 96);
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        void * t21 = gm_l1;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t21));
        gm_t88rcucg1b7uk t23 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_kind_jcnt4hm((void *)&gm_l7, t16, GM_CONV(gm_csguankko4lmhd, t23));
        {
            {
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                uint8_t t25 = (uint8_t)(t24 == ((uint8_t)11ULL));
                if (!t25) {
                    goto gm_lb5;
                }
                gm_l6 = ((uint8_t)0);
                goto gm_lb4;
            }
gm_lb5:;
            {
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                uint8_t t27 = (uint8_t)(t26 == ((uint8_t)20ULL));
                if (!t27) {
                    goto gm_lb6;
                }
                gm_l6 = ((uint8_t)0);
                goto gm_lb4;
            }
gm_lb6:;
            {
                gm_l6 = ((uint8_t)1);
                goto gm_lb4;
            }
gm_lb7:;
            gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 96, 46);
        }
gm_lb4:;
        uint8_t t28 = gm_l6;
        gm_l3 = t28;
    }
    uint8_t t29 = gm_l3;
    if (t29) {
        void * t30 = gm_l1;
        F.ret = t30;
        goto gm_fret;
    }
    void * t31 = gm_l0;
    void * t32 = GM_LD(void *, ((uint8_t *)t31));
    void * t33 = (void *)(((uint8_t *)t32) + 8);
    void * t34 = gm_l1;
    gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t34));
    gm_t88rcucg1b7uk t36 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t33, ((uint8_t)1), t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t37 = gm_l1;
    void * t38 = gm_l1;
    gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
    gm_t2b6tm8c0vgo1 t40;
    memset(&t40, 0, sizeof t40);
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)25ULL));
    GM_ST(void *, ((uint8_t *)&t40 + 8), t37);
    void * t41 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t36, &t40, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t41;
    void * t42 = gm_l1;
    gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t42));
    void * t44 = gm_l2;
    void * t45 = gm_l1;
    gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 72));
    gm_t2b6tm8c0vgo1 t47;
    memset(&t47, 0, sizeof t47);
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)18ULL));
    GM_ST(void *, ((uint8_t *)&t47 + 8), t44);
    void * t48 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t43, &t47, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t48;
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
extern __attribute__((aligned(4))) void * gm_f_held_4aoq406(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_hold_temp_array_rb5nkro(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_hold_temp_array_rb5nkro F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    uint32_t gm_l2; /* t */
    void * gm_l3; /* local */
    GM_STACK_CHECK(&F, 333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)gm_cs1qbc2gaqs));
    void * t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t4));
    void * t6 = gm_l1;
    gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t6) + 72));
    uint32_t t8 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t2, t3, t5, ((uint8_t)0), t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t8;
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 1200);
    uint32_t t11 = gm_l2;
    gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t10, t11);
    gm_l4 = t12;
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src3a4t4bi1ba85, 108, 5);
    void * t14 = gm_l1;
    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t14));
    uint32_t t16 = gm_l2;
    void * t17 = gm_l1;
    gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 72));
    gm_t2b6tm8c0vgo1 t19;
    memset(&t19, 0, sizeof t19);
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t19 + 8), t16);
    void * t20 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t15, &t19, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t20;
    void * t21 = gm_l3;
    void * t22 = gm_l1;
    gm_t3mf58gv9kf3n t23;
    GM_ST(void *, ((uint8_t *)&t23 + 0), t21);
    GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
    void * t24 = gm_l3;
    gm_trcmmd12cefb1 t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t25 + 8), t23);
    gm_tdevt6kjpn5ja t26;
    GM_ST(gm_trcmmd12cefb1, ((uint8_t *)&t26 + 0), t25);
    GM_ST(void *, ((uint8_t *)&t26 + 32), t24);
    memmove(gm_out, ((uint8_t *)&t26), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void * gm_f_index_uh6hfri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_index_value_ufcur8j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_index_value_ufcur8j F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b0 */
    void * gm_l2; /* index_ast */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    void * gm_l5; /* b1 */
    gm_tjpqvfd72o62u gm_l6; /* info */
    void * gm_l7; /* b */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_tnnjeerolos0a gm_l9; /* kind */
    gm_t88rcucg1b7uk gm_l10; /* e */
    void * gm_l11; /* i */
    gm_t88rcucg1b7uk gm_l12; /* it */
    gm_tbngattl1fo51 gm_l13; /* r */
    uint32_t gm_l14; /* t */
    void * gm_l15; /* lo */
    void * gm_l16; /* hi */
    void * gm_l17; /* s */
    gm_t88rcucg1b7uk gm_l19; /* bt */
    gm_t88rcucg1b7uk gm_l20; /* elem */
    uint8_t gm_l21; /* checked */
    gm_t88rcucg1b7uk gm_l22; /* e */
    gm_t88rcucg1b7uk gm_l23; /* e */
    gm_t88rcucg1b7uk gm_l24; /* e */
    gm_t88rcucg1b7uk gm_l25; /* e */
    GM_STACK_CHECK(&F, 2007u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t8cc1mtbrhlpt gm_l26;
    gm_t53at80pfdus3 gm_l27;
    gm_thetuhtouve7f gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_t53at80pfdus3 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        gm_l4 = t21;
        void * t22 = gm_l0;
        void * t23 = gm_l1;
        void * t24 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t24;
        {
            {
                void * t25 = gm_l0;
                void * t26 = gm_l5;
                gm_f_soa_of_jufdcj2((void *)&gm_l26, (void *)NULL, cx, NULL, t25, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l26))[32] != 0);
                if (!t27) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l26), 32);
                void * t28 = gm_l0;
                void * t29 = gm_l5;
                void * t30 = gm_l2;
                gm_tl874t2atnlh3 t31 = gm_l3;
                void * t32 = gm_f_soa_element_04p1d8c((void *)NULL, cx, NULL, t28, t29, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l6)), t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t32;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t33 = gm_l0;
        void * t34 = gm_l5;
        void * t35 = gm_f_auto_deref_array_g2mtf4v((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t35;
        {
            {
                void * t36 = gm_l4;
                void * t37 = gm_l0;
                void * t38 = (void *)(((uint8_t *)t37) + 96);
                void * t39 = gm_l0;
                void * t40 = GM_LD(void *, ((uint8_t *)t39));
                void * t41 = gm_l7;
                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t41));
                gm_t88rcucg1b7uk t43 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t38, t40, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_kind_jcnt4hm((void *)&gm_l27, t36, GM_CONV(gm_csguankko4lmhd, t43));
                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                uint8_t t45 = (uint8_t)(t44 == ((uint8_t)11ULL));
                if (!t45) {
                    goto gm_lb4;
                }
                gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l27) + 8) + 8));
                gm_l8 = t46;
                gm_tnnjeerolos0a t47 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l27) + 8) + 12));
                gm_l9 = t47;
                uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                uint8_t t49 = (uint8_t)(t48 == ((uint8_t)1ULL));
                if (!t49) {
                    goto gm_lb4;
                }
                gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 4));
                gm_l10 = t50;
                void * t51 = gm_l0;
                void * t52 = gm_l7;
                gm_t88rcucg1b7uk t53 = gm_l10;
                gm_t88rcucg1b7uk t54 = gm_l8;
                void * t55 = gm_l2;
                gm_tl874t2atnlh3 t56 = gm_l3;
                void * t57 = gm_f_enum_array_index_kma2hbd((void *)NULL, cx, NULL, t51, t52, t53, t54, t55, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t57;
                goto gm_fret;
            }
gm_lb4:;
        }
gm_lb3:;
        void * t58 = gm_l0;
        void * t59 = gm_l2;
        gm_tkn42u7na3t81 t60;
        memset(&t60, 0, sizeof t60);
        void * t61 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t61;
    }
    {
        void * t62 = gm_l0;
        void * t63 = (void *)(((uint8_t *)t62) + 96);
        void * t64 = gm_l0;
        void * t65 = GM_LD(void *, ((uint8_t *)t64));
        void * t66 = gm_l11;
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t66));
        gm_t88rcucg1b7uk t68 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t63, t65, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t68;
        {
            {
                void * t69 = gm_l0;
                void * t70 = GM_LD(void *, ((uint8_t *)t69));
                gm_t88rcucg1b7uk t71 = gm_l12;
                gm_thetuhtouve7f t72 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t70, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t72;
                uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l28))[8] != 0);
                if (!t73) {
                    goto gm_lb6;
                }
                gm_tbngattl1fo51 t74 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&gm_l28));
                gm_l13 = t74;
                void * t75 = gm_l0;
                gm_t88rcucg1b7uk t76 = gm_l12;
                gm_tl874t2atnlh3 t77 = gm_l3;
                uint32_t t78 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t75, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t78;
                gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                gm_t88rcucg1b7uk t80 = gm_l12;
                uint32_t t81 = gm_l14;
                gm_tl874t2atnlh3 t82 = gm_l3;
                gm_t2b6tm8c0vgo1 t83;
                memset(&t83, 0, sizeof t83);
                GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t83 + 8), t81);
                void * t84 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t80, &t83, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbl8il92l97ut t85;
                GM_ST(uint32_t, (uint8_t *)&t85 + 12, 0);
                GM_ST(void *, ((uint8_t *)&t85 + 0), t84);
                GM_ST(uint32_t, ((uint8_t *)&t85 + 8), ((uint32_t)0ULL));
                gm_tl874t2atnlh3 t86 = gm_l3;
                gm_t2b6tm8c0vgo1 t87;
                memset(&t87, 0, sizeof t87);
                GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)19ULL));
                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t87 + 8), t85);
                void * t88 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t79, &t87, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t88;
                gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                gm_t88rcucg1b7uk t90 = gm_l12;
                uint32_t t91 = gm_l14;
                gm_tl874t2atnlh3 t92 = gm_l3;
                gm_t2b6tm8c0vgo1 t93;
                memset(&t93, 0, sizeof t93);
                GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t93 + 8), t91);
                void * t94 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t90, &t93, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbl8il92l97ut t95;
                GM_ST(uint32_t, (uint8_t *)&t95 + 12, 0);
                GM_ST(void *, ((uint8_t *)&t95 + 0), t94);
                GM_ST(uint32_t, ((uint8_t *)&t95 + 8), ((uint32_t)1ULL));
                gm_tl874t2atnlh3 t96 = gm_l3;
                gm_t2b6tm8c0vgo1 t97;
                memset(&t97, 0, sizeof t97);
                GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)19ULL));
                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t97 + 8), t95);
                void * t98 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t89, &t97, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t98;
                void * t99 = gm_l0;
                void * t100 = gm_l7;
                gm_tl874t2atnlh3 t101 = gm_l3;
                void * t102 = gm_l15;
                void * t103 = gm_l16;
                uint8_t t104 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
                gm_tl874t2atnlh3 t105 = gm_l3;
                void * t106 = gm_f_slice_of_84iab6u((void *)NULL, cx, NULL, t99, t100, t101, t102, t103, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t106;
                gm_trqfvmjtt4sij t107;
                gm_f_list_ir0h3tc_2((void *)&t107, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l18 = t107;
                void * t108 = (void *)((uint8_t *)&F.l18);
                uint32_t t109 = gm_l14;
                void * t110 = gm_l11;
                gm_ttggq7ng4kp3p t111;
                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t111 + 0), t109);
                GM_ST(void *, ((uint8_t *)&t111 + 8), t110);
                gm_trcmmd12cefb1 t112;
                GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t112 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t112 + 8), t111);
                gm_tftb1odc739t5 t113 = gm_f_push_d6c76ud_2(t108, &t112);
                gm_l29 = t113;
                uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src3a4t4bi1ba85, 25, 9);
                void * t115 = gm_l17;
                gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t115));
                gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                void * t118 = gm_l17;
                gm_tl874t2atnlh3 t119 = gm_l3;
                void * t120 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t116, t117, t118, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t120;
                goto gm_fret;
            }
gm_lb6:;
        }
gm_lb5:;
    }
    {
        {
            {
                void * t121 = gm_l0;
                void * t122 = (void *)(((uint8_t *)t121) + 96);
                void * t123 = gm_l4;
                void * t124 = gm_l11;
                gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t124));
                gm_t9qkh0rsooc8d t126;
                GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)1ULL));
                uint8_t t127 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t122, t123, t125, t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t128 = (uint8_t)!t127;
                if (!t128) {
                    goto gm_lb8;
                }
                void * t129 = gm_l4;
                void * t130 = gm_l11;
                gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t130));
                uint8_t t132 = gm_f_is_error_8k27gd1(t129, GM_CONV(gm_csguankko4lmhd, t131));
                uint8_t t133 = (uint8_t)!t132;
                if (!t133) {
                    goto gm_lb8;
                }
                void * t134 = gm_l0;
                void * t135 = gm_l11;
                gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t135) + 72));
                void * t137 = gm_l0;
                gm_t88rcucg1b7uk t138 = gm_l12;
                gm_b16_8 t139 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t137, t138);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t140;
                GM_ST(gm_b16_8, ((uint8_t *)&t140 + 0), t139);
                gm_f_format_amt7kei((void *)&gm_l30, (void *)NULL, cx, NULL, t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_src3a4t4bi1ba85, 29, 26);
                gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                gm_b0_1 t144 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t134, t136, t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
        void * t145 = gm_l0;
        void * t146 = (void *)(((uint8_t *)t145) + 96);
        void * t147 = gm_l0;
        void * t148 = GM_LD(void *, ((uint8_t *)t147));
        void * t149 = gm_l7;
        gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t149));
        gm_t88rcucg1b7uk t151 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t146, t148, t150);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t151;
        gm_t88rcucg1b7uk t152 = gm_l19;
        gm_l20 = t152;
        gm_l21 = ((uint8_t)1);
        void * t153 = gm_l4;
        gm_t88rcucg1b7uk t154 = gm_l19;
        gm_f_kind_jcnt4hm((void *)&gm_l31, t153, GM_CONV(gm_csguankko4lmhd, t154));
        {
            {
                {
                    {
                        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                        uint8_t t156 = (uint8_t)(t155 == ((uint8_t)11ULL));
                        if (!t156) {
                            goto gm_lb12;
                        }
                        gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l31) + 8) + 8));
                        gm_l22 = t157;
                        goto gm_lb11;
                    }
gm_lb12:;
                    {
                        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                        uint8_t t159 = (uint8_t)(t158 == ((uint8_t)20ULL));
                        if (!t159) {
                            goto gm_lb13;
                        }
                        gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l31) + 8) + 4));
                        gm_l22 = t160;
                        goto gm_lb11;
                    }
gm_lb13:;
                    {
                        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                        uint8_t t162 = (uint8_t)(t161 == ((uint8_t)10ULL));
                        if (!t162) {
                            goto gm_lb14;
                        }
                        gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l31) + 8) + 4));
                        gm_l22 = t163;
                        goto gm_lb11;
                    }
gm_lb14:;
                    goto gm_lb10;
                }
gm_lb11:;
                gm_t88rcucg1b7uk t164 = gm_l22;
                gm_l20 = t164;
                goto gm_lb9;
            }
gm_lb10:;
            {
                uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                uint8_t t166 = (uint8_t)(t165 == ((uint8_t)9ULL));
                if (!t166) {
                    goto gm_lb15;
                }
                gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l31) + 8) + 4));
                gm_l25 = t167;
                gm_t88rcucg1b7uk t168 = gm_l25;
                gm_l20 = t168;
                gm_l21 = ((uint8_t)0);
                goto gm_lb9;
            }
gm_lb15:;
            {
                uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                uint8_t t170 = (uint8_t)(t169 == ((uint8_t)26ULL));
                if (!t170) {
                    goto gm_lb16;
                }
                void * t171 = gm_l0;
                void * t172 = GM_LD(void *, ((uint8_t *)t171));
                gm_tl874t2atnlh3 t173 = gm_l3;
                void * t174 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t172, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t174;
                goto gm_fret;
            }
gm_lb16:;
            {
                void * t175 = gm_l0;
                gm_tl874t2atnlh3 t176 = gm_l3;
                void * t177 = gm_l0;
                gm_t88rcucg1b7uk t178 = gm_l19;
                gm_b16_8 t179 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t177, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t180;
                GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t179);
                gm_f_format_6hur18u((void *)&gm_l32, (void *)NULL, cx, NULL, t180);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src3a4t4bi1ba85, 42, 28);
                gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
                gm_b0_1 t184 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t175, t176, t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t185 = gm_l0;
                void * t186 = GM_LD(void *, ((uint8_t *)t185));
                gm_tl874t2atnlh3 t187 = gm_l3;
                void * t188 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t186, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t188;
                goto gm_fret;
            }
gm_lb17:;
            gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 34, 5);
        }
gm_lb9:;
    }
    {
        gm_t88rcucg1b7uk t189 = gm_l20;
        void * t190 = gm_l7;
        void * t191 = gm_l11;
        uint8_t t192 = gm_l21;
        gm_tlcl4u558l6p5 t193;
        GM_ST(uint8_t, (uint8_t *)&t193 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t193 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t193 + 20, 0);
        GM_ST(void *, ((uint8_t *)&t193 + 0), t190);
        GM_ST(void *, ((uint8_t *)&t193 + 8), t191);
        GM_ST(uint8_t, ((uint8_t *)&t193 + 16), t192);
        gm_tl874t2atnlh3 t194 = gm_l3;
        gm_t2b6tm8c0vgo1 t195;
        memset(&t195, 0, sizeof t195);
        GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t195 + 8), t193);
        void * t196 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t189, &t195, t194);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t196;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_held_temp_d00dm1p(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_held_temp_d00dm1p F;
    void * gm_l0; /* e */
    void * gm_l1; /* b */
    void * gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l3;
    uint8_t gm_l4;
    void * gm_l5;
    uint8_t gm_l6;
    {
        {
            void * t10 = gm_l0;
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)46ULL));
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = GM_LD(void *, ((((uint8_t *)t10) + 8) + 8));
            gm_l1 = t13;
            void * t14 = gm_l1;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
            uint8_t t16 = (!(t15 == ((uint64_t)1ULL)));
            if (t16) {
                gm_l3 = ((uint8_t)1);
            } else {
                void * t17 = gm_l1;
                void * t18 = GM_LD(void *, ((uint8_t *)t17));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t17) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t19, gm_src3a4t4bi1ba85, 123, 32);
                {
                    {
                        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t18) + (uintptr_t)((uint64_t)0ULL) * 32) + 0));
                        uint8_t t21 = (uint8_t)(t20 == ((uint8_t)2ULL));
                        if (!t21) {
                            goto gm_lb4;
                        }
                        gm_l4 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l4 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 123, 32);
                }
gm_lb3:;
                uint8_t t22 = gm_l4;
                gm_l3 = t22;
            }
            uint8_t t23 = gm_l3;
            if (t23) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t24 = gm_l1;
            uint8_t t25 = (uint8_t)(GM_LD(void *, (((uint8_t *)t24) + 16)) != 0);
            if (t25) {
                void * t26 = GM_LD(void *, (((uint8_t *)t24) + 16));
                gm_l5 = t26;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t27 = gm_l5;
            gm_l2 = t27;
            void * t28 = gm_l2;
            {
                {
                    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)21ULL));
                    if (!t30) {
                        goto gm_lb7;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 125, 12);
            }
gm_lb6:;
            uint8_t t31 = gm_l6;
            F.ret = t31;
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
__attribute__((aligned(4))) void * gm_f_slice_expr_iamf9el(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint8_t a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_slice_expr_iamf9el F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    void * gm_l2; /* lo_ast */
    void * gm_l3; /* hi_ast */
    uint8_t gm_l4; /* inclusive */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* b */
    void * gm_l8; /* lo */
    void * gm_l9; /* x */
    void * gm_l10; /* hi */
    void * gm_l11; /* x */
    void * gm_l12; /* lo */
    void * gm_l13; /* x */
    void * gm_l14; /* hi */
    void * gm_l15; /* x */
    GM_STACK_CHECK(&F, 787u);
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
    gm_t53at80pfdus3 gm_l17;
    uint8_t gm_l18;
    gm_tqs5p4n2tq19v gm_l19;
    void * t19 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t19) + 472), ((uint8_t)0));
    void * t20 = gm_l0;
    void * t21 = gm_l1;
    gm_tkn42u7na3t81 t22;
    memset(&t22, 0, sizeof t22);
    void * t23 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t20, t21, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t23;
    {
        {
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, ((uint8_t *)t24));
            void * t26 = (void *)(((uint8_t *)t25) + 8);
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 96);
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, ((uint8_t *)t29));
            void * t31 = gm_l6;
            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t31));
            gm_t88rcucg1b7uk t33 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t28, t30, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_kind_jcnt4hm((void *)&gm_l17, t26, GM_CONV(gm_csguankko4lmhd, t33));
            {
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)11ULL));
                    if (!t35) {
                        goto gm_lb4;
                    }
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)20ULL));
                    if (!t37) {
                        goto gm_lb5;
                    }
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_l16 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 62, 8);
            }
gm_lb3:;
            uint8_t t38 = gm_l16;
            if (!t38) {
                goto gm_lb2;
            }
            void * t39 = gm_l0;
            void * t40 = gm_l6;
            gm_tqs5p4n2tq19v t41 = gm_f_place_mutability_0tlm4g8(t39, t40);
            gm_l19 = t41;
            {
                {
                    uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l19))[1] != 0);
                    if (t42) {
                        goto gm_lb8;
                    }
                    gm_l18 = ((uint8_t)1);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    gm_l18 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 62, 87);
            }
gm_lb7:;
            uint8_t t43 = gm_l18;
            if (!t43) {
                goto gm_lb2;
            }
            void * t44 = gm_l0;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t44) + 1240));
            uint8_t t46 = (uint8_t)!t45;
            if (!t46) {
                goto gm_lb2;
            }
            void * t47 = gm_l0;
            void * t48 = gm_l6;
            gm_f_hold_temp_array_rb5nkro((void *)&F.l7, (void *)NULL, cx, NULL, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = ((void *)0);
            {
                {
                    uint8_t t49 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                    if (!t49) {
                        goto gm_lb11;
                    }
                    void * t50 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    gm_l9 = t50;
                    void * t51 = gm_l0;
                    void * t52 = gm_l9;
                    gm_tkn42u7na3t81 t53;
                    memset(&t53, 0, sizeof t53);
                    void * t54 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t51, t52, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t54;
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            gm_l10 = ((void *)0);
            {
                {
                    uint8_t t55 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                    if (!t55) {
                        goto gm_lb13;
                    }
                    void * t56 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    gm_l11 = t56;
                    void * t57 = gm_l0;
                    void * t58 = gm_l11;
                    gm_tkn42u7na3t81 t59;
                    memset(&t59, 0, sizeof t59);
                    void * t60 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t57, t58, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t60;
                    goto gm_lb12;
                }
gm_lb13:;
            }
gm_lb12:;
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, ((uint8_t *)t61));
            void * t63 = gm_l0;
            void * t64 = GM_LD(void *, (((uint8_t *)&F.l7) + 32));
            void * t65 = gm_l1;
            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
            void * t67 = gm_l8;
            void * t68 = gm_l10;
            uint8_t t69 = gm_l4;
            void * t70 = gm_l1;
            gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
            gm_tl874t2atnlh3 t72 = gm_l5;
            gm_trcmmd12cefb1 t73 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)&F.l7));
            gm_tl874t2atnlh3 t74 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t71), GM_CONV(gm_cseahvg8v7p0vs, t72)));
            void * t75 = gm_f_slice_of_84iab6u((void *)NULL, cx, NULL, t63, t64, t66, t67, t68, t69, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t76 = gm_f_held_4aoq406((void *)NULL, cx, NULL, t62, &t73, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t76;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t77 = gm_l0;
    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)t77) + 1240));
    if (t78) {
        void * t79 = gm_l0;
        void * t80 = gm_l6;
        void * t81 = gm_f_const_temp_pjvp4uv((void *)NULL, cx, NULL, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t81;
    }
    gm_l12 = ((void *)0);
    {
        {
            uint8_t t82 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
            if (!t82) {
                goto gm_lb15;
            }
            void * t83 = GM_LD(void *, ((uint8_t *)&gm_l2));
            gm_l13 = t83;
            void * t84 = gm_l0;
            void * t85 = gm_l13;
            gm_tkn42u7na3t81 t86;
            memset(&t86, 0, sizeof t86);
            void * t87 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t84, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t87;
            goto gm_lb14;
        }
gm_lb15:;
    }
gm_lb14:;
    gm_l14 = ((void *)0);
    {
        {
            uint8_t t88 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
            if (!t88) {
                goto gm_lb17;
            }
            void * t89 = GM_LD(void *, ((uint8_t *)&gm_l3));
            gm_l15 = t89;
            void * t90 = gm_l0;
            void * t91 = gm_l15;
            gm_tkn42u7na3t81 t92;
            memset(&t92, 0, sizeof t92);
            void * t93 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t93;
            goto gm_lb16;
        }
gm_lb17:;
    }
gm_lb16:;
    void * t94 = gm_l0;
    void * t95 = gm_l6;
    void * t96 = gm_l1;
    gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
    void * t98 = gm_l12;
    void * t99 = gm_l14;
    uint8_t t100 = gm_l4;
    gm_tl874t2atnlh3 t101 = gm_l5;
    void * t102 = gm_f_slice_of_84iab6u((void *)NULL, cx, NULL, t94, t95, t97, t98, t99, t100, t101);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t102;
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
__attribute__((aligned(4))) void * gm_f_slice_of_84iab6u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2, void * a3, void * a4, uint8_t a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_slice_of_84iab6u F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b0 */
    gm_tl874t2atnlh3 gm_l2; /* base_span */
    void * gm_l3; /* lo */
    void * gm_l4; /* hi */
    uint8_t gm_l5; /* inclusive */
    gm_tl874t2atnlh3 gm_l6; /* span */
    void * gm_l7; /* tys */
    void * gm_l8; /* b */
    gm_t88rcucg1b7uk gm_l9; /* pointee */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_t88rcucg1b7uk gm_l11; /* bt */
    gm_tnnjeerolos0a gm_l12; /* kind */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    uint8_t gm_l14; /* is_const */
    gm_t88rcucg1b7uk gm_l15; /* e */
    gm_t88rcucg1b7uk gm_l16; /* e */
    uint8_t gm_l17; /* m */
    uint8_t gm_l18; /* c */
    gm_t88rcucg1b7uk gm_l19; /* e */
    uint8_t gm_l20; /* c */
    gm_t88rcucg1b7uk gm_l21; /* e */
    gm_t5fru0lp11n7t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    void * gm_l25; /* x */
    void * gm_l26; /* v */
    GM_STACK_CHECK(&F, 1977u);
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
    gm_t53at80pfdus3 gm_l27;
    uint8_t gm_l28;
    gm_t53at80pfdus3 gm_l29;
    gm_t8cc1mtbrhlpt gm_l30;
    gm_t53at80pfdus3 gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    uint8_t gm_l34;
    uint8_t gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_t53at80pfdus3 gm_l37;
    gm_tqs5p4n2tq19v gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    gm_tabb710d9mq2e gm_l43;
    void * gm_l44;
    gm_tabb710d9mq2e gm_l45;
    {
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, ((uint8_t *)t43));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_l7 = t45;
        void * t46 = gm_l1;
        gm_l8 = t46;
        {
            {
                void * t47 = gm_l7;
                void * t48 = gm_l0;
                void * t49 = (void *)(((uint8_t *)t48) + 96);
                void * t50 = gm_l0;
                void * t51 = GM_LD(void *, ((uint8_t *)t50));
                void * t52 = gm_l8;
                gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t52));
                gm_t88rcucg1b7uk t54 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t49, t51, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_kind_jcnt4hm((void *)&gm_l27, t47, GM_CONV(gm_csguankko4lmhd, t54));
                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                uint8_t t56 = (uint8_t)(t55 == ((uint8_t)8ULL));
                if (!t56) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l27) + 8) + 4));
                gm_l9 = t57;
                void * t58 = gm_l7;
                gm_t88rcucg1b7uk t59 = gm_l9;
                gm_f_kind_jcnt4hm((void *)&gm_l29, t58, GM_CONV(gm_csguankko4lmhd, t59));
                {
                    {
                        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                        uint8_t t61 = (uint8_t)(t60 == ((uint8_t)11ULL));
                        if (!t61) {
                            goto gm_lb4;
                        }
                        gm_l28 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                        uint8_t t63 = (uint8_t)(t62 == ((uint8_t)20ULL));
                        if (!t63) {
                            goto gm_lb5;
                        }
                        gm_l28 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        gm_l28 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 132, 71);
                }
gm_lb3:;
                uint8_t t64 = gm_l28;
                if (!t64) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t65 = gm_l9;
                void * t66 = gm_l8;
                gm_tl874t2atnlh3 t67 = gm_l6;
                gm_t2b6tm8c0vgo1 t68;
                memset(&t68, 0, sizeof t68);
                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)18ULL));
                GM_ST(void *, ((uint8_t *)&t68 + 8), t66);
                void * t69 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t65, &t68, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t69;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t70 = gm_l0;
        void * t71 = gm_l8;
        void * t72 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t70, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t72;
        {
            {
                void * t73 = gm_l0;
                void * t74 = gm_l8;
                gm_f_soa_of_jufdcj2((void *)&gm_l30, (void *)NULL, cx, NULL, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l30))[32] != 0);
                if (!t75) {
                    goto gm_lb8;
                }
                memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l30), 32);
                void * t76 = gm_l0;
                void * t77 = gm_l8;
                void * t78 = gm_l3;
                void * t79 = gm_l4;
                uint8_t t80 = gm_l5;
                gm_tl874t2atnlh3 t81 = gm_l6;
                void * t82 = gm_f_soa_slice_4dujfr9((void *)NULL, cx, NULL, t76, t77, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t78, t79, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t82;
                goto gm_fret;
            }
gm_lb8:;
        }
gm_lb7:;
    }
    {
        void * t83 = gm_l0;
        void * t84 = (void *)(((uint8_t *)t83) + 96);
        void * t85 = gm_l0;
        void * t86 = GM_LD(void *, ((uint8_t *)t85));
        void * t87 = gm_l8;
        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t87));
        gm_t88rcucg1b7uk t89 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t84, t86, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t89;
        {
            {
                void * t90 = gm_l7;
                gm_t88rcucg1b7uk t91 = gm_l11;
                gm_f_kind_jcnt4hm((void *)&gm_l31, t90, GM_CONV(gm_csguankko4lmhd, t91));
                uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                uint8_t t93 = (uint8_t)(t92 == ((uint8_t)11ULL));
                if (!t93) {
                    goto gm_lb10;
                }
                gm_tnnjeerolos0a t94 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l31) + 8) + 12));
                gm_l12 = t94;
                {
                    {
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                        uint8_t t96 = (uint8_t)(t95 == ((uint8_t)1ULL));
                        if (!t96) {
                            goto gm_lb12;
                        }
                        gm_l32 = ((uint8_t)1);
                        goto gm_lb11;
                    }
gm_lb12:;
                    {
                        gm_l32 = ((uint8_t)0);
                        goto gm_lb11;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 138, 47);
                }
gm_lb11:;
                uint8_t t97 = gm_l32;
                if (!t97) {
                    goto gm_lb10;
                }
                {
                    {
                        uint8_t t98 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                        if (t98) {
                            goto gm_lb15;
                        }
                        gm_l34 = ((uint8_t)0);
                        goto gm_lb14;
                    }
gm_lb15:;
                    {
                        gm_l34 = ((uint8_t)1);
                        goto gm_lb14;
                    }
gm_lb16:;
                    gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 138, 69);
                }
gm_lb14:;
                uint8_t t99 = gm_l34;
                if (t99) {
                    gm_l33 = ((uint8_t)1);
                } else {
                    {
                        {
                            uint8_t t100 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l4)) != 0);
                            if (t100) {
                                goto gm_lb18;
                            }
                            gm_l35 = ((uint8_t)0);
                            goto gm_lb17;
                        }
gm_lb18:;
                        {
                            gm_l35 = ((uint8_t)1);
                            goto gm_lb17;
                        }
gm_lb19:;
                        gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 138, 87);
                    }
gm_lb17:;
                    uint8_t t101 = gm_l35;
                    gm_l33 = t101;
                }
                uint8_t t102 = gm_l33;
                if (!t102) {
                    goto gm_lb10;
                }
                void * t103 = gm_l0;
                gm_tl874t2atnlh3 t104 = gm_l6;
                void * t105 = gm_l0;
                gm_t88rcucg1b7uk t106 = gm_l11;
                gm_b16_8 t107 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t105, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t108;
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
                gm_f_format_uc7ef2o((void *)&gm_l36, (void *)NULL, cx, NULL, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src3a4t4bi1ba85, 139, 24);
                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                gm_b0_1 t112 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t103, t104, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t113 = gm_l0;
                void * t114 = GM_LD(void *, ((uint8_t *)t113));
                gm_tl874t2atnlh3 t115 = gm_l6;
                void * t116 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t114, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t116;
                goto gm_fret;
            }
gm_lb10:;
        }
gm_lb9:;
    }
    {
        gm_t88rcucg1b7uk t117 = gm_l11;
        gm_l13 = t117;
        gm_l14 = ((uint8_t)0);
        void * t118 = gm_l7;
        gm_t88rcucg1b7uk t119 = gm_l11;
        gm_f_kind_jcnt4hm((void *)&gm_l37, t118, GM_CONV(gm_csguankko4lmhd, t119));
        {
            {
                {
                    {
                        uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                        uint8_t t121 = (uint8_t)(t120 == ((uint8_t)11ULL));
                        if (!t121) {
                            goto gm_lb23;
                        }
                        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 8));
                        gm_l15 = t122;
                        goto gm_lb22;
                    }
gm_lb23:;
                    {
                        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                        uint8_t t124 = (uint8_t)(t123 == ((uint8_t)20ULL));
                        if (!t124) {
                            goto gm_lb24;
                        }
                        gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 4));
                        gm_l15 = t125;
                        goto gm_lb22;
                    }
gm_lb24:;
                    goto gm_lb21;
                }
gm_lb22:;
                void * t126 = gm_l0;
                void * t127 = gm_l8;
                gm_tqs5p4n2tq19v t128 = gm_f_place_mutability_0tlm4g8(t126, t127);
                gm_l38 = t128;
                uint8_t t129 = (uint8_t)((((uint8_t *)&gm_l38))[1] != 0);
                if (t129) {
                    uint8_t t130 = GM_LD(uint8_t, ((uint8_t *)&gm_l38));
                    gm_l39 = t130;
                } else {
                    void * t131 = gm_l0;
                    gm_tl874t2atnlh3 t132 = gm_l2;
                    gm_b16_8 t133;
                    GM_ST(void *, (uint8_t *)&t133, (void *)(gm_s0bc9s1qderg8));
                    GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(69));
                    gm_b0_1 t134 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t131, t132, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t135 = gm_l0;
                    void * t136 = GM_LD(void *, ((uint8_t *)t135));
                    gm_tl874t2atnlh3 t137 = gm_l6;
                    void * t138 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t136, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t138;
                    goto gm_fret;
                }
                uint8_t t139 = gm_l39;
                gm_l17 = t139;
                gm_t88rcucg1b7uk t140 = gm_l15;
                gm_l13 = t140;
                uint8_t t141 = gm_l17;
                uint8_t t142 = (uint8_t)!t141;
                gm_l14 = t142;
                goto gm_lb20;
            }
gm_lb21:;
            {
                uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                uint8_t t144 = (uint8_t)(t143 == ((uint8_t)10ULL));
                if (!t144) {
                    goto gm_lb25;
                }
                uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 8));
                gm_l18 = t145;
                gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 4));
                gm_l19 = t146;
                gm_t88rcucg1b7uk t147 = gm_l19;
                gm_l13 = t147;
                uint8_t t148 = gm_l18;
                gm_l14 = t148;
                goto gm_lb20;
            }
gm_lb25:;
            {
                uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                uint8_t t150 = (uint8_t)(t149 == ((uint8_t)9ULL));
                if (!t150) {
                    goto gm_lb26;
                }
                uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 8));
                gm_l20 = t151;
                gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 4));
                gm_l21 = t152;
                {
                    {
                        uint8_t t153 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                        if (t153) {
                            goto gm_lb28;
                        }
                        gm_l41 = ((uint8_t)1);
                        goto gm_lb27;
                    }
gm_lb28:;
                    {
                        gm_l41 = ((uint8_t)0);
                        goto gm_lb27;
                    }
gm_lb29:;
                    gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 158, 16);
                }
gm_lb27:;
                uint8_t t154 = gm_l41;
                if (t154) {
                    gm_l40 = ((uint8_t)1);
                } else {
                    {
                        {
                            uint8_t t155 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l4)) != 0);
                            if (t155) {
                                goto gm_lb31;
                            }
                            gm_l42 = ((uint8_t)1);
                            goto gm_lb30;
                        }
gm_lb31:;
                        {
                            gm_l42 = ((uint8_t)0);
                            goto gm_lb30;
                        }
gm_lb32:;
                        gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 158, 30);
                    }
gm_lb30:;
                    uint8_t t156 = gm_l42;
                    gm_l40 = t156;
                }
                uint8_t t157 = gm_l40;
                if (t157) {
                    void * t158 = gm_l0;
                    gm_tl874t2atnlh3 t159 = gm_l6;
                    gm_b16_8 t160;
                    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_s7ed59qo09cfg));
                    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(40));
                    gm_b0_1 t161 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t158, t159, t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_t88rcucg1b7uk t162 = gm_l21;
                gm_l13 = t162;
                uint8_t t163 = gm_l20;
                gm_l14 = t163;
                goto gm_lb20;
            }
gm_lb26:;
            {
                uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                uint8_t t165 = (uint8_t)(t164 == ((uint8_t)26ULL));
                if (!t165) {
                    goto gm_lb33;
                }
                void * t166 = gm_l0;
                void * t167 = GM_LD(void *, ((uint8_t *)t166));
                gm_tl874t2atnlh3 t168 = gm_l6;
                void * t169 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t167, t168);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t169;
                goto gm_fret;
            }
gm_lb33:;
            {
                void * t170 = gm_l0;
                gm_tl874t2atnlh3 t171 = gm_l6;
                void * t172 = gm_l0;
                gm_t88rcucg1b7uk t173 = gm_l11;
                gm_b16_8 t174 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t172, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t175;
                GM_ST(gm_b16_8, ((uint8_t *)&t175 + 0), t174);
                gm_f_format_5v26vgi((void *)&gm_l43, (void *)NULL, cx, NULL, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                if (!t176) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_src3a4t4bi1ba85, 164, 28);
                gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                gm_b0_1 t179 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t170, t171, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t180 = gm_l0;
                void * t181 = GM_LD(void *, ((uint8_t *)t180));
                gm_tl874t2atnlh3 t182 = gm_l6;
                void * t183 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t181, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t183;
                goto gm_fret;
            }
gm_lb34:;
            gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 144, 5);
        }
gm_lb20:;
    }
    {
        void * t184 = gm_l3;
        void * t185 = gm_l4;
        gm_t5fru0lp11n7t t186;
        GM_ST(void *, ((uint8_t *)&t186 + 0), t184);
        GM_ST(void *, ((uint8_t *)&t186 + 8), t185);
        gm_l22 = t186;
        void * t187 = (void *)((uint8_t *)&gm_l22);
        uint64_t t188 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t189 = (uintptr_t)((uint64_t)2ULL);
        if (t188 > t189 || t189 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t188, t189, ((uint64_t)2ULL), gm_src3a4t4bi1ba85, 168, 5);
        uint64_t t190 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t191;
        GM_ST(void *, (uint8_t *)&t191, (void *)(((uint8_t *)t187 + (uintptr_t)((uint64_t)0ULL) * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(t190));
        gm_l23 = t191;
        gm_l24 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t192 = gm_l24;
                uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                uint8_t t194 = (t192 < t193);
                if (!t194) {
                    goto gm_lb35;
                }
                {
                    void * t195 = GM_LD(void *, ((uint8_t *)&gm_l23));
                    uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                    uint64_t t197 = gm_l24;
                    {
                        {
                            void * t198 = GM_LD(void *, (((uint8_t *)t195) + (uintptr_t)t197 * 8));
                            gm_l25 = t198;
                            uint8_t t199 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l25)) != 0);
                            if (t199) {
                                void * t200 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                gm_l44 = t200;
                            } else {
                                goto gm_lb37;
                            }
                            void * t201 = gm_l44;
                            gm_l26 = t201;
                            {
                                {
                                    void * t202 = gm_l0;
                                    void * t203 = (void *)(((uint8_t *)t202) + 96);
                                    void * t204 = gm_l7;
                                    void * t205 = gm_l26;
                                    gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t205));
                                    gm_t9qkh0rsooc8d t207;
                                    GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)1ULL));
                                    uint8_t t208 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t203, t204, t206, t207);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t209 = (uint8_t)!t208;
                                    if (!t209) {
                                        goto gm_lb41;
                                    }
                                    void * t210 = gm_l7;
                                    void * t211 = gm_l26;
                                    gm_t88rcucg1b7uk t212 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t211));
                                    uint8_t t213 = gm_f_is_error_8k27gd1(t210, GM_CONV(gm_csguankko4lmhd, t212));
                                    uint8_t t214 = (uint8_t)!t213;
                                    if (!t214) {
                                        goto gm_lb41;
                                    }
                                    void * t215 = gm_l0;
                                    void * t216 = gm_l26;
                                    gm_tl874t2atnlh3 t217 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t216) + 72));
                                    void * t218 = gm_l0;
                                    void * t219 = gm_l0;
                                    void * t220 = (void *)(((uint8_t *)t219) + 96);
                                    void * t221 = gm_l0;
                                    void * t222 = GM_LD(void *, ((uint8_t *)t221));
                                    void * t223 = gm_l26;
                                    gm_t88rcucg1b7uk t224 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t223));
                                    gm_t88rcucg1b7uk t225 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t220, t222, t224);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t226 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t218, t225);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t227;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t227 + 0), t226);
                                    gm_f_format_pkmkakl((void *)&gm_l45, (void *)NULL, cx, NULL, t227);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                    if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_src3a4t4bi1ba85, 171, 30);
                                    gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                                    gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                                    gm_b0_1 t231 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t215, t217, t230);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb40;
                                }
gm_lb41:;
                            }
gm_lb40:;
                            goto gm_lb38;
                        }
gm_lb39:;
                        gm_trap("no match arm matched the scrutinee", gm_src3a4t4bi1ba85, 168, 5);
                    }
gm_lb38:;
                }
gm_lb37:;
                uint64_t t232 = gm_l24;
                uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                uint64_t t235 = r233;
                gm_l24 = t235;
            }
        }
gm_lb35:;
    }
    {
        void * t236 = gm_l7;
        uint8_t t237 = gm_l14;
        gm_t88rcucg1b7uk t238 = gm_l13;
        gm_t88rcucg1b7uk t239 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t236, t237, t238);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t240 = gm_l8;
        void * t241 = gm_l3;
        void * t242 = gm_l4;
        uint8_t t243 = gm_l5;
        gm_tao63pk3r18v5 t244;
        GM_ST(uint8_t, (uint8_t *)&t244 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t244 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t244 + 28, 0);
        GM_ST(void *, ((uint8_t *)&t244 + 0), t240);
        GM_ST(void *, ((uint8_t *)&t244 + 8), t241);
        GM_ST(void *, ((uint8_t *)&t244 + 16), t242);
        GM_ST(uint8_t, ((uint8_t *)&t244 + 24), t243);
        gm_tl874t2atnlh3 t245 = gm_l6;
        gm_t2b6tm8c0vgo1 t246;
        GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t246 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t246 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t246 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)21ULL));
        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t246 + 8), t244);
        void * t247 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t239, &t246, t245);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t247;
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
