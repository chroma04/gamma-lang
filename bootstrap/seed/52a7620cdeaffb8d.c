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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint16_t f0; uint16_t f1; uint16_t f2; uint16_t f3; uint16_t f4; } gm_tdbjst0cng0lo;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_tbvif938ns15l;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tdbjst0cng0lo) == 10 && _Alignof(gm_tdbjst0cng0lo) == 2 && offsetof(gm_tdbjst0cng0lo, f0) == 0 && offsetof(gm_tdbjst0cng0lo, f1) == 2 && offsetof(gm_tdbjst0cng0lo, f2) == 4 && offsetof(gm_tdbjst0cng0lo, f3) == 6 && offsetof(gm_tdbjst0cng0lo, f4) == 8, "layout of `(u16, u16, u16, u16, u16)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_tb31k5l794t7p f0; uint8_t f1; uint8_t pe[7]; } gm_tgmjda3f8g6hg;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { gm_tbvif938ns15l f0; uint8_t f1; uint8_t pe[3]; } gm_t84dv0cg48dut;
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tbvif938ns15l) == 16 && _Alignof(gm_tbvif938ns15l) == 4 && offsetof(gm_tbvif938ns15l, f0) == 0 && offsetof(gm_tbvif938ns15l, f1) == 4, "layout of `(win64: bool, span: Span)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgmjda3f8g6hg) == 24 && _Alignof(gm_tgmjda3f8g6hg) == 8 && offsetof(gm_tgmjda3f8g6hg, f0) == 0 && offsetof(gm_tgmjda3f8g6hg, f1) == 16, "layout of `?(rune: u32, len: usize)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_t08olq97rscn3 f0; gm_tlivgedjs5ps3 f1; } gm_t70h2c775g4n7;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t84dv0cg48dut) == 20 && _Alignof(gm_t84dv0cg48dut) == 4 && offsetof(gm_t84dv0cg48dut, f0) == 0 && offsetof(gm_t84dv0cg48dut, f1) == 16, "layout of `?(win64: bool, span: Span)`");
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t70h2c775g4n7) == 104 && _Alignof(gm_t70h2c775g4n7) == 8 && offsetof(gm_t70h2c775g4n7, f0) == 0 && offsetof(gm_t70h2c775g4n7, f1) == 32, "layout of `(name: Ident, sig: FnSig)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t70h2c775g4n7 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_tg69e9d4krfe7;
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_b16_8 f0; gm_tg69e9d4krfe7 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t7rh4gj19r55v;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg69e9d4krfe7) == 112 && _Alignof(gm_tg69e9d4krfe7) == 8 && offsetof(gm_tg69e9d4krfe7, f0) == 0 && offsetof(gm_tg69e9d4krfe7, f1) == 8, "layout of `ForeignMemberKind`");
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t7rh4gj19r55v) == 144 && _Alignof(gm_t7rh4gj19r55v) == 8 && offsetof(gm_t7rh4gj19r55v, f0) == 0 && offsetof(gm_t7rh4gj19r55v, f1) == 16 && offsetof(gm_t7rh4gj19r55v, f2) == 128, "layout of `ForeignMember`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_add_fn_t4t83qh {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    gm_tdgfduqjheqdm l2; /* def */
    uint32_t ret;
};
struct gm_fr_apply_callconv_197l87p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_callconv_attr_dd08ovc {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_callconv_fn_ptr_l9kavc3 {
    gm_frame h;
    gm_tkacitepg5fk0 l8; /* a */
    gm_tddtjfggdat0d l10; /* sig */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_foreign_6s86jje {
    gm_frame h;
    gm_t7rh4gj19r55v l12; /* member */
    gm_tkacitepg5fk0 l18; /* a */
    gm_t6qooom8qa2do l33; /* value */
    gm_tlivgedjs5ps3 l35; /* sig */
    gm_tvpnktv5fo8nl l41; /* item */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_foreign_fn_u3dekca {
    gm_frame h;
    gm_tg93b272d11vo l4; /* resolved */
    gm_tqqhoivahirr0 l5; /* r */
    gm_t5v0c8ia6a5mo l9; /* p */
    gm_trqfvmjtt4sij l11; /* params */
    gm_trqfvmjtt4sij l12; /* locals */
    gm_t5v0c8ia6a5mo l16; /* p */
    gm_t9805qtonbsag ret;
};
struct gm_fr_declare_global_729nttf {
    gm_frame h;
    gm_t6qooom8qa2do ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
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
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cp9r5v5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nqsj3l3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uphdt5g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_escape_debug_lu2p5se {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_export_fn_8hcuasc {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_t5v0c8ia6a5mo l11; /* p */
    gm_b0_1 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_foreign_info_e0kd6uc {
    gm_frame h;
    gm_thognv9odov03 l4; /* x */
    gm_thognv9odov03 ret;
};
struct gm_fr_format_2ng6k0e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3mrt8ba {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_69lunt5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a14ppi0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_du76tsh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fea89f6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g6mj4ou {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k3kbc4n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mh1raea {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mprr1lp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_oq9akg7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qds4t66 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rb3ud4d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v2s58vm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_c_compatible_2hi3vi8 {
    gm_frame h;
    gm_tddtjfggdat0d l5; /* sig */
    gm_tpautr4br7h3l l13; /* def */
    gm_t0mglksrkfa5e l17; /* f */
    gm_t0mglksrkfa5e l21; /* f */
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_472i7vl {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_4p3ivmb {
    gm_frame h;
    gm_tq7qjpkmqmc5s l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_am7bo0v {
    gm_frame h;
    gm_tsoie1ppgdvcg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_krl2rii {
    gm_frame h;
    gm_thognv9odov03 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_roqi806 {
    gm_frame h;
    gm_tdgfduqjheqdm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8r12kcb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9a4nica {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_icepq2q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ta3kthk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_tk5tca7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
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
struct gm_fr_source_location_ty_slllard {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sys_effect_97o96a1 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_sys_set_pqe8mto {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* one */
    gm_tnpifv20irnph ret;
};
struct gm_fr_trap_hook_fn_5c0a4ds {
    gm_frame h;
    gm_tqqhoivahirr0 l3; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_value_sufn4da {
    gm_frame h;
    gm_t6qooom8qa2do ret;
};
struct gm_fr_within_sys_otp5qp6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_write_146u7aa {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint64_t gm_f_add_member_0jvs7ug(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, const void * a3);
__attribute__((aligned(4))) uint64_t gm_f_add_module_3cgsegf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_callconv_197l87p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_callconv_attr_dd08ovc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void gm_f_declare_foreign_6s86jje(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_declare_foreign_fn_u3dekca(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_global_729nttf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_decode_rune_ugbib8e(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_escape_debug_lu2p5se(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_export_fn_8hcuasc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_foreign_info_e0kd6uc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_thognv9odov03 gm_f_foreign_info_e0kd6uc_c(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_format_2ng6k0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_3mrt8ba(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_69lunt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a14ppi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_du76tsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fea89f6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_g6mj4ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_k3kbc4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_mh1raea(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_mprr1lp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_oq9akg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qds4t66(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_rb3ud4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_v2s58vm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_nt3g9ko(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_c_compatible_2hi3vi8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_runtime_symbol_rj1sp7s(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_krl2rii(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ta3kthk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk5tca7(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_source_location_ty_slllard(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_sys_effect_97o96a1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_sys_set_pqe8mto(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_trap_hook_fn_5c0a4ds(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_unsupported_value_sufn4da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_otp5qp6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_146u7aa(void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e55tuqo(void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_kgk1u1jppkr[40] __attribute__((aligned(8)));
extern const uint8_t gm_s01kcog235p88[45];
extern const uint8_t gm_s1de3v91lp97g[106];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s26gmgbksn2pm[61];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s310nl8k9dnl2[59];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s43ls0s4m665j[57];
extern const uint8_t gm_s4i1t9tcolakn[48];
extern const uint8_t gm_s4nl1jq95dg65[111];
extern const uint8_t gm_s523ivho5m3dq[13];
extern const uint8_t gm_s5974q1kmorlc[52];
extern const uint8_t gm_s5nig6ebl26qo[61];
extern const uint8_t gm_s6aprmc3mqp0t[2];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s6ket4hq2umi0[51];
extern const uint8_t gm_s7v0kgsq3q7vf[57];
extern const uint8_t gm_s80p0ld1j6rfg[10];
extern const uint8_t gm_s81csu1l86dmv[52];
extern const uint8_t gm_s8kfjp2od2jrl[51];
extern const uint8_t gm_s8ro53eq54s60[44];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sa291memmphhk[15];
extern const uint8_t gm_sa902c1tbtg0i[4];
extern const uint8_t gm_sbaogg9p69n9f[38];
extern const uint8_t gm_sbd6ket0ql5ab[9];
extern const uint8_t gm_scj8qp34gs3m8[58];
extern const uint8_t gm_scl6is0vlr1m0[62];
extern const uint8_t gm_sg0tsttb54h9c[85];
extern const uint8_t gm_sge2cgjaklnf2[5];
extern const uint8_t gm_shror0skne60i[45];
extern const uint8_t gm_shud0j6jb8ucc[37];
extern const uint8_t gm_si3qm04ntebij[6];
extern const uint8_t gm_sif4kr35r5dhh[81];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_siqa65grpcsk9[99];
extern const uint8_t gm_sj2jft9n0jm50[3];
extern const uint8_t gm_sj2ngf6ro6oja[65];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sjmngec2on365[5];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_snja7imrvdfb7[11];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sottk452c14qm[56];
extern const uint8_t gm_sovqp4emsh0q8[5];
extern const uint8_t gm_spqalnn72r2cn[34];
extern const uint8_t gm_spvg0rt0dloah[9];
extern const uint8_t gm_sq74a8ckacjn2[41];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccvj6up3gbolp[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sstqe1228ic6k[51];
extern const uint8_t gm_st4bliqvgaqre[127];
extern const uint8_t gm_stm1tprvbfoip[58];
extern const uint8_t gm_str26682vod5l[38];
extern const uint8_t gm_su274du674a98[38];
extern const uint8_t gm_su41v0g030vmk[95];
extern const gm_tmpl gm_tpl0ej2jrvho6u0;
extern const gm_tmpl gm_tpl0j5i33et5u62;
extern const gm_tmpl gm_tpl128vde47jcsu;
extern const gm_tmpl gm_tpl5rd4283boomi;
extern const gm_tmpl gm_tpl74mr52kbqst5;
extern const gm_tmpl gm_tpl7e5tr64a0u6e;
extern const gm_tmpl gm_tpl86cip41cbf58;
extern const gm_tmpl gm_tpl9d1siq0c9pir;
extern const gm_tmpl gm_tpl9kqhb5r0uube;
extern const gm_tmpl gm_tplbp75knfmjbql;
extern const gm_tmpl gm_tplduutdqd8m5r5;
extern const gm_tmpl gm_tplf0gekbh2mtku;
extern const gm_tmpl gm_tplf9434bnahqi1;
extern const gm_tmpl gm_tplijibrlknrr0j;
extern const gm_tmpl gm_tplkh4k0ikfsmp6;
extern const gm_tmpl gm_tpll8q455cp9mhi;
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
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_add_fn_t4t83qh F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 344);
    gm_tftb1odc739t5 t4 = gm_f_push_472i7vl(t3, &(*(gm_tqqhoivahirr0 *)((uint8_t *)a1)));
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmhupam4se3h5, 466, 5);
    void * t6 = gm_l0;
    void * t7 = (void *)(((uint8_t *)t6) + 384);
    gm_tftb1odc739t5 t8 = gm_f_push_roqi806(t7, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
    gm_l4 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmhupam4se3h5, 467, 5);
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 344) + 8));
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 468, 13);
    uint64_t t14 = r12;
    uint32_t t15 = (uint32_t)t14;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1fu8oa9 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 64)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cp9r5v5 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nqsj3l3 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 104)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ot5ttb7 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 136)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uphdt5g F;
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
inline __attribute__((aligned(4))) void gm_f_format_2ng6k0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_2ng6k0e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8kfjp2od2jrl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl86cip41cbf58), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3mrt8ba(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3mrt8ba F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s26gmgbksn2pm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbp75knfmjbql), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_69lunt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_69lunt5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s01kcog235p88));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl74mr52kbqst5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a14ppi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a14ppi0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stm1tprvbfoip));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9kqhb5r0uube), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_du76tsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_du76tsh F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scl6is0vlr1m0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7e5tr64a0u6e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fea89f6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fea89f6 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sstqe1228ic6k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0ej2jrvho6u0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g6mj4ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_g6mj4ou F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sif4kr35r5dhh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(80));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5rd4283boomi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_k3kbc4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_k3kbc4n F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su41v0g030vmk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(94));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplduutdqd8m5r5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mh1raea(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mh1raea F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spqalnn72r2cn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf9434bnahqi1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mprr1lp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mprr1lp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4i1t9tcolakn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplijibrlknrr0j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_oq9akg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_oq9akg7 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq74a8ckacjn2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf0gekbh2mtku), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qds4t66(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qds4t66 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shror0skne60i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkh4k0ikfsmp6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rb3ud4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rb3ud4d F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg0tsttb54h9c));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl128vde47jcsu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v2s58vm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v2s58vm F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5974q1kmorlc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0j5i33et5u62), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 40) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)1ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 547, 78);
    void * t6 = (void *)((((uint8_t *)t3) + (uintptr_t)t5 * 184) + 56);
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_get_espvf96(t6, t7);
    F.ret = t8;
    goto gm_fret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_472i7vl F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ot5ttb7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_icepq2q(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 136), ((uint8_t *)a1), 136);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_4p3ivmb F;
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
        gm_b16_8 t9 = gm_f_empty_slice_nqsj3l3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_tk5tca7(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 104), ((uint8_t *)a1), 104);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 104)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d1r4h19 F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_92arfe8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_m244brh(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_krl2rii(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_krl2rii F;
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
        gm_b16_8 t9 = gm_f_empty_slice_cp9r5v5();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ta3kthk(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_roqi806 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1fu8oa9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_9a4nica(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)a1), 64);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 64)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_9a4nica F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_icepq2q F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_m244brh F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ta3kthk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ta3kthk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk5tca7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_tk5tca7 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_set_state_3j300fi F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_tagrkbqh054h7 t4;
    gm_f_state_a5bnsjb((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 304));
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t5) + 304) + 8));
    uint64_t t8 = gm_l1;
    if ((uintptr_t)t8 >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)t8, t7, gm_srclv0dcbbfhves, 760, 5);
    memmove((((uint8_t *)t6) + (uintptr_t)t8 * 216), ((uint8_t *)&F.l2), 216);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_value_sufn4da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_value_sufn4da F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_b0_1 t5 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6qooom8qa2do t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)11ULL));
    memmove(gm_out, ((uint8_t *)&t6), 208);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_146u7aa(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_146u7aa F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_snja7imrvdfb7));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tpl9d1siq0c9pir), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_apply_callconv_197l87p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_apply_callconv_197l87p F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint8_t gm_l2; /* win64 */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tnpifv20irnph gm_l4; /* effects */
    GM_STACK_CHECK(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t2 = gm_l2;
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 344));
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t4) + 344) + 8));
    uint32_t t7 = gm_l1;
    uint64_t t8 = (uint64_t)t7;
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srccvj6up3gbolp, 253, 16);
    gm_tnpifv20irnph t9 = GM_LD(gm_tnpifv20irnph, ((((uint8_t *)t5) + (uintptr_t)t8 * 136) + 64));
    gm_l4 = t9;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, (((uint8_t *)t10) + 344));
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t10) + 344) + 8));
    uint32_t t13 = gm_l1;
    uint64_t t14 = (uint64_t)t13;
    if ((uintptr_t)t14 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t14, t12, gm_srccvj6up3gbolp, 254, 8);
    uint64_t t15 = GM_LD(uint64_t, (((((uint8_t *)t11) + (uintptr_t)t14 * 136) + 80) + 8));
    uint8_t t16 = (t15 > ((uint64_t)0ULL));
    if (t16) {
        void * t17 = gm_l0;
        gm_tl874t2atnlh3 t18 = gm_l3;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sj2ngf6ro6oja));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(64));
        gm_b0_1 t20 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t21 = gm_l0;
        void * t22 = (void *)((uint8_t *)&gm_l4);
        uint8_t t23 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = (uint8_t)!t23;
        if (t24) {
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l3;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_st4bliqvgaqre));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(126));
            gm_b0_1 t28 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t25, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 344));
            uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 344) + 8));
            uint32_t t32 = gm_l1;
            uint64_t t33 = (uint64_t)t32;
            if ((uintptr_t)t33 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t33, t31, gm_srccvj6up3gbolp, 259, 9);
            GM_ST(uint8_t, ((((uint8_t *)t30) + (uintptr_t)t33 * 136) + 128), ((uint8_t)1));
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
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_callconv_attr_dd08ovc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_callconv_attr_dd08ovc F;
    void * gm_l0; /* s */
    void * gm_l1; /* a */
    gm_b16_8 gm_l2; /* args */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint8_t gm_l4; /* x86_64 */
    gm_b16_8 gm_l5; /* other */
    GM_STACK_CHECK(&F, 696u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    void * t14 = gm_l1;
    uint8_t t15 = (uint8_t)(((((uint8_t *)t14) + 32))[16] != 0);
    if (t15) {
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 32));
        gm_l6 = t16;
    } else {
        gm_b16_8 t17 = gm_f_empty_slice_g3tddrb();
        gm_l6 = t17;
    }
    gm_b16_8 t18 = gm_l6;
    gm_l2 = t18;
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t20 = (!(t19 == ((uint64_t)1ULL)));
    if (t20) {
        gm_l7 = ((uint8_t)1);
    } else {
        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t22, gm_srccvj6up3gbolp, 225, 29);
        {
            {
                uint8_t t23 = GM_LD(uint8_t, (((((uint8_t *)t21) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 0));
                uint8_t t24 = (uint8_t)(t23 == ((uint8_t)8ULL));
                if (!t24) {
                    goto gm_lb2;
                }
                gm_l8 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l8 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 225, 29);
        }
gm_lb1:;
        uint8_t t25 = gm_l8;
        gm_l7 = t25;
    }
    uint8_t t26 = gm_l7;
    if (t26) {
        void * t27 = gm_l0;
        void * t28 = gm_l1;
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 56));
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s7v0kgsq3q7vf));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(56));
        gm_b0_1 t31 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t27, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqs5p4n2tq19v t32;
        memset(&t32, 0, sizeof t32);
        F.ret = t32;
        goto gm_fret;
    }
    {
        {
            void * t33 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t34, gm_srccvj6up3gbolp, 229, 22);
            uint8_t t35 = GM_LD(uint8_t, (((((uint8_t *)t33) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)8ULL));
            if (!t36) {
                goto gm_lb5;
            }
            memmove(((uint8_t *)&gm_l3), (((((uint8_t *)t33) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 8), 32);
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, ((uint8_t *)t37));
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t38) + 88) + 40));
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sje4rbc88csdu));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(6));
            uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
            gm_l4 = t41;
            {
                {
                    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_b16_8 t43;
                    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s6aprmc3mqp0t));
                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(1));
                    uint8_t t44 = (gm_eq0iqiubfm7e(((uint8_t *)&t42), ((uint8_t *)&t43)));
                    if (!t44) {
                        goto gm_lb7;
                    }
                    gm_tqs5p4n2tq19v t45;
                    GM_ST(uint8_t, ((uint8_t *)&t45), ((uint8_t)0));
                    ((uint8_t *)&t45)[1] = 1;
                    F.ret = t45;
                    goto gm_fret;
                }
gm_lb7:;
                {
                    {
                        {
                            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sjmngec2on365));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(4));
                            uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
                            if (!t48) {
                                goto gm_lb10;
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                            gm_b16_8 t50;
                            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_si3qm04ntebij));
                            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(5));
                            uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
                            if (!t51) {
                                goto gm_lb11;
                            }
                            goto gm_lb9;
                        }
gm_lb11:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    uint8_t t52 = gm_l4;
                    uint8_t t53 = (uint8_t)!t52;
                    if (t53) {
                        void * t54 = gm_l0;
                        void * t55 = gm_l1;
                        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 56));
                        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                        gm_tnpifv20irnph t58;
                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
                        gm_f_format_v2s58vm((void *)&gm_l9, (void *)NULL, cx, NULL, t58);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srccvj6up3gbolp, 235, 33);
                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                        gm_b0_1 t62 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t54, t56, t61);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqs5p4n2tq19v t63;
                        memset(&t63, 0, sizeof t63);
                        F.ret = t63;
                        goto gm_fret;
                    }
                    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_b16_8 t65;
                    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_si3qm04ntebij));
                    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(5));
                    uint8_t t66 = (gm_eq0iqiubfm7e(((uint8_t *)&t64), ((uint8_t *)&t65)));
                    gm_tqs5p4n2tq19v t67;
                    GM_ST(uint8_t, ((uint8_t *)&t67), t66);
                    ((uint8_t *)&t67)[1] = 1;
                    F.ret = t67;
                    goto gm_fret;
                }
gm_lb8:;
                {
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_l5 = t68;
                    void * t69 = gm_l0;
                    void * t70 = gm_l1;
                    gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 56));
                    gm_b16_8 t72 = gm_l5;
                    gm_tnpifv20irnph t73;
                    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                    gm_f_format_g6mj4ou((void *)&gm_l10, (void *)NULL, cx, NULL, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                    if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srccvj6up3gbolp, 241, 29);
                    gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                    gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                    gm_b0_1 t77 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t69, t71, t76);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqs5p4n2tq19v t78;
                    memset(&t78, 0, sizeof t78);
                    F.ret = t78;
                    goto gm_fret;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 231, 5);
            }
gm_lb6:;
        }
gm_lb5:;
        gm_tqs5p4n2tq19v t79;
        memset(&t79, 0, sizeof t79);
        F.ret = t79;
        goto gm_fret;
    }
gm_lb4:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_callconv_fn_ptr_l9kavc3 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* attrs */
    void * gm_l3; /* inner */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_tqs5p4n2tq19v gm_l5; /* win64 */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    uint8_t gm_l11; /* w */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l12;
    uint8_t gm_l13;
    gm_t53at80pfdus3 gm_l14;
    uint8_t gm_l15;
    gm_tqs5p4n2tq19v t12;
    memset(&t12, 0, sizeof t12);
    gm_l5 = t12;
    gm_b16_8 t13 = gm_l2;
    gm_l6 = t13;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l7;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t19 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t17) + (uintptr_t)t19 * 72), 72);
                        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                        gm_b16_8 t21;
                        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_spvg0rt0dloah));
                        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(8));
                        uint8_t t22 = (!gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                        if (t22) {
                            void * t23 = gm_l0;
                            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 56));
                            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                            gm_tnpifv20irnph t26;
                            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
                            gm_f_format_a14ppi0((void *)&gm_l12, (void *)NULL, cx, NULL, t26);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srccvj6up3gbolp, 269, 29);
                            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                            gm_b0_1 t30 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t23, t24, t29);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            {
                                {
                                    uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
                                    if (t31) {
                                        goto gm_lb7;
                                    }
                                    gm_l13 = ((uint8_t)0);
                                    goto gm_lb6;
                                }
gm_lb7:;
                                {
                                    gm_l13 = ((uint8_t)1);
                                    goto gm_lb6;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 270, 19);
                            }
gm_lb6:;
                            uint8_t t32 = gm_l13;
                            if (t32) {
                                void * t33 = gm_l0;
                                gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 56));
                                gm_b16_8 t35;
                                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s6ket4hq2umi0));
                                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(50));
                                gm_b0_1 t36 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t33, t34, t35);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            } else {
                                void * t37 = gm_l0;
                                void * t38 = (void *)((uint8_t *)&F.l8);
                                gm_tqs5p4n2tq19v t39 = gm_f_callconv_attr_dd08ovc((void *)NULL, cx, NULL, t37, t38);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l5 = t39;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 267, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t40 = gm_l7;
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            uint64_t t43 = r41;
            gm_l7 = t43;
        }
    }
gm_lb1:;
    void * t44 = gm_l0;
    uint64_t t45 = gm_l1;
    void * t46 = gm_l3;
    gm_t88rcucg1b7uk t47 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t44, t45, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t47;
    {
        {
            void * t48 = gm_l0;
            void * t49 = (void *)(((uint8_t *)t48) + 8);
            gm_t88rcucg1b7uk t50 = gm_l9;
            gm_f_kind_jcnt4hm((void *)&gm_l14, t49, GM_CONV(gm_csguankko4lmhd, t50));
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)16ULL));
            if (!t52) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l14) + 8), 48);
            uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
            if (t53) {
                uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
                gm_l15 = t54;
            } else {
                gm_t88rcucg1b7uk t55 = gm_l9;
                F.ret = t55;
                goto gm_fret;
            }
            uint8_t t56 = gm_l15;
            gm_l11 = t56;
            uint8_t t57 = gm_l11;
            uint8_t t58 = (uint8_t)!t57;
            if (t58) {
                gm_t88rcucg1b7uk t59 = gm_l9;
                F.ret = t59;
                goto gm_fret;
            }
            void * t60 = gm_l0;
            void * t61 = (void *)(((uint8_t *)&F.l10) + 24);
            uint8_t t62 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t63 = (uint8_t)!t62;
            if (t63) {
                void * t64 = gm_l0;
                gm_tl874t2atnlh3 t65 = gm_l4;
                gm_b16_8 t66;
                GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s1de3v91lp97g));
                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(105));
                gm_b0_1 t67 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t64, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t68 = gm_l0;
                void * t69 = (void *)(((uint8_t *)t68) + 8);
                gm_t88rcucg1b7uk t70 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t70;
                goto gm_fret;
            }
            void * t71 = gm_l0;
            void * t72 = (void *)(((uint8_t *)t71) + 8);
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
            gm_tnpifv20irnph t75 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l10) + 24));
            gm_tddtjfggdat0d t76;
            GM_ST(uint32_t, (uint8_t *)&t76 + 20, 0);
            GM_ST(uint8_t, (uint8_t *)&t76 + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 44, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t73);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76 + 16), t74);
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t76 + 24), t75);
            GM_ST(uint8_t, ((uint8_t *)&t76 + 40), ((uint8_t)1));
            gm_t53at80pfdus3 t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t77 + 8), t76);
            gm_t88rcucg1b7uk t78 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t72, &t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t78;
            goto gm_fret;
        }
gm_lb10:;
        gm_t88rcucg1b7uk t79 = gm_l9;
        F.ret = t79;
        goto gm_fret;
    }
gm_lb9:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_declare_foreign_6s86jje(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_foreign_6s86jje F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* block */
    gm_b16_8 gm_l4; /* lib */
    uint8_t gm_l5; /* lib_ok */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    uint64_t gm_l9; /* m */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_th2lqbo77pvsc gm_l13; /* link_name */
    gm_t84dv0cg48dut gm_l14; /* callconv */
    uint8_t gm_l15; /* thread_local */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint8_t gm_l19; /* ok */
    gm_b16_8 gm_l20; /* args */
    gm_tibupt4dmcqai gm_l21; /* lit */
    gm_b16_8 gm_l22; /* sym */
    uint8_t gm_l23; /* good */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint8_t gm_l26; /* c */
    uint8_t gm_l27; /* w */
    gm_b16_8 gm_l28; /* other */
    gm_t08olq97rscn3 gm_l29; /* n */
    gm_t08olq97rscn3 gm_l30; /* n */
    gm_t08olq97rscn3 gm_l31; /* own_name */
    gm_b16_8 gm_l32; /* sym */
    gm_t08olq97rscn3 gm_l34; /* n */
    uint32_t gm_l36; /* f */
    gm_tbvif938ns15l gm_l37; /* c */
    gm_t08olq97rscn3 gm_l38; /* n */
    void * gm_l39; /* ty_expr */
    gm_t88rcucg1b7uk gm_l40; /* t */
    uint64_t gm_l42; /* d */
    gm_t88rcucg1b7uk gm_l43; /* t */
    GM_STACK_CHECK(&F, 5150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    uint8_t gm_l44;
    uint8_t gm_l45;
    gm_tabb710d9mq2e gm_l46;
    uint8_t gm_l47;
    gm_tabb710d9mq2e gm_l48;
    uint8_t gm_l49;
    uint8_t gm_l50;
    gm_tqs5p4n2tq19v gm_l51;
    uint8_t gm_l52;
    gm_tabb710d9mq2e gm_l53;
    gm_t08olq97rscn3 gm_l54;
    gm_b16_8 gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_t9805qtonbsag gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tabb710d9mq2e gm_l59;
    uint8_t gm_l60;
    gm_tf5srh4mp4b3i gm_l61;
    gm_tabb710d9mq2e gm_l62;
    gm_tq7s98ihonkju gm_l63;
    gm_tftb1odc739t5 gm_l64;
    {
        void * t57 = gm_l3;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)t57));
        gm_l4 = t58;
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint8_t t60 = (t59 > ((uint64_t)0ULL));
        if (t60) {
            void * t61 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t61) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t64 = (!(t63 == ((uint8_t)45ULL)));
            gm_l44 = t64;
        } else {
            gm_l44 = ((uint8_t)0);
        }
        uint8_t t65 = gm_l44;
        gm_l5 = t65;
        gm_b16_8 t66 = gm_l4;
        gm_l6 = t66;
        gm_l7 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t67 = gm_l7;
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint8_t t69 = (t67 < t68);
                if (!t69) {
                    goto gm_lb1;
                }
                {
                    void * t70 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t72 = gm_l7;
                    {
                        {
                            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)t70) + (uintptr_t)t72 * 1));
                            gm_l8 = t73;
                            uint8_t t74 = gm_l8;
                            uint8_t t75 = gm_f_is_alnum_n81ervr(t74);
                            if (t75) {
                                gm_l45 = ((uint8_t)1);
                            } else {
                                uint8_t t76 = gm_l8;
                                gm_b16_8 t77;
                                GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sovqp4emsh0q8));
                                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(4));
                                uint8_t t78 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t77) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t77))) + i * 1, ((uint8_t *)&t76))) { t78 = 1; break; }
                                uint8_t t79 = (uint8_t)(t78);
                                gm_l45 = t79;
                            }
                            uint8_t t80 = gm_l45;
                            uint8_t t81 = (uint8_t)!t80;
                            if (t81) {
                                gm_l5 = ((uint8_t)0);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 56, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t82 = gm_l7;
                uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                uint64_t t85 = r83;
                gm_l7 = t85;
            }
        }
gm_lb1:;
        uint8_t t86 = gm_l5;
        uint8_t t87 = (uint8_t)!t86;
        if (t87) {
            gm_b16_8 t88 = gm_l4;
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            uint8_t t90 = gm_f_utf8_valid_a8oc1t8(t89);
            uint8_t t91 = (uint8_t)!t90;
            if (t91) {
                void * t92 = gm_l0;
                gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b16_8 t94;
                GM_ST(void *, (uint8_t *)&t94, (void *)(gm_su274du674a98));
                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(37));
                gm_f_unsupported_value_sufn4da((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t92, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            void * t95 = gm_l0;
            gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            gm_b16_8 t97 = gm_l4;
            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
            gm_b16_8 t99 = gm_f_escape_debug_lu2p5se((void *)NULL, cx, NULL, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t100;
            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t99);
            gm_f_format_du76tsh((void *)&gm_l46, (void *)NULL, cx, NULL, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
            if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srccvj6up3gbolp, 61, 28);
            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
            gm_b0_1 t104 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t95, t96, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t105 = gm_l0;
        void * t106 = GM_LD(void *, ((uint8_t *)t105));
        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_b16_8 t108 = gm_f_empty_slice_uphdt5g();
        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
        uint64_t t110 = gm_f_add_module_3cgsegf((void *)NULL, cx, NULL, t106, t107, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t110;
        void * t111 = gm_l0;
        void * t112 = GM_LD(void *, ((uint8_t *)t111));
        void * t113 = GM_LD(void *, (((uint8_t *)t112) + 200));
        uint64_t t114 = GM_LD(uint64_t, ((((uint8_t *)t112) + 200) + 8));
        uint64_t t115 = gm_l9;
        if ((uintptr_t)t115 >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)t115, t114, gm_srccvj6up3gbolp, 64, 5);
        void * t116 = gm_l0;
        void * t117 = GM_LD(void *, ((uint8_t *)t116));
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 200));
        uint64_t t119 = GM_LD(uint64_t, ((((uint8_t *)t117) + 200) + 8));
        uint64_t t120 = gm_l1;
        if ((uintptr_t)t120 >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)t120, t119, gm_srccvj6up3gbolp, 64, 32);
        gm_b16_8 t121 = GM_LD(gm_b16_8, ((((uint8_t *)t118) + (uintptr_t)t120 * 184) + 120));
        GM_ST(gm_b16_8, ((((uint8_t *)t113) + (uintptr_t)t115 * 184) + 120), t121);
        void * t122 = gm_l0;
        void * t123 = GM_LD(void *, ((uint8_t *)t122));
        void * t124 = GM_LD(void *, (((uint8_t *)t123) + 200));
        uint64_t t125 = GM_LD(uint64_t, ((((uint8_t *)t123) + 200) + 8));
        uint64_t t126 = gm_l9;
        if ((uintptr_t)t126 >= (uintptr_t)(t125)) gm_trap_index((uintptr_t)t126, t125, gm_srccvj6up3gbolp, 65, 5);
        void * t127 = gm_l0;
        void * t128 = GM_LD(void *, ((uint8_t *)t127));
        void * t129 = GM_LD(void *, (((uint8_t *)t128) + 200));
        uint64_t t130 = GM_LD(uint64_t, ((((uint8_t *)t128) + 200) + 8));
        uint64_t t131 = gm_l1;
        if ((uintptr_t)t131 >= (uintptr_t)(t130)) gm_trap_index((uintptr_t)t131, t130, gm_srccvj6up3gbolp, 65, 36);
        uint64_t t132 = GM_LD(uint64_t, ((((uint8_t *)t129) + (uintptr_t)t131 * 184) + 136));
        GM_ST(uint64_t, ((((uint8_t *)t124) + (uintptr_t)t126 * 184) + 136), t132);
        void * t133 = gm_l3;
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)t133) + 24));
        gm_l10 = t134;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t135 = gm_l11;
                    uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t137 = (t135 < t136);
                    if (!t137) {
                        goto gm_lb6;
                    }
                    {
                        {
                            void * t138 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t140 = gm_l11;
                            {
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t138) + (uintptr_t)t140 * 144), 144);
                                            gm_th2lqbo77pvsc t141;
                                            memset(&t141, 0, sizeof t141);
                                            gm_l13 = t141;
                                            gm_t84dv0cg48dut t142;
                                            memset(&t142, 0, sizeof t142);
                                            gm_l14 = t142;
                                            gm_l15 = ((uint8_t)0);
                                            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                            gm_l16 = t143;
                                            gm_l17 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t144 = gm_l17;
                                                    uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                    uint8_t t146 = (t144 < t145);
                                                    if (!t146) {
                                                        goto gm_lb11;
                                                    }
                                                    {
                                                        void * t147 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                                        uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                        uint64_t t149 = gm_l17;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&F.l18), (((uint8_t *)t147) + (uintptr_t)t149 * 72), 72);
                                                                {
                                                                    {
                                                                        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                                                        gm_b16_8 t151;
                                                                        GM_ST(void *, (uint8_t *)&t151, (void *)(gm_s80p0ld1j6rfg));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(9));
                                                                        uint8_t t152 = (gm_eq0iqiubfm7e(((uint8_t *)&t150), ((uint8_t *)&t151)));
                                                                        if (!t152) {
                                                                            goto gm_lb17;
                                                                        }
                                                                        gm_l19 = ((uint8_t)0);
                                                                        {
                                                                            {
                                                                                uint8_t t153 = (uint8_t)(((((uint8_t *)&F.l18) + 32))[16] != 0);
                                                                                if (!t153) {
                                                                                    goto gm_lb19;
                                                                                }
                                                                                gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 32));
                                                                                gm_l20 = t154;
                                                                                uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                                                uint8_t t156 = ((t155 == ((uint64_t)1ULL)));
                                                                                if (!t156) {
                                                                                    goto gm_lb19;
                                                                                }
                                                                                void * t157 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                                                                uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t158)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t158, gm_srccvj6up3gbolp, 74, 74);
                                                                                uint8_t t159 = GM_LD(uint8_t, (((((uint8_t *)t157) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 0));
                                                                                uint8_t t160 = (uint8_t)(t159 == ((uint8_t)2ULL));
                                                                                if (!t160) {
                                                                                    goto gm_lb19;
                                                                                }
                                                                                memmove(((uint8_t *)&gm_l21), (((((uint8_t *)t157) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 8), 24);
                                                                                gm_l19 = ((uint8_t)1);
                                                                                gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                                                                gm_l22 = t161;
                                                                                uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                                                uint8_t t163 = (t162 > ((uint64_t)0ULL));
                                                                                gm_l23 = t163;
                                                                                gm_b16_8 t164 = gm_l22;
                                                                                gm_l24 = t164;
                                                                                gm_l25 = ((uint64_t)0ULL);
                                                                                {
                                                                                    for (;;) {
                                                                                        uint64_t t165 = gm_l25;
                                                                                        uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                                                        uint8_t t167 = (t165 < t166);
                                                                                        if (!t167) {
                                                                                            goto gm_lb20;
                                                                                        }
                                                                                        {
                                                                                            void * t168 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                                                            uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                                                            uint64_t t170 = gm_l25;
                                                                                            {
                                                                                                {
                                                                                                    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)t168) + (uintptr_t)t170 * 1));
                                                                                                    gm_l26 = t171;
                                                                                                    uint8_t t172 = gm_l26;
                                                                                                    uint8_t t173 = gm_f_is_alnum_n81ervr(t172);
                                                                                                    if (t173) {
                                                                                                        gm_l47 = ((uint8_t)1);
                                                                                                    } else {
                                                                                                        uint8_t t174 = gm_l26;
                                                                                                        gm_b16_8 t175;
                                                                                                        GM_ST(void *, (uint8_t *)&t175, (void *)(gm_sge2cgjaklnf2));
                                                                                                        GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(4));
                                                                                                        uint8_t t176 = 0;
                                                                                                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t175) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t175))) + i * 1, ((uint8_t *)&t174))) { t176 = 1; break; }
                                                                                                        uint8_t t177 = (uint8_t)(t176);
                                                                                                        gm_l47 = t177;
                                                                                                    }
                                                                                                    uint8_t t178 = gm_l47;
                                                                                                    uint8_t t179 = (uint8_t)!t178;
                                                                                                    if (t179) {
                                                                                                        gm_l23 = ((uint8_t)0);
                                                                                                    }
                                                                                                    goto gm_lb23;
                                                                                                }
gm_lb24:;
                                                                                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 78, 25);
                                                                                            }
gm_lb23:;
                                                                                        }
gm_lb22:;
                                                                                        uint64_t t180 = gm_l25;
                                                                                        uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                                                                                        uint64_t t183 = r181;
                                                                                        gm_l25 = t183;
                                                                                    }
                                                                                }
gm_lb20:;
                                                                                uint8_t t184 = gm_l23;
                                                                                if (t184) {
                                                                                    gm_b16_8 t185 = gm_l22;
                                                                                    gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                                                                                    gm_th2lqbo77pvsc t187;
                                                                                    GM_ST(uint8_t, (uint8_t *)&t187 + 17, 0);
                                                                                    GM_ST(uint16_t, (uint8_t *)&t187 + 18, 0);
                                                                                    GM_ST(uint32_t, (uint8_t *)&t187 + 20, 0);
                                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t187), t186);
                                                                                    ((uint8_t *)&t187)[16] = 1;
                                                                                    gm_l13 = t187;
                                                                                } else {
                                                                                    gm_b16_8 t188 = gm_l22;
                                                                                    gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                                                                                    uint8_t t190 = gm_f_utf8_valid_a8oc1t8(t189);
                                                                                    uint8_t t191 = (uint8_t)!t190;
                                                                                    if (t191) {
                                                                                        void * t192 = gm_l0;
                                                                                        void * t193 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                                                                        uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t194)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t194, gm_srccvj6up3gbolp, 84, 82);
                                                                                        gm_tl874t2atnlh3 t195 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t193) + (uintptr_t)((uint64_t)0ULL) * 88) + 72));
                                                                                        gm_b16_8 t196;
                                                                                        GM_ST(void *, (uint8_t *)&t196, (void *)(gm_shud0j6jb8ucc));
                                                                                        GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(36));
                                                                                        gm_f_unsupported_value_sufn4da((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t192, t195, t196);
                                                                                        if (gm_unw.kind) {
                                                                                            goto gm_lad;
                                                                                        }
                                                                                        goto gm_fdone;
                                                                                    }
                                                                                    void * t197 = gm_l0;
                                                                                    void * t198 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                                                                    uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t199)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t199, gm_srccvj6up3gbolp, 85, 37);
                                                                                    gm_tl874t2atnlh3 t200 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t198) + (uintptr_t)((uint64_t)0ULL) * 88) + 72));
                                                                                    gm_b16_8 t201 = gm_l22;
                                                                                    gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                                                                                    gm_b16_8 t203 = gm_f_escape_debug_lu2p5se((void *)NULL, cx, NULL, t202);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    gm_tnpifv20irnph t204;
                                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t204 + 0), t203);
                                                                                    gm_f_format_3mrt8ba((void *)&gm_l48, (void *)NULL, cx, NULL, t204);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                                                                    if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srccvj6up3gbolp, 85, 51);
                                                                                    gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                                                                                    gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
                                                                                    gm_b0_1 t208 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t197, t200, t207);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                }
                                                                                goto gm_lb18;
                                                                            }
gm_lb19:;
                                                                        }
gm_lb18:;
                                                                        uint8_t t209 = gm_l19;
                                                                        uint8_t t210 = (uint8_t)!t209;
                                                                        if (t210) {
                                                                            void * t211 = gm_l0;
                                                                            gm_tl874t2atnlh3 t212 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 56));
                                                                            gm_b16_8 t213;
                                                                            GM_ST(void *, (uint8_t *)&t213, (void *)(gm_sottk452c14qm));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(55));
                                                                            gm_b0_1 t214 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t211, t212, t213);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb16;
                                                                    }
gm_lb17:;
                                                                    {
                                                                        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                                                        gm_b16_8 t216;
                                                                        GM_ST(void *, (uint8_t *)&t216, (void *)(gm_spvg0rt0dloah));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(8));
                                                                        uint8_t t217 = (gm_eq0iqiubfm7e(((uint8_t *)&t215), ((uint8_t *)&t216)));
                                                                        if (!t217) {
                                                                            goto gm_lb25;
                                                                        }
                                                                        {
                                                                            {
                                                                                uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                                                uint8_t t219 = (uint8_t)(t218 == ((uint8_t)0ULL));
                                                                                if (!t219) {
                                                                                    goto gm_lb27;
                                                                                }
                                                                                gm_l49 = ((uint8_t)1);
                                                                                goto gm_lb26;
                                                                            }
gm_lb27:;
                                                                            {
                                                                                gm_l49 = ((uint8_t)0);
                                                                                goto gm_lb26;
                                                                            }
gm_lb28:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 91, 24);
                                                                        }
gm_lb26:;
                                                                        uint8_t t220 = gm_l49;
                                                                        if (t220) {
                                                                            {
                                                                                {
                                                                                    uint8_t t221 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                                                                                    if (t221) {
                                                                                        goto gm_lb30;
                                                                                    }
                                                                                    gm_l50 = ((uint8_t)0);
                                                                                    goto gm_lb29;
                                                                                }
gm_lb30:;
                                                                                {
                                                                                    gm_l50 = ((uint8_t)1);
                                                                                    goto gm_lb29;
                                                                                }
gm_lb31:;
                                                                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 92, 28);
                                                                            }
gm_lb29:;
                                                                            uint8_t t222 = gm_l50;
                                                                            if (t222) {
                                                                                void * t223 = gm_l0;
                                                                                gm_tl874t2atnlh3 t224 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 56));
                                                                                gm_b16_8 t225;
                                                                                GM_ST(void *, (uint8_t *)&t225, (void *)(gm_str26682vod5l));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(37));
                                                                                gm_b0_1 t226 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t223, t224, t225);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                            }
                                                                            gm_t84dv0cg48dut t227;
                                                                            memset(&t227, 0, sizeof t227);
                                                                            gm_l14 = t227;
                                                                            {
                                                                                {
                                                                                    void * t228 = gm_l0;
                                                                                    void * t229 = (void *)((uint8_t *)&F.l18);
                                                                                    gm_tqs5p4n2tq19v t230 = gm_f_callconv_attr_dd08ovc((void *)NULL, cx, NULL, t228, t229);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    gm_l51 = t230;
                                                                                    uint8_t t231 = (uint8_t)((((uint8_t *)&gm_l51))[1] != 0);
                                                                                    if (!t231) {
                                                                                        goto gm_lb33;
                                                                                    }
                                                                                    uint8_t t232 = GM_LD(uint8_t, ((uint8_t *)&gm_l51));
                                                                                    gm_l27 = t232;
                                                                                    uint8_t t233 = gm_l27;
                                                                                    gm_tl874t2atnlh3 t234 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 56));
                                                                                    gm_tbvif938ns15l t235;
                                                                                    GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
                                                                                    GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
                                                                                    GM_ST(uint8_t, ((uint8_t *)&t235 + 0), t233);
                                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t235 + 4), t234);
                                                                                    gm_t84dv0cg48dut t236;
                                                                                    GM_ST(uint8_t, (uint8_t *)&t236 + 17, 0);
                                                                                    GM_ST(uint16_t, (uint8_t *)&t236 + 18, 0);
                                                                                    GM_ST(gm_tbvif938ns15l, ((uint8_t *)&t236), t235);
                                                                                    ((uint8_t *)&t236)[16] = 1;
                                                                                    gm_l14 = t236;
                                                                                    goto gm_lb32;
                                                                                }
gm_lb33:;
                                                                            }
gm_lb32:;
                                                                        } else {
                                                                            void * t237 = gm_l0;
                                                                            gm_tl874t2atnlh3 t238 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 56));
                                                                            gm_b16_8 t239;
                                                                            GM_ST(void *, (uint8_t *)&t239, (void *)(gm_s43ls0s4m665j));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(56));
                                                                            gm_b0_1 t240 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t237, t238, t239);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb16;
                                                                    }
gm_lb25:;
                                                                    {
                                                                        gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                                                        gm_b16_8 t242;
                                                                        GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s523ivho5m3dq));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(12));
                                                                        uint8_t t243 = (gm_eq0iqiubfm7e(((uint8_t *)&t241), ((uint8_t *)&t242)));
                                                                        if (!t243) {
                                                                            goto gm_lb34;
                                                                        }
                                                                        {
                                                                            {
                                                                                uint8_t t244 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                                                uint8_t t245 = (uint8_t)(t244 == ((uint8_t)1ULL));
                                                                                if (!t245) {
                                                                                    goto gm_lb36;
                                                                                }
                                                                                gm_l52 = ((uint8_t)1);
                                                                                goto gm_lb35;
                                                                            }
gm_lb36:;
                                                                            {
                                                                                gm_l52 = ((uint8_t)0);
                                                                                goto gm_lb35;
                                                                            }
gm_lb37:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 101, 24);
                                                                        }
gm_lb35:;
                                                                        uint8_t t246 = gm_l52;
                                                                        if (t246) {
                                                                            gm_l15 = ((uint8_t)1);
                                                                        } else {
                                                                            void * t247 = gm_l0;
                                                                            gm_tl874t2atnlh3 t248 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 56));
                                                                            gm_b16_8 t249;
                                                                            GM_ST(void *, (uint8_t *)&t249, (void *)(gm_s8ro53eq54s60));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t249 + 8, (uintptr_t)(43));
                                                                            gm_b0_1 t250 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t247, t248, t249);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb16;
                                                                    }
gm_lb34:;
                                                                    {
                                                                        gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                                                        gm_l28 = t251;
                                                                        void * t252 = gm_l0;
                                                                        gm_tl874t2atnlh3 t253 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 56));
                                                                        gm_b16_8 t254 = gm_l28;
                                                                        gm_tnpifv20irnph t255;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t254);
                                                                        gm_f_format_2ng6k0e((void *)&gm_l53, (void *)NULL, cx, NULL, t255);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                                                        if (!t256) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_srccvj6up3gbolp, 107, 42);
                                                                        gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                                                                        gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                                                                        gm_b0_1 t259 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t252, t253, t258);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb16;
                                                                    }
gm_lb38:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 71, 13);
                                                                }
gm_lb16:;
                                                                goto gm_lb14;
                                                            }
gm_lb15:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 70, 9);
                                                        }
gm_lb14:;
                                                    }
gm_lb13:;
                                                    uint64_t t260 = gm_l17;
                                                    uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                                                    uint64_t t263 = r261;
                                                    gm_l17 = t263;
                                                }
                                            }
gm_lb11:;
                                        }
                                        {
                                            {
                                                {
                                                    uint8_t t264 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                    uint8_t t265 = (uint8_t)(t264 == ((uint8_t)0ULL));
                                                    if (!t265) {
                                                        goto gm_lb40;
                                                    }
                                                    memmove(((uint8_t *)&gm_l29), ((((uint8_t *)&F.l12) + 16) + 8), 32);
                                                    memmove(((uint8_t *)&gm_l54), ((uint8_t *)&gm_l29), 32);
                                                    goto gm_lb39;
                                                }
gm_lb40:;
                                                {
                                                    uint8_t t266 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                    uint8_t t267 = (uint8_t)(t266 == ((uint8_t)1ULL));
                                                    if (!t267) {
                                                        goto gm_lb41;
                                                    }
                                                    memmove(((uint8_t *)&gm_l30), ((((uint8_t *)&F.l12) + 16) + 8), 32);
                                                    memmove(((uint8_t *)&gm_l54), ((uint8_t *)&gm_l30), 32);
                                                    goto gm_lb39;
                                                }
gm_lb41:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 110, 21);
                                            }
gm_lb39:;
                                            memmove(((uint8_t *)&gm_l31), ((uint8_t *)&gm_l54), 32);
                                            uint8_t t268 = (uint8_t)((((uint8_t *)&gm_l13))[16] != 0);
                                            if (t268) {
                                                gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                                gm_l55 = t269;
                                            } else {
                                                gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                                gm_l55 = t270;
                                            }
                                            gm_b16_8 t271 = gm_l55;
                                            gm_l32 = t271;
                                            gm_b16_8 t272 = gm_l32;
                                            uint8_t t273 = gm_f_is_runtime_symbol_rj1sp7s(t272);
                                            if (t273) {
                                                void * t274 = gm_l0;
                                                gm_tl874t2atnlh3 t275 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l31) + 16));
                                                gm_b16_8 t276 = gm_l32;
                                                gm_tnpifv20irnph t277;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t277 + 0), t276);
                                                gm_f_format_k3kbc4n((void *)&gm_l56, (void *)NULL, cx, NULL, t277);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t278 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                if (!t278) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_srccvj6up3gbolp, 117, 36);
                                                gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                                                gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
                                                gm_b0_1 t281 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t274, t275, t280);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            gm_t6qooom8qa2do t282;
                                            memset(&t282, 0, sizeof t282);
                                            GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)11ULL));
                                            F.l33 = t282;
                                            {
                                                {
                                                    uint8_t t283 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                    uint8_t t284 = (uint8_t)(t283 == ((uint8_t)0ULL));
                                                    if (!t284) {
                                                        goto gm_lb43;
                                                    }
                                                    memmove(((uint8_t *)&gm_l34), ((((uint8_t *)&F.l12) + 16) + 8), 32);
                                                    memmove(((uint8_t *)&F.l35), (((((uint8_t *)&F.l12) + 16) + 8) + 32), 72);
                                                    {
                                                        {
                                                            void * t285 = gm_l0;
                                                            uint64_t t286 = gm_l1;
                                                            void * t287 = (void *)((uint8_t *)&F.l35);
                                                            gm_t9805qtonbsag t288 = gm_f_declare_foreign_fn_u3dekca((void *)NULL, cx, NULL, t285, t286, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l34)), t287);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_l57 = t288;
                                                            uint8_t t289 = (uint8_t)((((uint8_t *)&gm_l57))[4] != 0);
                                                            if (!t289) {
                                                                goto gm_lb45;
                                                            }
                                                            uint32_t t290 = GM_LD(uint32_t, ((uint8_t *)&gm_l57));
                                                            gm_l36 = t290;
                                                            {
                                                                {
                                                                    uint8_t t291 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                                                                    if (!t291) {
                                                                        goto gm_lb47;
                                                                    }
                                                                    gm_tbvif938ns15l t292 = GM_LD(gm_tbvif938ns15l, ((uint8_t *)&gm_l14));
                                                                    gm_l37 = t292;
                                                                    void * t293 = gm_l0;
                                                                    uint32_t t294 = gm_l36;
                                                                    uint8_t t295 = GM_LD(uint8_t, ((uint8_t *)&gm_l37));
                                                                    gm_tl874t2atnlh3 t296 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l37) + 4));
                                                                    gm_b0_1 t297 = gm_f_apply_callconv_197l87p((void *)NULL, cx, NULL, t293, t294, t295, t296);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    goto gm_lb46;
                                                                }
gm_lb47:;
                                                            }
gm_lb46:;
                                                            void * t298 = gm_l0;
                                                            void * t299 = (void *)(((uint8_t *)t298) + 1528);
                                                            uint32_t t300 = gm_l36;
                                                            gm_b16_8 t301 = gm_l4;
                                                            gm_b16_8 t302 = GM_LD(gm_b16_8, ((uint8_t *)&t301));
                                                            gm_b16_8 t303 = gm_l32;
                                                            gm_thognv9odov03 t304;
                                                            GM_ST(uint32_t, (uint8_t *)&t304 + 4, 0);
                                                            GM_ST(uint32_t, ((uint8_t *)&t304 + 0), t300);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t304 + 8), t302);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t304 + 24), t303);
                                                            gm_tftb1odc739t5 t305 = gm_f_push_krl2rii(t299, &t304);
                                                            gm_l58 = t305;
                                                            uint8_t t306 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                                            if (!t306) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srccvj6up3gbolp, 124, 21);
                                                            uint32_t t307 = gm_l36;
                                                            gm_t6qooom8qa2do t308;
                                                            memset(&t308, 0, sizeof t308);
                                                            GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)5ULL));
                                                            GM_ST(uint32_t, ((uint8_t *)&t308 + 8), t307);
                                                            F.l33 = t308;
                                                            goto gm_lb44;
                                                        }
gm_lb45:;
                                                    }
gm_lb44:;
                                                    goto gm_lb42;
                                                }
gm_lb43:;
                                                {
                                                    uint8_t t309 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                    uint8_t t310 = (uint8_t)(t309 == ((uint8_t)1ULL));
                                                    if (!t310) {
                                                        goto gm_lb48;
                                                    }
                                                    memmove(((uint8_t *)&gm_l38), ((((uint8_t *)&F.l12) + 16) + 8), 32);
                                                    void * t311 = GM_LD(void *, (((((uint8_t *)&F.l12) + 16) + 8) + 32));
                                                    gm_l39 = t311;
                                                    void * t312 = gm_l0;
                                                    uint64_t t313 = gm_l1;
                                                    void * t314 = gm_l39;
                                                    gm_t88rcucg1b7uk t315 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t312, t313, t314);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l40 = t315;
                                                    {
                                                        {
                                                            void * t316 = gm_l0;
                                                            void * t317 = (void *)(((uint8_t *)t316) + 8);
                                                            gm_t88rcucg1b7uk t318 = gm_l40;
                                                            uint8_t t319 = gm_f_is_error_8k27gd1(t317, GM_CONV(gm_csguankko4lmhd, t318));
                                                            uint8_t t320 = (uint8_t)!t319;
                                                            if (!t320) {
                                                                goto gm_lb50;
                                                            }
                                                            void * t321 = gm_l0;
                                                            gm_t88rcucg1b7uk t322 = gm_l40;
                                                            uint8_t t323 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t321, t322, ((uint8_t)0));
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t324 = (uint8_t)!t323;
                                                            if (!t324) {
                                                                goto gm_lb50;
                                                            }
                                                            void * t325 = gm_l0;
                                                            void * t326 = gm_l39;
                                                            gm_tl874t2atnlh3 t327 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t326) + 72));
                                                            void * t328 = gm_l0;
                                                            gm_t88rcucg1b7uk t329 = gm_l40;
                                                            gm_b16_8 t330 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t328, t329);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_tnpifv20irnph t331;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t331 + 0), t330);
                                                            gm_f_format_oq9akg7((void *)&gm_l59, (void *)NULL, cx, NULL, t331);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t332 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                                            if (!t332) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 8, gm_srccvj6up3gbolp, 130, 102);
                                                            gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
                                                            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
                                                            gm_b0_1 t335 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t325, t327, t334);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_lb49;
                                                        }
gm_lb50:;
                                                    }
gm_lb49:;
                                                    gm_t88rcucg1b7uk t336 = gm_l40;
                                                    gm_t6qooom8qa2do t337;
                                                    memset(&t337, 0, sizeof t337);
                                                    GM_ST(uint8_t, ((uint8_t *)&t337 + 0), ((uint8_t)10ULL));
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t337 + 8), t336);
                                                    F.l33 = t337;
                                                    goto gm_lb42;
                                                }
gm_lb48:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 120, 9);
                                            }
gm_lb42:;
                                        }
                                        {
                                            void * t338 = gm_l0;
                                            void * t339 = GM_LD(void *, ((uint8_t *)t338));
                                            uint64_t t340 = gm_l9;
                                            gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                            gm_tf5srh4mp4b3i t342 = gm_f_lookup_59juqoo(t339, t340, t341);
                                            gm_l61 = t342;
                                            {
                                                {
                                                    uint8_t t343 = (uint8_t)((((uint8_t *)&gm_l61))[8] != 0);
                                                    if (t343) {
                                                        goto gm_lb52;
                                                    }
                                                    gm_l60 = ((uint8_t)0);
                                                    goto gm_lb51;
                                                }
gm_lb52:;
                                                {
                                                    gm_l60 = ((uint8_t)1);
                                                    goto gm_lb51;
                                                }
gm_lb53:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 134, 12);
                                            }
gm_lb51:;
                                            uint8_t t344 = gm_l60;
                                            if (t344) {
                                                void * t345 = gm_l0;
                                                gm_tl874t2atnlh3 t346 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l31) + 16));
                                                gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                                gm_tnpifv20irnph t348;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t348 + 0), t347);
                                                gm_f_format_qds4t66((void *)&gm_l62, (void *)NULL, cx, NULL, t348);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t349 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                if (!t349) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_srccvj6up3gbolp, 135, 36);
                                                gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                                                gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
                                                gm_b0_1 t352 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t345, t346, t351);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                goto gm_lb8;
                                            }
                                            gm_tl874t2atnlh3 t353 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                                            gm_th2lqbo77pvsc t354;
                                            memset(&t354, 0, sizeof t354);
                                            gm_b16_8 t355 = gm_f_empty_slice_atlcs4j();
                                            gm_tl874t2atnlh3 t356 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                                            gm_t2gtbh39ptekd t357;
                                            memset(&t357, 0, sizeof t357);
                                            GM_ST(uint8_t, ((uint8_t *)&t357 + 0), ((uint8_t)7ULL));
                                            gm_tc3oht4ksth9d t358;
                                            GM_ST(uint32_t, (uint8_t *)&t358 + 4, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t358 + 84, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t358 + 0), ((uint32_t)4294967295ULL));
                                            GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t358 + 8), t357);
                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t358 + 72), t356);
                                            gm_t9qkh0rsooc8d t359;
                                            GM_ST(uint8_t, ((uint8_t *)&t359 + 0), ((uint8_t)2ULL));
                                            gm_tq7s98ihonkju t360 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t358, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kgk1u1jppkr)));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l63 = t360;
                                            uint8_t t361 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                            if (!t361) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_srccvj6up3gbolp, 138, 139);
                                            void * t362 = GM_LD(void *, (((uint8_t *)&gm_l63) + 8));
                                            gm_tkoope2qgegd9 t363;
                                            memset(&t363, 0, sizeof t363);
                                            GM_ST(uint8_t, ((uint8_t *)&t363 + 0), ((uint8_t)4ULL));
                                            GM_ST(void *, ((uint8_t *)&t363 + 8), t362);
                                            gm_tvpnktv5fo8nl t364;
                                            GM_ST(uint8_t, (uint8_t *)&t364 + 57, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t364 + 58, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t364 + 60, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t364 + 0), ((uint32_t)4294967295ULL));
                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t364 + 4), t353);
                                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t364 + 16), t354);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t364 + 40), t355);
                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t364 + 56), t359);
                                            GM_ST(gm_tkoope2qgegd9, ((uint8_t *)&t364 + 64), t363);
                                            F.l41 = t364;
                                            void * t365 = gm_l0;
                                            void * t366 = GM_LD(void *, ((uint8_t *)t365));
                                            uint64_t t367 = gm_l9;
                                            uint64_t t368 = gm_f_add_member_0jvs7ug((void *)NULL, cx, NULL, t366, t367, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l31)), &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l41)));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l42 = t368;
                                            void * t369 = gm_l0;
                                            uint64_t t370 = gm_l42;
                                            gm_t6qooom8qa2do t371 = GM_LD(gm_t6qooom8qa2do, ((uint8_t *)&F.l33));
                                            gm_tagrkbqh054h7 t372;
                                            GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t372 + 0), ((uint8_t)2ULL));
                                            GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t372 + 8), t371);
                                            gm_b0_1 t373 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t369, t370, &t372);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            {
                                                {
                                                    uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&F.l33) + 0));
                                                    uint8_t t375 = (uint8_t)(t374 == ((uint8_t)10ULL));
                                                    if (!t375) {
                                                        goto gm_lb55;
                                                    }
                                                    gm_t88rcucg1b7uk t376 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l33) + 8));
                                                    gm_l43 = t376;
                                                    void * t377 = gm_l0;
                                                    void * t378 = (void *)(((uint8_t *)t377) + 1568);
                                                    uint64_t t379 = gm_l42;
                                                    gm_b16_8 t380 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                                    gm_t88rcucg1b7uk t381 = gm_l43;
                                                    uint64_t t382 = gm_l1;
                                                    uint8_t t383 = gm_l15;
                                                    gm_b16_8 t384 = gm_l32;
                                                    gm_th2lqbo77pvsc t385;
                                                    GM_ST(uint8_t, (uint8_t *)&t385 + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t385 + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t385 + 20, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t385), t384);
                                                    ((uint8_t *)&t385)[16] = 1;
                                                    gm_tf5srh4mp4b3i t386;
                                                    memset(&t386, 0, sizeof t386);
                                                    gm_tq7qjpkmqmc5s t387;
                                                    GM_ST(uint32_t, (uint8_t *)&t387 + 28, 0);
                                                    GM_ST(uint8_t, (uint8_t *)&t387 + 49, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t387 + 50, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t387 + 52, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t387 + 0), t379);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t387 + 8), t380);
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t387 + 24), t381);
                                                    GM_ST(uint64_t, ((uint8_t *)&t387 + 32), t382);
                                                    GM_ST(void *, ((uint8_t *)&t387 + 40), ((void *)0));
                                                    GM_ST(uint8_t, ((uint8_t *)&t387 + 48), t383);
                                                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t387 + 56), t385);
                                                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t387 + 80), t386);
                                                    GM_ST(void *, ((uint8_t *)&t387 + 96), ((void *)0));
                                                    gm_tftb1odc739t5 t388 = gm_f_push_4p3ivmb(t378, &t387);
                                                    gm_l64 = t388;
                                                    uint8_t t389 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                                    if (!t389) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srccvj6up3gbolp, 142, 13);
                                                    goto gm_lb54;
                                                }
gm_lb55:;
                                            }
gm_lb54:;
                                        }
                                        {
                                            goto gm_lb9;
                                        }
                                    }
gm_lb10:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 66, 5);
                                }
                            }
gm_lb9:;
                        }
                    }
gm_lb8:;
                }
                {
                    uint64_t t390 = gm_l11;
                    uint64_t r391; bool o392 = __builtin_add_overflow(t390, ((uint64_t)1ULL), &r391);
                    uint64_t t393 = r391;
                    gm_l11 = t393;
                }
            }
        }
gm_lb6:;
    }
    {
        uint64_t t394 = gm_l9;
        gm_t6qooom8qa2do t395;
        memset(&t395, 0, sizeof t395);
        GM_ST(uint8_t, ((uint8_t *)&t395 + 0), ((uint8_t)9ULL));
        GM_ST(uint64_t, ((uint8_t *)&t395 + 8), t394);
        memmove(gm_out, ((uint8_t *)&t395), 208);
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_declare_foreign_fn_u3dekca(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_foreign_fn_u3dekca F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* sig */
    gm_tnpifv20irnph gm_l6; /* effects */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t88rcucg1b7uk gm_l10; /* ret */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    uint32_t gm_l17; /* f */
    GM_STACK_CHECK(&F, 1865u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tqqhoivahirr0 gm_l18;
    uint8_t gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tgsvmddj3e85e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    void * t19 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t19) + 1480), ((uint8_t)1));
    void * t20 = gm_l0;
    uint64_t t21 = gm_l1;
    void * t22 = gm_l3;
    gm_b16_8 t23 = gm_f_empty_slice_3jt8vgk();
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
    gm_f_resolve_fn_sig_pamuhve((void *)&F.l4, (void *)NULL, cx, NULL, t20, t21, t22, ((uint8_t)0), t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t25 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t25) + 1480), ((uint8_t)0));
    uint8_t t26 = (uint8_t)((((uint8_t *)&F.l4))[136] != 0);
    if (t26) {
        memmove(((uint8_t *)&gm_l18), ((uint8_t *)&F.l4), 136);
    } else {
        gm_t9805qtonbsag t27;
        memset(&t27, 0, sizeof t27);
        F.ret = t27;
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l18), 136);
    gm_tnpifv20irnph t28 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l5) + 64));
    gm_l6 = t28;
    void * t29 = gm_l3;
    {
        {
            uint8_t t30 = (uint8_t)(((((uint8_t *)t29) + 32))[16] != 0);
            if (t30) {
                goto gm_lb2;
            }
            gm_l19 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l19 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 197, 8);
    }
gm_lb1:;
    uint8_t t31 = gm_l19;
    if (t31) {
        void * t32 = gm_l0;
        gm_tnpifv20irnph t33 = gm_f_sys_set_pqe8mto((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t33;
    }
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
    gm_l7 = t34;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t35 = gm_l8;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t37 = (t35 < t36);
            if (!t37) {
                goto gm_lb4;
            }
            {
                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t40 = gm_l8;
                {
                    {
                        memmove(((uint8_t *)&F.l9), (((uint8_t *)t38) + (uintptr_t)t40 * 56), 56);
                        {
                            {
                                void * t41 = gm_l0;
                                void * t42 = (void *)(((uint8_t *)t41) + 8);
                                gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                uint8_t t44 = gm_f_is_error_8k27gd1(t42, GM_CONV(gm_csguankko4lmhd, t43));
                                uint8_t t45 = (uint8_t)!t44;
                                if (!t45) {
                                    goto gm_lb10;
                                }
                                void * t46 = gm_l0;
                                gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                uint8_t t48 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t46, t47, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t49 = (uint8_t)!t48;
                                if (!t49) {
                                    goto gm_lb10;
                                }
                                void * t50 = gm_l0;
                                gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l9) + 20));
                                void * t52 = gm_l0;
                                gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                gm_b16_8 t54 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t52, t53);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tnpifv20irnph t55;
                                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                                gm_f_format_fea89f6((void *)&gm_l20, (void *)NULL, cx, NULL, t55);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srccvj6up3gbolp, 199, 94);
                                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                gm_b0_1 t59 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t50, t51, t58);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb9;
                            }
gm_lb10:;
                        }
gm_lb9:;
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 198, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t60 = gm_l8;
            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
            uint64_t t63 = r61;
            gm_l8 = t63;
        }
    }
gm_lb4:;
    gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
    gm_l10 = t64;
    {
        {
            void * t65 = gm_l0;
            void * t66 = (void *)(((uint8_t *)t65) + 8);
            gm_t88rcucg1b7uk t67 = gm_l10;
            uint8_t t68 = gm_f_is_error_8k27gd1(t66, GM_CONV(gm_csguankko4lmhd, t67));
            uint8_t t69 = (uint8_t)!t68;
            if (!t69) {
                goto gm_lb12;
            }
            void * t70 = gm_l0;
            void * t71 = (void *)(((uint8_t *)t70) + 8);
            gm_t88rcucg1b7uk t72 = gm_l10;
            uint8_t t73 = gm_f_is_unit_n83ttnb(t71, GM_CONV(gm_csguankko4lmhd, t72));
            uint8_t t74 = (uint8_t)!t73;
            if (!t74) {
                goto gm_lb12;
            }
            void * t75 = gm_l0;
            void * t76 = (void *)(((uint8_t *)t75) + 8);
            gm_t88rcucg1b7uk t77 = gm_l10;
            gm_t53at80pfdus3 t78;
            gm_f_kind_jcnt4hm((void *)&t78, t76, GM_CONV(gm_csguankko4lmhd, t77));
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t78) + 0));
            uint8_t t80 = (uint8_t)(!(t79 == ((uint8_t)5ULL)));
            if (!t80) {
                goto gm_lb12;
            }
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l10;
            gm_t53at80pfdus3 t83;
            memset(&t83, 0, sizeof t83);
            GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)5ULL));
            uint8_t t84 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t81, t82, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t85 = (uint8_t)!t84;
            if (!t85) {
                goto gm_lb12;
            }
            void * t86 = gm_l0;
            gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            void * t88 = gm_l0;
            gm_t88rcucg1b7uk t89 = gm_l10;
            gm_b16_8 t90 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t91;
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t90);
            gm_f_format_mprr1lp((void *)&gm_l21, (void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srccvj6up3gbolp, 204, 28);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_b0_1 t95 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t86, t87, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb11;
        }
gm_lb12:;
    }
gm_lb11:;
    gm_trqfvmjtt4sij t96;
    gm_f_list_go2qg0b((void *)&t96, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l11 = t96;
    gm_trqfvmjtt4sij t97;
    gm_f_list_p9s2t99((void *)&t97, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l12 = t97;
    gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
    gm_l13 = t99;
    gm_l14 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t100 = gm_l14;
            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
            uint8_t t102 = (t100 < t101);
            if (!t102) {
                goto gm_lb13;
            }
            {
                uint64_t t103 = gm_l14;
                void * t104 = GM_LD(void *, ((uint8_t *)&gm_l13));
                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint64_t t106 = gm_l14;
                gm_t5v0c8ia6a5mo t107 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t104) + (uintptr_t)t106 * 56));
                gm_tgsvmddj3e85e t108;
                GM_ST(uint64_t, ((uint8_t *)&t108 + 0), t103);
                GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t108 + 8), t107);
                gm_l22 = t108;
                {
                    {
                        uint64_t t109 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                        gm_l15 = t109;
                        memmove(((uint8_t *)&F.l16), (((uint8_t *)&gm_l22) + 8), 56);
                        void * t110 = (void *)((uint8_t *)&F.l12);
                        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                        gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l16) + 20));
                        gm_tsoie1ppgdvcg t114;
                        GM_ST(uint8_t, (uint8_t *)&t114 + 21, 0);
                        GM_ST(uint16_t, (uint8_t *)&t114 + 22, 0);
                        GM_ST(uint32_t, (uint8_t *)&t114 + 36, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t111);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t114 + 16), t112);
                        GM_ST(uint8_t, ((uint8_t *)&t114 + 20), ((uint8_t)0));
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t114 + 24), t113);
                        gm_tftb1odc739t5 t115 = gm_f_push_am7bo0v(t110, &t114);
                        gm_l23 = t115;
                        uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srccvj6up3gbolp, 209, 9);
                        void * t117 = (void *)((uint8_t *)&F.l11);
                        uint64_t t118 = gm_l15;
                        uint32_t t119 = (uint32_t)t118;
                        gm_tftb1odc739t5 t120 = gm_f_push_im9d4ro(t117, t119);
                        gm_l24 = t120;
                        uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srccvj6up3gbolp, 210, 9);
                        goto gm_lb16;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 208, 5);
                }
gm_lb16:;
            }
gm_lb15:;
            uint64_t t122 = gm_l14;
            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
            uint64_t t125 = r123;
            gm_l14 = t125;
        }
    }
gm_lb13:;
    void * t126 = gm_l0;
    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)a2));
    gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
    uint64_t t129 = gm_l1;
    gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
    gm_t88rcucg1b7uk t131 = gm_l10;
    gm_tnpifv20irnph t132 = gm_l6;
    gm_b16_8 t133 = gm_f_empty_slice_d914i5u();
    gm_tqu3m2l3n8mnc t134;
    memset(&t134, 0, sizeof t134);
    gm_tqqhoivahirr0 t135;
    GM_ST(uint32_t, (uint8_t *)&t135 + 28, 0);
    GM_ST(uint32_t, (uint8_t *)&t135 + 60, 0);
    GM_ST(uint8_t, (uint8_t *)&t135 + 129, 0);
    GM_ST(uint16_t, (uint8_t *)&t135 + 130, 0);
    GM_ST(uint32_t, (uint8_t *)&t135 + 132, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t135 + 0), t127);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t135 + 16), t128);
    GM_ST(uint64_t, ((uint8_t *)&t135 + 32), t129);
    GM_ST(gm_b16_8, ((uint8_t *)&t135 + 40), t130);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t135 + 56), t131);
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t135 + 64), t132);
    GM_ST(gm_b16_8, ((uint8_t *)&t135 + 80), t133);
    GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&t135 + 96), t134);
    GM_ST(uint8_t, ((uint8_t *)&t135 + 128), ((uint8_t)0));
    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
    gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
    gm_t9805qtonbsag t138;
    memset(&t138, 0, sizeof t138);
    gm_t9qkh0rsooc8d t139;
    GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)3ULL));
    gm_tdgfduqjheqdm t140;
    GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t140 + 57, 0);
    GM_ST(uint16_t, (uint8_t *)&t140 + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&t140 + 60, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t140 + 0), t139);
    GM_ST(gm_b16_8, ((uint8_t *)&t140 + 8), t136);
    GM_ST(gm_b16_8, ((uint8_t *)&t140 + 24), t137);
    GM_ST(void *, ((uint8_t *)&t140 + 40), ((void *)0));
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t140 + 48), t138);
    GM_ST(uint8_t, ((uint8_t *)&t140 + 56), ((uint8_t)0));
    uint32_t t141 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t126, &t135, &t140);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l17 = t141;
    void * t142 = gm_l3;
    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)t142) + 16));
    if (t143) {
        void * t144 = gm_l0;
        void * t145 = (void *)(((uint8_t *)t144) + 1488);
        uint32_t t146 = gm_l17;
        gm_tftb1odc739t5 t147 = gm_f_push_im9d4ro(t145, t146);
        gm_l25 = t147;
        uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srccvj6up3gbolp, 216, 23);
    }
    uint32_t t149 = gm_l17;
    gm_t9805qtonbsag t150;
    GM_ST(uint8_t, (uint8_t *)&t150 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t150 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t150), t149);
    ((uint8_t *)&t150)[4] = 1;
    F.ret = t150;
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
__attribute__((aligned(4))) void gm_f_declare_global_729nttf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_global_729nttf F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* b */
    void * gm_l4; /* ty_expr */
    gm_t88rcucg1b7uk gm_l5; /* ty */
    void * gm_l6; /* hook */
    GM_STACK_CHECK(&F, 1970u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    void * gm_l7;
    uint8_t gm_l8;
    gm_tkn42u7na3t81 gm_l9;
    gm_t9805qtonbsag gm_l10;
    uint32_t gm_l11;
    gm_tkn42u7na3t81 gm_l12;
    gm_t88rcucg1b7uk gm_l13;
    void * t7 = gm_l3;
    uint8_t t8 = (uint8_t)(GM_LD(void *, (((uint8_t *)t7) + 24)) != 0);
    if (t8) {
        void * t9 = GM_LD(void *, (((uint8_t *)t7) + 24));
        gm_l7 = t9;
    } else {
        void * t10 = gm_l0;
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s81csu1l86dmv));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(51));
        gm_b0_1 t13 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)11ULL));
        memmove(gm_out, ((uint8_t *)&t14), 208);
        goto gm_fdone;
    }
    void * t15 = gm_l7;
    gm_l4 = t15;
    void * t16 = gm_l0;
    uint64_t t17 = gm_l1;
    void * t18 = gm_l4;
    gm_t88rcucg1b7uk t19 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t16, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t19;
    void * t20 = gm_l0;
    void * t21 = (void *)(((uint8_t *)t20) + 8);
    gm_t88rcucg1b7uk t22 = gm_l5;
    uint8_t t23 = gm_f_is_error_8k27gd1(t21, GM_CONV(gm_csguankko4lmhd, t22));
    if (t23) {
        gm_t6qooom8qa2do t24;
        memset(&t24, 0, sizeof t24);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)11ULL));
        memmove(gm_out, ((uint8_t *)&t24), 208);
        goto gm_fdone;
    }
    void * t25 = gm_l0;
    uint64_t t26 = gm_l1;
    void * t27 = gm_l3;
    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 40));
    gm_t88rcucg1b7uk t29 = gm_l5;
    gm_tkn42u7na3t81 t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30), t29);
    ((uint8_t *)&t30)[4] = 1;
    gm_tkn42u7na3t81 t31 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t25, t26, t28, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t31;
    {
        {
            uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
            if (t32) {
                goto gm_lb2;
            }
            gm_l8 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l8 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 40, 8);
    }
gm_lb1:;
    uint8_t t33 = gm_l8;
    if (t33) {
        gm_t88rcucg1b7uk t34 = gm_l5;
        gm_t6qooom8qa2do t35;
        memset(&t35, 0, sizeof t35);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 8), t34);
        memmove(gm_out, ((uint8_t *)&t35), 208);
        goto gm_fdone;
    }
    {
        {
            void * t36 = gm_l0;
            uint8_t t37 = (uint8_t)(GM_LD(void *, (((uint8_t *)t36) + 2288)) != 0);
            if (!t37) {
                goto gm_lb5;
            }
            void * t38 = GM_LD(void *, (((uint8_t *)t36) + 2288));
            gm_l6 = t38;
            void * t39 = gm_l0;
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            gm_t88rcucg1b7uk t41 = gm_l5;
            uint8_t t42 = gm_f_has_param_u9t0efs(t40, GM_CONV(gm_csguankko4lmhd, t41));
            uint8_t t43 = (uint8_t)!t42;
            if (!t43) {
                goto gm_lb5;
            }
            void * t44 = gm_l6;
            void * t45 = gm_l0;
            uint64_t t46 = gm_l1;
            void * t47 = gm_l3;
            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 40));
            gm_t88rcucg1b7uk t49 = gm_l5;
            gm_tkn42u7na3t81 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t50), t49);
            ((uint8_t *)&t50)[4] = 1;
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sbd6ket0ql5ab));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(8));
            void * t52 = gm_l3;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 40));
            gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72));
            void * t55 = t44;
            gm_t9805qtonbsag t56;
            if (GM_PTR_TAGGED(t55)) {
                t56 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t55))(t45, t46, t48, t50, t51, GM_CONV(gm_cseahvg8v7p0vs, t54));
            } else {
                t56 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t55)(NULL, cx, NULL, t45, t46, t48, t50, t51, t54);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t56;
            uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
            if (t57) {
                uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                gm_l11 = t58;
            } else {
                gm_t6qooom8qa2do t59;
                memset(&t59, 0, sizeof t59);
                GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)11ULL));
                memmove(gm_out, ((uint8_t *)&t59), 208);
                goto gm_fdone;
            }
            uint32_t t60 = gm_l11;
            gm_t88rcucg1b7uk t61 = gm_l5;
            gm_t6qooom8qa2do t62;
            memset(&t62, 0, sizeof t62);
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)10ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t62 + 8), t61);
            memmove(gm_out, ((uint8_t *)&t62), 208);
            goto gm_fdone;
        }
gm_lb5:;
    }
gm_lb4:;
    void * t63 = gm_l0;
    uint64_t t64 = gm_l1;
    void * t65 = gm_l3;
    void * t66 = GM_LD(void *, (((uint8_t *)t65) + 40));
    gm_t88rcucg1b7uk t67 = gm_l5;
    gm_tkn42u7na3t81 t68;
    GM_ST(uint8_t, (uint8_t *)&t68 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t68 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t68), t67);
    ((uint8_t *)&t68)[4] = 1;
    gm_tkn42u7na3t81 t69 = gm_f_const_type_7rijs95((void *)NULL, cx, NULL, t63, t64, t66, t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t69;
    uint8_t t70 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
    if (t70) {
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
        gm_l13 = t71;
    } else {
        gm_t6qooom8qa2do t72;
        memset(&t72, 0, sizeof t72);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)11ULL));
        memmove(gm_out, ((uint8_t *)&t72), 208);
        goto gm_fdone;
    }
    gm_t88rcucg1b7uk t73 = gm_l13;
    gm_t88rcucg1b7uk t74 = gm_l5;
    gm_t6qooom8qa2do t75;
    memset(&t75, 0, sizeof t75);
    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)10ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t75 + 8), t74);
    memmove(gm_out, ((uint8_t *)&t75), 208);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_escape_debug_lu2p5se(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_escape_debug_lu2p5se F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l2; /* rest */
    gm_tb31k5l794t7p gm_l3; /* r */
    uint32_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tgmjda3f8g6hg gm_l5;
    gm_tb31k5l794t7p gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_trqfvmjtt4sij t14;
    gm_f_list_uvfu1k1((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t14;
    gm_b16_8 t15 = gm_l0;
    gm_l2 = t15;
    {
        for (;;) {
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t17 = (t16 > ((uint64_t)0ULL));
            if (!t17) {
                goto gm_lb1;
            }
            {
                gm_b16_8 t18 = gm_l2;
                gm_f_decode_rune_ugbib8e((void *)&gm_l5, t18);
                uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
                if (t19) {
                    gm_tb31k5l794t7p t20 = GM_LD(gm_tb31k5l794t7p, ((uint8_t *)&gm_l5));
                    gm_l6 = t20;
                } else {
                    goto gm_lb1;
                }
                gm_tb31k5l794t7p t21 = gm_l6;
                gm_l3 = t21;
                {
                    {
                        uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t23 = ((t22 == ((uint32_t)34ULL)));
                        if (!t23) {
                            goto gm_lb5;
                        }
                        void * t24 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t25;
                        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t26 = gm_f_extend_0cs85gs(t24, t25);
                        gm_l7 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srccvj6up3gbolp, 156, 20);
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint32_t t28 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t29 = ((t28 == ((uint32_t)39ULL)));
                        if (!t29) {
                            goto gm_lb6;
                        }
                        void * t30 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t31;
                        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sj2jft9n0jm50));
                        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
                        gm_l8 = t32;
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srccvj6up3gbolp, 157, 21);
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t35 = ((t34 == ((uint32_t)92ULL)));
                        if (!t35) {
                            goto gm_lb7;
                        }
                        void * t36 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t37;
                        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t38 = gm_f_extend_0cs85gs(t36, t37);
                        gm_l9 = t38;
                        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srccvj6up3gbolp, 158, 21);
                        goto gm_lb4;
                    }
gm_lb7:;
                    {
                        uint32_t t40 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t41 = ((t40 == ((uint32_t)10ULL)));
                        if (!t41) {
                            goto gm_lb8;
                        }
                        void * t42 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s6gt8b7hsisgu));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t44 = gm_f_extend_0cs85gs(t42, t43);
                        gm_l10 = t44;
                        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srccvj6up3gbolp, 159, 21);
                        goto gm_lb4;
                    }
gm_lb8:;
                    {
                        uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t47 = ((t46 == ((uint32_t)13ULL)));
                        if (!t47) {
                            goto gm_lb9;
                        }
                        void * t48 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t49;
                        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s9cb6j5ok7bdk));
                        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t50 = gm_f_extend_0cs85gs(t48, t49);
                        gm_l11 = t50;
                        uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srccvj6up3gbolp, 160, 21);
                        goto gm_lb4;
                    }
gm_lb9:;
                    {
                        uint32_t t52 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t53 = ((t52 == ((uint32_t)9ULL)));
                        if (!t53) {
                            goto gm_lb10;
                        }
                        void * t54 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t55;
                        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s22g482qk7a40));
                        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t54, t55);
                        gm_l12 = t56;
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srccvj6up3gbolp, 161, 21);
                        goto gm_lb4;
                    }
gm_lb10:;
                    {
                        uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t59 = ((t58 == ((uint32_t)0ULL)));
                        if (!t59) {
                            goto gm_lb11;
                        }
                        void * t60 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t61;
                        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s2v5amvfkfoll));
                        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t62 = gm_f_extend_0cs85gs(t60, t61);
                        gm_l13 = t62;
                        uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srccvj6up3gbolp, 162, 18);
                        goto gm_lb4;
                    }
gm_lb11:;
                    {
                        uint32_t t64 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        gm_l4 = t64;
                        uint32_t t65 = gm_l4;
                        uint8_t t66 = (t65 < ((uint32_t)32ULL));
                        if (t66) {
                            gm_l14 = ((uint8_t)1);
                        } else {
                            uint32_t t67 = gm_l4;
                            uint8_t t68 = (t67 >= ((uint32_t)127ULL));
                            if (t68) {
                                uint32_t t69 = gm_l4;
                                uint8_t t70 = (t69 <= ((uint32_t)159ULL));
                                gm_l15 = t70;
                            } else {
                                gm_l15 = ((uint8_t)0);
                            }
                            uint8_t t71 = gm_l15;
                            gm_l14 = t71;
                        }
                        uint8_t t72 = gm_l14;
                        if (t72) {
                            void * t73 = (void *)((uint8_t *)&F.l1);
                            uint32_t t74 = gm_l4;
                            gm_t88rcucg1b7uk t75;
                            GM_ST(uint32_t, ((uint8_t *)&t75 + 0), t74);
                            gm_tftb1odc739t5 t76 = gm_f_write_146u7aa(t73, t75);
                            gm_l16 = t76;
                            uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                            if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srccvj6up3gbolp, 165, 21);
                        } else {
                            void * t78 = (void *)((uint8_t *)&F.l1);
                            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t83 = (uintptr_t)t81;
                            if (t82 > t83 || t83 > (uintptr_t)t80) gm_trap_slice(t82, t83, t80, gm_srccvj6up3gbolp, 167, 32);
                            uint64_t t84 = ((uintptr_t)t81) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t85;
                            GM_ST(void *, (uint8_t *)&t85, (void *)(((uint8_t *)t79 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(t84));
                            gm_tftb1odc739t5 t86 = gm_f_extend_0cs85gs(t78, t85);
                            gm_l17 = t86;
                            uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                            if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srccvj6up3gbolp, 167, 21);
                        }
                        goto gm_lb4;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 155, 9);
                }
gm_lb4:;
                void * t88 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t91 = (uintptr_t)t90;
                uint64_t t92 = (uintptr_t)t89;
                if (t91 > t92 || t92 > (uintptr_t)t89) gm_trap_slice(t91, t92, t89, gm_srccvj6up3gbolp, 171, 16);
                uint64_t t93 = ((uintptr_t)t89) - (uintptr_t)t90;
                gm_b16_8 t94;
                GM_ST(void *, (uint8_t *)&t94, (void *)(((uint8_t *)t88 + (uintptr_t)t90 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(t93));
                gm_l2 = t94;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
    F.ret = t96;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_export_fn_8hcuasc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_export_fn_8hcuasc F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* sym */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint8_t gm_l5; /* ident */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 1239u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 344));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 344) + 8));
    uint32_t t17 = gm_l1;
    uint64_t t18 = (uint64_t)t17;
    if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srccvj6up3gbolp, 290, 12);
    memmove(((uint8_t *)&F.l4), (((uint8_t *)t15) + (uintptr_t)t18 * 136), 136);
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t20 = (t19 > ((uint64_t)0ULL));
    if (t20) {
        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t24 = gm_f_is_alpha_12215ga(t23);
        if (t24) {
            gm_l13 = ((uint8_t)1);
        } else {
            void * t25 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t25) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t28 = ((t27 == ((uint8_t)95ULL)));
            gm_l13 = t28;
        }
        uint8_t t29 = gm_l13;
        gm_l12 = t29;
    } else {
        gm_l12 = ((uint8_t)0);
    }
    uint8_t t30 = gm_l12;
    gm_l5 = t30;
    gm_b16_8 t31 = gm_l2;
    gm_l6 = t31;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l7;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t37 = gm_l7;
                {
                    {
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t35) + (uintptr_t)t37 * 1));
                        gm_l8 = t38;
                        uint8_t t39 = gm_l8;
                        uint8_t t40 = gm_f_is_alnum_n81ervr(t39);
                        if (t40) {
                            gm_l14 = ((uint8_t)1);
                        } else {
                            uint8_t t41 = gm_l8;
                            uint8_t t42 = ((t41 == ((uint8_t)95ULL)));
                            gm_l14 = t42;
                        }
                        uint8_t t43 = gm_l14;
                        uint8_t t44 = (uint8_t)!t43;
                        if (t44) {
                            gm_l5 = ((uint8_t)0);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 294, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t45 = gm_l7;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l7 = t48;
        }
    }
gm_lb1:;
    uint8_t t49 = gm_l5;
    uint8_t t50 = (uint8_t)!t49;
    if (t50) {
        void * t51 = gm_l0;
        gm_tl874t2atnlh3 t52 = gm_l3;
        gm_b16_8 t53 = gm_l2;
        gm_tnpifv20irnph t54;
        GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
        gm_f_format_mh1raea((void *)&gm_l15, (void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srccvj6up3gbolp, 298, 23);
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_b0_1 t58 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t51, t52, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_b16_8 t59 = gm_l2;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sog4rksrmsqd2));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(4));
    uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
    if (t61) {
        void * t62 = gm_l0;
        gm_tl874t2atnlh3 t63 = gm_l3;
        gm_b16_8 t64;
        GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s310nl8k9dnl2));
        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(58));
        gm_b0_1 t65 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t62, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_b16_8 t66 = gm_l2;
    uint8_t t67 = gm_f_is_runtime_symbol_rj1sp7s(t66);
    if (t67) {
        void * t68 = gm_l0;
        gm_tl874t2atnlh3 t69 = gm_l3;
        gm_b16_8 t70 = gm_l2;
        gm_tnpifv20irnph t71;
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
        gm_f_format_rb3ud4d((void *)&gm_l16, (void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srccvj6up3gbolp, 306, 23);
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        gm_b0_1 t75 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t68, t69, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_b16_8 t76 = gm_l2;
    void * t77 = gm_l0;
    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)t77) + 880));
    uint8_t t79 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t78) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t78))) + i * 16, ((uint8_t *)&t76))) { t79 = 1; break; }
    uint8_t t80 = (uint8_t)(t79);
    if (t80) {
        void * t81 = gm_l0;
        gm_tl874t2atnlh3 t82 = gm_l3;
        gm_b16_8 t83 = gm_l2;
        gm_tnpifv20irnph t84;
        GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
        gm_f_format_69lunt5((void *)&gm_l17, (void *)NULL, cx, NULL, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srccvj6up3gbolp, 310, 23);
        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
        gm_b0_1 t88 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t81, t82, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint64_t t89 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 80) + 8));
    uint8_t t90 = (t89 > ((uint64_t)0ULL));
    if (t90) {
        void * t91 = gm_l0;
        gm_tl874t2atnlh3 t92 = gm_l3;
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sbaogg9p69n9f));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(37));
        gm_b0_1 t94 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t91, t92, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t95 = gm_l0;
    void * t96 = (void *)(((uint8_t *)&F.l4) + 64);
    uint8_t t97 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t95, t96);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t98 = (uint8_t)!t97;
    if (t98) {
        void * t99 = gm_l0;
        gm_tl874t2atnlh3 t100 = gm_l3;
        gm_b16_8 t101;
        GM_ST(void *, (uint8_t *)&t101, (void *)(gm_siqa65grpcsk9));
        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(98));
        gm_b0_1 t102 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t99, t100, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
    gm_l9 = t103;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t104 = gm_l10;
            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t106 = (t104 < t105);
            if (!t106) {
                goto gm_lb6;
            }
            {
                void * t107 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t109 = gm_l10;
                {
                    {
                        memmove(((uint8_t *)&F.l11), (((uint8_t *)t107) + (uintptr_t)t109 * 56), 56);
                        void * t110 = gm_l0;
                        gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        uint8_t t112 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t110, t111, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t113 = (uint8_t)!t112;
                        if (t113) {
                            void * t114 = gm_l0;
                            gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 20));
                            void * t116 = gm_l0;
                            gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                            gm_b16_8 t118 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t116, t117);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t119;
                            GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t118);
                            gm_f_format_fea89f6((void *)&gm_l18, (void *)NULL, cx, NULL, t119);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                            if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srccvj6up3gbolp, 321, 65);
                            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                            gm_b0_1 t123 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t114, t115, t122);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 320, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t124 = gm_l10;
            uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
            uint64_t t127 = r125;
            gm_l10 = t127;
        }
    }
gm_lb6:;
    {
        {
            void * t128 = gm_l0;
            void * t129 = (void *)(((uint8_t *)t128) + 8);
            gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            uint8_t t131 = gm_f_is_unit_n83ttnb(t129, GM_CONV(gm_csguankko4lmhd, t130));
            uint8_t t132 = (uint8_t)!t131;
            if (!t132) {
                goto gm_lb12;
            }
            void * t133 = gm_l0;
            gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            uint8_t t135 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t133, t134, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t136 = (uint8_t)!t135;
            if (!t136) {
                goto gm_lb12;
            }
            void * t137 = gm_l0;
            gm_tl874t2atnlh3 t138 = gm_l3;
            void * t139 = gm_l0;
            gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            gm_b16_8 t141 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t142;
            GM_ST(gm_b16_8, ((uint8_t *)&t142 + 0), t141);
            gm_f_format_mprr1lp((void *)&gm_l19, (void *)NULL, cx, NULL, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srccvj6up3gbolp, 324, 23);
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
            gm_b0_1 t146 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t137, t138, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb11;
        }
gm_lb12:;
    }
gm_lb11:;
    void * t147 = gm_l0;
    void * t148 = (void *)(((uint8_t *)t147) + 880);
    gm_b16_8 t149 = gm_l2;
    gm_tftb1odc739t5 t150 = gm_f_push_3ef746b(t148, t149);
    gm_l20 = t150;
    uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srccvj6up3gbolp, 326, 5);
    void * t152 = gm_l0;
    void * t153 = (void *)(((uint8_t *)t152) + 920);
    uint32_t t154 = gm_l1;
    gm_tftb1odc739t5 t155 = gm_f_push_im9d4ro(t153, t154);
    gm_l21 = t155;
    uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srccvj6up3gbolp, 327, 5);
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
inline __attribute__((aligned(4))) void gm_f_foreign_info_e0kd6uc(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_foreign_info_e0kd6uc F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 1528));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t12) + (uintptr_t)t14 * 40), 40);
                        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&F.l4));
                        uint32_t t16 = gm_l1;
                        uint8_t t17 = ((t15 == t16));
                        if (t17) {
                            memmove(gm_out, ((uint8_t *)&F.l4), 40);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 178, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t18 = gm_l3;
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            uint64_t t21 = r19;
            gm_l3 = t21;
        }
    }
gm_lb1:;
    uint32_t t22 = gm_l1;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s2gnpj7k4l22n));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, (((uint8_t *)t24) + 344));
    uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 344) + 8));
    uint32_t t27 = gm_l1;
    uint64_t t28 = (uint64_t)t27;
    if ((uintptr_t)t28 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t28, t26, gm_srccvj6up3gbolp, 181, 53);
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t25) + (uintptr_t)t28 * 136));
    gm_thognv9odov03 t30;
    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t30 + 0), t22);
    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 8), t23);
    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 24), t29);
    memmove(gm_out, ((uint8_t *)&t30), 40);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_thognv9odov03 gm_f_foreign_info_e0kd6uc_c(void * a0, uint32_t a1) { gm_thognv9odov03 gm_r; gm_f_foreign_info_e0kd6uc(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_is_c_compatible_2hi3vi8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2) {
    struct gm_fr_is_c_compatible_2hi3vi8 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* in_struct */
    uint16_t gm_l3; /* bits */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* p */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_tnnjeerolos0a gm_l10; /* kind */
    uint32_t gm_l11; /* id */
    gm_tjpqvfd72o62u gm_l12; /* info */
    gm_b16_8 gm_l14; /* fs */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l18; /* fields */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    GM_STACK_CHECK(&F, 906u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    gm_t8cc1mtbrhlpt gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    gm_th2lqbo77pvsc gm_l30;
    gm_b16_8 gm_l31;
    {
        void * t46 = gm_l0;
        void * t47 = (void *)(((uint8_t *)t46) + 8);
        gm_t88rcucg1b7uk t48 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l22, t47, GM_CONV(gm_csguankko4lmhd, t48));
        {
            {
                {
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)0ULL));
                    if (!t50) {
                        goto gm_lb2;
                    }
                    uint16_t t51 = GM_LD(uint16_t, ((((uint8_t *)&gm_l22) + 8) + 2));
                    gm_l3 = t51;
                    uint16_t t52 = gm_l3;
                    gm_tdbjst0cng0lo t53;
                    GM_ST(uint16_t, ((uint8_t *)&t53 + 0), ((uint16_t)8ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t53 + 2), ((uint16_t)16ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t53 + 4), ((uint16_t)32ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t53 + 6), ((uint16_t)64ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t53 + 8), ((uint16_t)128ULL));
                    uint8_t t54 = 0;
                    if (gm_eq9oqmmiu8qc(((uint8_t *)&t53) + 0, ((uint8_t *)&t52))) t54 = 1;
                    if (gm_eq9oqmmiu8qc(((uint8_t *)&t53) + 2, ((uint8_t *)&t52))) t54 = 1;
                    if (gm_eq9oqmmiu8qc(((uint8_t *)&t53) + 4, ((uint8_t *)&t52))) t54 = 1;
                    if (gm_eq9oqmmiu8qc(((uint8_t *)&t53) + 6, ((uint8_t *)&t52))) t54 = 1;
                    if (gm_eq9oqmmiu8qc(((uint8_t *)&t53) + 8, ((uint8_t *)&t52))) t54 = 1;
                    uint8_t t55 = (uint8_t)(t54);
                    F.ret = t55;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    {
                        {
                            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)1ULL));
                            if (!t57) {
                                goto gm_lb5;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        {
                            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)2ULL));
                            if (!t59) {
                                goto gm_lb6;
                            }
                            goto gm_lb4;
                        }
gm_lb6:;
                        {
                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t61 = (uint8_t)(t60 == ((uint8_t)3ULL));
                            if (!t61) {
                                goto gm_lb7;
                            }
                            goto gm_lb4;
                        }
gm_lb7:;
                        {
                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)4ULL));
                            if (!t63) {
                                goto gm_lb8;
                            }
                            goto gm_lb4;
                        }
gm_lb8:;
                        {
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)7ULL));
                            if (!t65) {
                                goto gm_lb9;
                            }
                            goto gm_lb4;
                        }
gm_lb9:;
                        {
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)8ULL));
                            if (!t67) {
                                goto gm_lb10;
                            }
                            goto gm_lb4;
                        }
gm_lb10:;
                        {
                            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)9ULL));
                            if (!t69) {
                                goto gm_lb11;
                            }
                            goto gm_lb4;
                        }
gm_lb11:;
                        {
                            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)26ULL));
                            if (!t71) {
                                goto gm_lb12;
                            }
                            goto gm_lb4;
                        }
gm_lb12:;
                        goto gm_lb3;
                    }
gm_lb4:;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb3:;
                {
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)13ULL));
                    if (!t73) {
                        goto gm_lb13;
                    }
                    gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 8));
                    gm_l4 = t74;
                    void * t75 = gm_l0;
                    gm_t88rcucg1b7uk t76 = gm_l4;
                    uint8_t t77 = gm_f_is_nullable_pointer_nkie06s(t75, GM_CONV(gm_csguankko4lmhd, t76));
                    if (t77) {
                        void * t78 = gm_l0;
                        gm_t88rcucg1b7uk t79 = gm_l4;
                        uint8_t t80 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t78, t79, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l23 = t80;
                    } else {
                        gm_l23 = ((uint8_t)0);
                    }
                    uint8_t t81 = gm_l23;
                    F.ret = t81;
                    goto gm_fret;
                }
gm_lb13:;
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)16ULL));
                    if (!t83) {
                        goto gm_lb14;
                    }
                    memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l22) + 8), 48);
                    void * t84 = gm_l0;
                    void * t85 = (void *)(((uint8_t *)&F.l5) + 24);
                    uint8_t t86 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t84, t85);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t87 = (uint8_t)!t86;
                    if (t87) {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                    gm_l6 = t88;
                    gm_l7 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t89 = gm_l7;
                            uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t91 = (t89 < t90);
                            if (!t91) {
                                goto gm_lb15;
                            }
                            {
                                void * t92 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t94 = gm_l7;
                                {
                                    {
                                        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + (uintptr_t)t94 * 4));
                                        gm_l8 = t95;
                                        void * t96 = gm_l0;
                                        gm_t88rcucg1b7uk t97 = gm_l8;
                                        uint8_t t98 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t96, t97, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t99 = (uint8_t)!t98;
                                        if (t99) {
                                            F.ret = ((uint8_t)0);
                                            goto gm_fret;
                                        }
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 373, 13);
                                }
gm_lb18:;
                            }
gm_lb17:;
                            uint64_t t100 = gm_l7;
                            uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                            uint64_t t103 = r101;
                            gm_l7 = t103;
                        }
                    }
gm_lb15:;
                    void * t104 = gm_l0;
                    void * t105 = (void *)(((uint8_t *)t104) + 8);
                    gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                    uint8_t t107 = gm_f_is_unit_n83ttnb(t105, GM_CONV(gm_csguankko4lmhd, t106));
                    if (t107) {
                        gm_l24 = ((uint8_t)1);
                    } else {
                        void * t108 = gm_l0;
                        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        uint8_t t110 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t108, t109, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l24 = t110;
                    }
                    uint8_t t111 = gm_l24;
                    F.ret = t111;
                    goto gm_fret;
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)11ULL));
                    if (!t113) {
                        goto gm_lb20;
                    }
                    gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 8));
                    gm_l9 = t114;
                    gm_tnnjeerolos0a t115 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l22) + 8) + 12));
                    gm_l10 = t115;
                    uint8_t t116 = gm_l2;
                    if (t116) {
                        gm_tnnjeerolos0a t117 = gm_l10;
                        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t117) + 0));
                        uint8_t t119 = (uint8_t)(!(t118 == ((uint8_t)2ULL)));
                        gm_tnnjeerolos0a t120;
                        GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)2ULL));
                        gm_l26 = t119;
                    } else {
                        gm_l26 = ((uint8_t)0);
                    }
                    uint8_t t121 = gm_l26;
                    if (t121) {
                        void * t122 = gm_l0;
                        gm_t88rcucg1b7uk t123 = gm_l9;
                        uint8_t t124 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t122, t123, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l25 = t124;
                    } else {
                        gm_l25 = ((uint8_t)0);
                    }
                    uint8_t t125 = gm_l25;
                    F.ret = t125;
                    goto gm_fret;
                }
gm_lb20:;
                {
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)17ULL));
                    if (!t127) {
                        goto gm_lb21;
                    }
                    uint32_t t128 = GM_LD(uint32_t, (((uint8_t *)&gm_l22) + 8));
                    gm_l11 = t128;
                    {
                        {
                            void * t129 = gm_l0;
                            void * t130 = (void *)(((uint8_t *)t129) + 2440);
                            uint32_t t131 = gm_l11;
                            gm_f_get_nt3g9ko((void *)&gm_l27, t130, t131);
                            uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l27))[32] != 0);
                            if (!t132) {
                                goto gm_lb23;
                            }
                            memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l27), 32);
                            {
                                {
                                    uint8_t t133 = (uint8_t)(((((uint8_t *)&gm_l12) + 8))[8] != 0);
                                    if (t133) {
                                        goto gm_lb25;
                                    }
                                    gm_l28 = ((uint8_t)1);
                                    goto gm_lb24;
                                }
gm_lb25:;
                                {
                                    gm_l28 = ((uint8_t)0);
                                    goto gm_lb24;
                                }
gm_lb26:;
                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 382, 20);
                            }
gm_lb24:;
                            uint8_t t134 = gm_l28;
                            if (t134) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                    }
gm_lb22:;
                    void * t135 = gm_l0;
                    void * t136 = GM_LD(void *, (((uint8_t *)t135) + 1088));
                    uint64_t t137 = GM_LD(uint64_t, ((((uint8_t *)t135) + 1088) + 8));
                    uint32_t t138 = gm_l11;
                    uint64_t t139 = (uint64_t)t138;
                    if ((uintptr_t)t139 >= (uintptr_t)(t137)) gm_trap_index((uintptr_t)t139, t137, gm_srccvj6up3gbolp, 384, 19);
                    {
                        {
                            uint8_t t140 = GM_LD(uint8_t, (((((uint8_t *)t136) + (uintptr_t)t139 * 184) + 24) + 0));
                            uint8_t t141 = (uint8_t)(t140 == ((uint8_t)0ULL));
                            if (!t141) {
                                goto gm_lb28;
                            }
                            memmove(((uint8_t *)&F.l13), (((((uint8_t *)t136) + (uintptr_t)t139 * 184) + 24) + 8), 48);
                            {
                                {
                                    uint8_t t142 = (uint8_t)(((((uint8_t *)&F.l13) + 40))[4] != 0);
                                    if (t142) {
                                        goto gm_lb30;
                                    }
                                    gm_l29 = ((uint8_t)0);
                                    goto gm_lb29;
                                }
gm_lb30:;
                                {
                                    gm_l29 = ((uint8_t)1);
                                    goto gm_lb29;
                                }
gm_lb31:;
                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 386, 24);
                            }
gm_lb29:;
                            uint8_t t143 = gm_l29;
                            if (t143) {
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            void * t144 = gm_l0;
                            gm_t88rcucg1b7uk t145 = gm_l1;
                            gm_f_struct_fields_chqu3it((void *)&gm_l30, t144, GM_CONV(gm_csguankko4lmhd, t145));
                            uint8_t t146 = (uint8_t)((((uint8_t *)&gm_l30))[16] != 0);
                            if (t146) {
                                gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l30));
                                gm_l31 = t147;
                            } else {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            gm_b16_8 t148 = gm_l31;
                            gm_l14 = t148;
                            gm_b16_8 t149 = gm_l14;
                            gm_l15 = t149;
                            gm_l16 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t150 = gm_l16;
                                    uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint8_t t152 = (t150 < t151);
                                    if (!t152) {
                                        goto gm_lb32;
                                    }
                                    {
                                        void * t153 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint64_t t155 = gm_l16;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l17), (((uint8_t *)t153) + (uintptr_t)t155 * 40), 40);
                                                void * t156 = gm_l0;
                                                gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                                                uint8_t t158 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t156, t157, ((uint8_t)1));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t159 = (uint8_t)!t158;
                                                if (t159) {
                                                    F.ret = ((uint8_t)0);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb35;
                                            }
gm_lb36:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 388, 21);
                                        }
gm_lb35:;
                                    }
gm_lb34:;
                                    uint64_t t160 = gm_l16;
                                    uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                                    uint64_t t163 = r161;
                                    gm_l16 = t163;
                                }
                            }
gm_lb32:;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb28:;
                        {
                            uint8_t t164 = GM_LD(uint8_t, (((((uint8_t *)t136) + (uintptr_t)t139 * 184) + 24) + 0));
                            uint8_t t165 = (uint8_t)(t164 == ((uint8_t)2ULL));
                            if (!t165) {
                                goto gm_lb37;
                            }
                            gm_b16_8 t166 = GM_LD(gm_b16_8, (((((uint8_t *)t136) + (uintptr_t)t139 * 184) + 24) + 8));
                            gm_l18 = t166;
                            gm_b16_8 t167 = gm_l18;
                            gm_l19 = t167;
                            gm_l20 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t168 = gm_l20;
                                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                    uint8_t t170 = (t168 < t169);
                                    if (!t170) {
                                        goto gm_lb38;
                                    }
                                    {
                                        void * t171 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                        uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                        uint64_t t173 = gm_l20;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l21), (((uint8_t *)t171) + (uintptr_t)t173 * 40), 40);
                                                void * t174 = gm_l0;
                                                gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                                                uint8_t t176 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t174, t175, ((uint8_t)1));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t177 = (uint8_t)!t176;
                                                if (t177) {
                                                    F.ret = ((uint8_t)0);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb41;
                                            }
gm_lb42:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 394, 21);
                                        }
gm_lb41:;
                                    }
gm_lb40:;
                                    uint64_t t178 = gm_l20;
                                    uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                                    uint64_t t181 = r179;
                                    gm_l20 = t181;
                                }
                            }
gm_lb38:;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb37:;
                        {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb43:;
                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 384, 13);
                    }
gm_lb27:;
                }
gm_lb21:;
            }
            {
                {
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb44:;
                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 367, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_source_location_ty_slllard(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_source_location_ty_slllard F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* prelude */
    uint64_t gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* t */
    GM_STACK_CHECK(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l4;
    gm_tf5srh4mp4b3i gm_l5;
    uint64_t gm_l6;
    gm_t6qooom8qa2do gm_l7;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)(((((uint8_t *)t5) + 320))[8] != 0);
    if (t6) {
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t5) + 320));
        gm_l4 = t7;
    } else {
        gm_tkn42u7na3t81 t8;
        memset(&t8, 0, sizeof t8);
        F.ret = t8;
        goto gm_fret;
    }
    uint64_t t9 = gm_l4;
    gm_l1 = t9;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    uint64_t t12 = gm_l1;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sa291memmphhk));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(14));
    gm_tf5srh4mp4b3i t14 = gm_f_lookup_59juqoo(t11, t12, t13);
    gm_l5 = t14;
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l6 = t16;
    } else {
        gm_tkn42u7na3t81 t17;
        memset(&t17, 0, sizeof t17);
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l6;
    gm_l2 = t18;
    {
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l2;
            gm_f_resolve_decl_pus042v((void *)&gm_l7, (void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
            if (!t22) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
            gm_l3 = t23;
            gm_t88rcucg1b7uk t24 = gm_l3;
            gm_tkn42u7na3t81 t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[4] = 1;
            F.ret = t25;
            goto gm_fret;
        }
gm_lb2:;
        gm_tkn42u7na3t81 t26;
        memset(&t26, 0, sizeof t26);
        F.ret = t26;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_sys_effect_97o96a1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_sys_effect_97o96a1 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* prelude */
    uint64_t gm_l2; /* d */
    uint32_t gm_l3; /* e */
    GM_STACK_CHECK(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l4;
    gm_tf5srh4mp4b3i gm_l5;
    uint64_t gm_l6;
    gm_t6qooom8qa2do gm_l7;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)(((((uint8_t *)t5) + 320))[8] != 0);
    if (t6) {
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t5) + 320));
        gm_l4 = t7;
    } else {
        gm_t9805qtonbsag t8;
        memset(&t8, 0, sizeof t8);
        F.ret = t8;
        goto gm_fret;
    }
    uint64_t t9 = gm_l4;
    gm_l1 = t9;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    uint64_t t12 = gm_l1;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sa902c1tbtg0i));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
    gm_tf5srh4mp4b3i t14 = gm_f_lookup_59juqoo(t11, t12, t13);
    gm_l5 = t14;
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l6 = t16;
    } else {
        gm_t9805qtonbsag t17;
        memset(&t17, 0, sizeof t17);
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l6;
    gm_l2 = t18;
    {
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l2;
            gm_f_resolve_decl_pus042v((void *)&gm_l7, (void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)2ULL));
            if (!t22) {
                goto gm_lb2;
            }
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 8));
            gm_l3 = t23;
            uint32_t t24 = gm_l3;
            gm_t9805qtonbsag t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[4] = 1;
            F.ret = t25;
            goto gm_fret;
        }
gm_lb2:;
        gm_t9805qtonbsag t26;
        memset(&t26, 0, sizeof t26);
        F.ret = t26;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_sys_set_pqe8mto(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_sys_set_pqe8mto F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* sys */
    GM_STACK_CHECK(&F, 258u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9805qtonbsag gm_l3;
    uint32_t gm_l4;
    gm_tftb1odc739t5 gm_l5;
    void * t2 = gm_l0;
    gm_t9805qtonbsag t3 = gm_f_sys_effect_97o96a1((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t3;
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        gm_l4 = t5;
    } else {
        gm_tnpifv20irnph t6 = gm_f_empty_effects_r0iv3pv();
        F.ret = t6;
        goto gm_fret;
    }
    uint32_t t7 = gm_l4;
    gm_l1 = t7;
    gm_trqfvmjtt4sij t8;
    gm_f_list_084fcd6((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t8;
    void * t9 = (void *)((uint8_t *)&F.l2);
    uint32_t t10 = gm_l1;
    gm_b16_8 t11 = gm_f_empty_slice_fh7svmj();
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    gm_tiilmfd16v3fc t13;
    GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t10);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t12);
    gm_tftb1odc739t5 t14 = gm_f_push_d1r4h19(t9, &t13);
    gm_l5 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srccvj6up3gbolp, 11, 5);
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
    gm_tnpifv20irnph t18 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t17);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_trap_hook_fn_5c0a4ds(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_trap_hook_fn_5c0a4ds F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tkn42u7na3t81 gm_l4; /* loc */
    uint8_t gm_l5; /* params_ok */
    gm_t88rcucg1b7uk gm_l6; /* l */
    uint32_t gm_l7; /* h */
    GM_STACK_CHECK(&F, 544u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 88) + 24));
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_soo4rtckloi4d));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(12));
    uint8_t t10 = (!gm_eq0iqiubfm7e(((uint8_t *)&t8), ((uint8_t *)&t9)));
    if (t10) {
        void * t11 = gm_l0;
        gm_tl874t2atnlh3 t12 = gm_l2;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s5nig6ebl26qo));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(60));
        gm_b0_1 t14 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 344));
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 344) + 8));
    uint32_t t18 = gm_l1;
    uint64_t t19 = (uint64_t)t18;
    if ((uintptr_t)t19 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t19, t17, gm_srccvj6up3gbolp, 337, 12);
    memmove(((uint8_t *)&F.l3), (((uint8_t *)t16) + (uintptr_t)t19 * 136), 136);
    void * t20 = gm_l0;
    gm_tkn42u7na3t81 t21 = gm_f_source_location_ty_slllard((void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t21;
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 40) + 8));
    uint8_t t23 = ((t22 == ((uint64_t)2ULL)));
    if (t23) {
        void * t24 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 40) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t25, gm_srccvj6up3gbolp, 339, 42);
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t24) + (uintptr_t)((uint64_t)0ULL) * 56) + 16));
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 8);
        gm_t88rcucg1b7uk t29 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = (gm_eq9m6mvtmgde(((uint8_t *)&t26), ((uint8_t *)&t29)));
        gm_l9 = t30;
    } else {
        gm_l9 = ((uint8_t)0);
    }
    uint8_t t31 = gm_l9;
    if (t31) {
        uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 80) + 8));
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        gm_l8 = t33;
    } else {
        gm_l8 = ((uint8_t)0);
    }
    uint8_t t34 = gm_l8;
    gm_l5 = t34;
    uint8_t t35 = gm_l5;
    if (t35) {
        gm_l5 = ((uint8_t)0);
        {
            {
                uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                if (!t36) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
                gm_l6 = t37;
                void * t38 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
                uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 40) + 8));
                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t39, gm_srccvj6up3gbolp, 342, 35);
                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t38) + (uintptr_t)((uint64_t)1ULL) * 56) + 16));
                gm_t88rcucg1b7uk t41 = gm_l6;
                uint8_t t42 = (gm_eq9m6mvtmgde(((uint8_t *)&t40), ((uint8_t *)&t41)));
                gm_l5 = t42;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    uint8_t t43 = gm_l5;
    uint8_t t44 = (uint8_t)!t43;
    if (t44) {
        gm_l11 = ((uint8_t)1);
    } else {
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 8);
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 56));
        gm_t53at80pfdus3 t48;
        gm_f_kind_jcnt4hm((void *)&t48, t46, GM_CONV(gm_csguankko4lmhd, t47));
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
        uint8_t t50 = (uint8_t)(!(t49 == ((uint8_t)5ULL)));
        gm_t53at80pfdus3 t51;
        memset(&t51, 0, sizeof t51);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)5ULL));
        gm_l11 = t50;
    }
    uint8_t t52 = gm_l11;
    if (t52) {
        gm_l10 = ((uint8_t)1);
    } else {
        void * t53 = gm_l0;
        void * t54 = (void *)(((uint8_t *)&F.l3) + 64);
        uint8_t t55 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t56 = (uint8_t)!t55;
        gm_l10 = t56;
    }
    uint8_t t57 = gm_l10;
    if (t57) {
        void * t58 = gm_l0;
        gm_tl874t2atnlh3 t59 = gm_l2;
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s4nl1jq95dg65));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(110));
        gm_b0_1 t61 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t62 = gm_l0;
            uint8_t t63 = (uint8_t)(((((uint8_t *)t62) + 960))[4] != 0);
            if (!t63) {
                goto gm_lb4;
            }
            uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)t62) + 960));
            gm_l7 = t64;
            uint32_t t65 = gm_l7;
            uint32_t t66 = gm_l1;
            uint8_t t67 = (!(t65 == t66));
            if (t67) {
                void * t68 = gm_l0;
                gm_tl874t2atnlh3 t69 = gm_l2;
                gm_b16_8 t70;
                GM_ST(void *, (uint8_t *)&t70, (void *)(gm_scj8qp34gs3m8));
                GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(57));
                gm_b0_1 t71 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t68, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t72 = gm_l0;
    uint32_t t73 = gm_l1;
    gm_t9805qtonbsag t74;
    GM_ST(uint8_t, (uint8_t *)&t74 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t74 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t74), t73);
    ((uint8_t *)&t74)[4] = 1;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t72) + 960), t74);
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
extern __attribute__((aligned(4))) void gm_f_unsupported_value_sufn4da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_within_sys_otp5qp6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_within_sys_otp5qp6 F;
    void * gm_l0; /* s */
    void * gm_l1; /* set */
    gm_t9805qtonbsag gm_l2; /* sys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tiilmfd16v3fc gm_l5; /* e */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_t9805qtonbsag t8 = gm_f_sys_effect_97o96a1((void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t8;
    void * t9 = gm_l1;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t9));
    gm_l3 = t10;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t16 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t14) + (uintptr_t)t16 * 24), 24);
                        gm_t9805qtonbsag t17 = gm_l2;
                        uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                        gm_t9805qtonbsag t19;
                        GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t19), t18);
                        ((uint8_t *)&t19)[4] = 1;
                        uint8_t t20 = (!gm_eqkricceou4u(((uint8_t *)&t17), ((uint8_t *)&t19)));
                        if (t20) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 26, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l4;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l4 = t24;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)1);
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
