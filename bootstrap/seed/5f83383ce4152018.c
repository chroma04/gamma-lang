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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { __int128 f0; gm_b16_8 f1; } gm_t7a4aqr549lb5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
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
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; unsigned __int128 f1; } gm_tou0girmhri5g;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7a4aqr549lb5) == 32 && _Alignof(gm_t7a4aqr549lb5) == 16 && offsetof(gm_t7a4aqr549lb5, f0) == 0 && offsetof(gm_t7a4aqr549lb5, f1) == 16, "layout of `(i128, string)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_tkaapcii86udk f0; uint8_t f1; uint8_t pe[7]; } gm_tab260m0n3sqj;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
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
typedef struct { gm_tvdg94bk7aa22 f0; } gm_th9nl3oso747e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
typedef struct { uint64_t f0; gm_tov4mtrfp5e7v f1; uint8_t pe[4]; } gm_tji1ull97ruec;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tou0girmhri5g) == 32 && _Alignof(gm_tou0girmhri5g) == 16 && offsetof(gm_tou0girmhri5g, f0) == 0 && offsetof(gm_tou0girmhri5g, f1) == 16, "layout of `([]u8, u128)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tab260m0n3sqj) == 32 && _Alignof(gm_tab260m0n3sqj) == 8 && offsetof(gm_tab260m0n3sqj, f0) == 0 && offsetof(gm_tab260m0n3sqj, f1) == 24, "layout of `?Place`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_th9nl3oso747e) == 12 && _Alignof(gm_th9nl3oso747e) == 4 && offsetof(gm_th9nl3oso747e, f0) == 0, "layout of `(Builtin,)`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
_Static_assert(sizeof(gm_tji1ull97ruec) == 24 && _Alignof(gm_tji1ull97ruec) == 8 && offsetof(gm_tji1ull97ruec, f0) == 0 && offsetof(gm_tji1ull97ruec, f1) == 8, "layout of `(i: usize, x: BitField)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { void * f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_t2i13nq5toap9;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t2i13nq5toap9) == 64 && _Alignof(gm_t2i13nq5toap9) == 16 && offsetof(gm_t2i13nq5toap9, f0) == 0 && offsetof(gm_t2i13nq5toap9, f1) == 16, "layout of `(frame: ^Frame, value: Value)`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_tjoi8o798ne9k f2; } gm_ta160mkacvukc;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_tjoi8o798ne9k f1; } gm_tbnj1edh31eaa;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_ta160mkacvukc v0; gm_tbl8il92l97ut v1; gm_t2i13nq5toap9 v2; gm_tbnj1edh31eaa v3; void * v4; gm_tbgd14esf0jvi v5; } f1; } gm_t39uai1ajk3qv;
_Static_assert(sizeof(gm_ta160mkacvukc) == 80 && _Alignof(gm_ta160mkacvukc) == 16 && offsetof(gm_ta160mkacvukc, f0) == 0 && offsetof(gm_ta160mkacvukc, f1) == 8 && offsetof(gm_ta160mkacvukc, f2) == 16, "layout of `(frame: ^Frame, label: u32, value: ?Value)`");
_Static_assert(sizeof(gm_tbnj1edh31eaa) == 80 && _Alignof(gm_tbnj1edh31eaa) == 16 && offsetof(gm_tbnj1edh31eaa, f0) == 0 && offsetof(gm_tbnj1edh31eaa, f1) == 16, "layout of `(with_id: u32, value: ?Value)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; double v1; } f1; } gm_t26jhpkpc0sda;
_Static_assert(sizeof(gm_t39uai1ajk3qv) == 96 && _Alignof(gm_t39uai1ajk3qv) == 16 && offsetof(gm_t39uai1ajk3qv, f0) == 0 && offsetof(gm_t39uai1ajk3qv, f1) == 16, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_tkaapcii86udk v1; } f1; } gm_t5kq49rr2n9f8;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_b16_8 v1; } f1; } gm_t65n7r6ql4pgd;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; void * v1; } f1; } gm_t7g11k265ojef;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; unsigned __int128 v1; } f1; } gm_t8carr7mku484;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; } f1; } gm_t9j082e6qvnjj;
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; uint8_t v1; } f1; } gm_tsg40glpvq01o;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_t6evimt84lndq v1; } f1; } gm_ttk92ufj2f6eh;
_Static_assert(sizeof(gm_t26jhpkpc0sda) == 112 && _Alignof(gm_t26jhpkpc0sda) == 16 && offsetof(gm_t26jhpkpc0sda, f0) == 0 && offsetof(gm_t26jhpkpc0sda, f1) == 16, "layout of `Flow!f64`");
_Static_assert(sizeof(gm_t5kq49rr2n9f8) == 112 && _Alignof(gm_t5kq49rr2n9f8) == 16 && offsetof(gm_t5kq49rr2n9f8, f0) == 0 && offsetof(gm_t5kq49rr2n9f8, f1) == 16, "layout of `Flow!Place`");
_Static_assert(sizeof(gm_t65n7r6ql4pgd) == 112 && _Alignof(gm_t65n7r6ql4pgd) == 16 && offsetof(gm_t65n7r6ql4pgd, f0) == 0 && offsetof(gm_t65n7r6ql4pgd, f1) == 16, "layout of `Flow![]Value`");
_Static_assert(sizeof(gm_t7g11k265ojef) == 112 && _Alignof(gm_t7g11k265ojef) == 16 && offsetof(gm_t7g11k265ojef, f0) == 0 && offsetof(gm_t7g11k265ojef, f1) == 16, "layout of `Flow!^Cell`");
_Static_assert(sizeof(gm_t8carr7mku484) == 112 && _Alignof(gm_t8carr7mku484) == 16 && offsetof(gm_t8carr7mku484, f0) == 0 && offsetof(gm_t8carr7mku484, f1) == 16, "layout of `Flow!u128`");
_Static_assert(sizeof(gm_t9j082e6qvnjj) == 112 && _Alignof(gm_t9j082e6qvnjj) == 16 && offsetof(gm_t9j082e6qvnjj, f0) == 0 && offsetof(gm_t9j082e6qvnjj, f1) == 16, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tsg40glpvq01o) == 112 && _Alignof(gm_tsg40glpvq01o) == 16 && offsetof(gm_tsg40glpvq01o, f0) == 0 && offsetof(gm_tsg40glpvq01o, f1) == 16, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_ttk92ufj2f6eh) == 112 && _Alignof(gm_ttk92ufj2f6eh) == 16 && offsetof(gm_ttk92ufj2f6eh, f0) == 0 && offsetof(gm_ttk92ufj2f6eh, f1) == 16, "layout of `Flow!Value`");
struct gm_fr_abs_v7cf214 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_append_sajo2rq {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_big_float_jmiufgl {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    double ret;
};
struct gm_fr_big_of_1cq0qao {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bigint_i128_p6m54kv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_ta24rd13gupia ret;
};
struct gm_fr_binary_fmoblrg {
    gm_frame h;
    gm_t6evimt84lndq l2; /* a */
    gm_t6evimt84lndq l3; /* b */
    gm_trqfvmjtt4sij l9; /* out */
    gm_t6evimt84lndq l13; /* x */
    gm_t6evimt84lndq l14; /* y */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_bit_s517clu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bits_of_hithcdi {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_tbci5ospk5nnn l6; /* tag */
    gm_t9p38b040grms ret;
};
struct gm_fr_builtin_psq6ql3 {
    gm_frame h;
    void * l0; /* m */
    gm_tvdg94bk7aa22 l1; /* op */
    gm_b16_8 l2; /* args */
    void * l3; /* e */
    void * l4; /* tys */
    gm_tl874t2atnlh3 l5; /* span */
    uint8_t l6; /* ok */
    gm_t6evimt84lndq l7; /* msg */
    gm_b16_8 l8; /* text */
    gm_b16_8 l9; /* vals */
    gm_t6evimt84lndq l10; /* x */
    gm_t88rcucg1b7uk l11; /* at */
    unsigned __int128 l12; /* bits */
    uint8_t l13; /* b */
    double l14; /* f */
    uint32_t l15; /* i */
    gm_tbci5ospk5nnn l16; /* tag */
    unsigned __int128 l17; /* bits */
    double l18; /* f */
    gm_tbci5ospk5nnn l19; /* i */
    gm_t88rcucg1b7uk l20; /* t */
    unsigned __int128 l21; /* bits */
    gm_tbci5ospk5nnn l22; /* v */
    gm_tngtuiuskn938 l23; /* r */
    double l24; /* f */
    gm_tngtuiuskn938 l25; /* q */
    gm_tngtuiuskn938 l26; /* r */
    gm_tngtuiuskn938 l27; /* r */
    gm_tbci5ospk5nnn l28; /* v */
    unsigned __int128 l29; /* bits */
    double l30; /* f */
    gm_tngtuiuskn938 l31; /* q */
    gm_t6evimt84lndq l32; /* y */
    unsigned __int128 l33; /* a */
    unsigned __int128 l34; /* b */
    int32_t l35; /* c */
    double l36; /* a */
    double l37; /* b */
    unsigned __int128 l38; /* v */
    unsigned __int128 l39; /* lo */
    unsigned __int128 l40; /* hi */
    gm_tbci5ospk5nnn l41; /* a */
    unsigned __int128 l42; /* bits */
    gm_tbci5ospk5nnn l43; /* v */
    gm_tngtuiuskn938 l44; /* r */
    gm_tbci5ospk5nnn l45; /* a */
    double l46; /* f */
    unsigned __int128 l47; /* a */
    unsigned __int128 l48; /* b */
    gm_tbci5ospk5nnn l49; /* d */
    gm_t9qkh0rsooc8d l50; /* flavor */
    unsigned __int128 l51; /* base */
    unsigned __int128 l52; /* n */
    gm_tbci5ospk5nnn l53; /* exact */
    gm_t88rcucg1b7uk l54; /* t */
    gm_tngtuiuskn938 l55; /* r */
    uint8_t l56; /* fits */
    gm_b16_8 l57; /* text */
    double l58; /* f */
    double l59; /* f */
    double l60; /* f */
    double l61; /* f */
    uint32_t l62; /* i */
    gm_tbci5ospk5nnn l63; /* tag */
    uint64_t l64; /* n */
    gm_tab260m0n3sqj l65; /* start */
    void * l66; /* a */
    gm_to6glrduves6o l67; /* sl */
    unsigned __int128 l68; /* bits */
    __int128 l69; /* i */
    uint64_t l70; /* k */
    gm_tkaapcii86udk l71; /* pl */
    gm_tkaapcii86udk l72; /* pl */
    gm_tkaapcii86udk l73; /* pl */
    void * l74; /* a */
    gm_t6evimt84lndq l75; /* v */
    gm_to6glrduves6o l76; /* d */
    gm_to6glrduves6o l77; /* s */
    gm_trqfvmjtt4sij l78; /* elems */
    uint64_t l79; /* <tmp> */
    uint64_t l80; /* <tmp> */
    uint64_t l81; /* k */
    gm_b16_8 l82; /* <tmp> */
    uint64_t l83; /* <tmp> */
    uint64_t l84; /* k */
    gm_t6evimt84lndq l85; /* v */
    void * l86; /* inner */
    gm_t6evimt84lndq l87; /* pv */
    gm_tkaapcii86udk l88; /* pl */
    uint64_t l89; /* elem */
    gm_t88rcucg1b7uk l90; /* t */
    gm_t88rcucg1b7uk l91; /* t */
    uint64_t l92; /* index */
    uint64_t l93; /* base */
    unsigned __int128 l94; /* bits */
    gm_trqfvmjtt4sij l95; /* path */
    double l96; /* f */
    unsigned __int128 l97; /* raw */
    gm_ttovhfee8mf28 l98; /* info */
    uint32_t l99; /* n */
    unsigned __int128 l100; /* v */
    gm_closure l101; /* bit */
    unsigned __int128 l102; /* r */
    uint32_t l103; /* c */
    uint32_t l104; /* c */
    uint32_t l105; /* c */
    uint32_t l106; /* <tmp> */
    uint32_t l107; /* <tmp> */
    uint32_t l108; /* i */
    uint32_t l109; /* <tmp> */
    uint32_t l110; /* <tmp> */
    uint32_t l111; /* i */
    uint32_t l112; /* <tmp> */
    uint32_t l113; /* <tmp> */
    uint32_t l114; /* i */
    unsigned __int128 l115; /* amount_bits */
    gm_tbci5ospk5nnn l116; /* amount */
    uint32_t l117; /* k */
    uint32_t l118; /* <tmp> */
    uint32_t l119; /* <tmp> */
    uint32_t l120; /* i */
    unsigned __int128 l121; /* seed */
    uint64_t l122; /* h */
    gm_tvdg94bk7aa22 l123; /* other */
    gm_ttk92ufj2f6eh l124;
    gm_t6evimt84lndq l125;
    gm_ttk92ufj2f6eh l126;
    gm_th2lqbo77pvsc l127;
    gm_b16_8 l128;
    gm_b16_8 l129;
    gm_tabb710d9mq2e l130;
    gm_t65n7r6ql4pgd l131;
    gm_t6evimt84lndq l132;
    gm_t88rcucg1b7uk l133;
    gm_t53at80pfdus3 l134;
    gm_t26jhpkpc0sda l135;
    unsigned __int128 l136;
    gm_t26jhpkpc0sda l137;
    gm_thrdpodrs8pgt l138;
    gm_tbci5ospk5nnn l139;
    gm_t53at80pfdus3 l140;
    gm_t26jhpkpc0sda l141;
    gm_t53at80pfdus3 l142;
    gm_t26jhpkpc0sda l143;
    gm_thrdpodrs8pgt l144;
    gm_tbci5ospk5nnn l145;
    gm_t53at80pfdus3 l146;
    uint8_t l147;
    gm_tq7s98ihonkju l148;
    gm_tvegkb6ouf3m0 l149;
    gm_tngtuiuskn938 l150;
    uint8_t l151;
    gm_tq7s98ihonkju l152;
    gm_tvegkb6ouf3m0 l153;
    gm_tbci5ospk5nnn l154;
    gm_ttk92ufj2f6eh l155;
    gm_ttk92ufj2f6eh l156;
    double l157;
    uint8_t l158;
    uint8_t l159;
    uint8_t l160;
    gm_ttk92ufj2f6eh l161;
    gm_tq7s98ihonkju l162;
    gm_tabb710d9mq2e l163;
    gm_t8carr7mku484 l164;
    uint8_t l165;
    gm_thrdpodrs8pgt l166;
    gm_tbci5ospk5nnn l167;
    __int128 l168;
    __int128 l169;
    uint8_t l170;
    gm_tq7s98ihonkju l171;
    gm_t5kq49rr2n9f8 l172;
    gm_tq7s98ihonkju l173;
    gm_t6evimt84lndq l174;
    gm_t6evimt84lndq l175;
    gm_tq7s98ihonkju l176;
    gm_tftb1odc739t5 l177;
    gm_thf8as07kj3uj l178;
    gm_t9j082e6qvnjj l179;
    gm_t6evimt84lndq l180;
    gm_t53at80pfdus3 l181;
    gm_tbccvikg6b96e l182;
    uint64_t l183;
    gm_tftb1odc739t5 l184;
    gm_tq7s98ihonkju l185;
    gm_tq7s98ihonkju l186;
    gm_t26jhpkpc0sda l187;
    gm_tabb710d9mq2e l188;
    gm_tf5srh4mp4b3i l189;
    uint64_t l190;
    gm_t9j082e6qvnjj l191;
    gm_tabb710d9mq2e l192;
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_byte_at_m94bj3e {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_cmp_9aseneh {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    int32_t ret;
};
struct gm_fr_cmp_i1knrgu {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    int32_t ret;
};
struct gm_fr_coerce_fesnqv4 {
    gm_frame h;
    gm_t6evimt84lndq l4; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_const_cell_p4adg2s {
    gm_frame h;
    gm_tjcptv9sagfq8 l5; /* c */
    gm_t7g11k265ojef ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_convert_value_v1i8v3i {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_display_covn4i2 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_div_d8sd4qc {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_div_f0qrfbr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_div_floor_u3mfihk {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_empty_slice_84b0b46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_2 {
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
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_equal_hinsj77 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* a */
    gm_t6evimt84lndq l2; /* b */
    gm_tsg40glpvq01o ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_ll5819n {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_tys_foan32f {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_tpautr4br7h3l l10; /* def */
    gm_trqfvmjtt4sij l11; /* out */
    gm_t0mglksrkfa5e l14; /* f */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fit_8hvi4bv {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* exact */
    gm_tngtuiuskn938 l6; /* r */
    gm_t8carr7mku484 ret;
};
struct gm_fr_fit_small_7o94i8v {
    gm_frame h;
    gm_t8carr7mku484 ret;
};
struct gm_fr_float_exact_1g17c6c {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* num */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_float_rem_24loign {
    gm_frame h;
    gm_tngtuiuskn938 l2; /* a */
    gm_tngtuiuskn938 l3; /* b */
    gm_tngtuiuskn938 l4; /* q */
    gm_tbci5ospk5nnn l5; /* t */
    gm_tngtuiuskn938 l6; /* r */
    double ret;
};
struct gm_fr_float_to_int_sf4dap7 {
    gm_frame h;
    gm_tngtuiuskn938 l4; /* q */
    gm_tbci5ospk5nnn l5; /* fl */
    gm_tngtuiuskn938 l6; /* frac */
    gm_tbci5ospk5nnn l7; /* i */
    gm_tngtuiuskn938 l9; /* half */
    gm_tngtuiuskn938 l11; /* r */
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_fn_0957mgc {
    gm_frame h;
    gm_t6evimt84lndq ret;
};
struct gm_fr_fn_0l5daab {
    gm_frame h;
    gm_t6evimt84lndq ret;
};
struct gm_fr_format_0fuesvo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0sujqbp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9u3jvus {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eruojoe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g0ibdjv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o6d0gj1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q8f0n70 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u0vfg08 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ven7kt5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_i128_1n0s49q {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_bytes_ql9o4k0 {
    gm_frame h;
    gm_trqfvmjtt4sij l17; /* elems */
    gm_tjoi8o798ne9k ret;
};
struct gm_fr_get_pk8ncss {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_hash_byte_words_gtg9hm1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_value_j2e3sqh {
    gm_frame h;
    void * l0; /* m */
    gm_t6evimt84lndq l1; /* v */
    gm_to6glrduves6o l19; /* sl */
    void * l25; /* a */
    gm_t6evimt84lndq l29; /* x */
    gm_t6evimt84lndq l35; /* x */
    gm_tpautr4br7h3l l37; /* def */
    gm_t6evimt84lndq l44; /* x */
    gm_t9j082e6qvnjj ret;
};
struct gm_fr_hash_word_r89fvfg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_int_info_2r9qb5n {
    gm_frame h;
    gm_ttovhfee8mf28 ret;
};
struct gm_fr_int_op_jnafd2a {
    gm_frame h;
    gm_tbci5ospk5nnn l16; /* a */
    gm_tbci5ospk5nnn l17; /* c */
    gm_tbci5ospk5nnn l18; /* exact */
    gm_tbci5ospk5nnn l25; /* a */
    gm_tbci5ospk5nnn l26; /* c */
    gm_tbci5ospk5nnn l27; /* exact */
    gm_t8carr7mku484 ret;
};
struct gm_fr_int_range_hue3vms {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* half */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_int_value_6hmh8tj {
    gm_frame h;
    __int128 ret;
};
struct gm_fr_is_even_3kgcpjs {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_negative_rb46l9j {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_less_d82rkss {
    gm_frame h;
    gm_t6evimt84lndq l1; /* a */
    gm_t6evimt84lndq l2; /* b */
    gm_tbci5ospk5nnn l11; /* ti */
    gm_tbci5ospk5nnn l12; /* tj */
    gm_t6evimt84lndq l25; /* p */
    gm_t6evimt84lndq l26; /* q */
    gm_tsg40glpvq01o ret;
};
struct gm_fr_list_0202sdh_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kl0t9m0 {
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
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mask_gbhl4hp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_mul_nfj5rjv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_od1e7nd {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_offsets_niooj98 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* tys */
    gm_trqfvmjtt4sij l7; /* offs */
    gm_tpautr4br7h3l l13; /* def */
    gm_trqfvmjtt4sij l14; /* tys */
    gm_t0mglksrkfa5e l17; /* f */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_payload_ty_mu1ii1k {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_push_0pgacsj_2 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7o6oubd {
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
struct gm_fr_put_bytes_1ltl1lv {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l23; /* x */
    uint8_t ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hvk9f35 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reinterpret_jmr6jts {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_trqfvmjtt4sij l6; /* bytes */
    gm_t6evimt84lndq l9; /* r */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_rem_ib4l1vl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_round_float_saju9fu {
    gm_frame h;
    gm_tngtuiuskn938 l4; /* r */
    gm_t26jhpkpc0sda ret;
};
struct gm_fr_same_place_1r7qrqk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_scalar_binary_bvc65kt {
    gm_frame h;
    gm_t6evimt84lndq l2; /* a */
    gm_t6evimt84lndq l3; /* b */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
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
struct gm_fr_sub_mler6q8 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
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
struct gm_fr_u8_elems_akjlvj2 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_unary_19d8qg7 {
    gm_frame h;
    gm_t6evimt84lndq l4; /* v */
    gm_trqfvmjtt4sij l7; /* out */
    gm_t6evimt84lndq l10; /* el */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_unary_value_vi7cs6u {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_unit_value_evae6mr {
    gm_frame h;
    gm_t6evimt84lndq ret;
};
struct gm_fr_unsupported_2seq49l {
    gm_frame h;
    gm_t39uai1ajk3qv ret;
};
struct gm_fr_variant_tag_4pbamin {
    gm_frame h;
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_wrap_bt8r149 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
GM_NOINLINE int gm_c_builtin_psq6ql3_1105(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out);
GM_NOINLINE int gm_c_builtin_psq6ql3_214(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out);
GM_NOINLINE int gm_c_builtin_psq6ql3_299(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out);
GM_NOINLINE int gm_c_builtin_psq6ql3_666(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out);
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_append_sajo2rq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_args_6f16vj1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_big_of_1cq0qao(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, unsigned __int128 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_bigint_i128_p6m54kv_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_binary_fmoblrg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_bits_of_hithcdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_builtin_psq6ql3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_coerce_fesnqv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_const_cell_p4adg2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_convert_value_v1i8v3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_copy_nk4jid6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_display_covn4i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_mod_floor_eqv1ila(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_equal_hinsj77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_eval_2vbtp1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_ll5819n(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_field_tys_foan32f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_8gjlcrm(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_fit_8hvi4bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_fit_small_7o94i8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, __int128 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_float_exact_1g17c6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
__attribute__((aligned(4))) double gm_f_float_rem_24loign(void *env, gm_node *cx, gm_node *inst, double a0, double a1);
__attribute__((aligned(4))) void gm_f_float_to_int_sf4dap7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3);
inline __attribute__((aligned(4))) void gm_f_fn_0957mgc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) void gm_f_fn_0l5daab(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) void gm_f_format_0fuesvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_0sujqbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6hv2i2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_9u3jvus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hmhrs8a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_nb73kt4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o6d0gj1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0);
inline __attribute__((aligned(4))) void gm_f_format_q8f0n70(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_u0vfg08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u128_gl5fk0t(void *gm_out, void *env, gm_node *cx, gm_node *inst, unsigned __int128 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_get_bytes_ql9o4k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) void gm_f_hash_byte_words_gtg9hm1(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_value_j2e3sqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_hash_word_r89fvfg(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_ttovhfee8mf28 gm_f_int_info_2r9qb5n(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_int_op_jnafd2a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, unsigned __int128 a2, unsigned __int128 a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) void gm_f_int_range_hue3vms(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __int128 gm_f_int_value_6hmh8tj(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_less_d82rkss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_nf7o5kl(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0);
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_15vqrvd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_new_2jv6hag(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_70le0lp(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_lcff781(void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_offset_nkn711s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_offsets_niooj98(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_mu1ii1k(void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_place_9889dgp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_pow_mhlb5fr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_put_bytes_1ltl1lv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvk9f35(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_reinterpret_jmr6jts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_round_float_saju9fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk_c(gm_tkaapcii86udk a0, gm_tkaapcii86udk a1);
__attribute__((aligned(4))) void gm_f_scalar_binary_bvc65kt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_store_kejn5q1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_string_of_plna4gu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trap_jftbss6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_u8_elems_akjlvj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_closure a3);
__attribute__((aligned(4))) void gm_f_unary_19d8qg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_unary_value_vi7cs6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) void gm_f_unit_value_evae6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_unsupported_2seq49l(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_variant_tag_4pbamin(void *gm_out, void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) gm_thrdpodrs8pgt gm_f_variant_tag_4pbamin_c(void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cpr60jghihe[16] __attribute__((aligned(8)));
extern uint8_t gm_k0cjq15hqfg[40] __attribute__((aligned(8)));
extern uint8_t gm_k6rrrg8di6i[40] __attribute__((aligned(8)));
extern uint8_t gm_kb0gdpup6ei[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmnul2f0o4[40] __attribute__((aligned(8)));
extern uint8_t gm_kequv58cbtg[40] __attribute__((aligned(8)));
extern uint8_t gm_kgj4rhjmudg[40] __attribute__((aligned(8)));
extern uint8_t gm_kha5d1oq7pb[40] __attribute__((aligned(8)));
extern uint8_t gm_khqbs9bi0vi[40] __attribute__((aligned(8)));
extern uint8_t gm_kil3h1h1ecr[40] __attribute__((aligned(8)));
extern uint8_t gm_kinrtga7ov4[40] __attribute__((aligned(8)));
extern uint8_t gm_kkei5iuecp0[40] __attribute__((aligned(8)));
extern uint8_t gm_kl8d110025v[40] __attribute__((aligned(8)));
extern uint8_t gm_klc17hcqkii[40] __attribute__((aligned(8)));
extern uint8_t gm_klr1ounjj8d[40] __attribute__((aligned(8)));
extern uint8_t gm_knm7lgi0bll[40] __attribute__((aligned(8)));
extern uint8_t gm_ksi8gqve35e[40] __attribute__((aligned(8)));
extern uint8_t gm_ksrlu8s6v53[40] __attribute__((aligned(8)));
extern uint8_t gm_ktbnd3fuoao[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0dggtblbl7jq[30];
extern const uint8_t gm_s0q6kpmhsprri[33];
extern const uint8_t gm_s13hosdv3k3g6[21];
extern const uint8_t gm_s2kubr08sqj72[15];
extern const uint8_t gm_s3385032lmmqq[24];
extern const uint8_t gm_s3o2km4i906va[32];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s5k1c58je5d4s[25];
extern const uint8_t gm_s8afn1h3asffl[32];
extern const uint8_t gm_s94n39ltlvmhe[36];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_s9ofbo8c28rrv[16];
extern const uint8_t gm_sbjuojbqo51av[36];
extern const uint8_t gm_sbsu8rcurukea[25];
extern const uint8_t gm_sciu02cqahje3[52];
extern const uint8_t gm_sckeklr60tlq3[34];
extern const uint8_t gm_scn0pl9eq3k2k[24];
extern const uint8_t gm_sd3b7a2bnmpef[25];
extern const uint8_t gm_sdpkj0pb6qtc0[33];
extern const uint8_t gm_sdrg69bmlm0tm[30];
extern const uint8_t gm_seg4idqqmijmd[34];
extern const uint8_t gm_shq9t6lutffbf[4];
extern const uint8_t gm_sjn4krvshoajd[26];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const uint8_t gm_sllgvbfukep8p[24];
extern const uint8_t gm_smqhtg33814q9[23];
extern const uint8_t gm_smr3su985na96[16];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sqe4aodms9tfs[14];
extern const uint8_t gm_sqh0568fl8oao[14];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src6b5gn9iggr1p[];
extern const char gm_src9vg8k4mbj38f[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcue4ionbe9lgh[];
extern const uint8_t gm_srfcp96u5v1ui[37];
extern const uint8_t gm_srffcdgl461h6[15];
extern const uint8_t gm_srtm6s969i45s[22];
extern const uint8_t gm_su1oqg46rlg2d[10];
extern const uint8_t gm_sv7lsha1e0i8r[13];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const gm_tmpl gm_tpl0j5bgvjcbn0u;
extern const gm_tmpl gm_tpl15v8rvh46nek;
extern const gm_tmpl gm_tpl4isjs1bg9fhg;
extern const gm_tmpl gm_tpl598lk1ciiqvp;
extern const gm_tmpl gm_tpldrlq7uo0cbrm;
extern const gm_tmpl gm_tplgqr018o660ue;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplrpvalpqjvipe;
extern const gm_tmpl gm_tplui184heavke2;
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b) {
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
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_abs_v7cf214 F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, ((uint8_t *)&F.l1), ((uint8_t)0));
    memmove(gm_out, ((uint8_t *)&F.l1), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t5, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_append_sajo2rq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_append_sajo2rq F;
    gm_b16_8 gm_l0; /* path */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    gm_trqfvmjtt4sij t2;
    gm_f_list_kl0t9m0((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t4 = gm_l0;
    gm_tftb1odc739t5 t5 = gm_f_extend_ll5819n(t3, t4);
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src9vg8k4mbj38f, 547, 5);
    void * t7 = (void *)((uint8_t *)&F.l2);
    uint64_t t8 = gm_l1;
    gm_tftb1odc739t5 t9 = gm_f_push_7o6oubd(t7, t8);
    gm_l4 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src9vg8k4mbj38f, 548, 5);
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
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
inline __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_big_float_jmiufgl F;
    void * gm_l0; /* m */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tngtuiuskn938 t2;
    gm_f_from_int_ofjuoqh((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    double t3 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t2);
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
inline __attribute__((aligned(4))) void gm_f_big_of_1cq0qao(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, unsigned __int128 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_big_of_1cq0qao F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_ttovhfee8mf28 gm_l3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
    gm_l3 = t4;
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
    if (t5) {
        void * t6 = gm_l0;
        unsigned __int128 t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = gm_l2;
        __int128 t9 = gm_f_int_value_6hmh8tj(t6, t7, GM_CONV(gm_csguankko4lmhd, t8));
        gm_f_from_i128_1n0s49q((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    unsigned __int128 t10 = gm_l1;
    gm_f_from_u128_gl5fk0t((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t10);
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
inline __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bigint_i128_p6m54kv F;
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_f_to_i128_o7ud1ue((void *)&(*(gm_ta24rd13gupia *)gm_out), &t2);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_bit_s517clu F;
    uint32_t gm_l0; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t2 = GM_LD(unsigned __int128, ((uint8_t *)&((struct gm_fr_builtin_psq6ql3 *)((&F.h)->up))->l100));
    uint32_t t3 = gm_l0;
    unsigned __int128 t4 = (unsigned __int128)t3;
    unsigned __int128 n5 = (unsigned __int128)t4;
    if (n5 >= 128) gm_trap_shift((uint64_t)n5, "u128", gm_srcue4ionbe9lgh, 603, 43);
    unsigned __int128 t6 = (unsigned __int128)(t2 >> (int)n5);
    unsigned __int128 t7 = (unsigned __int128)(t6 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
    uint8_t t8 = ((t7 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_at_m94bj3e F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 88) + 8));
    if (t5) {
        uint64_t t6 = gm_l1;
        gm_l3 = t6;
    } else {
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 1064, 98);
        uint64_t t10 = r8;
        uint64_t t11 = gm_l1;
        uint64_t r12; bool o13 = __builtin_sub_overflow(t10, t11, &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 1064, 98);
        uint64_t t14 = r12;
        gm_l3 = t14;
    }
    uint64_t t15 = gm_l3;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_cmp_9aseneh F;
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_mul_nfj5rjv((void *)&t3, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int32_t t4 = gm_f_cmp_i1knrgu(&t2, &t3);
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
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_i1knrgu F;
    int32_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = (!(t2 == t3));
    if (t4) {
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a0));
        if (t5) {
            F.ret = ((int32_t)-1LL);
            goto gm_fret;
        }
        F.ret = ((int32_t)1LL);
        goto gm_fret;
    }
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    int32_t t10 = gm_f_mag_cmp_s39veq6(t7, t9);
    gm_l2 = t10;
    uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t11) {
        int32_t t12 = gm_l2;
        if (t12 == ((int32_t)-2147483648LL)) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 360, 28);
        int32_t t13 = (int32_t)((uint32_t)0 - (uint32_t)t12);
        F.ret = t13;
        goto gm_fret;
    }
    int32_t t14 = gm_l2;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 2128);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_trlabqsjjp0ds t6;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6 + 12), t5);
    gm_t9805qtonbsag t7 = gm_f_get_pk8ncss(t3, t6);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t3, t5);
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srclv3pj5es0f8o, 330, 5);
    uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)a0));
    gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_tbci5ospk5nnn t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t10 + 8), t9);
    memmove(gm_out, ((uint8_t *)&t10), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_d8sd4qc F;
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)a1)));
    if (t2) {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scn0pl9eq3k2k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
        gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8), gm_src6b5gn9iggr1p, 102, 22);
    }
    gm_tbci5ospk5nnn t4;
    gm_f_mul_nfj5rjv((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t5;
    gm_f_mul_nfj5rjv((void *)&t5, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t4, &t5);
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
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_f0qrfbr F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tngtuiuskn938 gm_l3;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
    gm_f_div_rem_sc09dcc((void *)&gm_l3, (void *)NULL, cx, NULL, &t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 48);
    memmove(gm_out, ((uint8_t *)&F.l2), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_floor_u3mfihk F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tngtuiuskn938 gm_l3;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
    gm_f_div_mod_floor_eqv1ila((void *)&gm_l3, (void *)NULL, cx, NULL, &t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 48);
    memmove(gm_out, ((uint8_t *)&F.l2), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_2onqfca(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 4);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_ll5819n(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_ll5819n F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_hvk9f35(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 8);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
inline __attribute__((aligned(4))) void gm_f_fn_0957mgc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_fn_0957mgc F;
    uint64_t gm_l0; /* k */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hash_value_j2e3sqh *)((&F.h)->up))->l0));
    uint64_t t3 = gm_l0;
    gm_tkaapcii86udk t4;
    gm_f_offset_nkn711s((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&((struct gm_fr_hash_value_j2e3sqh *)((&F.h)->up))->l19)), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_load_nf7o5kl((void *)&(*(gm_t6evimt84lndq *)gm_out), t2, &t4);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_fn_0l5daab(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_fn_0l5daab F;
    uint64_t gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hash_value_j2e3sqh *)((&F.h)->up))->l25));
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint64_t t5 = gm_l0;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcue4ionbe9lgh, 785, 67);
    memmove(gm_out, (((uint8_t *)t3) + (uintptr_t)t6 * 48), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_0fuesvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0fuesvo F;
    gm_t7a4aqr549lb5 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t7a4aqr549lb5 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0q6kpmhsprri));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t7a4aqr549lb5 t3 = GM_LD(gm_t7a4aqr549lb5, ((uint8_t *)a0));
    gm_f_format_nb73kt4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrpvalpqjvipe), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_0sujqbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0sujqbp F;
    gm_tou0girmhri5g gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tou0girmhri5g gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sckeklr60tlq3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_tou0girmhri5g t3 = GM_LD(gm_tou0girmhri5g, ((uint8_t *)a0));
    gm_f_format_6hv2i2f((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0j5bgvjcbn0u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9u3jvus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_9u3jvus F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_eruojoe F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5k1c58je5d4s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgqr018o660ue), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_g0ibdjv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shq9t6lutffbf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldrlq7uo0cbrm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o6d0gj1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0) {
    struct gm_fr_format_o6d0gj1 F;
    gm_th9nl3oso747e gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_th9nl3oso747e gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th9nl3oso747e t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdpkj0pb6qtc0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_f_format_hmhrs8a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl598lk1ciiqvp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q8f0n70(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q8f0n70 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3o2km4i906va));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplui184heavke2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_u0vfg08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_u0vfg08 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s13hosdv3k3g6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4isjs1bg9fhg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ven7kt5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sciu02cqahje3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl15v8rvh46nek), t3);
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
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0) {
    struct gm_fr_from_i128_1n0s49q F;
    __int128 gm_l0; /* v */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    __int128 t2 = gm_l0;
    unsigned __int128 t3 = (t2 > ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)) ? (unsigned __int128)t2 - (unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)) : (unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)) - (unsigned __int128)t2);
    gm_f_from_u128_gl5fk0t((void *)&F.l1, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    __int128 t4 = gm_l0;
    uint8_t t5 = (t4 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t5);
    memmove(gm_out, ((uint8_t *)&F.l1), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_pk8ncss F;
    void * gm_l0; /* m */
    gm_trlabqsjjp0ds gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_8gjlcrm(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    uint64_t t8 = gm_l4;
    gm_l2 = t8;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint64_t t12 = gm_l2;
    if ((uintptr_t)t12 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t12, t11, gm_srcbsc23is03ttm, 139, 12);
    uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t10) + (uintptr_t)t12 * 24) + 20));
    gm_t9805qtonbsag t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[4] = 1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_word_r89fvfg(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_word_r89fvfg F;
    void * gm_l0; /* h */
    uint64_t gm_l1; /* x */
    unsigned __int128 gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t t4 = gm_l1;
    uint64_t t5 = (uint64_t)(t3 ^ t4);
    unsigned __int128 t6 = (unsigned __int128)t5;
    unsigned __int128 r7; bool o8 = __builtin_mul_overflow(t6, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 11400714819323198485ULL)), &r7);
    unsigned __int128 t9 = r7;
    gm_l2 = t9;
    void * t10 = gm_l0;
    unsigned __int128 t11 = gm_l2;
    uint64_t t12 = (uint64_t)t11;
    unsigned __int128 t13 = gm_l2;
    unsigned __int128 n14 = (unsigned __int128)((uint64_t)64ULL);
    if (n14 >= 128) gm_trap_shift((uint64_t)n14, "u128", gm_srcue4ionbe9lgh, 665, 26);
    unsigned __int128 t15 = (unsigned __int128)(t13 >> (int)n14);
    uint64_t t16 = (uint64_t)t15;
    uint64_t t17 = (uint64_t)(t12 ^ t16);
    GM_ST(uint64_t, ((uint8_t *)t10), t17);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_even_3kgcpjs F;
    GM_STACK_CHECK_SHALLOW(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        void * t4 = GM_LD(void *, (((uint8_t *)a0) + 8));
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t5, gm_srclv3pj5es0f8o, 344, 63);
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 4));
        uint32_t t7 = (uint32_t)(t6 & ((uint32_t)1ULL));
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        gm_l1 = t8;
    }
    uint8_t t9 = gm_l1;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_ab2ohk2 F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint8_t t3 = ((t2 == ((uint64_t)1ULL)));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_rb46l9j F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    F.ret = t2;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi_2();
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
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kl0t9m0 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_84b0b46();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t gm_l3;
    memmove(((uint8_t *)&F.l2), ((uint8_t *)&F.l1), 40);
    void * t2 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t2);
    uint8_t t3 = gm_l0;
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        gm_l3 = t5;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l3;
    gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_tbci5ospk5nnn t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t8 + 8), t7);
    memmove(gm_out, ((uint8_t *)&t8), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_nfj5rjv F;
    GM_STACK_CHECK(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&F.l1));
    uint8_t t4 = (!(t2 == t3));
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_trqfvmjtt4sij t9;
    gm_f_mag_mul_if5s38k((void *)&t9, (void *)NULL, cx, NULL, t6, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t4, &t9);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_od1e7nd F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_mul_nfj5rjv((void *)&t3, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t2, &t3);
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t gm_l2;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)&F.l0) + 8) + 8));
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        gm_l2 = t6;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l2;
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t7);
    memmove(gm_out, ((uint8_t *)&F.l1), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t r4; bool o5 = __builtin_mul_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t6 = r4;
    uint32_t t7 = (uint32_t)t6;
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ddqa9t6_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7o6oubd F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_84b0b46();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hvk9f35(t8, ((uint64_t)1ULL), t10);
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
    uint64_t t18 = gm_l1;
    GM_ST(uint64_t, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)16ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvk9f35(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hvk9f35 F;
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
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_rem_ib4l1vl F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tngtuiuskn938 gm_l3;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
    gm_f_div_rem_sc09dcc((void *)&gm_l3, (void *)NULL, cx, NULL, &t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 48), 48);
    memmove(gm_out, ((uint8_t *)&F.l2), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t2, &t6);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_mler6q8 F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_mul_nfj5rjv((void *)&t3, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t4;
    gm_f_sub_pktm5cr((void *)&t4, (void *)NULL, cx, NULL, &t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t5;
    gm_f_mul_nfj5rjv((void *)&t5, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t4, &t5);
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t6, t8);
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
inline __attribute__((aligned(4))) void gm_f_unit_value_evae6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_unit_value_evae6mr F;
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tq7s98ihonkju gm_l0;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi_2();
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    gm_tq7s98ihonkju t4 = gm_f_new_lcff781((void *)NULL, cx, NULL, t3, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kequv58cbtg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l0 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l0) + 8, gm_src9vg8k4mbj38f, 133, 49);
    void * t6 = GM_LD(void *, (((uint8_t *)&gm_l0) + 8));
    gm_t6evimt84lndq t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)3ULL));
    GM_ST(void *, ((uint8_t *)&t7 + 16), t6);
    memmove(gm_out, ((uint8_t *)&t7), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_unsupported_2seq49l(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unsupported_2seq49l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK_SHALLOW(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tbgd14esf0jvi t4;
    GM_ST(uint32_t, (uint8_t *)&t4 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 16), t3);
    gm_t39uai1ajk3qv t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)5ULL));
    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t5 + 16), t4);
    memmove(gm_out, ((uint8_t *)&t5), 96);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wrap_bt8r149 F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_ttovhfee8mf28 gm_l3;
    unsigned __int128 t2 = gm_l1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_ttovhfee8mf28 t5 = gm_f_int_info_2r9qb5n(t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_l3 = t5;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    unsigned __int128 t7 = gm_f_mask_gbhl4hp(t6);
    unsigned __int128 t8 = (unsigned __int128)(t2 & t7);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
GM_NOINLINE int gm_c_builtin_psq6ql3_1105(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out) {
#define F (*gm_Fp)
    {
        uint8_t t1106 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t1107 = (uint8_t)(t1106 == ((uint8_t)35ULL));
        if (!t1107) {
            goto gm_lb128;
        }
        gm_t6evimt84lndq t1108 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
        gm_ttk92ufj2f6eh t1109;
        memset(&t1109, 0, sizeof t1109);
        ((uint8_t *)&t1109)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1109 + 16), t1108);
        memmove(gm_out, ((uint8_t *)&t1109), 112);
        return 1;
    }
gm_lb128:;
    {
        uint8_t t1110 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t1111 = (uint8_t)(t1110 == ((uint8_t)78ULL));
        if (!t1111) {
            goto gm_lb129;
        }
        {
            {
                uint8_t t1112 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t1113 = (uint8_t)(t1112 == ((uint8_t)5ULL));
                if (!t1113) {
                    goto gm_lb131;
                }
                memmove(((uint8_t *)&F.l76), (((uint8_t *)&F.l10) + 16), 32);
                {
                    {
                        void * t1114 = GM_LD(void *, ((uint8_t *)&F.l9));
                        uint64_t t1115 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1115)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1115, gm_srcue4ionbe9lgh, 560, 26);
                        uint8_t t1116 = GM_LD(uint8_t, ((((uint8_t *)t1114) + (uintptr_t)((uint64_t)1ULL) * 48) + 0));
                        uint8_t t1117 = (uint8_t)(t1116 == ((uint8_t)5ULL));
                        if (!t1117) {
                            goto gm_lb133;
                        }
                        memmove(((uint8_t *)&F.l77), ((((uint8_t *)t1114) + (uintptr_t)((uint64_t)1ULL) * 48) + 16), 32);
                        gm_trqfvmjtt4sij t1118;
                        gm_f_list_0202sdh_2((void *)&t1118, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            return 2;
                        }
                        F.l78 = t1118;
                        F.l80 = ((uint64_t)0ULL);
                        uint64_t t1119 = GM_LD(uint64_t, (((uint8_t *)&F.l77) + 24));
                        F.l79 = t1119;
                        {
                            for (;;) {
                                uint64_t t1121 = F.l80;
                                uint64_t t1122 = F.l79;
                                uint8_t t1123 = (t1121 < t1122);
                                if (!t1123) {
                                    goto gm_lb134;
                                }
                                {
                                    {
                                        {
                                            uint64_t t1124 = F.l80;
                                            F.l81 = t1124;
                                            void * t1125 = (void *)((uint8_t *)&F.l78);
                                            void * t1126 = F.l0;
                                            uint64_t t1127 = F.l81;
                                            gm_tkaapcii86udk t1128;
                                            gm_f_offset_nkn711s((void *)&t1128, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&F.l77)), t1127);
                                            if (gm_unw.kind) {
                                                goto gm_ll1120;
                                            }
                                            gm_t6evimt84lndq t1129;
                                            gm_f_load_nf7o5kl((void *)&t1129, t1126, &t1128);
                                            gm_t6evimt84lndq t1130;
                                            gm_f_copy_nk4jid6((void *)&t1130, (void *)NULL, cx, NULL, &t1129);
                                            if (gm_unw.kind) {
                                                goto gm_ll1120;
                                            }
                                            gm_tftb1odc739t5 t1131 = gm_f_push_0pgacsj_2(t1125, &t1130);
                                            F.l177 = t1131;
                                            uint8_t t1132 = (uint8_t)!((((uint8_t *)&F.l177))[0] != 0);
                                            if (!t1132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l177) + 1, gm_srcue4ionbe9lgh, 563, 33);
                                            goto gm_lb137;
                                        }
gm_lb138:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 563, 13);
                                    }
gm_lb137:;
                                }
gm_lb136:;
                                uint64_t t1133 = F.l80;
                                uint64_t r1134; bool o1135 = __builtin_add_overflow(t1133, ((uint64_t)1ULL), &r1134);
                                uint64_t t1136 = r1134;
                                F.l80 = t1136;
                            }
                        }
                        goto gm_ls1137;
gm_ll1120:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb134; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb136; }
                        }
                        return 2;
gm_ls1137:;
gm_lb134:;
                        gm_b16_8 t1138 = GM_LD(gm_b16_8, ((uint8_t *)&F.l78));
                        gm_b16_8 t1139 = GM_LD(gm_b16_8, ((uint8_t *)&t1138));
                        F.l82 = t1139;
                        F.l83 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t1141 = F.l83;
                                uint64_t t1142 = GM_LD(uint64_t, (((uint8_t *)&F.l82) + 8));
                                uint8_t t1143 = (t1141 < t1142);
                                if (!t1143) {
                                    goto gm_lb139;
                                }
                                {
                                    uint64_t t1144 = F.l83;
                                    void * t1145 = GM_LD(void *, ((uint8_t *)&F.l82));
                                    uint64_t t1146 = GM_LD(uint64_t, (((uint8_t *)&F.l82) + 8));
                                    uint64_t t1147 = F.l83;
                                    gm_t6evimt84lndq t1148 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t1145) + (uintptr_t)t1147 * 48));
                                    gm_thf8as07kj3uj t1149;
                                    GM_ST(uint64_t, (uint8_t *)&t1149 + 8, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t1149 + 0), t1144);
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1149 + 16), t1148);
                                    F.l178 = t1149;
                                    {
                                        {
                                            uint64_t t1150 = GM_LD(uint64_t, ((uint8_t *)&F.l178));
                                            F.l84 = t1150;
                                            memmove(((uint8_t *)&F.l85), (((uint8_t *)&F.l178) + 16), 48);
                                            void * t1151 = F.l0;
                                            uint64_t t1152 = F.l84;
                                            gm_tkaapcii86udk t1153 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&F.l76));
                                            uint64_t t1154 = (uint64_t)t1152;
                                            gm_tkaapcii86udk t1155;
                                            gm_f_offset_nkn711s((void *)&t1155, (void *)NULL, cx, NULL, &t1153, t1154);
                                            if (gm_unw.kind) {
                                                goto gm_ll1140;
                                            }
                                            gm_tl874t2atnlh3 t1156 = F.l5;
                                            gm_t6evimt84lndq t1157 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l85));
                                            gm_f_store_kejn5q1((void *)&F.l179, (void *)NULL, cx, NULL, t1151, &t1155, &t1157, t1156);
                                            if (gm_unw.kind) {
                                                goto gm_ll1140;
                                            }
                                            uint8_t t1158 = (uint8_t)!((((uint8_t *)&F.l179))[0] != 0);
                                            if (t1158) {
                                            } else {
                                                gm_t39uai1ajk3qv t1159 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l179) + 16));
                                                gm_ttk92ufj2f6eh t1160;
                                                GM_ST(uint8_t, (uint8_t *)&t1160 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t1160 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t1160 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t1160 + 8, 0);
                                                ((uint8_t *)&t1160)[0] = 1;
                                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1160 + 16), t1159);
                                                memmove(gm_out, ((uint8_t *)&t1160), 112);
                                                return 1;
                                            }
                                            goto gm_lb142;
                                        }
gm_lb143:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 564, 13);
                                    }
gm_lb142:;
                                }
gm_lb141:;
                                uint64_t t1161 = F.l83;
                                uint64_t r1162; bool o1163 = __builtin_add_overflow(t1161, ((uint64_t)1ULL), &r1162);
                                uint64_t t1164 = r1162;
                                F.l83 = t1164;
                            }
                        }
                        goto gm_ls1165;
gm_ll1140:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb139; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb141; }
                        }
                        return 2;
gm_ls1165:;
gm_lb139:;
                        gm_t6evimt84lndq t1166;
                        gm_f_unit_value_evae6mr((void *)&t1166, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            return 2;
                        }
                        gm_ttk92ufj2f6eh t1167;
                        memset(&t1167, 0, sizeof t1167);
                        ((uint8_t *)&t1167)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1167 + 16), t1166);
                        memmove(gm_out, ((uint8_t *)&t1167), 112);
                        return 1;
                    }
gm_lb133:;
                    void * t1168 = F.l0;
                    gm_tl874t2atnlh3 t1169 = F.l5;
                    gm_b16_8 t1170;
                    GM_ST(void *, (uint8_t *)&t1170, (void *)(gm_su1oqg46rlg2d));
                    GM_ST(uintptr_t, (uint8_t *)&t1170 + 8, (uintptr_t)(9));
                    gm_t39uai1ajk3qv t1171;
                    gm_f_unsupported_2seq49l((void *)&t1171, t1168, GM_CONV(gm_cseahvg8v7p0vs, t1169), t1170);
                    gm_ttk92ufj2f6eh t1172;
                    GM_ST(uint8_t, (uint8_t *)&t1172 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1172 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1172 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1172 + 8, 0);
                    ((uint8_t *)&t1172)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1172 + 16), t1171);
                    memmove(gm_out, ((uint8_t *)&t1172), 112);
                    return 1;
                }
gm_lb132:;
            }
gm_lb131:;
            void * t1173 = F.l0;
            gm_tl874t2atnlh3 t1174 = F.l5;
            gm_b16_8 t1175;
            GM_ST(void *, (uint8_t *)&t1175, (void *)(gm_su1oqg46rlg2d));
            GM_ST(uintptr_t, (uint8_t *)&t1175 + 8, (uintptr_t)(9));
            gm_t39uai1ajk3qv t1176;
            gm_f_unsupported_2seq49l((void *)&t1176, t1173, GM_CONV(gm_cseahvg8v7p0vs, t1174), t1175);
            gm_ttk92ufj2f6eh t1177;
            GM_ST(uint8_t, (uint8_t *)&t1177 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1177 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1177 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1177 + 8, 0);
            ((uint8_t *)&t1177)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1177 + 16), t1176);
            memmove(gm_out, ((uint8_t *)&t1177), 112);
            return 1;
        }
gm_lb130:;
    }
gm_lb129:;
    {
        uint8_t t1178 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t1179 = (uint8_t)(t1178 == ((uint8_t)8ULL));
        if (!t1179) {
            goto gm_lb144;
        }
        gm_t6evimt84lndq t1180 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
        gm_ttk92ufj2f6eh t1181;
        memset(&t1181, 0, sizeof t1181);
        ((uint8_t *)&t1181)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1181 + 16), t1180);
        memmove(gm_out, ((uint8_t *)&t1181), 112);
        return 1;
    }
gm_lb144:;
    {
        uint8_t t1182 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t1183 = (uint8_t)(t1182 == ((uint8_t)9ULL));
        if (!t1183) {
            goto gm_lb145;
        }
        {
            {
                uint8_t t1184 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t1185 = (uint8_t)(t1184 == ((uint8_t)7ULL));
                if (!t1185) {
                    goto gm_lb147;
                }
                void * t1186 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
                F.l86 = t1186;
                void * t1187 = F.l86;
                memmove(((uint8_t *)&F.l180), ((uint8_t *)t1187), 48);
                goto gm_lb146;
            }
gm_lb147:;
            memmove(((uint8_t *)&F.l180), ((uint8_t *)&F.l10), 48);
        }
gm_lb146:;
        memmove(((uint8_t *)&F.l87), ((uint8_t *)&F.l180), 48);
        {
            {
                uint8_t t1188 = GM_LD(uint8_t, (((uint8_t *)&F.l87) + 0));
                uint8_t t1189 = (uint8_t)(t1188 == ((uint8_t)4ULL));
                if (!t1189) {
                    goto gm_lb149;
                }
                memmove(((uint8_t *)&F.l88), (((uint8_t *)&F.l87) + 16), 24);
                F.l89 = ((uint64_t)1ULL);
                void * t1190 = F.l4;
                gm_t88rcucg1b7uk t1191 = F.l11;
                gm_f_kind_jcnt4hm((void *)&F.l181, t1190, GM_CONV(gm_csguankko4lmhd, t1191));
                {
                    {
                        {
                            {
                                uint8_t t1192 = GM_LD(uint8_t, (((uint8_t *)&F.l181) + 0));
                                uint8_t t1193 = (uint8_t)(t1192 == ((uint8_t)8ULL));
                                if (!t1193) {
                                    goto gm_lb153;
                                }
                                gm_t88rcucg1b7uk t1194 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l181) + 8) + 4));
                                F.l90 = t1194;
                                goto gm_lb152;
                            }
gm_lb153:;
                            {
                                uint8_t t1195 = GM_LD(uint8_t, (((uint8_t *)&F.l181) + 0));
                                uint8_t t1196 = (uint8_t)(t1195 == ((uint8_t)9ULL));
                                if (!t1196) {
                                    goto gm_lb154;
                                }
                                gm_t88rcucg1b7uk t1197 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l181) + 8) + 4));
                                F.l90 = t1197;
                                goto gm_lb152;
                            }
gm_lb154:;
                            goto gm_lb151;
                        }
gm_lb152:;
                        void * t1198 = F.l0;
                        void * t1199 = GM_LD(void *, ((uint8_t *)t1198));
                        gm_t88rcucg1b7uk t1200 = F.l90;
                        gm_f_layout_a5ovnrm((void *)&F.l182, (void *)NULL, cx, NULL, t1199, t1200);
                        if (gm_unw.kind) {
                            return 2;
                        }
                        uint64_t t1201 = GM_LD(uint64_t, ((uint8_t *)&F.l182));
                        uint64_t t1202 = (t1201 > ((uint64_t)1ULL) ? t1201 : ((uint64_t)1ULL));
                        F.l89 = t1202;
                        goto gm_lb150;
                    }
gm_lb151:;
                    {
                        goto gm_lb150;
                    }
gm_lb155:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 576, 13);
                }
gm_lb150:;
                uint64_t t1203 = GM_LD(uint64_t, ((((uint8_t *)&F.l88) + 8) + 8));
                uint8_t t1204 = (t1203 > ((uint64_t)0ULL));
                if (t1204) {
                    void * t1205 = GM_LD(void *, (((uint8_t *)&F.l88) + 8));
                    uint64_t t1206 = GM_LD(uint64_t, ((((uint8_t *)&F.l88) + 8) + 8));
                    uint64_t t1207 = GM_LD(uint64_t, ((((uint8_t *)&F.l88) + 8) + 8));
                    uint64_t r1208; bool o1209 = __builtin_sub_overflow(t1207, ((uint64_t)1ULL), &r1208);
                    if (o1209) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 580, 55);
                    uint64_t t1210 = r1208;
                    if ((uintptr_t)t1210 >= (uintptr_t)(t1206)) gm_trap_index((uintptr_t)t1210, t1206, gm_srcue4ionbe9lgh, 580, 47);
                    uint64_t t1211 = GM_LD(uint64_t, (((uint8_t *)t1205) + (uintptr_t)t1210 * 8));
                    F.l183 = t1211;
                } else {
                    F.l183 = ((uint64_t)0ULL);
                }
                uint64_t t1212 = F.l183;
                F.l92 = t1212;
                void * t1213 = GM_LD(void *, ((uint8_t *)&F.l88));
                void * t1214 = t1213;
                uint64_t t1215 = (uintptr_t)t1214;
                uint64_t t1216 = (uint64_t)t1215;
                unsigned __int128 n1217 = (unsigned __int128)((uint64_t)12ULL);
                if (n1217 >= 64) gm_trap_shift((uint64_t)n1217, "u64", gm_srcue4ionbe9lgh, 581, 21);
                uint64_t r1218 = (uint64_t)((uint64_t)t1216 << (int)n1217);
                bool o1219 = (uint64_t)(r1218 >> (int)n1217) != t1216;
                if (o1219) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 581, 21);
                uint64_t t1220 = r1218;
                F.l93 = t1220;
                void * t1221 = F.l0;
                uint64_t t1222 = F.l93;
                uint64_t t1223 = F.l92;
                uint64_t t1224 = F.l89;
                uint64_t r1225; bool o1226 = __builtin_mul_overflow(t1223, t1224, &r1225);
                uint64_t t1227 = r1225;
                uint64_t r1228; bool o1229 = __builtin_add_overflow(t1222, t1227, &r1228);
                uint64_t t1230 = r1228;
                unsigned __int128 t1231 = (unsigned __int128)t1230;
                void * t1232 = F.l3;
                gm_t88rcucg1b7uk t1233 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1232));
                unsigned __int128 t1234 = gm_f_wrap_bt8r149(t1221, t1231, GM_CONV(gm_csguankko4lmhd, t1233));
                gm_t6evimt84lndq t1235;
                GM_ST(uint8_t, (uint8_t *)&t1235 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1235 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1235 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1235 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1235 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1235 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1235 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1235 + 16), t1234);
                gm_ttk92ufj2f6eh t1236;
                memset(&t1236, 0, sizeof t1236);
                ((uint8_t *)&t1236)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1236 + 16), t1235);
                memmove(gm_out, ((uint8_t *)&t1236), 112);
                return 1;
            }
gm_lb149:;
            gm_t6evimt84lndq t1237;
            GM_ST(uint8_t, (uint8_t *)&t1237 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1237 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1237 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1237 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t1237 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t1237 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t1237 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t1237 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
            gm_ttk92ufj2f6eh t1238;
            memset(&t1238, 0, sizeof t1238);
            ((uint8_t *)&t1238)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1238 + 16), t1237);
            memmove(gm_out, ((uint8_t *)&t1238), 112);
            return 1;
        }
gm_lb148:;
    }
gm_lb145:;
    {
        uint8_t t1239 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t1240 = (uint8_t)(t1239 == ((uint8_t)13ULL));
        if (!t1240) {
            goto gm_lb156;
        }
        {
            {
                uint8_t t1241 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t1242 = (uint8_t)(t1241 == ((uint8_t)0ULL));
                if (!t1242) {
                    goto gm_lb158;
                }
                unsigned __int128 t1243 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l94 = t1243;
                unsigned __int128 t1244 = F.l94;
                uint8_t t1245 = ((t1244 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                if (t1245) {
                    gm_t6evimt84lndq t1246;
                    memset(&t1246, 0, sizeof t1246);
                    GM_ST(uint8_t, ((uint8_t *)&t1246 + 0), ((uint8_t)6ULL));
                    gm_ttk92ufj2f6eh t1247;
                    memset(&t1247, 0, sizeof t1247);
                    ((uint8_t *)&t1247)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1247 + 16), t1246);
                    memmove(gm_out, ((uint8_t *)&t1247), 112);
                    return 1;
                }
                gm_trqfvmjtt4sij t1248;
                gm_f_list_kl0t9m0((void *)&t1248, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    return 2;
                }
                F.l95 = t1248;
                void * t1249 = (void *)((uint8_t *)&F.l95);
                gm_tftb1odc739t5 t1250 = gm_f_push_7o6oubd(t1249, ((uint64_t)0ULL));
                F.l184 = t1250;
                uint8_t t1251 = (uint8_t)!((((uint8_t *)&F.l184))[0] != 0);
                if (!t1251) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l184) + 1, gm_srcue4ionbe9lgh, 590, 13);
                gm_t6evimt84lndq t1252;
                gm_f_unit_value_evae6mr((void *)&t1252, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    return 2;
                }
                gm_tjoi8o798ne9k t1253;
                GM_ST(uint8_t, (uint8_t *)&t1253 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t1253 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t1253 + 52, 0);
                GM_ST(uint64_t, (uint8_t *)&t1253 + 56, 0);
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1253 + 0), t1252);
                GM_ST(uint8_t, ((uint8_t *)&t1253 + 48), ((uint8_t)1));
                gm_tq7s98ihonkju t1254 = gm_f_new_15vqrvd((void *)NULL, cx, NULL, &t1253, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdmnul2f0o4)));
                if (gm_unw.kind) {
                    return 2;
                }
                F.l185 = t1254;
                uint8_t t1255 = (uint8_t)!((((uint8_t *)&F.l185))[0] != 0);
                if (!t1255) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l185) + 8, gm_srcue4ionbe9lgh, 591, 52);
                void * t1256 = GM_LD(void *, (((uint8_t *)&F.l185) + 8));
                gm_b16_8 t1257 = GM_LD(gm_b16_8, ((uint8_t *)&F.l95));
                gm_b16_8 t1258 = GM_LD(gm_b16_8, ((uint8_t *)&t1257));
                gm_tkaapcii86udk t1259;
                GM_ST(void *, ((uint8_t *)&t1259 + 0), t1256);
                GM_ST(gm_b16_8, ((uint8_t *)&t1259 + 8), t1258);
                gm_t6evimt84lndq t1260;
                GM_ST(uint8_t, (uint8_t *)&t1260 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1260 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1260 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1260 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1260 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1260 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1260 + 16), t1259);
                gm_tq7s98ihonkju t1261 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1260, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klr1ounjj8d)));
                if (gm_unw.kind) {
                    return 2;
                }
                F.l186 = t1261;
                uint8_t t1262 = (uint8_t)!((((uint8_t *)&F.l186))[0] != 0);
                if (!t1262) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l186) + 8, gm_srcue4ionbe9lgh, 591, 26);
                void * t1263 = GM_LD(void *, (((uint8_t *)&F.l186) + 8));
                gm_t6evimt84lndq t1264;
                memset(&t1264, 0, sizeof t1264);
                GM_ST(uint8_t, ((uint8_t *)&t1264 + 0), ((uint8_t)7ULL));
                GM_ST(void *, ((uint8_t *)&t1264 + 16), t1263);
                gm_ttk92ufj2f6eh t1265;
                memset(&t1265, 0, sizeof t1265);
                ((uint8_t *)&t1265)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1265 + 16), t1264);
                memmove(gm_out, ((uint8_t *)&t1265), 112);
                return 1;
            }
gm_lb158:;
            void * t1266 = F.l0;
            gm_tl874t2atnlh3 t1267 = F.l5;
            gm_b16_8 t1268;
            GM_ST(void *, (uint8_t *)&t1268, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t1268 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t1269;
            gm_f_unsupported_2seq49l((void *)&t1269, t1266, GM_CONV(gm_cseahvg8v7p0vs, t1267), t1268);
            gm_ttk92ufj2f6eh t1270;
            GM_ST(uint8_t, (uint8_t *)&t1270 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1270 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1270 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1270 + 8, 0);
            ((uint8_t *)&t1270)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1270 + 16), t1269);
            memmove(gm_out, ((uint8_t *)&t1270), 112);
            return 1;
        }
gm_lb157:;
    }
gm_lb156:;
    {
        uint8_t t1271 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t1272 = (uint8_t)(t1271 == ((uint8_t)7ULL));
        if (!t1272) {
            goto gm_lb159;
        }
        void * t1273 = F.l0;
        gm_t88rcucg1b7uk t1274 = F.l11;
        void * t1275 = F.l3;
        gm_t88rcucg1b7uk t1276 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1275));
        gm_tl874t2atnlh3 t1277 = F.l5;
        gm_t6evimt84lndq t1278 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
        gm_f_reinterpret_jmr6jts((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t1273, &t1278, t1274, t1276, t1277);
        if (gm_unw.kind) {
            return 2;
        }
        return 1;
    }
gm_lb159:;
    {
        uint8_t t1279 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t1280 = (uint8_t)(t1279 == ((uint8_t)26ULL));
        if (!t1280) {
            goto gm_lb160;
        }
        {
            {
                uint8_t t1281 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t1282 = (uint8_t)(t1281 == ((uint8_t)1ULL));
                if (!t1282) {
                    goto gm_lb162;
                }
                double t1283 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l96 = t1283;
                void * t1284 = F.l0;
                double t1285 = F.l96;
                double t1286 = (double)(__builtin_sqrt(t1285));
                void * t1287 = F.l3;
                gm_t88rcucg1b7uk t1288 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1287));
                gm_f_round_float_saju9fu((void *)&F.l187, (void *)NULL, cx, NULL, t1284, t1286, t1288);
                if (gm_unw.kind) {
                    return 2;
                }
                uint8_t t1289 = (uint8_t)!((((uint8_t *)&F.l187))[0] != 0);
                if (t1289) {
                } else {
                    gm_t39uai1ajk3qv t1290 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l187) + 16));
                    gm_ttk92ufj2f6eh t1291;
                    GM_ST(uint8_t, (uint8_t *)&t1291 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1291 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1291 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1291 + 8, 0);
                    ((uint8_t *)&t1291)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1291 + 16), t1290);
                    memmove(gm_out, ((uint8_t *)&t1291), 112);
                    return 1;
                }
                double t1292 = GM_LD(double, (((uint8_t *)&F.l187) + 16));
                gm_t6evimt84lndq t1293;
                memset(&t1293, 0, sizeof t1293);
                GM_ST(uint8_t, ((uint8_t *)&t1293 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t1293 + 16), t1292);
                gm_ttk92ufj2f6eh t1294;
                memset(&t1294, 0, sizeof t1294);
                ((uint8_t *)&t1294)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1294 + 16), t1293);
                memmove(gm_out, ((uint8_t *)&t1294), 112);
                return 1;
            }
gm_lb162:;
            void * t1295 = F.l0;
            gm_tl874t2atnlh3 t1296 = F.l5;
            gm_b16_8 t1297;
            GM_ST(void *, (uint8_t *)&t1297, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t1297 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t1298;
            gm_f_unsupported_2seq49l((void *)&t1298, t1295, GM_CONV(gm_cseahvg8v7p0vs, t1296), t1297);
            gm_ttk92ufj2f6eh t1299;
            GM_ST(uint8_t, (uint8_t *)&t1299 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1299 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1299 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1299 + 8, 0);
            ((uint8_t *)&t1299)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1299 + 16), t1298);
            memmove(gm_out, ((uint8_t *)&t1299), 112);
            return 1;
        }
gm_lb161:;
    }
gm_lb160:;
    {
        {
            {
                {
                    uint8_t t1300 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1301 = (uint8_t)(t1300 == ((uint8_t)28ULL));
                    if (!t1301) {
                        goto gm_lb165;
                    }
                    goto gm_lb164;
                }
gm_lb165:;
                {
                    uint8_t t1302 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1303 = (uint8_t)(t1302 == ((uint8_t)29ULL));
                    if (!t1303) {
                        goto gm_lb166;
                    }
                    goto gm_lb164;
                }
gm_lb166:;
                {
                    uint8_t t1304 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1305 = (uint8_t)(t1304 == ((uint8_t)30ULL));
                    if (!t1305) {
                        goto gm_lb167;
                    }
                    goto gm_lb164;
                }
gm_lb167:;
                {
                    uint8_t t1306 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1307 = (uint8_t)(t1306 == ((uint8_t)31ULL));
                    if (!t1307) {
                        goto gm_lb168;
                    }
                    goto gm_lb164;
                }
gm_lb168:;
                {
                    uint8_t t1308 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1309 = (uint8_t)(t1308 == ((uint8_t)34ULL));
                    if (!t1309) {
                        goto gm_lb169;
                    }
                    goto gm_lb164;
                }
gm_lb169:;
                {
                    uint8_t t1310 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1311 = (uint8_t)(t1310 == ((uint8_t)32ULL));
                    if (!t1311) {
                        goto gm_lb170;
                    }
                    goto gm_lb164;
                }
gm_lb170:;
                {
                    uint8_t t1312 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1313 = (uint8_t)(t1312 == ((uint8_t)33ULL));
                    if (!t1313) {
                        goto gm_lb171;
                    }
                    goto gm_lb164;
                }
gm_lb171:;
                goto gm_lb163;
            }
gm_lb164:;
            {
                {
                    {
                        {
                            uint8_t t1314 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                            uint8_t t1315 = (uint8_t)(t1314 == ((uint8_t)0ULL));
                            if (!t1315) {
                                goto gm_lb173;
                            }
                            unsigned __int128 t1316 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                            F.l97 = t1316;
                            void * t1317 = F.l0;
                            gm_t88rcucg1b7uk t1318 = F.l11;
                            gm_ttovhfee8mf28 t1319 = gm_f_int_info_2r9qb5n(t1317, GM_CONV(gm_csguankko4lmhd, t1318));
                            F.l98 = t1319;
                            uint32_t t1320 = GM_LD(uint32_t, (((uint8_t *)&F.l98) + 4));
                            F.l99 = t1320;
                            unsigned __int128 t1321 = F.l97;
                            uint32_t t1322 = F.l99;
                            unsigned __int128 t1323 = gm_f_mask_gbhl4hp(t1322);
                            unsigned __int128 t1324 = (unsigned __int128)(t1321 & t1323);
                            F.l100 = t1324;
                            gm_closure t1325 = ((gm_closure){(void *)gm_f_bit_s517clu, (void *)&F.h, cx});
                            F.l101 = t1325;
                            F.l102 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                            {
                                {
                                    {
                                        uint8_t t1326 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                                        uint8_t t1327 = (uint8_t)(t1326 == ((uint8_t)28ULL));
                                        if (!t1327) {
                                            goto gm_lb175;
                                        }
                                        F.l103 = ((uint32_t)0ULL);
                                        {
                                            for (;;) {
                                                uint32_t t1329 = F.l103;
                                                uint32_t t1330 = F.l99;
                                                uint8_t t1331 = (t1329 < t1330);
                                                if (!t1331) {
                                                    goto gm_lb176;
                                                }
                                                gm_closure t1332 = F.l101;
                                                uint32_t t1333 = F.l99;
                                                uint32_t r1334; bool o1335 = __builtin_sub_overflow(t1333, ((uint32_t)1ULL), &r1334);
                                                if (o1335) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 608, 45);
                                                uint32_t t1336 = r1334;
                                                uint32_t t1337 = F.l103;
                                                uint32_t r1338; bool o1339 = __builtin_sub_overflow(t1336, t1337, &r1338);
                                                if (o1339) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 608, 45);
                                                uint32_t t1340 = r1338;
                                                gm_closure t1341 = t1332;
                                                gm_node *ccx1342 = t1341.cx;
                                                void * t1343 = t1341.code;
                                                uint8_t t1344;
                                                if (GM_PTR_TAGGED(t1343)) {
                                                    t1344 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1343))(t1340);
                                                } else {
                                                    t1344 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1343)(t1341.env, ccx1342, NULL, t1340);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_ll1328;
                                                }
                                                uint8_t t1345 = (uint8_t)!t1344;
                                                if (!t1345) {
                                                    goto gm_lb176;
                                                }
                                                {
                                                    uint32_t t1346 = F.l103;
                                                    uint32_t r1347; bool o1348 = __builtin_add_overflow(t1346, ((uint32_t)1ULL), &r1347);
                                                    if (o1348) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 608, 58);
                                                    uint32_t t1349 = r1347;
                                                    F.l103 = t1349;
                                                }
gm_lb178:;
                                            }
                                        }
                                        goto gm_ls1350;
gm_ll1328:;
                                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb176; }
                                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb178; }
                                        }
                                        return 2;
gm_ls1350:;
gm_lb176:;
                                        uint32_t t1351 = F.l103;
                                        unsigned __int128 t1352 = (unsigned __int128)t1351;
                                        gm_t6evimt84lndq t1353;
                                        GM_ST(uint8_t, (uint8_t *)&t1353 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t1353 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t1353 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1353 + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1353 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1353 + 40, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t1353 + 0), ((uint8_t)0ULL));
                                        GM_ST(unsigned __int128, ((uint8_t *)&t1353 + 16), t1352);
                                        gm_ttk92ufj2f6eh t1354;
                                        memset(&t1354, 0, sizeof t1354);
                                        ((uint8_t *)&t1354)[0] = 0;
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1354 + 16), t1353);
                                        memmove(gm_out, ((uint8_t *)&t1354), 112);
                                        return 1;
                                    }
gm_lb175:;
                                    {
                                        uint8_t t1355 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                                        uint8_t t1356 = (uint8_t)(t1355 == ((uint8_t)29ULL));
                                        if (!t1356) {
                                            goto gm_lb179;
                                        }
                                        F.l104 = ((uint32_t)0ULL);
                                        {
                                            for (;;) {
                                                uint32_t t1358 = F.l104;
                                                uint32_t t1359 = F.l99;
                                                uint8_t t1360 = (t1358 < t1359);
                                                if (!t1360) {
                                                    goto gm_lb180;
                                                }
                                                gm_closure t1361 = F.l101;
                                                uint32_t t1362 = F.l104;
                                                gm_closure t1363 = t1361;
                                                gm_node *ccx1364 = t1363.cx;
                                                void * t1365 = t1363.code;
                                                uint8_t t1366;
                                                if (GM_PTR_TAGGED(t1365)) {
                                                    t1366 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1365))(t1362);
                                                } else {
                                                    t1366 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1365)(t1363.env, ccx1364, NULL, t1362);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_ll1357;
                                                }
                                                uint8_t t1367 = (uint8_t)!t1366;
                                                if (!t1367) {
                                                    goto gm_lb180;
                                                }
                                                {
                                                    uint32_t t1368 = F.l104;
                                                    uint32_t r1369; bool o1370 = __builtin_add_overflow(t1368, ((uint32_t)1ULL), &r1369);
                                                    if (o1370) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 613, 50);
                                                    uint32_t t1371 = r1369;
                                                    F.l104 = t1371;
                                                }
gm_lb182:;
                                            }
                                        }
                                        goto gm_ls1372;
gm_ll1357:;
                                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb180; }
                                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb182; }
                                        }
                                        return 2;
gm_ls1372:;
gm_lb180:;
                                        uint32_t t1373 = F.l104;
                                        unsigned __int128 t1374 = (unsigned __int128)t1373;
                                        gm_t6evimt84lndq t1375;
                                        GM_ST(uint8_t, (uint8_t *)&t1375 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t1375 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t1375 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1375 + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1375 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1375 + 40, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t1375 + 0), ((uint8_t)0ULL));
                                        GM_ST(unsigned __int128, ((uint8_t *)&t1375 + 16), t1374);
                                        gm_ttk92ufj2f6eh t1376;
                                        memset(&t1376, 0, sizeof t1376);
                                        ((uint8_t *)&t1376)[0] = 0;
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1376 + 16), t1375);
                                        memmove(gm_out, ((uint8_t *)&t1376), 112);
                                        return 1;
                                    }
gm_lb179:;
                                    {
                                        uint8_t t1377 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                                        uint8_t t1378 = (uint8_t)(t1377 == ((uint8_t)30ULL));
                                        if (!t1378) {
                                            goto gm_lb183;
                                        }
                                        F.l105 = ((uint32_t)0ULL);
                                        F.l107 = ((uint32_t)0ULL);
                                        uint32_t t1379 = F.l99;
                                        F.l106 = t1379;
                                        {
                                            for (;;) {
                                                uint32_t t1381 = F.l107;
                                                uint32_t t1382 = F.l106;
                                                uint8_t t1383 = (t1381 < t1382);
                                                if (!t1383) {
                                                    goto gm_lb184;
                                                }
                                                {
                                                    {
                                                        {
                                                            uint32_t t1384 = F.l107;
                                                            F.l108 = t1384;
                                                            gm_closure t1385 = F.l101;
                                                            uint32_t t1386 = F.l108;
                                                            gm_closure t1387 = t1385;
                                                            gm_node *ccx1388 = t1387.cx;
                                                            void * t1389 = t1387.code;
                                                            uint8_t t1390;
                                                            if (GM_PTR_TAGGED(t1389)) {
                                                                t1390 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1389))(t1386);
                                                            } else {
                                                                t1390 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1389)(t1387.env, ccx1388, NULL, t1386);
                                                            }
                                                            if (gm_unw.kind) {
                                                                goto gm_ll1380;
                                                            }
                                                            if (t1390) {
                                                                uint32_t t1391 = F.l105;
                                                                uint32_t r1392; bool o1393 = __builtin_add_overflow(t1391, ((uint32_t)1ULL), &r1392);
                                                                if (o1393) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 619, 37);
                                                                uint32_t t1394 = r1392;
                                                                F.l105 = t1394;
                                                            }
                                                            goto gm_lb187;
                                                        }
gm_lb188:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 618, 21);
                                                    }
gm_lb187:;
                                                }
gm_lb186:;
                                                uint32_t t1395 = F.l107;
                                                uint32_t r1396; bool o1397 = __builtin_add_overflow(t1395, ((uint32_t)1ULL), &r1396);
                                                uint32_t t1398 = r1396;
                                                F.l107 = t1398;
                                            }
                                        }
                                        goto gm_ls1399;
gm_ll1380:;
                                        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb184; }
                                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb186; }
                                        }
                                        return 2;
gm_ls1399:;
gm_lb184:;
                                        uint32_t t1400 = F.l105;
                                        unsigned __int128 t1401 = (unsigned __int128)t1400;
                                        gm_t6evimt84lndq t1402;
                                        GM_ST(uint8_t, (uint8_t *)&t1402 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t1402 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t1402 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1402 + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1402 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1402 + 40, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t1402 + 0), ((uint8_t)0ULL));
                                        GM_ST(unsigned __int128, ((uint8_t *)&t1402 + 16), t1401);
                                        gm_ttk92ufj2f6eh t1403;
                                        memset(&t1403, 0, sizeof t1403);
                                        ((uint8_t *)&t1403)[0] = 0;
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1403 + 16), t1402);
                                        memmove(gm_out, ((uint8_t *)&t1403), 112);
                                        return 1;
                                    }
gm_lb183:;
                                }
                                {
                                    {
                                        uint8_t t1404 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                                        uint8_t t1405 = (uint8_t)(t1404 == ((uint8_t)31ULL));
                                        if (!t1405) {
                                            goto gm_lb189;
                                        }
                                        uint32_t t1406 = F.l99;
                                        if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 624, 24);
                                        uint32_t t1407 = (uint32_t)(t1406 % ((uint32_t)8ULL));
                                        uint8_t t1408 = (!(t1407 == ((uint32_t)0ULL)));
                                        if (t1408) {
                                            void * t1409 = F.l0;
                                            gm_tl874t2atnlh3 t1410 = F.l5;
                                            uint32_t t1411 = F.l99;
                                            gm_t88rcucg1b7uk t1412;
                                            GM_ST(uint32_t, ((uint8_t *)&t1412 + 0), t1411);
                                            gm_f_format_ven7kt5((void *)&F.l188, (void *)NULL, cx, NULL, t1412);
                                            if (gm_unw.kind) {
                                                return 2;
                                            }
                                            uint8_t t1413 = (uint8_t)!((((uint8_t *)&F.l188))[0] != 0);
                                            if (!t1413) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l188) + 8, gm_srcue4ionbe9lgh, 624, 62);
                                            gm_b16_8 t1414 = GM_LD(gm_b16_8, (((uint8_t *)&F.l188) + 8));
                                            gm_b16_8 t1415 = GM_LD(gm_b16_8, ((uint8_t *)&t1414));
                                            gm_t39uai1ajk3qv t1416;
                                            gm_f_trap_jftbss6((void *)&t1416, (void *)NULL, cx, NULL, t1409, t1410, t1415);
                                            if (gm_unw.kind) {
                                                return 2;
                                            }
                                            gm_ttk92ufj2f6eh t1417;
                                            GM_ST(uint8_t, (uint8_t *)&t1417 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t1417 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t1417 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t1417 + 8, 0);
                                            ((uint8_t *)&t1417)[0] = 1;
                                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1417 + 16), t1416);
                                            memmove(gm_out, ((uint8_t *)&t1417), 112);
                                            return 1;
                                        }
                                        F.l110 = ((uint32_t)0ULL);
                                        uint32_t t1418 = F.l99;
                                        if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 625, 33);
                                        uint32_t t1419 = (uint32_t)(t1418 / ((uint32_t)8ULL));
                                        F.l109 = t1419;
                                        {
                                            for (;;) {
                                                uint32_t t1421 = F.l110;
                                                uint32_t t1422 = F.l109;
                                                uint8_t t1423 = (t1421 < t1422);
                                                if (!t1423) {
                                                    goto gm_lb190;
                                                }
                                                {
                                                    {
                                                        {
                                                            uint32_t t1424 = F.l110;
                                                            F.l111 = t1424;
                                                            unsigned __int128 t1425 = F.l102;
                                                            unsigned __int128 t1426 = F.l100;
                                                            uint32_t t1427 = F.l111;
                                                            uint32_t r1428; bool o1429 = __builtin_mul_overflow(t1427, ((uint32_t)8ULL), &r1428);
                                                            if (o1429) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 625, 58);
                                                            uint32_t t1430 = r1428;
                                                            unsigned __int128 t1431 = (unsigned __int128)t1430;
                                                            unsigned __int128 n1432 = (unsigned __int128)t1431;
                                                            if (n1432 >= 128) gm_trap_shift((uint64_t)n1432, "u128", gm_srcue4ionbe9lgh, 625, 48);
                                                            unsigned __int128 t1433 = (unsigned __int128)(t1426 >> (int)n1432);
                                                            unsigned __int128 t1434 = (unsigned __int128)(t1433 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 255ULL)));
                                                            uint32_t t1435 = F.l99;
                                                            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 625, 83);
                                                            uint32_t t1436 = (uint32_t)(t1435 / ((uint32_t)8ULL));
                                                            uint32_t r1437; bool o1438 = __builtin_sub_overflow(t1436, ((uint32_t)1ULL), &r1437);
                                                            if (o1438) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 625, 83);
                                                            uint32_t t1439 = r1437;
                                                            uint32_t t1440 = F.l111;
                                                            uint32_t r1441; bool o1442 = __builtin_sub_overflow(t1439, t1440, &r1441);
                                                            if (o1442) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 625, 83);
                                                            uint32_t t1443 = r1441;
                                                            uint32_t r1444; bool o1445 = __builtin_mul_overflow(t1443, ((uint32_t)8ULL), &r1444);
                                                            if (o1445) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 625, 82);
                                                            uint32_t t1446 = r1444;
                                                            unsigned __int128 t1447 = (unsigned __int128)t1446;
                                                            unsigned __int128 n1448 = (unsigned __int128)t1447;
                                                            if (n1448 >= 128) gm_trap_shift((uint64_t)n1448, "u128", gm_srcue4ionbe9lgh, 625, 46);
                                                            unsigned __int128 r1449 = (unsigned __int128)((unsigned __int128)t1434 << (int)n1448);
                                                            bool o1450 = (unsigned __int128)(r1449 >> (int)n1448) != t1434;
                                                            if (o1450) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 625, 46);
                                                            unsigned __int128 t1451 = r1449;
                                                            unsigned __int128 t1452 = (unsigned __int128)(t1425 | t1451);
                                                            F.l102 = t1452;
                                                            goto gm_lb193;
                                                        }
gm_lb194:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 625, 21);
                                                    }
gm_lb193:;
                                                }
gm_lb192:;
                                                uint32_t t1453 = F.l110;
                                                uint32_t r1454; bool o1455 = __builtin_add_overflow(t1453, ((uint32_t)1ULL), &r1454);
                                                uint32_t t1456 = r1454;
                                                F.l110 = t1456;
                                            }
                                        }
gm_lb190:;
                                        goto gm_lb174;
                                    }
gm_lb189:;
                                    {
                                        uint8_t t1457 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                                        uint8_t t1458 = (uint8_t)(t1457 == ((uint8_t)34ULL));
                                        if (!t1458) {
                                            goto gm_lb195;
                                        }
                                        F.l113 = ((uint32_t)0ULL);
                                        uint32_t t1459 = F.l99;
                                        F.l112 = t1459;
                                        {
                                            for (;;) {
                                                uint32_t t1461 = F.l113;
                                                uint32_t t1462 = F.l112;
                                                uint8_t t1463 = (t1461 < t1462);
                                                if (!t1463) {
                                                    goto gm_lb196;
                                                }
                                                {
                                                    {
                                                        {
                                                            uint32_t t1464 = F.l113;
                                                            F.l114 = t1464;
                                                            gm_closure t1465 = F.l101;
                                                            uint32_t t1466 = F.l114;
                                                            gm_closure t1467 = t1465;
                                                            gm_node *ccx1468 = t1467.cx;
                                                            void * t1469 = t1467.code;
                                                            uint8_t t1470;
                                                            if (GM_PTR_TAGGED(t1469)) {
                                                                t1470 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1469))(t1466);
                                                            } else {
                                                                t1470 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1469)(t1467.env, ccx1468, NULL, t1466);
                                                            }
                                                            if (gm_unw.kind) {
                                                                goto gm_ll1460;
                                                            }
                                                            if (t1470) {
                                                                unsigned __int128 t1471 = F.l102;
                                                                uint32_t t1472 = F.l99;
                                                                uint32_t r1473; bool o1474 = __builtin_sub_overflow(t1472, ((uint32_t)1ULL), &r1473);
                                                                if (o1474) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 628, 54);
                                                                uint32_t t1475 = r1473;
                                                                uint32_t t1476 = F.l114;
                                                                uint32_t r1477; bool o1478 = __builtin_sub_overflow(t1475, t1476, &r1477);
                                                                if (o1478) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 628, 54);
                                                                uint32_t t1479 = r1477;
                                                                unsigned __int128 t1480 = (unsigned __int128)t1479;
                                                                unsigned __int128 n1481 = (unsigned __int128)t1480;
                                                                if (n1481 >= 128) gm_trap_shift((uint64_t)n1481, "u128", gm_srcue4ionbe9lgh, 628, 38);
                                                                unsigned __int128 r1482 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n1481);
                                                                bool o1483 = (unsigned __int128)(r1482 >> (int)n1481) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                                                                if (o1483) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 628, 38);
                                                                unsigned __int128 t1484 = r1482;
                                                                unsigned __int128 t1485 = (unsigned __int128)(t1471 | t1484);
                                                                F.l102 = t1485;
                                                            }
                                                            goto gm_lb199;
                                                        }
gm_lb200:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 627, 32);
                                                    }
gm_lb199:;
                                                }
gm_lb198:;
                                                uint32_t t1486 = F.l113;
                                                uint32_t r1487; bool o1488 = __builtin_add_overflow(t1486, ((uint32_t)1ULL), &r1487);
                                                uint32_t t1489 = r1487;
                                                F.l113 = t1489;
                                            }
                                        }
                                        goto gm_ls1490;
gm_ll1460:;
                                        if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb196; }
                                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb198; }
                                        }
                                        return 2;
gm_ls1490:;
gm_lb196:;
                                        goto gm_lb174;
                                    }
gm_lb195:;
                                }
                                {
                                    {
                                        {
                                            {
                                                void * t1491 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                uint64_t t1492 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1492)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1492, gm_srcue4ionbe9lgh, 631, 42);
                                                uint8_t t1493 = GM_LD(uint8_t, ((((uint8_t *)t1491) + (uintptr_t)((uint64_t)1ULL) * 48) + 0));
                                                uint8_t t1494 = (uint8_t)(t1493 == ((uint8_t)0ULL));
                                                if (!t1494) {
                                                    goto gm_lb203;
                                                }
                                                unsigned __int128 t1495 = GM_LD(unsigned __int128, ((((uint8_t *)t1491) + (uintptr_t)((uint64_t)1ULL) * 48) + 16));
                                                F.l115 = t1495;
                                                void * t1496 = F.l0;
                                                unsigned __int128 t1497 = F.l115;
                                                void * t1498 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                uint64_t t1499 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1499)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1499, gm_srcue4ionbe9lgh, 632, 53);
                                                gm_t88rcucg1b7uk t1500 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1498) + (uintptr_t)((uint64_t)1ULL) * 88));
                                                gm_f_big_of_1cq0qao((void *)&F.l116, (void *)NULL, cx, NULL, t1496, t1497, t1500);
                                                if (gm_unw.kind) {
                                                    return 2;
                                                }
                                                F.l117 = ((uint32_t)0ULL);
                                                uint8_t t1501 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l116)));
                                                uint8_t t1502 = (uint8_t)!t1501;
                                                if (t1502) {
                                                    uint32_t t1503 = F.l99;
                                                    uint64_t t1504 = (uint64_t)t1503;
                                                    gm_tbci5ospk5nnn t1505 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l116));
                                                    gm_tbci5ospk5nnn t1506;
                                                    gm_f_from_u64_ud13q5v((void *)&t1506, (void *)NULL, cx, NULL, t1504);
                                                    if (gm_unw.kind) {
                                                        return 2;
                                                    }
                                                    gm_tbci5ospk5nnn t1507;
                                                    gm_f_rem_ib4l1vl((void *)&t1507, (void *)NULL, cx, NULL, &t1505, &t1506);
                                                    if (gm_unw.kind) {
                                                        return 2;
                                                    }
                                                    gm_tf5srh4mp4b3i t1508 = gm_f_to_u64_m01t1da(&t1507);
                                                    F.l189 = t1508;
                                                    uint8_t t1509 = (uint8_t)((((uint8_t *)&F.l189))[8] != 0);
                                                    if (t1509) {
                                                        uint64_t t1510 = GM_LD(uint64_t, ((uint8_t *)&F.l189));
                                                        F.l190 = t1510;
                                                    } else {
                                                        F.l190 = ((uint64_t)0ULL);
                                                    }
                                                    uint64_t t1511 = F.l190;
                                                    uint32_t t1512 = (uint32_t)t1511;
                                                    F.l117 = t1512;
                                                }
                                                gm_tvdg94bk7aa22 t1513 = F.l1;
                                                uint8_t t1514 = GM_LD(uint8_t, (((uint8_t *)&t1513) + 0));
                                                uint8_t t1515 = (uint8_t)((t1514 == ((uint8_t)33ULL)));
                                                gm_tvdg94bk7aa22 t1516;
                                                GM_ST(uint8_t, (uint8_t *)&t1516 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t1516 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t1516 + 4, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t1516 + 8, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t1516 + 0), ((uint8_t)33ULL));
                                                if (t1515) {
                                                    uint32_t t1517 = F.l99;
                                                    uint32_t t1518 = F.l117;
                                                    uint32_t r1519; bool o1520 = __builtin_sub_overflow(t1517, t1518, &r1519);
                                                    if (o1520) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 635, 43);
                                                    uint32_t t1521 = r1519;
                                                    uint32_t t1522 = F.l99;
                                                    if (t1522 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 635, 42);
                                                    uint32_t t1523 = (uint32_t)(t1521 % t1522);
                                                    F.l117 = t1523;
                                                }
                                                F.l119 = ((uint32_t)0ULL);
                                                uint32_t t1524 = F.l99;
                                                F.l118 = t1524;
                                                {
                                                    for (;;) {
                                                        uint32_t t1526 = F.l119;
                                                        uint32_t t1527 = F.l118;
                                                        uint8_t t1528 = (t1526 < t1527);
                                                        if (!t1528) {
                                                            goto gm_lb204;
                                                        }
                                                        {
                                                            {
                                                                {
                                                                    uint32_t t1529 = F.l119;
                                                                    F.l120 = t1529;
                                                                    gm_closure t1530 = F.l101;
                                                                    uint32_t t1531 = F.l120;
                                                                    gm_closure t1532 = t1530;
                                                                    gm_node *ccx1533 = t1532.cx;
                                                                    void * t1534 = t1532.code;
                                                                    uint8_t t1535;
                                                                    if (GM_PTR_TAGGED(t1534)) {
                                                                        t1535 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1534))(t1531);
                                                                    } else {
                                                                        t1535 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1534)(t1532.env, ccx1533, NULL, t1531);
                                                                    }
                                                                    if (gm_unw.kind) {
                                                                        goto gm_ll1525;
                                                                    }
                                                                    if (t1535) {
                                                                        unsigned __int128 t1536 = F.l102;
                                                                        uint32_t t1537 = F.l120;
                                                                        uint32_t t1538 = F.l117;
                                                                        uint32_t r1539; bool o1540 = __builtin_add_overflow(t1537, t1538, &r1539);
                                                                        if (o1540) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 638, 59);
                                                                        uint32_t t1541 = r1539;
                                                                        uint32_t t1542 = F.l99;
                                                                        if (t1542 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 638, 58);
                                                                        uint32_t t1543 = (uint32_t)(t1541 % t1542);
                                                                        unsigned __int128 t1544 = (unsigned __int128)t1543;
                                                                        unsigned __int128 n1545 = (unsigned __int128)t1544;
                                                                        if (n1545 >= 128) gm_trap_shift((uint64_t)n1545, "u128", gm_srcue4ionbe9lgh, 638, 42);
                                                                        unsigned __int128 r1546 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n1545);
                                                                        bool o1547 = (unsigned __int128)(r1546 >> (int)n1545) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                                                                        if (o1547) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 638, 42);
                                                                        unsigned __int128 t1548 = r1546;
                                                                        unsigned __int128 t1549 = (unsigned __int128)(t1536 | t1548);
                                                                        F.l102 = t1549;
                                                                    }
                                                                    goto gm_lb207;
                                                                }
gm_lb208:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 637, 21);
                                                            }
gm_lb207:;
                                                        }
gm_lb206:;
                                                        uint32_t t1550 = F.l119;
                                                        uint32_t r1551; bool o1552 = __builtin_add_overflow(t1550, ((uint32_t)1ULL), &r1551);
                                                        uint32_t t1553 = r1551;
                                                        F.l119 = t1553;
                                                    }
                                                }
                                                goto gm_ls1554;
gm_ll1525:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb204; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb206; }
                                                }
                                                return 2;
gm_ls1554:;
gm_lb204:;
                                                goto gm_lb202;
                                            }
gm_lb203:;
                                            void * t1555 = F.l0;
                                            gm_tl874t2atnlh3 t1556 = F.l5;
                                            gm_b16_8 t1557;
                                            GM_ST(void *, (uint8_t *)&t1557, (void *)(gm_sv7lsha1e0i8r));
                                            GM_ST(uintptr_t, (uint8_t *)&t1557 + 8, (uintptr_t)(12));
                                            gm_t39uai1ajk3qv t1558;
                                            gm_f_unsupported_2seq49l((void *)&t1558, t1555, GM_CONV(gm_cseahvg8v7p0vs, t1556), t1557);
                                            gm_ttk92ufj2f6eh t1559;
                                            GM_ST(uint8_t, (uint8_t *)&t1559 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t1559 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t1559 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t1559 + 8, 0);
                                            ((uint8_t *)&t1559)[0] = 1;
                                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1559 + 16), t1558);
                                            memmove(gm_out, ((uint8_t *)&t1559), 112);
                                            return 1;
                                        }
gm_lb202:;
                                        goto gm_lb174;
                                    }
gm_lb201:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 605, 13);
                                }
                            }
gm_lb174:;
                        }
                        {
                            void * t1560 = F.l0;
                            unsigned __int128 t1561 = F.l102;
                            gm_t88rcucg1b7uk t1562 = F.l11;
                            unsigned __int128 t1563 = gm_f_wrap_bt8r149(t1560, t1561, GM_CONV(gm_csguankko4lmhd, t1562));
                            gm_t6evimt84lndq t1564;
                            GM_ST(uint8_t, (uint8_t *)&t1564 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1564 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1564 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1564 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1564 + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1564 + 40, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t1564 + 0), ((uint8_t)0ULL));
                            GM_ST(unsigned __int128, ((uint8_t *)&t1564 + 16), t1563);
                            gm_ttk92ufj2f6eh t1565;
                            memset(&t1565, 0, sizeof t1565);
                            ((uint8_t *)&t1565)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1565 + 16), t1564);
                            memmove(gm_out, ((uint8_t *)&t1565), 112);
                            return 1;
                        }
                    }
gm_lb173:;
                }
                {
                    void * t1566 = F.l0;
                    gm_tl874t2atnlh3 t1567 = F.l5;
                    gm_b16_8 t1568;
                    GM_ST(void *, (uint8_t *)&t1568, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t1568 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t1569;
                    gm_f_unsupported_2seq49l((void *)&t1569, t1566, GM_CONV(gm_cseahvg8v7p0vs, t1567), t1568);
                    gm_ttk92ufj2f6eh t1570;
                    GM_ST(uint8_t, (uint8_t *)&t1570 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1570 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1570 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1570 + 8, 0);
                    ((uint8_t *)&t1570)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1570 + 16), t1569);
                    memmove(gm_out, ((uint8_t *)&t1570), 112);
                    return 1;
                }
            }
gm_lb172:;
        }
    }
gm_lb163:;
    return 0;
#undef F
}
GM_NOINLINE int gm_c_builtin_psq6ql3_214(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out) {
#define F (*gm_Fp)
    void * t215 = F.l0;
    void * t216 = GM_LD(void *, ((uint8_t *)t215));
    void * t217 = (void *)(((uint8_t *)t216) + 8);
    F.l4 = t217;
    void * t218 = F.l3;
    gm_tl874t2atnlh3 t219 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t218) + 72));
    F.l5 = t219;
    {
        {
            uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t221 = (uint8_t)(t220 == ((uint8_t)63ULL));
            if (!t221) {
                goto gm_lb2;
            }
            void * t222 = F.l0;
            gm_tl874t2atnlh3 t223 = F.l5;
            gm_b16_8 t224;
            GM_ST(void *, (uint8_t *)&t224, (void *)(gm_sd3b7a2bnmpef));
            GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(24));
            gm_t39uai1ajk3qv t225;
            gm_f_trap_jftbss6((void *)&t225, (void *)NULL, cx, NULL, t222, t223, t224);
            if (gm_unw.kind) {
                return 1;
            }
            gm_ttk92ufj2f6eh t226;
            GM_ST(uint8_t, (uint8_t *)&t226 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t226 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t226 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t226 + 8, 0);
            ((uint8_t *)&t226)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t226 + 16), t225);
            memmove(gm_out, ((uint8_t *)&t226), 112);
            return 2;
        }
gm_lb2:;
        {
            uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t228 = (uint8_t)(t227 == ((uint8_t)61ULL));
            if (!t228) {
                goto gm_lb3;
            }
            {
                {
                    void * t229 = F.l0;
                    void * t230 = GM_LD(void *, ((uint8_t *)&F.l2));
                    uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t231)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t231, gm_srcue4ionbe9lgh, 367, 39);
                    void * t232 = (void *)(((uint8_t *)t230) + (uintptr_t)((uint64_t)0ULL) * 88);
                    gm_f_eval_2vbtp1v((void *)&F.l124, (void *)NULL, cx, NULL, t229, t232);
                    if (gm_unw.kind) {
                        return 1;
                    }
                    uint8_t t233 = (uint8_t)!((((uint8_t *)&F.l124))[0] != 0);
                    if (t233) {
                    } else {
                        gm_t39uai1ajk3qv t234 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l124) + 16));
                        gm_ttk92ufj2f6eh t235;
                        GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t235 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t235 + 8, 0);
                        ((uint8_t *)&t235)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t235 + 16), t234);
                        memmove(gm_out, ((uint8_t *)&t235), 112);
                        return 2;
                    }
                    memmove(((uint8_t *)&F.l125), (((uint8_t *)&F.l124) + 16), 48);
                    uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&F.l125) + 0));
                    uint8_t t237 = (uint8_t)(t236 == ((uint8_t)2ULL));
                    if (!t237) {
                        goto gm_lb5;
                    }
                    uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&F.l125) + 16));
                    F.l6 = t238;
                    void * t239 = F.l0;
                    void * t240 = GM_LD(void *, ((uint8_t *)&F.l2));
                    uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t241)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t241, gm_srcue4ionbe9lgh, 368, 32);
                    void * t242 = (void *)(((uint8_t *)t240) + (uintptr_t)((uint64_t)1ULL) * 88);
                    gm_f_eval_2vbtp1v((void *)&F.l126, (void *)NULL, cx, NULL, t239, t242);
                    if (gm_unw.kind) {
                        return 1;
                    }
                    uint8_t t243 = (uint8_t)!((((uint8_t *)&F.l126))[0] != 0);
                    if (t243) {
                    } else {
                        gm_t39uai1ajk3qv t244 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l126) + 16));
                        gm_ttk92ufj2f6eh t245;
                        GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t245 + 8, 0);
                        ((uint8_t *)&t245)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t245 + 16), t244);
                        memmove(gm_out, ((uint8_t *)&t245), 112);
                        return 2;
                    }
                    memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l126) + 16), 48);
                    uint8_t t246 = F.l6;
                    if (t246) {
                        gm_t6evimt84lndq t247;
                        gm_f_unit_value_evae6mr((void *)&t247, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        gm_ttk92ufj2f6eh t248;
                        memset(&t248, 0, sizeof t248);
                        ((uint8_t *)&t248)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t248 + 16), t247);
                        memmove(gm_out, ((uint8_t *)&t248), 112);
                        return 2;
                    }
                    void * t249 = F.l0;
                    gm_t6evimt84lndq t250 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l7));
                    gm_f_string_of_plna4gu((void *)&F.l127, (void *)NULL, cx, NULL, t249, &t250);
                    if (gm_unw.kind) {
                        return 1;
                    }
                    uint8_t t251 = (uint8_t)((((uint8_t *)&F.l127))[16] != 0);
                    if (t251) {
                        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&F.l127));
                        F.l128 = t252;
                    } else {
                        void * t253 = F.l0;
                        gm_tl874t2atnlh3 t254 = F.l5;
                        gm_b16_8 t255;
                        GM_ST(void *, (uint8_t *)&t255, (void *)(gm_smqhtg33814q9));
                        GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(22));
                        gm_t39uai1ajk3qv t256;
                        gm_f_unsupported_2seq49l((void *)&t256, t253, GM_CONV(gm_cseahvg8v7p0vs, t254), t255);
                        gm_ttk92ufj2f6eh t257;
                        GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t257 + 8, 0);
                        ((uint8_t *)&t257)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t257 + 16), t256);
                        memmove(gm_out, ((uint8_t *)&t257), 112);
                        return 2;
                    }
                    gm_b16_8 t258 = F.l128;
                    F.l8 = t258;
                    void * t259 = F.l0;
                    gm_tl874t2atnlh3 t260 = F.l5;
                    gm_b16_8 t261 = F.l8;
                    gm_b16_8 t262 = GM_LD(gm_b16_8, ((uint8_t *)&t261));
                    gm_b16_8 t263;
                    GM_ST(void *, (uint8_t *)&t263, (void *)(gm_spi8n2atkg603));
                    GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(16));
                    uint8_t t264 = (gm_eq0iqiubfm7e(((uint8_t *)&t262), ((uint8_t *)&t263)));
                    if (t264) {
                        gm_b16_8 t265 = F.l8;
                        gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                        F.l129 = t266;
                    } else {
                        gm_b16_8 t267 = F.l8;
                        gm_tnpifv20irnph t268;
                        GM_ST(gm_b16_8, ((uint8_t *)&t268 + 0), t267);
                        gm_f_format_u0vfg08((void *)&F.l130, (void *)NULL, cx, NULL, t268);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        uint8_t t269 = (uint8_t)!((((uint8_t *)&F.l130))[0] != 0);
                        if (!t269) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l130) + 8, gm_srcue4ionbe9lgh, 371, 80);
                        gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&F.l130) + 8));
                        gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
                        F.l129 = t271;
                    }
                    gm_b16_8 t272 = F.l129;
                    gm_t39uai1ajk3qv t273;
                    gm_f_trap_jftbss6((void *)&t273, (void *)NULL, cx, NULL, t259, t260, t272);
                    if (gm_unw.kind) {
                        return 1;
                    }
                    gm_ttk92ufj2f6eh t274;
                    GM_ST(uint8_t, (uint8_t *)&t274 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t274 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t274 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t274 + 8, 0);
                    ((uint8_t *)&t274)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t274 + 16), t273);
                    memmove(gm_out, ((uint8_t *)&t274), 112);
                    return 2;
                }
gm_lb5:;
                void * t275 = F.l0;
                gm_tl874t2atnlh3 t276 = F.l5;
                gm_b16_8 t277;
                GM_ST(void *, (uint8_t *)&t277, (void *)(gm_s2kubr08sqj72));
                GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(14));
                gm_t39uai1ajk3qv t278;
                gm_f_unsupported_2seq49l((void *)&t278, t275, GM_CONV(gm_cseahvg8v7p0vs, t276), t277);
                gm_ttk92ufj2f6eh t279;
                GM_ST(uint8_t, (uint8_t *)&t279 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t279 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t279 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t279 + 8, 0);
                ((uint8_t *)&t279)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t279 + 16), t278);
                memmove(gm_out, ((uint8_t *)&t279), 112);
                return 2;
            }
gm_lb4:;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 364, 5);
    }
gm_lb1:;
    void * t280 = F.l0;
    gm_b16_8 t281 = F.l2;
    gm_f_args_6f16vj1((void *)&F.l131, (void *)NULL, cx, NULL, t280, t281);
    if (gm_unw.kind) {
        return 1;
    }
    uint8_t t282 = (uint8_t)!((((uint8_t *)&F.l131))[0] != 0);
    if (t282) {
    } else {
        gm_t39uai1ajk3qv t283 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l131) + 16));
        gm_ttk92ufj2f6eh t284;
        GM_ST(uint8_t, (uint8_t *)&t284 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t284 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t284 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t284 + 8, 0);
        ((uint8_t *)&t284)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t284 + 16), t283);
        memmove(gm_out, ((uint8_t *)&t284), 112);
        return 2;
    }
    gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&F.l131) + 16));
    F.l9 = t285;
    uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
    uint8_t t287 = (t286 > ((uint64_t)0ULL));
    if (t287) {
        void * t288 = GM_LD(void *, ((uint8_t *)&F.l9));
        uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
        memmove(((uint8_t *)&F.l132), (((uint8_t *)t288) + (uintptr_t)((uint64_t)0ULL) * 48), 48);
    } else {
        gm_t6evimt84lndq t290;
        memset(&t290, 0, sizeof t290);
        GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)6ULL));
        F.l132 = t290;
    }
    memmove(((uint8_t *)&F.l10), ((uint8_t *)&F.l132), 48);
    uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint8_t t292 = (t291 > ((uint64_t)0ULL));
    if (t292) {
        void * t293 = GM_LD(void *, ((uint8_t *)&F.l2));
        uint64_t t294 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        gm_t88rcucg1b7uk t295 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t293) + (uintptr_t)((uint64_t)0ULL) * 88));
        F.l133 = t295;
    } else {
        void * t296 = F.l3;
        gm_t88rcucg1b7uk t297 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t296));
        F.l133 = t297;
    }
    gm_t88rcucg1b7uk t298 = F.l133;
    F.l11 = t298;
    return 0;
#undef F
}
GM_NOINLINE int gm_c_builtin_psq6ql3_299(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out) {
#define F (*gm_Fp)
    {
        uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t301 = (uint8_t)(t300 == ((uint8_t)0ULL));
        if (!t301) {
            goto gm_lb8;
        }
        {
            {
                uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t303 = (uint8_t)(t302 == ((uint8_t)0ULL));
                if (!t303) {
                    goto gm_lb10;
                }
                unsigned __int128 t304 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l12 = t304;
                void * t305 = F.l4;
                void * t306 = F.l3;
                gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t306));
                gm_f_kind_jcnt4hm((void *)&F.l134, t305, GM_CONV(gm_csguankko4lmhd, t307));
                {
                    {
                        uint8_t t308 = GM_LD(uint8_t, (((uint8_t *)&F.l134) + 0));
                        uint8_t t309 = (uint8_t)(t308 == ((uint8_t)3ULL));
                        if (!t309) {
                            goto gm_lb12;
                        }
                        void * t310 = F.l0;
                        void * t311 = F.l0;
                        void * t312 = F.l0;
                        unsigned __int128 t313 = F.l12;
                        gm_t88rcucg1b7uk t314 = F.l11;
                        gm_tbci5ospk5nnn t315;
                        gm_f_big_of_1cq0qao((void *)&t315, (void *)NULL, cx, NULL, t312, t313, t314);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        double t316 = gm_f_big_float_jmiufgl((void *)NULL, cx, NULL, t311, &t315);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        void * t317 = F.l3;
                        gm_t88rcucg1b7uk t318 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t317));
                        gm_f_round_float_saju9fu((void *)&F.l135, (void *)NULL, cx, NULL, t310, t316, t318);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        uint8_t t319 = (uint8_t)!((((uint8_t *)&F.l135))[0] != 0);
                        if (t319) {
                        } else {
                            gm_t39uai1ajk3qv t320 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l135) + 16));
                            gm_ttk92ufj2f6eh t321;
                            GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t321 + 8, 0);
                            ((uint8_t *)&t321)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t321 + 16), t320);
                            memmove(gm_out, ((uint8_t *)&t321), 112);
                            return 2;
                        }
                        double t322 = GM_LD(double, (((uint8_t *)&F.l135) + 16));
                        gm_t6evimt84lndq t323;
                        memset(&t323, 0, sizeof t323);
                        GM_ST(uint8_t, ((uint8_t *)&t323 + 0), ((uint8_t)1ULL));
                        GM_ST(double, ((uint8_t *)&t323 + 16), t322);
                        gm_ttk92ufj2f6eh t324;
                        memset(&t324, 0, sizeof t324);
                        ((uint8_t *)&t324)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t324 + 16), t323);
                        memmove(gm_out, ((uint8_t *)&t324), 112);
                        return 2;
                    }
gm_lb12:;
                    {
                        {
                            {
                                uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&F.l134) + 0));
                                uint8_t t326 = (uint8_t)(t325 == ((uint8_t)0ULL));
                                if (!t326) {
                                    goto gm_lb15;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                            {
                                uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&F.l134) + 0));
                                uint8_t t328 = (uint8_t)(t327 == ((uint8_t)1ULL));
                                if (!t328) {
                                    goto gm_lb16;
                                }
                                goto gm_lb14;
                            }
gm_lb16:;
                            {
                                uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&F.l134) + 0));
                                uint8_t t330 = (uint8_t)(t329 == ((uint8_t)2ULL));
                                if (!t330) {
                                    goto gm_lb17;
                                }
                                goto gm_lb14;
                            }
gm_lb17:;
                            goto gm_lb13;
                        }
gm_lb14:;
                        void * t331 = F.l0;
                        void * t332 = F.l0;
                        unsigned __int128 t333 = F.l12;
                        gm_t88rcucg1b7uk t334 = F.l11;
                        __int128 t335 = gm_f_int_value_6hmh8tj(t332, t333, GM_CONV(gm_csguankko4lmhd, t334));
                        unsigned __int128 t336;
                        memset(&t336, 0, sizeof t336); memcpy(&t336, ((uint8_t *)&t335), sizeof t336 < 16 ? sizeof t336 : 16);
                        void * t337 = F.l3;
                        gm_t88rcucg1b7uk t338 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t337));
                        unsigned __int128 t339 = gm_f_wrap_bt8r149(t331, t336, GM_CONV(gm_csguankko4lmhd, t338));
                        gm_t6evimt84lndq t340;
                        GM_ST(uint8_t, (uint8_t *)&t340 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t340 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t340 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t340 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t340 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t340 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t340 + 16), t339);
                        gm_ttk92ufj2f6eh t341;
                        memset(&t341, 0, sizeof t341);
                        ((uint8_t *)&t341)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t341 + 16), t340);
                        memmove(gm_out, ((uint8_t *)&t341), 112);
                        return 2;
                    }
gm_lb13:;
                    {
                        void * t342 = F.l0;
                        gm_tl874t2atnlh3 t343 = F.l5;
                        gm_b16_8 t344;
                        GM_ST(void *, (uint8_t *)&t344, (void *)(gm_smr3su985na96));
                        GM_ST(uintptr_t, (uint8_t *)&t344 + 8, (uintptr_t)(15));
                        gm_t39uai1ajk3qv t345;
                        gm_f_unsupported_2seq49l((void *)&t345, t342, GM_CONV(gm_cseahvg8v7p0vs, t343), t344);
                        gm_ttk92ufj2f6eh t346;
                        GM_ST(uint8_t, (uint8_t *)&t346 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t346 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t346 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t346 + 8, 0);
                        ((uint8_t *)&t346)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t346 + 16), t345);
                        memmove(gm_out, ((uint8_t *)&t346), 112);
                        return 2;
                    }
gm_lb18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 380, 27);
                }
gm_lb11:;
            }
gm_lb10:;
            {
                uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t348 = (uint8_t)(t347 == ((uint8_t)2ULL));
                if (!t348) {
                    goto gm_lb19;
                }
                uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 16));
                F.l13 = t349;
                uint8_t t350 = F.l13;
                if (t350) {
                    F.l136 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                } else {
                    F.l136 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                }
                unsigned __int128 t351 = F.l136;
                gm_t6evimt84lndq t352;
                GM_ST(uint8_t, (uint8_t *)&t352 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t352 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t352 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t352 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t352 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t352 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t352 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t352 + 16), t351);
                gm_ttk92ufj2f6eh t353;
                memset(&t353, 0, sizeof t353);
                ((uint8_t *)&t353)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t353 + 16), t352);
                memmove(gm_out, ((uint8_t *)&t353), 112);
                return 2;
            }
gm_lb19:;
            {
                uint8_t t354 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t355 = (uint8_t)(t354 == ((uint8_t)1ULL));
                if (!t355) {
                    goto gm_lb20;
                }
                double t356 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l14 = t356;
                void * t357 = F.l0;
                double t358 = F.l14;
                void * t359 = F.l3;
                gm_t88rcucg1b7uk t360 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t359));
                gm_f_round_float_saju9fu((void *)&F.l137, (void *)NULL, cx, NULL, t357, t358, t360);
                if (gm_unw.kind) {
                    return 1;
                }
                uint8_t t361 = (uint8_t)!((((uint8_t *)&F.l137))[0] != 0);
                if (t361) {
                } else {
                    gm_t39uai1ajk3qv t362 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l137) + 16));
                    gm_ttk92ufj2f6eh t363;
                    GM_ST(uint8_t, (uint8_t *)&t363 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t363 + 8, 0);
                    ((uint8_t *)&t363)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t363 + 16), t362);
                    memmove(gm_out, ((uint8_t *)&t363), 112);
                    return 2;
                }
                double t364 = GM_LD(double, (((uint8_t *)&F.l137) + 16));
                gm_t6evimt84lndq t365;
                memset(&t365, 0, sizeof t365);
                GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t365 + 16), t364);
                gm_ttk92ufj2f6eh t366;
                memset(&t366, 0, sizeof t366);
                ((uint8_t *)&t366)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t366 + 16), t365);
                memmove(gm_out, ((uint8_t *)&t366), 112);
                return 2;
            }
gm_lb20:;
            {
                uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t368 = (uint8_t)(t367 == ((uint8_t)10ULL));
                if (!t368) {
                    goto gm_lb21;
                }
                uint32_t t369 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 16));
                F.l15 = t369;
                void * t370 = F.l0;
                gm_t88rcucg1b7uk t371 = F.l11;
                uint32_t t372 = F.l15;
                gm_f_variant_tag_4pbamin((void *)&F.l138, t370, GM_CONV(gm_csguankko4lmhd, t371), t372);
                uint8_t t373 = (uint8_t)((((uint8_t *)&F.l138))[48] != 0);
                if (t373) {
                    memmove(((uint8_t *)&F.l139), ((uint8_t *)&F.l138), 48);
                } else {
                    void * t374 = F.l0;
                    gm_tl874t2atnlh3 t375 = F.l5;
                    gm_b16_8 t376;
                    GM_ST(void *, (uint8_t *)&t376, (void *)(gm_smr3su985na96));
                    GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t377;
                    gm_f_unsupported_2seq49l((void *)&t377, t374, GM_CONV(gm_cseahvg8v7p0vs, t375), t376);
                    gm_ttk92ufj2f6eh t378;
                    GM_ST(uint8_t, (uint8_t *)&t378 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t378 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t378 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t378 + 8, 0);
                    ((uint8_t *)&t378)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t378 + 16), t377);
                    memmove(gm_out, ((uint8_t *)&t378), 112);
                    return 2;
                }
                memmove(((uint8_t *)&F.l16), ((uint8_t *)&F.l139), 48);
                void * t379 = F.l0;
                unsigned __int128 t380 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                void * t381 = F.l3;
                gm_t88rcucg1b7uk t382 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t381));
                unsigned __int128 t383 = gm_f_wrap_bt8r149(t379, t380, GM_CONV(gm_csguankko4lmhd, t382));
                gm_t6evimt84lndq t384;
                GM_ST(uint8_t, (uint8_t *)&t384 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t384 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t384 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t384 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t384 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t384 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t384 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t384 + 16), t383);
                gm_ttk92ufj2f6eh t385;
                memset(&t385, 0, sizeof t385);
                ((uint8_t *)&t385)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t385 + 16), t384);
                memmove(gm_out, ((uint8_t *)&t385), 112);
                return 2;
            }
gm_lb21:;
            {
                void * t386 = F.l0;
                gm_tl874t2atnlh3 t387 = F.l5;
                gm_b16_8 t388;
                GM_ST(void *, (uint8_t *)&t388, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t388 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t389;
                gm_f_unsupported_2seq49l((void *)&t389, t386, GM_CONV(gm_cseahvg8v7p0vs, t387), t388);
                gm_ttk92ufj2f6eh t390;
                GM_ST(uint8_t, (uint8_t *)&t390 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t390 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t390 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t390 + 8, 0);
                ((uint8_t *)&t390)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t390 + 16), t389);
                memmove(gm_out, ((uint8_t *)&t390), 112);
                return 2;
            }
gm_lb22:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 379, 21);
        }
gm_lb9:;
    }
gm_lb8:;
    {
        {
            {
                uint8_t t391 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t392 = (uint8_t)(t391 == ((uint8_t)1ULL));
                if (!t392) {
                    goto gm_lb25;
                }
                goto gm_lb24;
            }
gm_lb25:;
            {
                uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t394 = (uint8_t)(t393 == ((uint8_t)2ULL));
                if (!t394) {
                    goto gm_lb26;
                }
                goto gm_lb24;
            }
gm_lb26:;
            {
                uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t396 = (uint8_t)(t395 == ((uint8_t)3ULL));
                if (!t396) {
                    goto gm_lb27;
                }
                goto gm_lb24;
            }
gm_lb27:;
            {
                uint8_t t397 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t398 = (uint8_t)(t397 == ((uint8_t)4ULL));
                if (!t398) {
                    goto gm_lb28;
                }
                goto gm_lb24;
            }
gm_lb28:;
            goto gm_lb23;
        }
gm_lb24:;
        {
            {
                uint8_t t399 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t400 = (uint8_t)(t399 == ((uint8_t)0ULL));
                if (!t400) {
                    goto gm_lb30;
                }
                unsigned __int128 t401 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l17 = t401;
                {
                    {
                        void * t402 = F.l4;
                        void * t403 = F.l3;
                        gm_t88rcucg1b7uk t404 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t403));
                        gm_f_kind_jcnt4hm((void *)&F.l140, t402, GM_CONV(gm_csguankko4lmhd, t404));
                        uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&F.l140) + 0));
                        uint8_t t406 = (uint8_t)(t405 == ((uint8_t)3ULL));
                        if (!t406) {
                            goto gm_lb32;
                        }
                        void * t407 = F.l0;
                        void * t408 = F.l0;
                        void * t409 = F.l0;
                        unsigned __int128 t410 = F.l17;
                        gm_t88rcucg1b7uk t411 = F.l11;
                        gm_tbci5ospk5nnn t412;
                        gm_f_big_of_1cq0qao((void *)&t412, (void *)NULL, cx, NULL, t409, t410, t411);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        double t413 = gm_f_big_float_jmiufgl((void *)NULL, cx, NULL, t408, &t412);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        void * t414 = F.l3;
                        gm_t88rcucg1b7uk t415 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t414));
                        gm_f_round_float_saju9fu((void *)&F.l141, (void *)NULL, cx, NULL, t407, t413, t415);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        uint8_t t416 = (uint8_t)!((((uint8_t *)&F.l141))[0] != 0);
                        if (t416) {
                        } else {
                            gm_t39uai1ajk3qv t417 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l141) + 16));
                            gm_ttk92ufj2f6eh t418;
                            GM_ST(uint8_t, (uint8_t *)&t418 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t418 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t418 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t418 + 8, 0);
                            ((uint8_t *)&t418)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t418 + 16), t417);
                            memmove(gm_out, ((uint8_t *)&t418), 112);
                            return 2;
                        }
                        double t419 = GM_LD(double, (((uint8_t *)&F.l141) + 16));
                        gm_t6evimt84lndq t420;
                        memset(&t420, 0, sizeof t420);
                        GM_ST(uint8_t, ((uint8_t *)&t420 + 0), ((uint8_t)1ULL));
                        GM_ST(double, ((uint8_t *)&t420 + 16), t419);
                        gm_ttk92ufj2f6eh t421;
                        memset(&t421, 0, sizeof t421);
                        ((uint8_t *)&t421)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t421 + 16), t420);
                        memmove(gm_out, ((uint8_t *)&t421), 112);
                        return 2;
                    }
gm_lb32:;
                }
gm_lb31:;
                void * t422 = F.l0;
                unsigned __int128 t423 = F.l17;
                void * t424 = F.l3;
                gm_t88rcucg1b7uk t425 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t424));
                unsigned __int128 t426 = gm_f_wrap_bt8r149(t422, t423, GM_CONV(gm_csguankko4lmhd, t425));
                gm_t6evimt84lndq t427;
                GM_ST(uint8_t, (uint8_t *)&t427 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t427 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t427 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t427 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t427 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t427 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t427 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t427 + 16), t426);
                gm_ttk92ufj2f6eh t428;
                memset(&t428, 0, sizeof t428);
                ((uint8_t *)&t428)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t428 + 16), t427);
                memmove(gm_out, ((uint8_t *)&t428), 112);
                return 2;
            }
gm_lb30:;
            {
                uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t430 = (uint8_t)(t429 == ((uint8_t)1ULL));
                if (!t430) {
                    goto gm_lb33;
                }
                double t431 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l18 = t431;
                {
                    {
                        void * t432 = F.l4;
                        void * t433 = F.l3;
                        gm_t88rcucg1b7uk t434 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t433));
                        gm_f_kind_jcnt4hm((void *)&F.l142, t432, GM_CONV(gm_csguankko4lmhd, t434));
                        uint8_t t435 = GM_LD(uint8_t, (((uint8_t *)&F.l142) + 0));
                        uint8_t t436 = (uint8_t)(t435 == ((uint8_t)3ULL));
                        if (!t436) {
                            goto gm_lb35;
                        }
                        void * t437 = F.l0;
                        double t438 = F.l18;
                        void * t439 = F.l3;
                        gm_t88rcucg1b7uk t440 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t439));
                        gm_f_round_float_saju9fu((void *)&F.l143, (void *)NULL, cx, NULL, t437, t438, t440);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        uint8_t t441 = (uint8_t)!((((uint8_t *)&F.l143))[0] != 0);
                        if (t441) {
                        } else {
                            gm_t39uai1ajk3qv t442 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l143) + 16));
                            gm_ttk92ufj2f6eh t443;
                            GM_ST(uint8_t, (uint8_t *)&t443 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t443 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t443 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t443 + 8, 0);
                            ((uint8_t *)&t443)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t443 + 16), t442);
                            memmove(gm_out, ((uint8_t *)&t443), 112);
                            return 2;
                        }
                        double t444 = GM_LD(double, (((uint8_t *)&F.l143) + 16));
                        gm_t6evimt84lndq t445;
                        memset(&t445, 0, sizeof t445);
                        GM_ST(uint8_t, ((uint8_t *)&t445 + 0), ((uint8_t)1ULL));
                        GM_ST(double, ((uint8_t *)&t445 + 16), t444);
                        gm_ttk92ufj2f6eh t446;
                        memset(&t446, 0, sizeof t446);
                        ((uint8_t *)&t446)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t446 + 16), t445);
                        memmove(gm_out, ((uint8_t *)&t446), 112);
                        return 2;
                    }
gm_lb35:;
                }
gm_lb34:;
                void * t447 = F.l0;
                double t448 = F.l18;
                void * t449 = F.l3;
                gm_t88rcucg1b7uk t450 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t449));
                gm_tvdg94bk7aa22 t451 = F.l1;
                gm_f_float_to_int_sf4dap7((void *)&F.l144, (void *)NULL, cx, NULL, t447, t448, t450, t451);
                if (gm_unw.kind) {
                    return 1;
                }
                uint8_t t452 = (uint8_t)((((uint8_t *)&F.l144))[48] != 0);
                if (t452) {
                    memmove(((uint8_t *)&F.l145), ((uint8_t *)&F.l144), 48);
                } else {
                    void * t453 = F.l0;
                    gm_tl874t2atnlh3 t454 = F.l5;
                    gm_b16_8 t455;
                    GM_ST(void *, (uint8_t *)&t455, (void *)(gm_s9o2c36nosv54));
                    GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(40));
                    gm_t39uai1ajk3qv t456;
                    gm_f_trap_jftbss6((void *)&t456, (void *)NULL, cx, NULL, t453, t454, t455);
                    if (gm_unw.kind) {
                        return 1;
                    }
                    gm_ttk92ufj2f6eh t457;
                    GM_ST(uint8_t, (uint8_t *)&t457 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t457 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t457 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t457 + 8, 0);
                    ((uint8_t *)&t457)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t457 + 16), t456);
                    memmove(gm_out, ((uint8_t *)&t457), 112);
                    return 2;
                }
                memmove(((uint8_t *)&F.l19), ((uint8_t *)&F.l145), 48);
                void * t458 = F.l0;
                unsigned __int128 t459 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)));
                void * t460 = F.l3;
                gm_t88rcucg1b7uk t461 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t460));
                unsigned __int128 t462 = gm_f_wrap_bt8r149(t458, t459, GM_CONV(gm_csguankko4lmhd, t461));
                gm_t6evimt84lndq t463;
                GM_ST(uint8_t, (uint8_t *)&t463 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t463 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t463 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t463 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t463 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t463 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t463 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t463 + 16), t462);
                gm_ttk92ufj2f6eh t464;
                memset(&t464, 0, sizeof t464);
                ((uint8_t *)&t464)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t464 + 16), t463);
                memmove(gm_out, ((uint8_t *)&t464), 112);
                return 2;
            }
gm_lb33:;
            {
                void * t465 = F.l0;
                gm_tl874t2atnlh3 t466 = F.l5;
                gm_b16_8 t467;
                GM_ST(void *, (uint8_t *)&t467, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t467 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t468;
                gm_f_unsupported_2seq49l((void *)&t468, t465, GM_CONV(gm_cseahvg8v7p0vs, t466), t467);
                gm_ttk92ufj2f6eh t469;
                GM_ST(uint8_t, (uint8_t *)&t469 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t469 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t469 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t469 + 8, 0);
                ((uint8_t *)&t469)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t469 + 16), t468);
                memmove(gm_out, ((uint8_t *)&t469), 112);
                return 2;
            }
gm_lb36:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 393, 42);
        }
gm_lb29:;
    }
gm_lb23:;
    {
        uint8_t t470 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t471 = (uint8_t)(t470 == ((uint8_t)5ULL));
        if (!t471) {
            goto gm_lb37;
        }
        {
            {
                void * t472 = F.l4;
                void * t473 = F.l3;
                gm_t88rcucg1b7uk t474 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t473));
                gm_f_kind_jcnt4hm((void *)&F.l146, t472, GM_CONV(gm_csguankko4lmhd, t474));
                uint8_t t475 = GM_LD(uint8_t, (((uint8_t *)&F.l146) + 0));
                uint8_t t476 = (uint8_t)(t475 == ((uint8_t)13ULL));
                if (!t476) {
                    goto gm_lb39;
                }
                gm_t88rcucg1b7uk t477 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l146) + 8));
                F.l20 = t477;
                {
                    {
                        uint8_t t478 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t479 = (uint8_t)(t478 == ((uint8_t)0ULL));
                        if (!t479) {
                            goto gm_lb41;
                        }
                        unsigned __int128 t480 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                        F.l21 = t480;
                        void * t481 = F.l0;
                        unsigned __int128 t482 = F.l21;
                        gm_t88rcucg1b7uk t483 = F.l11;
                        gm_f_big_of_1cq0qao((void *)&F.l22, (void *)NULL, cx, NULL, t481, t482, t483);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        void * t484 = F.l0;
                        gm_t88rcucg1b7uk t485 = F.l20;
                        gm_f_int_range_hue3vms((void *)&F.l23, (void *)NULL, cx, NULL, t484, t485);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        int32_t t486 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                        uint8_t t487 = (t486 < ((int32_t)0LL));
                        if (t487) {
                            F.l147 = ((uint8_t)1);
                        } else {
                            int32_t t488 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l23) + 48)));
                            uint8_t t489 = (t488 > ((int32_t)0LL));
                            F.l147 = t489;
                        }
                        uint8_t t490 = F.l147;
                        if (t490) {
                            gm_t6evimt84lndq t491;
                            memset(&t491, 0, sizeof t491);
                            GM_ST(uint8_t, ((uint8_t *)&t491 + 0), ((uint8_t)6ULL));
                            gm_ttk92ufj2f6eh t492;
                            memset(&t492, 0, sizeof t492);
                            ((uint8_t *)&t492)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t492 + 16), t491);
                            memmove(gm_out, ((uint8_t *)&t492), 112);
                            return 2;
                        }
                        void * t493 = F.l0;
                        unsigned __int128 t494 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                        gm_t88rcucg1b7uk t495 = F.l20;
                        unsigned __int128 t496 = gm_f_wrap_bt8r149(t493, t494, GM_CONV(gm_csguankko4lmhd, t495));
                        gm_t6evimt84lndq t497;
                        GM_ST(uint8_t, (uint8_t *)&t497 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t497 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t497 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t497 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t497 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t497 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t497 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t497 + 16), t496);
                        gm_tq7s98ihonkju t498 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t497, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kgj4rhjmudg)));
                        if (gm_unw.kind) {
                            return 1;
                        }
                        F.l148 = t498;
                        uint8_t t499 = (uint8_t)!((((uint8_t *)&F.l148))[0] != 0);
                        if (!t499) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l148) + 8, gm_srcue4ionbe9lgh, 412, 34);
                        void * t500 = GM_LD(void *, (((uint8_t *)&F.l148) + 8));
                        gm_t6evimt84lndq t501;
                        memset(&t501, 0, sizeof t501);
                        GM_ST(uint8_t, ((uint8_t *)&t501 + 0), ((uint8_t)7ULL));
                        GM_ST(void *, ((uint8_t *)&t501 + 16), t500);
                        gm_ttk92ufj2f6eh t502;
                        memset(&t502, 0, sizeof t502);
                        ((uint8_t *)&t502)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t502 + 16), t501);
                        memmove(gm_out, ((uint8_t *)&t502), 112);
                        return 2;
                    }
gm_lb41:;
                    {
                        uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t504 = (uint8_t)(t503 == ((uint8_t)1ULL));
                        if (!t504) {
                            goto gm_lb42;
                        }
                        double t505 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                        F.l24 = t505;
                        double t506 = F.l24;
                        gm_f_float_exact_1g17c6c((void *)&F.l149, (void *)NULL, cx, NULL, t506);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        uint8_t t507 = (uint8_t)((((uint8_t *)&F.l149))[96] != 0);
                        if (t507) {
                            memmove(((uint8_t *)&F.l150), ((uint8_t *)&F.l149), 96);
                        } else {
                            gm_t6evimt84lndq t508;
                            memset(&t508, 0, sizeof t508);
                            GM_ST(uint8_t, ((uint8_t *)&t508 + 0), ((uint8_t)6ULL));
                            gm_ttk92ufj2f6eh t509;
                            memset(&t509, 0, sizeof t509);
                            ((uint8_t *)&t509)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t509 + 16), t508);
                            memmove(gm_out, ((uint8_t *)&t509), 112);
                            return 2;
                        }
                        memmove(((uint8_t *)&F.l25), ((uint8_t *)&F.l150), 96);
                        uint8_t t510 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l25)));
                        uint8_t t511 = (uint8_t)!t510;
                        if (t511) {
                            gm_t6evimt84lndq t512;
                            memset(&t512, 0, sizeof t512);
                            GM_ST(uint8_t, ((uint8_t *)&t512 + 0), ((uint8_t)6ULL));
                            gm_ttk92ufj2f6eh t513;
                            memset(&t513, 0, sizeof t513);
                            ((uint8_t *)&t513)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t513 + 16), t512);
                            memmove(gm_out, ((uint8_t *)&t513), 112);
                            return 2;
                        }
                        void * t514 = F.l0;
                        gm_t88rcucg1b7uk t515 = F.l20;
                        gm_f_int_range_hue3vms((void *)&F.l26, (void *)NULL, cx, NULL, t514, t515);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        int32_t t516 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)));
                        uint8_t t517 = (t516 < ((int32_t)0LL));
                        if (t517) {
                            F.l151 = ((uint8_t)1);
                        } else {
                            int32_t t518 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l26) + 48)));
                            uint8_t t519 = (t518 > ((int32_t)0LL));
                            F.l151 = t519;
                        }
                        uint8_t t520 = F.l151;
                        if (t520) {
                            gm_t6evimt84lndq t521;
                            memset(&t521, 0, sizeof t521);
                            GM_ST(uint8_t, ((uint8_t *)&t521 + 0), ((uint8_t)6ULL));
                            gm_ttk92ufj2f6eh t522;
                            memset(&t522, 0, sizeof t522);
                            ((uint8_t *)&t522)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t522 + 16), t521);
                            memmove(gm_out, ((uint8_t *)&t522), 112);
                            return 2;
                        }
                        void * t523 = F.l0;
                        unsigned __int128 t524 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)));
                        gm_t88rcucg1b7uk t525 = F.l20;
                        unsigned __int128 t526 = gm_f_wrap_bt8r149(t523, t524, GM_CONV(gm_csguankko4lmhd, t525));
                        gm_t6evimt84lndq t527;
                        GM_ST(uint8_t, (uint8_t *)&t527 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t527 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t527 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t527 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t527 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t527 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t527 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t527 + 16), t526);
                        gm_tq7s98ihonkju t528 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t527, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kkei5iuecp0)));
                        if (gm_unw.kind) {
                            return 1;
                        }
                        F.l152 = t528;
                        uint8_t t529 = (uint8_t)!((((uint8_t *)&F.l152))[0] != 0);
                        if (!t529) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l152) + 8, gm_srcue4ionbe9lgh, 419, 34);
                        void * t530 = GM_LD(void *, (((uint8_t *)&F.l152) + 8));
                        gm_t6evimt84lndq t531;
                        memset(&t531, 0, sizeof t531);
                        GM_ST(uint8_t, ((uint8_t *)&t531 + 0), ((uint8_t)7ULL));
                        GM_ST(void *, ((uint8_t *)&t531 + 16), t530);
                        gm_ttk92ufj2f6eh t532;
                        memset(&t532, 0, sizeof t532);
                        ((uint8_t *)&t532)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t532 + 16), t531);
                        memmove(gm_out, ((uint8_t *)&t532), 112);
                        return 2;
                    }
gm_lb42:;
                    {
                        void * t533 = F.l0;
                        gm_tl874t2atnlh3 t534 = F.l5;
                        gm_b16_8 t535;
                        GM_ST(void *, (uint8_t *)&t535, (void *)(gm_smr3su985na96));
                        GM_ST(uintptr_t, (uint8_t *)&t535 + 8, (uintptr_t)(15));
                        gm_t39uai1ajk3qv t536;
                        gm_f_unsupported_2seq49l((void *)&t536, t533, GM_CONV(gm_cseahvg8v7p0vs, t534), t535);
                        gm_ttk92ufj2f6eh t537;
                        GM_ST(uint8_t, (uint8_t *)&t537 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t537 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t537 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t537 + 8, 0);
                        ((uint8_t *)&t537)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t537 + 16), t536);
                        memmove(gm_out, ((uint8_t *)&t537), 112);
                        return 2;
                    }
gm_lb43:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 407, 13);
                }
gm_lb40:;
            }
gm_lb39:;
            void * t538 = F.l0;
            gm_tl874t2atnlh3 t539 = F.l5;
            gm_b16_8 t540;
            GM_ST(void *, (uint8_t *)&t540, (void *)(gm_smr3su985na96));
            GM_ST(uintptr_t, (uint8_t *)&t540 + 8, (uintptr_t)(15));
            gm_t39uai1ajk3qv t541;
            gm_f_unsupported_2seq49l((void *)&t541, t538, GM_CONV(gm_cseahvg8v7p0vs, t539), t540);
            gm_ttk92ufj2f6eh t542;
            GM_ST(uint8_t, (uint8_t *)&t542 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t542 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t542 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t542 + 8, 0);
            ((uint8_t *)&t542)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t542 + 16), t541);
            memmove(gm_out, ((uint8_t *)&t542), 112);
            return 2;
        }
gm_lb38:;
    }
gm_lb37:;
    {
        uint8_t t543 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t544 = (uint8_t)(t543 == ((uint8_t)6ULL));
        if (!t544) {
            goto gm_lb44;
        }
        void * t545 = F.l0;
        void * t546 = F.l3;
        gm_t88rcucg1b7uk t547 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t546));
        gm_f_int_range_hue3vms((void *)&F.l27, (void *)NULL, cx, NULL, t545, t547);
        if (gm_unw.kind) {
            return 1;
        }
        gm_f_zero_s25l990((void *)&F.l28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            return 1;
        }
        {
            {
                uint8_t t548 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t549 = (uint8_t)(t548 == ((uint8_t)0ULL));
                if (!t549) {
                    goto gm_lb46;
                }
                unsigned __int128 t550 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l29 = t550;
                void * t551 = F.l0;
                unsigned __int128 t552 = F.l29;
                gm_t88rcucg1b7uk t553 = F.l11;
                gm_f_big_of_1cq0qao((void *)&F.l28, (void *)NULL, cx, NULL, t551, t552, t553);
                if (gm_unw.kind) {
                    return 1;
                }
                goto gm_lb45;
            }
gm_lb46:;
            {
                uint8_t t554 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t555 = (uint8_t)(t554 == ((uint8_t)1ULL));
                if (!t555) {
                    goto gm_lb47;
                }
                double t556 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l30 = t556;
                double t557 = F.l30;
                double t558 = F.l30;
                uint8_t t559 = (!(t557 == t558));
                if (t559) {
                    gm_t6evimt84lndq t560;
                    GM_ST(uint8_t, (uint8_t *)&t560 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t560 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t560 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t560 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t560 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t560 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t560 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t560 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    gm_ttk92ufj2f6eh t561;
                    memset(&t561, 0, sizeof t561);
                    ((uint8_t *)&t561)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t561 + 16), t560);
                    memmove(gm_out, ((uint8_t *)&t561), 112);
                    return 2;
                }
                {
                    {
                        double t562 = F.l30;
                        gm_f_float_exact_1g17c6c((void *)&F.l153, (void *)NULL, cx, NULL, t562);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        uint8_t t563 = (uint8_t)((((uint8_t *)&F.l153))[96] != 0);
                        if (!t563) {
                            goto gm_lb49;
                        }
                        memmove(((uint8_t *)&F.l31), ((uint8_t *)&F.l153), 96);
                        gm_tbci5ospk5nnn t564 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l31));
                        gm_tbci5ospk5nnn t565 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l31) + 48));
                        gm_f_div_floor_u3mfihk((void *)&F.l28, (void *)NULL, cx, NULL, &t564, &t565);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        {
                            {
                                uint8_t t566 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l31)));
                                if (!t566) {
                                    goto gm_lb51;
                                }
                                uint8_t t567 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l31)));
                                uint8_t t568 = (uint8_t)!t567;
                                if (!t568) {
                                    goto gm_lb51;
                                }
                                gm_tbci5ospk5nnn t569 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l28));
                                gm_tbci5ospk5nnn t570;
                                gm_f_one_08qg1op((void *)&t570, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    return 1;
                                }
                                gm_f_add_420lj6o((void *)&F.l28, (void *)NULL, cx, NULL, &t569, &t570);
                                if (gm_unw.kind) {
                                    return 1;
                                }
                                goto gm_lb50;
                            }
gm_lb51:;
                        }
gm_lb50:;
                        goto gm_lb48;
                    }
gm_lb49:;
                    double t571 = F.l30;
                    uint8_t t572 = (t571 < ((double)0e0));
                    if (t572) {
                        memmove(((uint8_t *)&F.l154), ((uint8_t *)&F.l27), 48);
                    } else {
                        memmove(((uint8_t *)&F.l154), (((uint8_t *)&F.l27) + 48), 48);
                    }
                    memmove(((uint8_t *)&F.l28), ((uint8_t *)&F.l154), 48);
                }
gm_lb48:;
                goto gm_lb45;
            }
gm_lb47:;
            {
                void * t573 = F.l0;
                gm_tl874t2atnlh3 t574 = F.l5;
                gm_b16_8 t575;
                GM_ST(void *, (uint8_t *)&t575, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t575 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t576;
                gm_f_unsupported_2seq49l((void *)&t576, t573, GM_CONV(gm_cseahvg8v7p0vs, t574), t575);
                gm_ttk92ufj2f6eh t577;
                GM_ST(uint8_t, (uint8_t *)&t577 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t577 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t577 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t577 + 8, 0);
                ((uint8_t *)&t577)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t577 + 16), t576);
                memmove(gm_out, ((uint8_t *)&t577), 112);
                return 2;
            }
gm_lb52:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 427, 13);
        }
gm_lb45:;
        int32_t t578 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)));
        uint8_t t579 = (t578 < ((int32_t)0LL));
        if (t579) {
            memmove(((uint8_t *)&F.l28), ((uint8_t *)&F.l27), 48);
        }
        int32_t t580 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l27) + 48)));
        uint8_t t581 = (t580 > ((int32_t)0LL));
        if (t581) {
            memmove(((uint8_t *)&F.l28), (((uint8_t *)&F.l27) + 48), 48);
        }
        void * t582 = F.l0;
        unsigned __int128 t583 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)));
        void * t584 = F.l3;
        gm_t88rcucg1b7uk t585 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t584));
        unsigned __int128 t586 = gm_f_wrap_bt8r149(t582, t583, GM_CONV(gm_csguankko4lmhd, t585));
        gm_t6evimt84lndq t587;
        GM_ST(uint8_t, (uint8_t *)&t587 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t587 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t587 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t587 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t587 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t587 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t587 + 0), ((uint8_t)0ULL));
        GM_ST(unsigned __int128, ((uint8_t *)&t587 + 16), t586);
        gm_ttk92ufj2f6eh t588;
        memset(&t588, 0, sizeof t588);
        ((uint8_t *)&t588)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t588 + 16), t587);
        memmove(gm_out, ((uint8_t *)&t588), 112);
        return 2;
    }
gm_lb44:;
    {
        {
            {
                uint8_t t589 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t590 = (uint8_t)(t589 == ((uint8_t)16ULL));
                if (!t590) {
                    goto gm_lb55;
                }
                goto gm_lb54;
            }
gm_lb55:;
            {
                uint8_t t591 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t592 = (uint8_t)(t591 == ((uint8_t)17ULL));
                if (!t592) {
                    goto gm_lb56;
                }
                goto gm_lb54;
            }
gm_lb56:;
            goto gm_lb53;
        }
gm_lb54:;
        void * t593 = GM_LD(void *, ((uint8_t *)&F.l9));
        uint64_t t594 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t594)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t594, gm_srcue4ionbe9lgh, 445, 18);
        memmove(((uint8_t *)&F.l32), (((uint8_t *)t593) + (uintptr_t)((uint64_t)1ULL) * 48), 48);
        {
            {
                uint8_t t595 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t596 = (uint8_t)(t595 == ((uint8_t)0ULL));
                if (!t596) {
                    goto gm_lb58;
                }
                unsigned __int128 t597 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l33 = t597;
                {
                    {
                        uint8_t t598 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                        uint8_t t599 = (uint8_t)(t598 == ((uint8_t)0ULL));
                        if (!t599) {
                            goto gm_lb60;
                        }
                        unsigned __int128 t600 = GM_LD(unsigned __int128, (((uint8_t *)&F.l32) + 16));
                        F.l34 = t600;
                        void * t601 = F.l0;
                        unsigned __int128 t602 = F.l33;
                        gm_t88rcucg1b7uk t603 = F.l11;
                        gm_tbci5ospk5nnn t604;
                        gm_f_big_of_1cq0qao((void *)&t604, (void *)NULL, cx, NULL, t601, t602, t603);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        void * t605 = F.l0;
                        unsigned __int128 t606 = F.l34;
                        void * t607 = GM_LD(void *, ((uint8_t *)&F.l2));
                        uint64_t t608 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t608)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t608, gm_srcue4ionbe9lgh, 449, 58);
                        gm_t88rcucg1b7uk t609 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t607) + (uintptr_t)((uint64_t)1ULL) * 88));
                        gm_tbci5ospk5nnn t610;
                        gm_f_big_of_1cq0qao((void *)&t610, (void *)NULL, cx, NULL, t605, t606, t609);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        int32_t t611 = gm_f_cmp_i1knrgu(&t604, &t610);
                        F.l35 = t611;
                        int32_t t612 = F.l35;
                        uint8_t t613 = (t612 <= ((int32_t)0LL));
                        gm_tvdg94bk7aa22 t614 = F.l1;
                        uint8_t t615 = GM_LD(uint8_t, (((uint8_t *)&t614) + 0));
                        uint8_t t616 = (uint8_t)((t615 == ((uint8_t)16ULL)));
                        uint8_t t617 = ((t613 == t616));
                        gm_tvdg94bk7aa22 t618;
                        GM_ST(uint8_t, (uint8_t *)&t618 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t618 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t618 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t618 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t618 + 0), ((uint8_t)16ULL));
                        if (t617) {
                            gm_t6evimt84lndq t619 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
                            gm_ttk92ufj2f6eh t620;
                            memset(&t620, 0, sizeof t620);
                            ((uint8_t *)&t620)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t620 + 16), t619);
                            F.l155 = t620;
                        } else {
                            gm_t6evimt84lndq t621 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l32));
                            gm_ttk92ufj2f6eh t622;
                            memset(&t622, 0, sizeof t622);
                            ((uint8_t *)&t622)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t622 + 16), t621);
                            F.l155 = t622;
                        }
                        memmove(gm_out, ((uint8_t *)&F.l155), 112);
                        return 2;
                    }
gm_lb60:;
                    void * t623 = F.l0;
                    gm_tl874t2atnlh3 t624 = F.l5;
                    gm_b16_8 t625;
                    GM_ST(void *, (uint8_t *)&t625, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t625 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t626;
                    gm_f_unsupported_2seq49l((void *)&t626, t623, GM_CONV(gm_cseahvg8v7p0vs, t624), t625);
                    gm_ttk92ufj2f6eh t627;
                    GM_ST(uint8_t, (uint8_t *)&t627 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t627 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t627 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t627 + 8, 0);
                    ((uint8_t *)&t627)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t627 + 16), t626);
                    memmove(gm_out, ((uint8_t *)&t627), 112);
                    return 2;
                }
gm_lb59:;
            }
gm_lb58:;
            {
                uint8_t t628 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t629 = (uint8_t)(t628 == ((uint8_t)1ULL));
                if (!t629) {
                    goto gm_lb61;
                }
                double t630 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l36 = t630;
                {
                    {
                        uint8_t t631 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                        uint8_t t632 = (uint8_t)(t631 == ((uint8_t)1ULL));
                        if (!t632) {
                            goto gm_lb63;
                        }
                        double t633 = GM_LD(double, (((uint8_t *)&F.l32) + 16));
                        F.l37 = t633;
                        double t634 = F.l36;
                        double t635 = F.l36;
                        uint8_t t636 = (!(t634 == t635));
                        if (t636) {
                            gm_t6evimt84lndq t637 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l32));
                            gm_ttk92ufj2f6eh t638;
                            memset(&t638, 0, sizeof t638);
                            ((uint8_t *)&t638)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t638 + 16), t637);
                            memmove(gm_out, ((uint8_t *)&t638), 112);
                            return 2;
                        }
                        double t639 = F.l37;
                        double t640 = F.l37;
                        uint8_t t641 = (!(t639 == t640));
                        if (t641) {
                            gm_t6evimt84lndq t642 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
                            gm_ttk92ufj2f6eh t643;
                            memset(&t643, 0, sizeof t643);
                            ((uint8_t *)&t643)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t643 + 16), t642);
                            memmove(gm_out, ((uint8_t *)&t643), 112);
                            return 2;
                        }
                        double t644 = F.l36;
                        double t645 = F.l37;
                        uint8_t t646 = (t644 <= t645);
                        gm_tvdg94bk7aa22 t647 = F.l1;
                        uint8_t t648 = GM_LD(uint8_t, (((uint8_t *)&t647) + 0));
                        uint8_t t649 = (uint8_t)((t648 == ((uint8_t)16ULL)));
                        uint8_t t650 = ((t646 == t649));
                        gm_tvdg94bk7aa22 t651;
                        GM_ST(uint8_t, (uint8_t *)&t651 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t651 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t651 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t651 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t651 + 0), ((uint8_t)16ULL));
                        if (t650) {
                            gm_t6evimt84lndq t652 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
                            gm_ttk92ufj2f6eh t653;
                            memset(&t653, 0, sizeof t653);
                            ((uint8_t *)&t653)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t653 + 16), t652);
                            F.l156 = t653;
                        } else {
                            gm_t6evimt84lndq t654 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l32));
                            gm_ttk92ufj2f6eh t655;
                            memset(&t655, 0, sizeof t655);
                            ((uint8_t *)&t655)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t655 + 16), t654);
                            F.l156 = t655;
                        }
                        memmove(gm_out, ((uint8_t *)&F.l156), 112);
                        return 2;
                    }
gm_lb63:;
                    void * t656 = F.l0;
                    gm_tl874t2atnlh3 t657 = F.l5;
                    gm_b16_8 t658;
                    GM_ST(void *, (uint8_t *)&t658, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t658 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t659;
                    gm_f_unsupported_2seq49l((void *)&t659, t656, GM_CONV(gm_cseahvg8v7p0vs, t657), t658);
                    gm_ttk92ufj2f6eh t660;
                    GM_ST(uint8_t, (uint8_t *)&t660 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t660 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t660 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t660 + 8, 0);
                    ((uint8_t *)&t660)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t660 + 16), t659);
                    memmove(gm_out, ((uint8_t *)&t660), 112);
                    return 2;
                }
gm_lb62:;
            }
gm_lb61:;
            {
                void * t661 = F.l0;
                gm_tl874t2atnlh3 t662 = F.l5;
                gm_b16_8 t663;
                GM_ST(void *, (uint8_t *)&t663, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t663 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t664;
                gm_f_unsupported_2seq49l((void *)&t664, t661, GM_CONV(gm_cseahvg8v7p0vs, t662), t663);
                gm_ttk92ufj2f6eh t665;
                GM_ST(uint8_t, (uint8_t *)&t665 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t665 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t665 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t665 + 8, 0);
                ((uint8_t *)&t665)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t665 + 16), t664);
                memmove(gm_out, ((uint8_t *)&t665), 112);
                return 2;
            }
gm_lb64:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 446, 13);
        }
gm_lb57:;
    }
gm_lb53:;
    return 0;
#undef F
}
GM_NOINLINE int gm_c_builtin_psq6ql3_666(struct gm_fr_builtin_psq6ql3 *gm_Fp, void *env, gm_node *cx, gm_node *inst, void *gm_out) {
#define F (*gm_Fp)
    {
        uint8_t t667 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t668 = (uint8_t)(t667 == ((uint8_t)18ULL));
        if (!t668) {
            goto gm_lb65;
        }
        {
            {
                uint8_t t669 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t670 = (uint8_t)(t669 == ((uint8_t)0ULL));
                if (!t670) {
                    goto gm_lb67;
                }
                unsigned __int128 t671 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l38 = t671;
                {
                    {
                        void * t672 = GM_LD(void *, ((uint8_t *)&F.l9));
                        uint64_t t673 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t673)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t673, gm_srcue4ionbe9lgh, 463, 25);
                        uint8_t t674 = GM_LD(uint8_t, ((((uint8_t *)t672) + (uintptr_t)((uint64_t)1ULL) * 48) + 0));
                        uint8_t t675 = (uint8_t)(t674 == ((uint8_t)0ULL));
                        if (!t675) {
                            goto gm_lb69;
                        }
                        unsigned __int128 t676 = GM_LD(unsigned __int128, ((((uint8_t *)t672) + (uintptr_t)((uint64_t)1ULL) * 48) + 16));
                        F.l39 = t676;
                        {
                            {
                                void * t677 = GM_LD(void *, ((uint8_t *)&F.l9));
                                uint64_t t678 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t678)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t678, gm_srcue4ionbe9lgh, 464, 25);
                                uint8_t t679 = GM_LD(uint8_t, ((((uint8_t *)t677) + (uintptr_t)((uint64_t)2ULL) * 48) + 0));
                                uint8_t t680 = (uint8_t)(t679 == ((uint8_t)0ULL));
                                if (!t680) {
                                    goto gm_lb71;
                                }
                                unsigned __int128 t681 = GM_LD(unsigned __int128, ((((uint8_t *)t677) + (uintptr_t)((uint64_t)2ULL) * 48) + 16));
                                F.l40 = t681;
                                void * t682 = F.l0;
                                unsigned __int128 t683 = F.l38;
                                gm_t88rcucg1b7uk t684 = F.l11;
                                gm_f_big_of_1cq0qao((void *)&F.l41, (void *)NULL, cx, NULL, t682, t683, t684);
                                if (gm_unw.kind) {
                                    return 1;
                                }
                                void * t685 = F.l0;
                                unsigned __int128 t686 = F.l39;
                                gm_t88rcucg1b7uk t687 = F.l11;
                                gm_tbci5ospk5nnn t688 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l41));
                                gm_tbci5ospk5nnn t689;
                                gm_f_big_of_1cq0qao((void *)&t689, (void *)NULL, cx, NULL, t685, t686, t687);
                                if (gm_unw.kind) {
                                    return 1;
                                }
                                int32_t t690 = gm_f_cmp_i1knrgu(&t688, &t689);
                                uint8_t t691 = (t690 < ((int32_t)0LL));
                                if (t691) {
                                    void * t692 = GM_LD(void *, ((uint8_t *)&F.l9));
                                    uint64_t t693 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t693)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t693, gm_srcue4ionbe9lgh, 466, 53);
                                    gm_t6evimt84lndq t694 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t692) + (uintptr_t)((uint64_t)1ULL) * 48));
                                    gm_ttk92ufj2f6eh t695;
                                    memset(&t695, 0, sizeof t695);
                                    ((uint8_t *)&t695)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t695 + 16), t694);
                                    memmove(gm_out, ((uint8_t *)&t695), 112);
                                    return 2;
                                }
                                void * t696 = F.l0;
                                unsigned __int128 t697 = F.l40;
                                gm_t88rcucg1b7uk t698 = F.l11;
                                gm_tbci5ospk5nnn t699 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l41));
                                gm_tbci5ospk5nnn t700;
                                gm_f_big_of_1cq0qao((void *)&t700, (void *)NULL, cx, NULL, t696, t697, t698);
                                if (gm_unw.kind) {
                                    return 1;
                                }
                                int32_t t701 = gm_f_cmp_i1knrgu(&t699, &t700);
                                uint8_t t702 = (t701 > ((int32_t)0LL));
                                if (t702) {
                                    void * t703 = GM_LD(void *, ((uint8_t *)&F.l9));
                                    uint64_t t704 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t704)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t704, gm_srcue4ionbe9lgh, 467, 53);
                                    gm_t6evimt84lndq t705 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t703) + (uintptr_t)((uint64_t)2ULL) * 48));
                                    gm_ttk92ufj2f6eh t706;
                                    memset(&t706, 0, sizeof t706);
                                    ((uint8_t *)&t706)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t706 + 16), t705);
                                    memmove(gm_out, ((uint8_t *)&t706), 112);
                                    return 2;
                                }
                                gm_t6evimt84lndq t707 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
                                gm_ttk92ufj2f6eh t708;
                                memset(&t708, 0, sizeof t708);
                                ((uint8_t *)&t708)[0] = 0;
                                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t708 + 16), t707);
                                memmove(gm_out, ((uint8_t *)&t708), 112);
                                return 2;
                            }
gm_lb71:;
                            void * t709 = F.l0;
                            gm_tl874t2atnlh3 t710 = F.l5;
                            gm_b16_8 t711;
                            GM_ST(void *, (uint8_t *)&t711, (void *)(gm_sv7lsha1e0i8r));
                            GM_ST(uintptr_t, (uint8_t *)&t711 + 8, (uintptr_t)(12));
                            gm_t39uai1ajk3qv t712;
                            gm_f_unsupported_2seq49l((void *)&t712, t709, GM_CONV(gm_cseahvg8v7p0vs, t710), t711);
                            gm_ttk92ufj2f6eh t713;
                            GM_ST(uint8_t, (uint8_t *)&t713 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t713 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t713 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t713 + 8, 0);
                            ((uint8_t *)&t713)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t713 + 16), t712);
                            memmove(gm_out, ((uint8_t *)&t713), 112);
                            return 2;
                        }
gm_lb70:;
                    }
gm_lb69:;
                    void * t714 = F.l0;
                    gm_tl874t2atnlh3 t715 = F.l5;
                    gm_b16_8 t716;
                    GM_ST(void *, (uint8_t *)&t716, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t716 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t717;
                    gm_f_unsupported_2seq49l((void *)&t717, t714, GM_CONV(gm_cseahvg8v7p0vs, t715), t716);
                    gm_ttk92ufj2f6eh t718;
                    GM_ST(uint8_t, (uint8_t *)&t718 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t718 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t718 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t718 + 8, 0);
                    ((uint8_t *)&t718)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t718 + 16), t717);
                    memmove(gm_out, ((uint8_t *)&t718), 112);
                    return 2;
                }
gm_lb68:;
            }
gm_lb67:;
            void * t719 = F.l0;
            gm_tl874t2atnlh3 t720 = F.l5;
            gm_b16_8 t721;
            GM_ST(void *, (uint8_t *)&t721, (void *)(gm_seg4idqqmijmd));
            GM_ST(uintptr_t, (uint8_t *)&t721 + 8, (uintptr_t)(33));
            gm_t39uai1ajk3qv t722;
            gm_f_unsupported_2seq49l((void *)&t722, t719, GM_CONV(gm_cseahvg8v7p0vs, t720), t721);
            gm_ttk92ufj2f6eh t723;
            GM_ST(uint8_t, (uint8_t *)&t723 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t723 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t723 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t723 + 8, 0);
            ((uint8_t *)&t723)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t723 + 16), t722);
            memmove(gm_out, ((uint8_t *)&t723), 112);
            return 2;
        }
gm_lb66:;
    }
gm_lb65:;
    {
        uint8_t t724 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t725 = (uint8_t)(t724 == ((uint8_t)19ULL));
        if (!t725) {
            goto gm_lb72;
        }
        {
            {
                uint8_t t726 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t727 = (uint8_t)(t726 == ((uint8_t)0ULL));
                if (!t727) {
                    goto gm_lb74;
                }
                unsigned __int128 t728 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l42 = t728;
                void * t729 = F.l0;
                unsigned __int128 t730 = F.l42;
                gm_t88rcucg1b7uk t731 = F.l11;
                gm_f_big_of_1cq0qao((void *)&F.l43, (void *)NULL, cx, NULL, t729, t730, t731);
                if (gm_unw.kind) {
                    return 1;
                }
                void * t732 = F.l0;
                void * t733 = F.l3;
                gm_t88rcucg1b7uk t734 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t733));
                gm_f_int_range_hue3vms((void *)&F.l44, (void *)NULL, cx, NULL, t732, t734);
                if (gm_unw.kind) {
                    return 1;
                }
                gm_tbci5ospk5nnn t735 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l43));
                gm_f_abs_v7cf214((void *)&F.l45, (void *)NULL, cx, NULL, &t735);
                if (gm_unw.kind) {
                    return 1;
                }
                int32_t t736 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l45)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l44) + 48)));
                uint8_t t737 = (t736 > ((int32_t)0LL));
                if (t737) {
                    void * t738 = F.l0;
                    gm_tl874t2atnlh3 t739 = F.l5;
                    gm_b16_8 t740;
                    GM_ST(void *, (uint8_t *)&t740, (void *)(gm_sjn4krvshoajd));
                    GM_ST(uintptr_t, (uint8_t *)&t740 + 8, (uintptr_t)(25));
                    gm_t39uai1ajk3qv t741;
                    gm_f_trap_jftbss6((void *)&t741, (void *)NULL, cx, NULL, t738, t739, t740);
                    if (gm_unw.kind) {
                        return 1;
                    }
                    gm_ttk92ufj2f6eh t742;
                    GM_ST(uint8_t, (uint8_t *)&t742 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t742 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t742 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t742 + 8, 0);
                    ((uint8_t *)&t742)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t742 + 16), t741);
                    memmove(gm_out, ((uint8_t *)&t742), 112);
                    return 2;
                }
                void * t743 = F.l0;
                unsigned __int128 t744 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l45)));
                void * t745 = F.l3;
                gm_t88rcucg1b7uk t746 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t745));
                unsigned __int128 t747 = gm_f_wrap_bt8r149(t743, t744, GM_CONV(gm_csguankko4lmhd, t746));
                gm_t6evimt84lndq t748;
                GM_ST(uint8_t, (uint8_t *)&t748 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t748 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t748 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t748 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t748 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t748 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t748 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t748 + 16), t747);
                gm_ttk92ufj2f6eh t749;
                memset(&t749, 0, sizeof t749);
                ((uint8_t *)&t749)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t749 + 16), t748);
                memmove(gm_out, ((uint8_t *)&t749), 112);
                return 2;
            }
gm_lb74:;
            {
                uint8_t t750 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t751 = (uint8_t)(t750 == ((uint8_t)1ULL));
                if (!t751) {
                    goto gm_lb75;
                }
                double t752 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l46 = t752;
                double t753 = F.l46;
                uint8_t t754 = (t753 < ((double)0e0));
                if (t754) {
                    F.l158 = ((uint8_t)1);
                } else {
                    double t755 = F.l46;
                    uint8_t t756 = ((t755 == ((double)0e0)));
                    if (t756) {
                        double t757 = F.l46;
                        uint64_t t758;
                        memset(&t758, 0, sizeof t758); memcpy(&t758, ((uint8_t *)&t757), sizeof t758 < 8 ? sizeof t758 : 8);
                        unsigned __int128 n759 = (unsigned __int128)((uint64_t)63ULL);
                        if (n759 >= 64) gm_trap_shift((uint64_t)n759, "u64", gm_srcue4ionbe9lgh, 478, 68);
                        uint64_t t760 = (uint64_t)(t758 >> (int)n759);
                        uint8_t t761 = ((t760 == ((uint64_t)1ULL)));
                        F.l159 = t761;
                    } else {
                        F.l159 = ((uint8_t)0);
                    }
                    uint8_t t762 = F.l159;
                    F.l158 = t762;
                }
                uint8_t t763 = F.l158;
                if (t763) {
                    double t764 = F.l46;
                    double t765 = -t764;
                    F.l157 = t765;
                } else {
                    double t766 = F.l46;
                    F.l157 = t766;
                }
                double t767 = F.l157;
                gm_t6evimt84lndq t768;
                memset(&t768, 0, sizeof t768);
                GM_ST(uint8_t, ((uint8_t *)&t768 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t768 + 16), t767);
                gm_ttk92ufj2f6eh t769;
                memset(&t769, 0, sizeof t769);
                ((uint8_t *)&t769)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t769 + 16), t768);
                memmove(gm_out, ((uint8_t *)&t769), 112);
                return 2;
            }
gm_lb75:;
            {
                void * t770 = F.l0;
                gm_tl874t2atnlh3 t771 = F.l5;
                gm_b16_8 t772;
                GM_ST(void *, (uint8_t *)&t772, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t772 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t773;
                gm_f_unsupported_2seq49l((void *)&t773, t770, GM_CONV(gm_cseahvg8v7p0vs, t771), t772);
                gm_ttk92ufj2f6eh t774;
                GM_ST(uint8_t, (uint8_t *)&t774 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t774 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t774 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t774 + 8, 0);
                ((uint8_t *)&t774)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t774 + 16), t773);
                memmove(gm_out, ((uint8_t *)&t774), 112);
                return 2;
            }
gm_lb76:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 470, 17);
        }
gm_lb73:;
    }
gm_lb72:;
    {
        uint8_t t775 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t776 = (uint8_t)(t775 == ((uint8_t)20ULL));
        if (!t776) {
            goto gm_lb77;
        }
        {
            {
                uint8_t t777 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t778 = (uint8_t)(t777 == ((uint8_t)0ULL));
                if (!t778) {
                    goto gm_lb79;
                }
                unsigned __int128 t779 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l47 = t779;
                {
                    {
                        void * t780 = GM_LD(void *, ((uint8_t *)&F.l9));
                        uint64_t t781 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t781)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t781, gm_srcue4ionbe9lgh, 483, 24);
                        uint8_t t782 = GM_LD(uint8_t, ((((uint8_t *)t780) + (uintptr_t)((uint64_t)1ULL) * 48) + 0));
                        uint8_t t783 = (uint8_t)(t782 == ((uint8_t)0ULL));
                        if (!t783) {
                            goto gm_lb81;
                        }
                        unsigned __int128 t784 = GM_LD(unsigned __int128, ((((uint8_t *)t780) + (uintptr_t)((uint64_t)1ULL) * 48) + 16));
                        F.l48 = t784;
                        void * t785 = F.l0;
                        unsigned __int128 t786 = F.l47;
                        gm_t88rcucg1b7uk t787 = F.l11;
                        gm_tbci5ospk5nnn t788;
                        gm_f_big_of_1cq0qao((void *)&t788, (void *)NULL, cx, NULL, t785, t786, t787);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        void * t789 = F.l0;
                        unsigned __int128 t790 = F.l48;
                        void * t791 = GM_LD(void *, ((uint8_t *)&F.l2));
                        uint64_t t792 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t792)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t792, gm_srcue4ionbe9lgh, 484, 50);
                        gm_t88rcucg1b7uk t793 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t791) + (uintptr_t)((uint64_t)1ULL) * 88));
                        gm_tbci5ospk5nnn t794;
                        gm_f_big_of_1cq0qao((void *)&t794, (void *)NULL, cx, NULL, t789, t790, t793);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        gm_tbci5ospk5nnn t795;
                        gm_f_sub_pktm5cr((void *)&t795, (void *)NULL, cx, NULL, &t788, &t794);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        gm_f_abs_v7cf214((void *)&F.l49, (void *)NULL, cx, NULL, &t795);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        void * t796 = F.l0;
                        unsigned __int128 t797 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l49)));
                        void * t798 = F.l3;
                        gm_t88rcucg1b7uk t799 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t798));
                        unsigned __int128 t800 = gm_f_wrap_bt8r149(t796, t797, GM_CONV(gm_csguankko4lmhd, t799));
                        gm_t6evimt84lndq t801;
                        GM_ST(uint8_t, (uint8_t *)&t801 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t801 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t801 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t801 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t801 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t801 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t801 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t801 + 16), t800);
                        gm_ttk92ufj2f6eh t802;
                        memset(&t802, 0, sizeof t802);
                        ((uint8_t *)&t802)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t802 + 16), t801);
                        memmove(gm_out, ((uint8_t *)&t802), 112);
                        return 2;
                    }
gm_lb81:;
                    void * t803 = F.l0;
                    gm_tl874t2atnlh3 t804 = F.l5;
                    gm_b16_8 t805;
                    GM_ST(void *, (uint8_t *)&t805, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t805 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t806;
                    gm_f_unsupported_2seq49l((void *)&t806, t803, GM_CONV(gm_cseahvg8v7p0vs, t804), t805);
                    gm_ttk92ufj2f6eh t807;
                    GM_ST(uint8_t, (uint8_t *)&t807 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t807 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t807 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t807 + 8, 0);
                    ((uint8_t *)&t807)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t807 + 16), t806);
                    memmove(gm_out, ((uint8_t *)&t807), 112);
                    return 2;
                }
gm_lb80:;
            }
gm_lb79:;
            void * t808 = F.l0;
            gm_tl874t2atnlh3 t809 = F.l5;
            gm_b16_8 t810;
            GM_ST(void *, (uint8_t *)&t810, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t810 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t811;
            gm_f_unsupported_2seq49l((void *)&t811, t808, GM_CONV(gm_cseahvg8v7p0vs, t809), t810);
            gm_ttk92ufj2f6eh t812;
            GM_ST(uint8_t, (uint8_t *)&t812 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t812 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t812 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t812 + 8, 0);
            ((uint8_t *)&t812)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t812 + 16), t811);
            memmove(gm_out, ((uint8_t *)&t812), 112);
            return 2;
        }
gm_lb78:;
    }
gm_lb77:;
    {
        uint8_t t813 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t814 = (uint8_t)(t813 == ((uint8_t)21ULL));
        if (!t814) {
            goto gm_lb82;
        }
        gm_t9qkh0rsooc8d t815 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 4));
        F.l50 = t815;
        {
            {
                uint8_t t816 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t817 = (uint8_t)(t816 == ((uint8_t)0ULL));
                if (!t817) {
                    goto gm_lb84;
                }
                unsigned __int128 t818 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                F.l51 = t818;
                {
                    {
                        void * t819 = GM_LD(void *, ((uint8_t *)&F.l9));
                        uint64_t t820 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t820)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t820, gm_srcue4ionbe9lgh, 489, 24);
                        uint8_t t821 = GM_LD(uint8_t, ((((uint8_t *)t819) + (uintptr_t)((uint64_t)1ULL) * 48) + 0));
                        uint8_t t822 = (uint8_t)(t821 == ((uint8_t)0ULL));
                        if (!t822) {
                            goto gm_lb86;
                        }
                        unsigned __int128 t823 = GM_LD(unsigned __int128, ((((uint8_t *)t819) + (uintptr_t)((uint64_t)1ULL) * 48) + 16));
                        F.l52 = t823;
                        void * t824 = F.l0;
                        unsigned __int128 t825 = F.l51;
                        gm_t88rcucg1b7uk t826 = F.l11;
                        gm_tbci5ospk5nnn t827;
                        gm_f_big_of_1cq0qao((void *)&t827, (void *)NULL, cx, NULL, t824, t825, t826);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        unsigned __int128 t828 = F.l52;
                        uint32_t t829 = (uint32_t)t828;
                        gm_f_pow_mhlb5fr((void *)&F.l53, (void *)NULL, cx, NULL, &t827, t829);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        gm_t88rcucg1b7uk t830 = F.l11;
                        F.l54 = t830;
                        void * t831 = F.l0;
                        gm_t88rcucg1b7uk t832 = F.l54;
                        gm_f_int_range_hue3vms((void *)&F.l55, (void *)NULL, cx, NULL, t831, t832);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        int32_t t833 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l55)));
                        uint8_t t834 = (t833 >= ((int32_t)0LL));
                        if (t834) {
                            int32_t t835 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l55) + 48)));
                            uint8_t t836 = (t835 <= ((int32_t)0LL));
                            F.l160 = t836;
                        } else {
                            F.l160 = ((uint8_t)0);
                        }
                        uint8_t t837 = F.l160;
                        F.l56 = t837;
                        {
                            {
                                uint8_t t838 = GM_LD(uint8_t, (((uint8_t *)&F.l50) + 0));
                                uint8_t t839 = (uint8_t)(t838 == ((uint8_t)3ULL));
                                if (!t839) {
                                    goto gm_lb88;
                                }
                                uint8_t t840 = F.l56;
                                if (t840) {
                                    void * t841 = F.l0;
                                    unsigned __int128 t842 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)));
                                    gm_t88rcucg1b7uk t843 = F.l54;
                                    unsigned __int128 t844 = gm_f_wrap_bt8r149(t841, t842, GM_CONV(gm_csguankko4lmhd, t843));
                                    gm_t6evimt84lndq t845;
                                    GM_ST(uint8_t, (uint8_t *)&t845 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t845 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t845 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t845 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t845 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t845 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t845 + 0), ((uint8_t)0ULL));
                                    GM_ST(unsigned __int128, ((uint8_t *)&t845 + 16), t844);
                                    gm_tq7s98ihonkju t846 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t845, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl8d110025v)));
                                    if (gm_unw.kind) {
                                        return 1;
                                    }
                                    F.l162 = t846;
                                    uint8_t t847 = (uint8_t)!((((uint8_t *)&F.l162))[0] != 0);
                                    if (!t847) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l162) + 8, gm_srcue4ionbe9lgh, 495, 53);
                                    void * t848 = GM_LD(void *, (((uint8_t *)&F.l162) + 8));
                                    gm_t6evimt84lndq t849;
                                    memset(&t849, 0, sizeof t849);
                                    GM_ST(uint8_t, ((uint8_t *)&t849 + 0), ((uint8_t)7ULL));
                                    GM_ST(void *, ((uint8_t *)&t849 + 16), t848);
                                    gm_ttk92ufj2f6eh t850;
                                    memset(&t850, 0, sizeof t850);
                                    ((uint8_t *)&t850)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t850 + 16), t849);
                                    F.l161 = t850;
                                } else {
                                    gm_t6evimt84lndq t851;
                                    memset(&t851, 0, sizeof t851);
                                    GM_ST(uint8_t, ((uint8_t *)&t851 + 0), ((uint8_t)6ULL));
                                    gm_ttk92ufj2f6eh t852;
                                    memset(&t852, 0, sizeof t852);
                                    ((uint8_t *)&t852)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t852 + 16), t851);
                                    F.l161 = t852;
                                }
                                memmove(gm_out, ((uint8_t *)&F.l161), 112);
                                return 2;
                            }
gm_lb88:;
                            {
                                uint8_t t853 = GM_LD(uint8_t, (((uint8_t *)&F.l50) + 0));
                                uint8_t t854 = (uint8_t)(t853 == ((uint8_t)0ULL));
                                if (!t854) {
                                    goto gm_lb89;
                                }
                                uint8_t t855 = F.l56;
                                uint8_t t856 = (uint8_t)!t855;
                                if (t856) {
                                    void * t857 = F.l0;
                                    unsigned __int128 t858 = F.l51;
                                    gm_t88rcucg1b7uk t859 = F.l11;
                                    gm_tbci5ospk5nnn t860;
                                    gm_f_big_of_1cq0qao((void *)&t860, (void *)NULL, cx, NULL, t857, t858, t859);
                                    if (gm_unw.kind) {
                                        return 1;
                                    }
                                    gm_b16_8 t861 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &t860);
                                    if (gm_unw.kind) {
                                        return 1;
                                    }
                                    F.l57 = t861;
                                    void * t862 = F.l0;
                                    gm_tl874t2atnlh3 t863 = F.l5;
                                    gm_b16_8 t864 = F.l57;
                                    unsigned __int128 t865 = F.l52;
                                    gm_tou0girmhri5g t866;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t866 + 0), t864);
                                    GM_ST(unsigned __int128, ((uint8_t *)&t866 + 16), t865);
                                    gm_f_format_0sujqbp((void *)&F.l163, (void *)NULL, cx, NULL, &t866);
                                    if (gm_unw.kind) {
                                        return 1;
                                    }
                                    uint8_t t867 = (uint8_t)!((((uint8_t *)&F.l163))[0] != 0);
                                    if (!t867) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l163) + 8, gm_srcue4ionbe9lgh, 498, 42);
                                    gm_b16_8 t868 = GM_LD(gm_b16_8, (((uint8_t *)&F.l163) + 8));
                                    gm_b16_8 t869 = GM_LD(gm_b16_8, ((uint8_t *)&t868));
                                    gm_t39uai1ajk3qv t870;
                                    gm_f_trap_jftbss6((void *)&t870, (void *)NULL, cx, NULL, t862, t863, t869);
                                    if (gm_unw.kind) {
                                        return 1;
                                    }
                                    gm_ttk92ufj2f6eh t871;
                                    GM_ST(uint8_t, (uint8_t *)&t871 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t871 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t871 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t871 + 8, 0);
                                    ((uint8_t *)&t871)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t871 + 16), t870);
                                    memmove(gm_out, ((uint8_t *)&t871), 112);
                                    return 2;
                                }
                                goto gm_lb87;
                            }
gm_lb89:;
                            {
                                goto gm_lb87;
                            }
gm_lb90:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 494, 13);
                        }
gm_lb87:;
                        void * t872 = F.l0;
                        gm_t9qkh0rsooc8d t873 = F.l50;
                        gm_t88rcucg1b7uk t874 = F.l54;
                        gm_b16_8 t875;
                        GM_ST(void *, (uint8_t *)&t875, (void *)(gm_svpofgjtij7vj));
                        GM_ST(uintptr_t, (uint8_t *)&t875 + 8, (uintptr_t)(3));
                        gm_tl874t2atnlh3 t876 = F.l5;
                        gm_tbci5ospk5nnn t877 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l53));
                        gm_f_fit_8hvi4bv((void *)&F.l164, (void *)NULL, cx, NULL, t872, &t877, t873, t874, t875, t876);
                        if (gm_unw.kind) {
                            return 1;
                        }
                        uint8_t t878 = (uint8_t)!((((uint8_t *)&F.l164))[0] != 0);
                        if (t878) {
                        } else {
                            gm_t39uai1ajk3qv t879 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l164) + 16));
                            gm_ttk92ufj2f6eh t880;
                            GM_ST(uint8_t, (uint8_t *)&t880 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t880 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t880 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t880 + 8, 0);
                            ((uint8_t *)&t880)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t880 + 16), t879);
                            memmove(gm_out, ((uint8_t *)&t880), 112);
                            return 2;
                        }
                        unsigned __int128 t881 = GM_LD(unsigned __int128, (((uint8_t *)&F.l164) + 16));
                        gm_t6evimt84lndq t882;
                        GM_ST(uint8_t, (uint8_t *)&t882 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t882 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t882 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t882 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t882 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t882 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t882 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t882 + 16), t881);
                        gm_ttk92ufj2f6eh t883;
                        memset(&t883, 0, sizeof t883);
                        ((uint8_t *)&t883)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t883 + 16), t882);
                        memmove(gm_out, ((uint8_t *)&t883), 112);
                        return 2;
                    }
gm_lb86:;
                    void * t884 = F.l0;
                    gm_tl874t2atnlh3 t885 = F.l5;
                    gm_b16_8 t886;
                    GM_ST(void *, (uint8_t *)&t886, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t886 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t887;
                    gm_f_unsupported_2seq49l((void *)&t887, t884, GM_CONV(gm_cseahvg8v7p0vs, t885), t886);
                    gm_ttk92ufj2f6eh t888;
                    GM_ST(uint8_t, (uint8_t *)&t888 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t888 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t888 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t888 + 8, 0);
                    ((uint8_t *)&t888)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t888 + 16), t887);
                    memmove(gm_out, ((uint8_t *)&t888), 112);
                    return 2;
                }
gm_lb85:;
            }
gm_lb84:;
            void * t889 = F.l0;
            gm_tl874t2atnlh3 t890 = F.l5;
            gm_b16_8 t891;
            GM_ST(void *, (uint8_t *)&t891, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t891 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t892;
            gm_f_unsupported_2seq49l((void *)&t892, t889, GM_CONV(gm_cseahvg8v7p0vs, t890), t891);
            gm_ttk92ufj2f6eh t893;
            GM_ST(uint8_t, (uint8_t *)&t893 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t893 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t893 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t893 + 8, 0);
            ((uint8_t *)&t893)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t893 + 16), t892);
            memmove(gm_out, ((uint8_t *)&t893), 112);
            return 2;
        }
gm_lb83:;
    }
gm_lb82:;
    {
        uint8_t t894 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t895 = (uint8_t)(t894 == ((uint8_t)22ULL));
        if (!t895) {
            goto gm_lb91;
        }
        {
            {
                uint8_t t896 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t897 = (uint8_t)(t896 == ((uint8_t)1ULL));
                if (!t897) {
                    goto gm_lb93;
                }
                double t898 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l58 = t898;
                double t899 = F.l58;
                double t900 = F.l58;
                uint8_t t901 = (!(t899 == t900));
                gm_t6evimt84lndq t902;
                memset(&t902, 0, sizeof t902);
                GM_ST(uint8_t, ((uint8_t *)&t902 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t902 + 16), t901);
                gm_ttk92ufj2f6eh t903;
                memset(&t903, 0, sizeof t903);
                ((uint8_t *)&t903)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t903 + 16), t902);
                memmove(gm_out, ((uint8_t *)&t903), 112);
                return 2;
            }
gm_lb93:;
            void * t904 = F.l0;
            gm_tl874t2atnlh3 t905 = F.l5;
            gm_b16_8 t906;
            GM_ST(void *, (uint8_t *)&t906, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t906 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t907;
            gm_f_unsupported_2seq49l((void *)&t907, t904, GM_CONV(gm_cseahvg8v7p0vs, t905), t906);
            gm_ttk92ufj2f6eh t908;
            GM_ST(uint8_t, (uint8_t *)&t908 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t908 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t908 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t908 + 8, 0);
            ((uint8_t *)&t908)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t908 + 16), t907);
            memmove(gm_out, ((uint8_t *)&t908), 112);
            return 2;
        }
gm_lb92:;
    }
gm_lb91:;
    {
        uint8_t t909 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t910 = (uint8_t)(t909 == ((uint8_t)23ULL));
        if (!t910) {
            goto gm_lb94;
        }
        {
            {
                uint8_t t911 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t912 = (uint8_t)(t911 == ((uint8_t)1ULL));
                if (!t912) {
                    goto gm_lb96;
                }
                double t913 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l59 = t913;
                double t914 = F.l59;
                double t915 = F.l59;
                uint8_t t916 = ((t914 == t915));
                if (t916) {
                    double t917 = F.l59;
                    double t918 = F.l59;
                    double t919 = (t917 - t918);
                    uint8_t t920 = (!(t919 == ((double)0e0)));
                    F.l165 = t920;
                } else {
                    F.l165 = ((uint8_t)0);
                }
                uint8_t t921 = F.l165;
                gm_t6evimt84lndq t922;
                memset(&t922, 0, sizeof t922);
                GM_ST(uint8_t, ((uint8_t *)&t922 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t922 + 16), t921);
                gm_ttk92ufj2f6eh t923;
                memset(&t923, 0, sizeof t923);
                ((uint8_t *)&t923)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t923 + 16), t922);
                memmove(gm_out, ((uint8_t *)&t923), 112);
                return 2;
            }
gm_lb96:;
            void * t924 = F.l0;
            gm_tl874t2atnlh3 t925 = F.l5;
            gm_b16_8 t926;
            GM_ST(void *, (uint8_t *)&t926, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t926 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t927;
            gm_f_unsupported_2seq49l((void *)&t927, t924, GM_CONV(gm_cseahvg8v7p0vs, t925), t926);
            gm_ttk92ufj2f6eh t928;
            GM_ST(uint8_t, (uint8_t *)&t928 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t928 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t928 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t928 + 8, 0);
            ((uint8_t *)&t928)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t928 + 16), t927);
            memmove(gm_out, ((uint8_t *)&t928), 112);
            return 2;
        }
gm_lb95:;
    }
gm_lb94:;
    {
        uint8_t t929 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t930 = (uint8_t)(t929 == ((uint8_t)24ULL));
        if (!t930) {
            goto gm_lb97;
        }
        {
            {
                uint8_t t931 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t932 = (uint8_t)(t931 == ((uint8_t)1ULL));
                if (!t932) {
                    goto gm_lb99;
                }
                double t933 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l60 = t933;
                double t934 = F.l60;
                double t935 = F.l60;
                double t936 = (t934 - t935);
                uint8_t t937 = ((t936 == ((double)0e0)));
                gm_t6evimt84lndq t938;
                memset(&t938, 0, sizeof t938);
                GM_ST(uint8_t, ((uint8_t *)&t938 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t938 + 16), t937);
                gm_ttk92ufj2f6eh t939;
                memset(&t939, 0, sizeof t939);
                ((uint8_t *)&t939)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t939 + 16), t938);
                memmove(gm_out, ((uint8_t *)&t939), 112);
                return 2;
            }
gm_lb99:;
            void * t940 = F.l0;
            gm_tl874t2atnlh3 t941 = F.l5;
            gm_b16_8 t942;
            GM_ST(void *, (uint8_t *)&t942, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t942 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t943;
            gm_f_unsupported_2seq49l((void *)&t943, t940, GM_CONV(gm_cseahvg8v7p0vs, t941), t942);
            gm_ttk92ufj2f6eh t944;
            GM_ST(uint8_t, (uint8_t *)&t944 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t944 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t944 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t944 + 8, 0);
            ((uint8_t *)&t944)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t944 + 16), t943);
            memmove(gm_out, ((uint8_t *)&t944), 112);
            return 2;
        }
gm_lb98:;
    }
gm_lb97:;
    {
        uint8_t t945 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t946 = (uint8_t)(t945 == ((uint8_t)25ULL));
        if (!t946) {
            goto gm_lb100;
        }
        {
            {
                uint8_t t947 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t948 = (uint8_t)(t947 == ((uint8_t)1ULL));
                if (!t948) {
                    goto gm_lb102;
                }
                double t949 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                F.l61 = t949;
                double t950 = F.l61;
                uint64_t t951;
                memset(&t951, 0, sizeof t951); memcpy(&t951, ((uint8_t *)&t950), sizeof t951 < 8 ? sizeof t951 : 8);
                unsigned __int128 n952 = (unsigned __int128)((uint64_t)63ULL);
                if (n952 >= 64) gm_trap_shift((uint64_t)n952, "u64", gm_srcue4ionbe9lgh, 518, 26);
                uint64_t t953 = (uint64_t)(t951 >> (int)n952);
                uint8_t t954 = ((t953 == ((uint64_t)1ULL)));
                gm_t6evimt84lndq t955;
                memset(&t955, 0, sizeof t955);
                GM_ST(uint8_t, ((uint8_t *)&t955 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t955 + 16), t954);
                gm_ttk92ufj2f6eh t956;
                memset(&t956, 0, sizeof t956);
                ((uint8_t *)&t956)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t956 + 16), t955);
                memmove(gm_out, ((uint8_t *)&t956), 112);
                return 2;
            }
gm_lb102:;
            void * t957 = F.l0;
            gm_tl874t2atnlh3 t958 = F.l5;
            gm_b16_8 t959;
            GM_ST(void *, (uint8_t *)&t959, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t959 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t960;
            gm_f_unsupported_2seq49l((void *)&t960, t957, GM_CONV(gm_cseahvg8v7p0vs, t958), t959);
            gm_ttk92ufj2f6eh t961;
            GM_ST(uint8_t, (uint8_t *)&t961 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t961 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t961 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t961 + 8, 0);
            ((uint8_t *)&t961)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t961 + 16), t960);
            memmove(gm_out, ((uint8_t *)&t961), 112);
            return 2;
        }
gm_lb101:;
    }
gm_lb100:;
    {
        uint8_t t962 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        uint8_t t963 = (uint8_t)(t962 == ((uint8_t)14ULL));
        if (!t963) {
            goto gm_lb103;
        }
        {
            {
                uint8_t t964 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t965 = (uint8_t)(t964 == ((uint8_t)10ULL));
                if (!t965) {
                    goto gm_lb105;
                }
                uint32_t t966 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 16));
                F.l62 = t966;
                void * t967 = F.l0;
                gm_t88rcucg1b7uk t968 = F.l11;
                uint32_t t969 = F.l62;
                gm_f_variant_tag_4pbamin((void *)&F.l166, t967, GM_CONV(gm_csguankko4lmhd, t968), t969);
                uint8_t t970 = (uint8_t)((((uint8_t *)&F.l166))[48] != 0);
                if (t970) {
                    memmove(((uint8_t *)&F.l167), ((uint8_t *)&F.l166), 48);
                } else {
                    void * t971 = F.l0;
                    gm_tl874t2atnlh3 t972 = F.l5;
                    gm_b16_8 t973;
                    GM_ST(void *, (uint8_t *)&t973, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t973 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t974;
                    gm_f_unsupported_2seq49l((void *)&t974, t971, GM_CONV(gm_cseahvg8v7p0vs, t972), t973);
                    gm_ttk92ufj2f6eh t975;
                    GM_ST(uint8_t, (uint8_t *)&t975 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t975 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t975 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t975 + 8, 0);
                    ((uint8_t *)&t975)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t975 + 16), t974);
                    memmove(gm_out, ((uint8_t *)&t975), 112);
                    return 2;
                }
                memmove(((uint8_t *)&F.l63), ((uint8_t *)&F.l167), 48);
                void * t976 = F.l0;
                unsigned __int128 t977 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l63)));
                void * t978 = F.l3;
                gm_t88rcucg1b7uk t979 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t978));
                unsigned __int128 t980 = gm_f_wrap_bt8r149(t976, t977, GM_CONV(gm_csguankko4lmhd, t979));
                gm_t6evimt84lndq t981;
                GM_ST(uint8_t, (uint8_t *)&t981 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t981 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t981 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t981 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t981 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t981 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t981 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t981 + 16), t980);
                gm_ttk92ufj2f6eh t982;
                memset(&t982, 0, sizeof t982);
                ((uint8_t *)&t982)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t982 + 16), t981);
                memmove(gm_out, ((uint8_t *)&t982), 112);
                return 2;
            }
gm_lb105:;
            void * t983 = F.l0;
            gm_tl874t2atnlh3 t984 = F.l5;
            gm_b16_8 t985;
            GM_ST(void *, (uint8_t *)&t985, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t985 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t986;
            gm_f_unsupported_2seq49l((void *)&t986, t983, GM_CONV(gm_cseahvg8v7p0vs, t984), t985);
            gm_ttk92ufj2f6eh t987;
            GM_ST(uint8_t, (uint8_t *)&t987 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t987 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t987 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t987 + 8, 0);
            ((uint8_t *)&t987)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t987 + 16), t986);
            memmove(gm_out, ((uint8_t *)&t987), 112);
            return 2;
        }
gm_lb104:;
    }
gm_lb103:;
    {
        {
            {
                uint8_t t988 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t989 = (uint8_t)(t988 == ((uint8_t)36ULL));
                if (!t989) {
                    goto gm_lb108;
                }
                goto gm_lb107;
            }
gm_lb108:;
            {
                uint8_t t990 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t991 = (uint8_t)(t990 == ((uint8_t)37ULL));
                if (!t991) {
                    goto gm_lb109;
                }
                goto gm_lb107;
            }
gm_lb109:;
            {
                uint8_t t992 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t993 = (uint8_t)(t992 == ((uint8_t)38ULL));
                if (!t993) {
                    goto gm_lb110;
                }
                goto gm_lb107;
            }
gm_lb110:;
            {
                uint8_t t994 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t995 = (uint8_t)(t994 == ((uint8_t)39ULL));
                if (!t995) {
                    goto gm_lb111;
                }
                goto gm_lb107;
            }
gm_lb111:;
            goto gm_lb106;
        }
gm_lb107:;
        F.l64 = ((uint64_t)0ULL);
        gm_tab260m0n3sqj t996;
        memset(&t996, 0, sizeof t996);
        F.l65 = t996;
        {
            {
                uint8_t t997 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t998 = (uint8_t)(t997 == ((uint8_t)3ULL));
                if (!t998) {
                    goto gm_lb113;
                }
                void * t999 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
                F.l66 = t999;
                void * t1000 = F.l66;
                uint64_t t1001 = GM_LD(uint64_t, (((uint8_t *)t1000) + 8));
                uint64_t t1002 = (uint64_t)t1001;
                F.l64 = t1002;
                goto gm_lb112;
            }
gm_lb113:;
            {
                uint8_t t1003 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t1004 = (uint8_t)(t1003 == ((uint8_t)5ULL));
                if (!t1004) {
                    goto gm_lb114;
                }
                memmove(((uint8_t *)&F.l67), (((uint8_t *)&F.l10) + 16), 32);
                uint64_t t1005 = GM_LD(uint64_t, (((uint8_t *)&F.l67) + 24));
                F.l64 = t1005;
                gm_tkaapcii86udk t1006 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&F.l67));
                gm_tab260m0n3sqj t1007;
                GM_ST(uint8_t, (uint8_t *)&t1007 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t1007 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t1007 + 28, 0);
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1007), t1006);
                ((uint8_t *)&t1007)[24] = 1;
                F.l65 = t1007;
                goto gm_lb112;
            }
gm_lb114:;
            {
                void * t1008 = F.l0;
                gm_tl874t2atnlh3 t1009 = F.l5;
                gm_b16_8 t1010;
                GM_ST(void *, (uint8_t *)&t1010, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1010 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1011;
                gm_f_unsupported_2seq49l((void *)&t1011, t1008, GM_CONV(gm_cseahvg8v7p0vs, t1009), t1010);
                gm_ttk92ufj2f6eh t1012;
                GM_ST(uint8_t, (uint8_t *)&t1012 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1012 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1012 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1012 + 8, 0);
                ((uint8_t *)&t1012)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1012 + 16), t1011);
                memmove(gm_out, ((uint8_t *)&t1012), 112);
                return 2;
            }
gm_lb115:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 528, 13);
        }
gm_lb112:;
        {
            {
                uint8_t t1013 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t1014 = (uint8_t)(t1013 == ((uint8_t)38ULL));
                if (!t1014) {
                    goto gm_lb117;
                }
                F.l168 = ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                goto gm_lb116;
            }
gm_lb117:;
            {
                uint8_t t1015 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                uint8_t t1016 = (uint8_t)(t1015 == ((uint8_t)39ULL));
                if (!t1016) {
                    goto gm_lb118;
                }
                uint64_t t1017 = F.l64;
                __int128 t1018 = (__int128)t1017;
                __int128 r1019; bool o1020 = __builtin_sub_overflow(t1018, ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r1019);
                if (o1020) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 538, 26);
                __int128 t1021 = r1019;
                F.l168 = t1021;
                goto gm_lb116;
            }
gm_lb118:;
            {
                {
                    {
                        void * t1022 = GM_LD(void *, ((uint8_t *)&F.l9));
                        uint64_t t1023 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1023)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1023, gm_srcue4ionbe9lgh, 540, 35);
                        uint8_t t1024 = GM_LD(uint8_t, ((((uint8_t *)t1022) + (uintptr_t)((uint64_t)1ULL) * 48) + 0));
                        uint8_t t1025 = (uint8_t)(t1024 == ((uint8_t)0ULL));
                        if (!t1025) {
                            goto gm_lb121;
                        }
                        unsigned __int128 t1026 = GM_LD(unsigned __int128, ((((uint8_t *)t1022) + (uintptr_t)((uint64_t)1ULL) * 48) + 16));
                        F.l68 = t1026;
                        void * t1027 = F.l0;
                        unsigned __int128 t1028 = F.l68;
                        void * t1029 = GM_LD(void *, ((uint8_t *)&F.l2));
                        uint64_t t1030 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1030)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1030, gm_srcue4ionbe9lgh, 541, 39);
                        gm_t88rcucg1b7uk t1031 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1029) + (uintptr_t)((uint64_t)1ULL) * 88));
                        __int128 t1032 = gm_f_int_value_6hmh8tj(t1027, t1028, GM_CONV(gm_csguankko4lmhd, t1031));
                        F.l169 = t1032;
                        goto gm_lb120;
                    }
gm_lb121:;
                    void * t1033 = F.l0;
                    gm_tl874t2atnlh3 t1034 = F.l5;
                    gm_b16_8 t1035;
                    GM_ST(void *, (uint8_t *)&t1035, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t1035 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t1036;
                    gm_f_unsupported_2seq49l((void *)&t1036, t1033, GM_CONV(gm_cseahvg8v7p0vs, t1034), t1035);
                    gm_ttk92ufj2f6eh t1037;
                    GM_ST(uint8_t, (uint8_t *)&t1037 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1037 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1037 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1037 + 8, 0);
                    ((uint8_t *)&t1037)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1037 + 16), t1036);
                    memmove(gm_out, ((uint8_t *)&t1037), 112);
                    return 2;
                    F.l169 = ((__int128)0);
                }
gm_lb120:;
                __int128 t1038 = F.l169;
                F.l168 = t1038;
                goto gm_lb116;
            }
gm_lb119:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 536, 23);
        }
gm_lb116:;
        __int128 t1039 = F.l168;
        F.l69 = t1039;
        __int128 t1040 = F.l69;
        uint8_t t1041 = (t1040 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
        if (t1041) {
            F.l170 = ((uint8_t)1);
        } else {
            __int128 t1042 = F.l69;
            uint64_t t1043 = F.l64;
            __int128 t1044 = (__int128)t1043;
            uint8_t t1045 = (t1042 >= t1044);
            F.l170 = t1045;
        }
        uint8_t t1046 = F.l170;
        if (t1046) {
            gm_t6evimt84lndq t1047;
            memset(&t1047, 0, sizeof t1047);
            GM_ST(uint8_t, ((uint8_t *)&t1047 + 0), ((uint8_t)6ULL));
            gm_ttk92ufj2f6eh t1048;
            memset(&t1048, 0, sizeof t1048);
            ((uint8_t *)&t1048)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1048 + 16), t1047);
            memmove(gm_out, ((uint8_t *)&t1048), 112);
            return 2;
        }
        __int128 t1049 = F.l69;
        uint64_t t1050 = (uint64_t)t1049;
        F.l70 = t1050;
        gm_tvdg94bk7aa22 t1051 = F.l1;
        uint8_t t1052 = GM_LD(uint8_t, (((uint8_t *)&t1051) + 0));
        uint8_t t1053 = (uint8_t)((t1052 == ((uint8_t)37ULL)));
        gm_tvdg94bk7aa22 t1054;
        GM_ST(uint8_t, (uint8_t *)&t1054 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t1054 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t1054 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t1054 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t1054 + 0), ((uint8_t)37ULL));
        if (t1053) {
            {
                {
                    uint8_t t1055 = (uint8_t)((((uint8_t *)&F.l65))[24] != 0);
                    if (!t1055) {
                        goto gm_lb123;
                    }
                    memmove(((uint8_t *)&F.l71), ((uint8_t *)&F.l65), 24);
                    uint64_t t1056 = F.l70;
                    gm_tkaapcii86udk t1057;
                    gm_f_offset_nkn711s((void *)&t1057, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&F.l71)), t1056);
                    if (gm_unw.kind) {
                        return 1;
                    }
                    gm_t6evimt84lndq t1058;
                    GM_ST(uint8_t, (uint8_t *)&t1058 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1058 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1058 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1058 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1058 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1058 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1058 + 16), t1057);
                    gm_tq7s98ihonkju t1059 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1058, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6rrrg8di6i)));
                    if (gm_unw.kind) {
                        return 1;
                    }
                    F.l171 = t1059;
                    uint8_t t1060 = (uint8_t)!((((uint8_t *)&F.l171))[0] != 0);
                    if (!t1060) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l171) + 8, gm_srcue4ionbe9lgh, 547, 47);
                    void * t1061 = GM_LD(void *, (((uint8_t *)&F.l171) + 8));
                    gm_t6evimt84lndq t1062;
                    memset(&t1062, 0, sizeof t1062);
                    GM_ST(uint8_t, ((uint8_t *)&t1062 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t1062 + 16), t1061);
                    gm_ttk92ufj2f6eh t1063;
                    memset(&t1063, 0, sizeof t1063);
                    ((uint8_t *)&t1063)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1063 + 16), t1062);
                    memmove(gm_out, ((uint8_t *)&t1063), 112);
                    return 2;
                }
gm_lb123:;
            }
gm_lb122:;
            void * t1064 = F.l0;
            void * t1065 = GM_LD(void *, ((uint8_t *)&F.l2));
            uint64_t t1066 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t1066)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t1066, gm_srcue4ionbe9lgh, 548, 36);
            void * t1067 = (void *)(((uint8_t *)t1065) + (uintptr_t)((uint64_t)0ULL) * 88);
            gm_f_place_9889dgp((void *)&F.l172, (void *)NULL, cx, NULL, t1064, t1067);
            if (gm_unw.kind) {
                return 1;
            }
            uint8_t t1068 = (uint8_t)!((((uint8_t *)&F.l172))[0] != 0);
            if (t1068) {
            } else {
                gm_t39uai1ajk3qv t1069 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l172) + 16));
                gm_ttk92ufj2f6eh t1070;
                GM_ST(uint8_t, (uint8_t *)&t1070 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1070 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1070 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1070 + 8, 0);
                ((uint8_t *)&t1070)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1070 + 16), t1069);
                memmove(gm_out, ((uint8_t *)&t1070), 112);
                return 2;
            }
            memmove(((uint8_t *)&F.l72), (((uint8_t *)&F.l172) + 16), 24);
            void * t1071 = GM_LD(void *, ((uint8_t *)&F.l72));
            gm_b16_8 t1072 = GM_LD(gm_b16_8, (((uint8_t *)&F.l72) + 8));
            uint64_t t1073 = F.l70;
            gm_b16_8 t1074 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t1072, t1073);
            if (gm_unw.kind) {
                return 1;
            }
            gm_tkaapcii86udk t1075;
            GM_ST(void *, ((uint8_t *)&t1075 + 0), t1071);
            GM_ST(gm_b16_8, ((uint8_t *)&t1075 + 8), t1074);
            gm_t6evimt84lndq t1076;
            GM_ST(uint8_t, (uint8_t *)&t1076 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1076 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1076 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1076 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t1076 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t1076 + 0), ((uint8_t)4ULL));
            GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1076 + 16), t1075);
            gm_tq7s98ihonkju t1077 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1076, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kb0gdpup6ei)));
            if (gm_unw.kind) {
                return 1;
            }
            F.l173 = t1077;
            uint8_t t1078 = (uint8_t)!((((uint8_t *)&F.l173))[0] != 0);
            if (!t1078) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l173) + 8, gm_srcue4ionbe9lgh, 549, 30);
            void * t1079 = GM_LD(void *, (((uint8_t *)&F.l173) + 8));
            gm_t6evimt84lndq t1080;
            memset(&t1080, 0, sizeof t1080);
            GM_ST(uint8_t, ((uint8_t *)&t1080 + 0), ((uint8_t)7ULL));
            GM_ST(void *, ((uint8_t *)&t1080 + 16), t1079);
            gm_ttk92ufj2f6eh t1081;
            memset(&t1081, 0, sizeof t1081);
            ((uint8_t *)&t1081)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1081 + 16), t1080);
            memmove(gm_out, ((uint8_t *)&t1081), 112);
            return 2;
        }
        {
            {
                uint8_t t1082 = (uint8_t)((((uint8_t *)&F.l65))[24] != 0);
                if (!t1082) {
                    goto gm_lb125;
                }
                memmove(((uint8_t *)&F.l73), ((uint8_t *)&F.l65), 24);
                void * t1083 = F.l0;
                uint64_t t1084 = F.l70;
                gm_tkaapcii86udk t1085;
                gm_f_offset_nkn711s((void *)&t1085, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&F.l73)), t1084);
                if (gm_unw.kind) {
                    return 1;
                }
                gm_f_load_nf7o5kl((void *)&F.l174, t1083, &t1085);
                goto gm_lb124;
            }
gm_lb125:;
            {
                {
                    uint8_t t1086 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                    uint8_t t1087 = (uint8_t)(t1086 == ((uint8_t)3ULL));
                    if (!t1087) {
                        goto gm_lb127;
                    }
                    void * t1088 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
                    F.l74 = t1088;
                    void * t1089 = F.l74;
                    void * t1090 = GM_LD(void *, ((uint8_t *)t1089));
                    uint64_t t1091 = GM_LD(uint64_t, (((uint8_t *)t1089) + 8));
                    uint64_t t1092 = F.l70;
                    uint64_t t1093 = (uint64_t)t1092;
                    if ((uintptr_t)t1093 >= (uintptr_t)(t1091)) gm_trap_index((uintptr_t)t1093, t1091, gm_srcue4ionbe9lgh, 553, 17);
                    memmove(((uint8_t *)&F.l175), (((uint8_t *)t1090) + (uintptr_t)t1093 * 48), 48);
                    goto gm_lb126;
                }
gm_lb127:;
                void * t1094 = F.l0;
                gm_tl874t2atnlh3 t1095 = F.l5;
                gm_b16_8 t1096;
                GM_ST(void *, (uint8_t *)&t1096, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1096 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1097;
                gm_f_unsupported_2seq49l((void *)&t1097, t1094, GM_CONV(gm_cseahvg8v7p0vs, t1095), t1096);
                gm_ttk92ufj2f6eh t1098;
                GM_ST(uint8_t, (uint8_t *)&t1098 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1098 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1098 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1098 + 8, 0);
                ((uint8_t *)&t1098)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1098 + 16), t1097);
                memmove(gm_out, ((uint8_t *)&t1098), 112);
                return 2;
                F.l175 = GM_ZERO(gm_t6evimt84lndq);
            }
gm_lb126:;
            memmove(((uint8_t *)&F.l174), ((uint8_t *)&F.l175), 48);
        }
gm_lb124:;
        memmove(((uint8_t *)&F.l75), ((uint8_t *)&F.l174), 48);
        gm_t6evimt84lndq t1099;
        gm_f_copy_nk4jid6((void *)&t1099, (void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l75)));
        if (gm_unw.kind) {
            return 1;
        }
        gm_tq7s98ihonkju t1100 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1099, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klc17hcqkii)));
        if (gm_unw.kind) {
            return 1;
        }
        F.l176 = t1100;
        uint8_t t1101 = (uint8_t)!((((uint8_t *)&F.l176))[0] != 0);
        if (!t1101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l176) + 8, gm_srcue4ionbe9lgh, 555, 26);
        void * t1102 = GM_LD(void *, (((uint8_t *)&F.l176) + 8));
        gm_t6evimt84lndq t1103;
        memset(&t1103, 0, sizeof t1103);
        GM_ST(uint8_t, ((uint8_t *)&t1103 + 0), ((uint8_t)7ULL));
        GM_ST(void *, ((uint8_t *)&t1103 + 16), t1102);
        gm_ttk92ufj2f6eh t1104;
        memset(&t1104, 0, sizeof t1104);
        ((uint8_t *)&t1104)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1104 + 16), t1103);
        memmove(gm_out, ((uint8_t *)&t1104), 112);
        return 2;
    }
gm_lb106:;
    return 0;
#undef F
}
extern __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_big_of_1cq0qao(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, unsigned __int128 a1, gm_t88rcucg1b7uk a2);
extern __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_bigint_i128_p6m54kv_c(gm_tbci5ospk5nnn a0) { gm_ta24rd13gupia gm_r; gm_f_bigint_i128_p6m54kv(&gm_r, &a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_binary_fmoblrg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_binary_fmoblrg F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_tlcm4lvu3g1a8 gm_l7; /* lp */
    void * gm_l8; /* xa */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l15; /* ya */
    gm_t88rcucg1b7uk gm_l16; /* rlane */
    GM_STACK_CHECK(&F, 1682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_tb7u19loo3j87 gm_l17;
    gm_thf8as07kj3uj gm_l18;
    gm_t88rcucg1b7uk gm_l19;
    uint8_t gm_l20;
    gm_tb7u19loo3j87 gm_l21;
    gm_ttk92ufj2f6eh gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tq7s98ihonkju gm_l24;
    {
        {
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, ((uint8_t *)t16));
            void * t18 = (void *)(((uint8_t *)t17) + 8);
            gm_t88rcucg1b7uk t19 = gm_l4;
            gm_f_vector_parts_crrfopm((void *)&gm_l17, t18, GM_CONV(gm_csguankko4lmhd, t19));
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            gm_tlcm4lvu3g1a8 t21 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l17));
            gm_l7 = t21;
            {
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)3ULL));
                    if (!t23) {
                        goto gm_lb4;
                    }
                    void * t24 = GM_LD(void *, (((uint8_t *)a2) + 16));
                    gm_l8 = t24;
                    gm_trqfvmjtt4sij t25;
                    gm_f_list_0202sdh_2((void *)&t25, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l9 = t25;
                    void * t26 = gm_l8;
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)t26));
                    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                    gm_l10 = t28;
                    gm_l11 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t29 = gm_l11;
                            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint8_t t31 = (t29 < t30);
                            if (!t31) {
                                goto gm_lb5;
                            }
                            {
                                uint64_t t32 = gm_l11;
                                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t35 = gm_l11;
                                gm_t6evimt84lndq t36 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t33) + (uintptr_t)t35 * 48));
                                gm_thf8as07kj3uj t37;
                                GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t37 + 0), t32);
                                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t37 + 16), t36);
                                gm_l18 = t37;
                                {
                                    {
                                        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                                        gm_l12 = t38;
                                        memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l18) + 16), 48);
                                        memmove(((uint8_t *)&F.l14), ((uint8_t *)a3), 48);
                                        {
                                            {
                                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
                                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)3ULL));
                                                if (!t40) {
                                                    goto gm_lb11;
                                                }
                                                void * t41 = GM_LD(void *, (((uint8_t *)a3) + 16));
                                                gm_l15 = t41;
                                                void * t42 = gm_l15;
                                                void * t43 = GM_LD(void *, ((uint8_t *)t42));
                                                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t42) + 8));
                                                uint64_t t45 = gm_l12;
                                                if ((uintptr_t)t45 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t45, t44, gm_srcue4ionbe9lgh, 113, 36);
                                                memmove(((uint8_t *)&F.l14), (((uint8_t *)t43) + (uintptr_t)t45 * 48), 48);
                                                goto gm_lb10;
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        void * t46 = gm_l0;
                                        void * t47 = GM_LD(void *, ((uint8_t *)t46));
                                        void * t48 = (void *)(((uint8_t *)t47) + 8);
                                        gm_t88rcucg1b7uk t49 = gm_l5;
                                        gm_f_vector_parts_crrfopm((void *)&gm_l21, t48, GM_CONV(gm_csguankko4lmhd, t49));
                                        {
                                            {
                                                uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
                                                if (t50) {
                                                    goto gm_lb13;
                                                }
                                                gm_l20 = ((uint8_t)0);
                                                goto gm_lb12;
                                            }
gm_lb13:;
                                            {
                                                gm_l20 = ((uint8_t)1);
                                                goto gm_lb12;
                                            }
gm_lb14:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 114, 25);
                                        }
gm_lb12:;
                                        uint8_t t51 = gm_l20;
                                        if (t51) {
                                            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                                            gm_l19 = t52;
                                        } else {
                                            gm_t88rcucg1b7uk t53 = gm_l5;
                                            gm_l19 = t53;
                                        }
                                        gm_t88rcucg1b7uk t54 = gm_l19;
                                        gm_l16 = t54;
                                        void * t55 = (void *)((uint8_t *)&F.l9);
                                        void * t56 = gm_l0;
                                        gm_ts6oqppjh0ba3 t57 = gm_l1;
                                        gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                                        gm_t88rcucg1b7uk t59 = gm_l16;
                                        gm_tl874t2atnlh3 t60 = gm_l6;
                                        gm_t6evimt84lndq t61 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l13));
                                        gm_t6evimt84lndq t62 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l14));
                                        gm_f_scalar_binary_bvc65kt((void *)&gm_l22, (void *)NULL, cx, NULL, t56, t57, &t61, &t62, t58, t59, t60);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                        if (t63) {
                                        } else {
                                            gm_t39uai1ajk3qv t64 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l22) + 16));
                                            gm_ttk92ufj2f6eh t65;
                                            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t65 + 8, 0);
                                            ((uint8_t *)&t65)[0] = 1;
                                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t65 + 16), t64);
                                            memmove(gm_out, ((uint8_t *)&t65), 112);
                                            goto gm_fdone;
                                        }
                                        gm_tftb1odc739t5 t66 = gm_f_push_0pgacsj_2(t55, &(*(gm_t6evimt84lndq *)(((uint8_t *)&gm_l22) + 16)));
                                        gm_l23 = t66;
                                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                        if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcue4ionbe9lgh, 115, 13);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 111, 9);
                                }
gm_lb8:;
                            }
gm_lb7:;
                            uint64_t t68 = gm_l11;
                            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                            uint64_t t71 = r69;
                            gm_l11 = t71;
                        }
                    }
gm_lb5:;
                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                    gm_tnpifv20irnph t73;
                    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                    gm_tq7s98ihonkju t74 = gm_f_new_lcff781((void *)NULL, cx, NULL, t73, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kha5d1oq7pb)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t74;
                    uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcue4ionbe9lgh, 117, 21);
                    void * t76 = GM_LD(void *, (((uint8_t *)&gm_l24) + 8));
                    gm_t6evimt84lndq t77;
                    memset(&t77, 0, sizeof t77);
                    GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)3ULL));
                    GM_ST(void *, ((uint8_t *)&t77 + 16), t76);
                    gm_ttk92ufj2f6eh t78;
                    memset(&t78, 0, sizeof t78);
                    ((uint8_t *)&t78)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t78 + 16), t77);
                    memmove(gm_out, ((uint8_t *)&t78), 112);
                    goto gm_fdone;
                }
gm_lb4:;
                void * t79 = gm_l0;
                gm_tl874t2atnlh3 t80 = gm_l6;
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_s0dggtblbl7jq));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(29));
                gm_t39uai1ajk3qv t82;
                gm_f_unsupported_2seq49l((void *)&t82, t79, GM_CONV(gm_cseahvg8v7p0vs, t80), t81);
                gm_ttk92ufj2f6eh t83;
                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t83 + 8, 0);
                ((uint8_t *)&t83)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t83 + 16), t82);
                memmove(gm_out, ((uint8_t *)&t83), 112);
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t84 = gm_l0;
    gm_ts6oqppjh0ba3 t85 = gm_l1;
    gm_t88rcucg1b7uk t86 = gm_l4;
    gm_t88rcucg1b7uk t87 = gm_l5;
    gm_tl874t2atnlh3 t88 = gm_l6;
    gm_t6evimt84lndq t89 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a2));
    gm_t6evimt84lndq t90 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a3));
    gm_f_scalar_binary_bvc65kt((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t84, t85, &t89, &t90, t86, t87, t88);
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
extern __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) void gm_f_bits_of_hithcdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_bits_of_hithcdi F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* b */
    unsigned __int128 gm_l4; /* raw */
    uint32_t gm_l5; /* index */
    void * gm_l7; /* a */
    gm_tcmkompbl09il gm_l8; /* bl */
    unsigned __int128 gm_l9; /* r */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_tov4mtrfp5e7v gm_l13; /* f */
    unsigned __int128 gm_l14; /* x */
    GM_STACK_CHECK(&F, 1311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t9p38b040grms gm_l15;
    gm_thrdpodrs8pgt gm_l16;
    gm_tbci5ospk5nnn gm_l17;
    gm_t57euh612ligr gm_l18;
    gm_tcmkompbl09il gm_l19;
    gm_tji1ull97ruec gm_l20;
    gm_t9p38b040grms gm_l21;
    unsigned __int128 gm_l22;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)2ULL));
            if (!t14) {
                goto gm_lb2;
            }
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
            gm_l3 = t15;
            uint8_t t16 = gm_l3;
            if (t16) {
                gm_t9p38b040grms t17;
                GM_ST(uint8_t, (uint8_t *)&t17 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
                GM_ST(unsigned __int128, ((uint8_t *)&t17), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
                ((uint8_t *)&t17)[16] = 1;
                gm_l15 = t17;
            } else {
                gm_t9p38b040grms t18;
                GM_ST(uint8_t, (uint8_t *)&t18 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t18 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t18 + 20, 0);
                GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
                GM_ST(unsigned __int128, ((uint8_t *)&t18), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                ((uint8_t *)&t18)[16] = 1;
                gm_l15 = t18;
            }
            memmove(gm_out, ((uint8_t *)&gm_l15), 32);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb3;
            }
            unsigned __int128 t21 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
            gm_l4 = t21;
            unsigned __int128 t22 = gm_l4;
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, ((uint8_t *)t23));
            gm_t88rcucg1b7uk t25 = gm_l2;
            uint32_t t26 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            unsigned __int128 t27 = gm_f_mask_gbhl4hp(t26);
            unsigned __int128 t28 = (unsigned __int128)(t22 & t27);
            gm_t9p38b040grms t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 20, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
            GM_ST(unsigned __int128, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t29), 32);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)10ULL));
            if (!t31) {
                goto gm_lb4;
            }
            uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
            gm_l5 = t32;
            void * t33 = gm_l0;
            gm_t88rcucg1b7uk t34 = gm_l2;
            uint32_t t35 = gm_l5;
            gm_f_variant_tag_4pbamin((void *)&gm_l16, t33, GM_CONV(gm_csguankko4lmhd, t34), t35);
            uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l16))[48] != 0);
            if (t36) {
                memmove(((uint8_t *)&gm_l17), ((uint8_t *)&gm_l16), 48);
            } else {
                gm_t9p38b040grms t37;
                memset(&t37, 0, sizeof t37);
                memmove(gm_out, ((uint8_t *)&t37), 32);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l17), 48);
            unsigned __int128 t38 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
            void * t39 = gm_l0;
            void * t40 = GM_LD(void *, ((uint8_t *)t39));
            gm_t88rcucg1b7uk t41 = gm_l2;
            uint32_t t42 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            unsigned __int128 t43 = gm_f_mask_gbhl4hp(t42);
            unsigned __int128 t44 = (unsigned __int128)(t38 & t43);
            gm_t9p38b040grms t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 20, 0);
            GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
            GM_ST(unsigned __int128, ((uint8_t *)&t45), t44);
            ((uint8_t *)&t45)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t45), 32);
            goto gm_fdone;
        }
gm_lb4:;
        {
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)3ULL));
            if (!t47) {
                goto gm_lb5;
            }
            void * t48 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l7 = t48;
            void * t49 = gm_l0;
            void * t50 = GM_LD(void *, ((uint8_t *)t49));
            gm_t88rcucg1b7uk t51 = gm_l2;
            gm_f_bits_layout_u6kkccp((void *)&gm_l18, (void *)NULL, cx, NULL, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t52 = (uint8_t)((((uint8_t *)&gm_l18))[24] != 0);
            if (t52) {
                memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 24);
            } else {
                gm_t9p38b040grms t53;
                memset(&t53, 0, sizeof t53);
                memmove(gm_out, ((uint8_t *)&t53), 32);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l19), 24);
            gm_l9 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_l10 = t54;
            gm_l11 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t55 = gm_l11;
                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t57 = (t55 < t56);
                    if (!t57) {
                        goto gm_lb6;
                    }
                    {
                        uint64_t t58 = gm_l11;
                        void * t59 = GM_LD(void *, ((uint8_t *)&gm_l10));
                        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t t61 = gm_l11;
                        gm_tov4mtrfp5e7v t62 = GM_LD(gm_tov4mtrfp5e7v, (((uint8_t *)t59) + (uintptr_t)t61 * 12));
                        gm_tji1ull97ruec t63;
                        GM_ST(uint32_t, (uint8_t *)&t63 + 20, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t63 + 0), t58);
                        GM_ST(gm_tov4mtrfp5e7v, ((uint8_t *)&t63 + 8), t62);
                        gm_l20 = t63;
                        {
                            {
                                uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                gm_l12 = t64;
                                gm_tov4mtrfp5e7v t65 = GM_LD(gm_tov4mtrfp5e7v, (((uint8_t *)&gm_l20) + 8));
                                gm_l13 = t65;
                                void * t66 = gm_l0;
                                void * t67 = gm_l7;
                                void * t68 = GM_LD(void *, ((uint8_t *)t67));
                                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
                                uint64_t t70 = gm_l12;
                                if ((uintptr_t)t70 >= (uintptr_t)(t69)) gm_trap_index((uintptr_t)t70, t69, gm_srcue4ionbe9lgh, 707, 32);
                                gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
                                gm_t6evimt84lndq t72 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t68) + (uintptr_t)t70 * 48));
                                gm_f_bits_of_hithcdi((void *)&gm_l21, (void *)NULL, cx, NULL, t66, &t72, t71);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
                                if (t73) {
                                    unsigned __int128 t74 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l21));
                                    gm_l22 = t74;
                                } else {
                                    gm_t9p38b040grms t75;
                                    memset(&t75, 0, sizeof t75);
                                    memmove(gm_out, ((uint8_t *)&t75), 32);
                                    goto gm_fdone;
                                }
                                unsigned __int128 t76 = gm_l22;
                                gm_l14 = t76;
                                unsigned __int128 t77 = gm_l9;
                                unsigned __int128 t78 = gm_l14;
                                uint32_t t79 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 4));
                                unsigned __int128 t80 = gm_f_mask_gbhl4hp(t79);
                                unsigned __int128 t81 = (unsigned __int128)(t78 & t80);
                                uint32_t t82 = GM_LD(uint32_t, ((uint8_t *)&gm_l13));
                                unsigned __int128 t83 = (unsigned __int128)t82;
                                unsigned __int128 n84 = (unsigned __int128)t83;
                                if (n84 >= 128) gm_trap_shift((uint64_t)n84, "u128", gm_srcue4ionbe9lgh, 708, 22);
                                unsigned __int128 r85 = (unsigned __int128)((unsigned __int128)t81 << (int)n84);
                                bool o86 = (unsigned __int128)(r85 >> (int)n84) != t81;
                                if (o86) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 708, 22);
                                unsigned __int128 t87 = r85;
                                unsigned __int128 t88 = (unsigned __int128)(t77 | t87);
                                gm_l9 = t88;
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 706, 13);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t89 = gm_l11;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    uint64_t t92 = r90;
                    gm_l11 = t92;
                }
            }
gm_lb6:;
            unsigned __int128 t93 = gm_l9;
            gm_t9p38b040grms t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 20, 0);
            GM_ST(uint64_t, (uint8_t *)&t94 + 24, 0);
            GM_ST(unsigned __int128, ((uint8_t *)&t94), t93);
            ((uint8_t *)&t94)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t94), 32);
            goto gm_fdone;
        }
gm_lb5:;
        {
            gm_t9p38b040grms t95;
            memset(&t95, 0, sizeof t95);
            memmove(gm_out, ((uint8_t *)&t95), 32);
            goto gm_fdone;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 696, 5);
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
__attribute__((aligned(4))) void gm_f_builtin_psq6ql3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_builtin_psq6ql3 F;
    GM_STACK_CHECK(&F, 29858u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    F.l3 = a3;
    switch (gm_c_builtin_psq6ql3_214(&F, env, cx, inst, gm_out)) {
    case 1:
        goto gm_lad;
    case 2:
        goto gm_fdone;
    }
    {
        {
            switch (gm_c_builtin_psq6ql3_299(&F, env, cx, inst, gm_out)) {
            case 1:
                goto gm_lad;
            case 2:
                goto gm_fdone;
            }
            switch (gm_c_builtin_psq6ql3_666(&F, env, cx, inst, gm_out)) {
            case 1:
                goto gm_lad;
            case 2:
                goto gm_fdone;
            }
            switch (gm_c_builtin_psq6ql3_1105(&F, env, cx, inst, gm_out)) {
            case 1:
                goto gm_fdone;
            case 2:
                goto gm_lad;
            }
            {
                {
                    uint8_t t1571 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t1572 = (uint8_t)(t1571 == ((uint8_t)62ULL));
                    if (!t1572) {
                        goto gm_lb209;
                    }
                    {
                        {
                            void * t1573 = GM_LD(void *, ((uint8_t *)&F.l9));
                            uint64_t t1574 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1574)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1574, gm_srcue4ionbe9lgh, 645, 27);
                            uint8_t t1575 = GM_LD(uint8_t, ((((uint8_t *)t1573) + (uintptr_t)((uint64_t)1ULL) * 48) + 0));
                            uint8_t t1576 = (uint8_t)(t1575 == ((uint8_t)0ULL));
                            if (!t1576) {
                                goto gm_lb211;
                            }
                            unsigned __int128 t1577 = GM_LD(unsigned __int128, ((((uint8_t *)t1573) + (uintptr_t)((uint64_t)1ULL) * 48) + 16));
                            F.l121 = t1577;
                            unsigned __int128 t1578 = F.l121;
                            uint64_t t1579 = (uint64_t)t1578;
                            uint64_t t1580 = (uint64_t)(((uint64_t)14695981039346656037ULL) ^ t1579);
                            F.l122 = t1580;
                            void * t1581 = F.l0;
                            gm_t88rcucg1b7uk t1582 = F.l11;
                            void * t1583 = (void *)((uint8_t *)&F.l122);
                            gm_tl874t2atnlh3 t1584 = F.l5;
                            gm_t6evimt84lndq t1585 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
                            gm_f_hash_value_j2e3sqh((void *)&F.l191, (void *)NULL, cx, NULL, t1581, &t1585, t1582, t1583, t1584);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t1586 = (uint8_t)!((((uint8_t *)&F.l191))[0] != 0);
                            if (t1586) {
                            } else {
                                gm_t39uai1ajk3qv t1587 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&F.l191) + 16));
                                gm_ttk92ufj2f6eh t1588;
                                GM_ST(uint8_t, (uint8_t *)&t1588 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t1588 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t1588 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t1588 + 8, 0);
                                ((uint8_t *)&t1588)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1588 + 16), t1587);
                                memmove(gm_out, ((uint8_t *)&t1588), 112);
                                goto gm_fdone;
                            }
                            uint64_t t1589 = F.l122;
                            unsigned __int128 t1590 = (unsigned __int128)t1589;
                            gm_t6evimt84lndq t1591;
                            GM_ST(uint8_t, (uint8_t *)&t1591 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1591 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1591 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1591 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1591 + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1591 + 40, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t1591 + 0), ((uint8_t)0ULL));
                            GM_ST(unsigned __int128, ((uint8_t *)&t1591 + 16), t1590);
                            gm_ttk92ufj2f6eh t1592;
                            memset(&t1592, 0, sizeof t1592);
                            ((uint8_t *)&t1592)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1592 + 16), t1591);
                            memmove(gm_out, ((uint8_t *)&t1592), 112);
                            goto gm_fdone;
                        }
gm_lb211:;
                        void * t1593 = F.l0;
                        gm_tl874t2atnlh3 t1594 = F.l5;
                        gm_b16_8 t1595;
                        GM_ST(void *, (uint8_t *)&t1595, (void *)(gm_srffcdgl461h6));
                        GM_ST(uintptr_t, (uint8_t *)&t1595 + 8, (uintptr_t)(14));
                        gm_t39uai1ajk3qv t1596;
                        gm_f_unsupported_2seq49l((void *)&t1596, t1593, GM_CONV(gm_cseahvg8v7p0vs, t1594), t1595);
                        gm_ttk92ufj2f6eh t1597;
                        GM_ST(uint8_t, (uint8_t *)&t1597 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1597 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1597 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t1597 + 8, 0);
                        ((uint8_t *)&t1597)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1597 + 16), t1596);
                        memmove(gm_out, ((uint8_t *)&t1597), 112);
                        goto gm_fdone;
                    }
gm_lb210:;
                }
gm_lb209:;
                {
                    gm_tvdg94bk7aa22 t1598 = F.l1;
                    F.l123 = t1598;
                    void * t1599 = F.l0;
                    gm_tl874t2atnlh3 t1600 = F.l5;
                    gm_tvdg94bk7aa22 t1601 = F.l123;
                    gm_th9nl3oso747e t1602;
                    GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t1602 + 0), t1601);
                    gm_f_format_o6d0gj1((void *)&F.l192, (void *)NULL, cx, NULL, t1602);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1603 = (uint8_t)!((((uint8_t *)&F.l192))[0] != 0);
                    if (!t1603) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&F.l192) + 8, gm_srcue4ionbe9lgh, 650, 46);
                    gm_b16_8 t1604 = GM_LD(gm_b16_8, (((uint8_t *)&F.l192) + 8));
                    gm_b16_8 t1605 = GM_LD(gm_b16_8, ((uint8_t *)&t1604));
                    gm_t39uai1ajk3qv t1606;
                    gm_f_unsupported_2seq49l((void *)&t1606, t1599, GM_CONV(gm_cseahvg8v7p0vs, t1600), t1605);
                    gm_ttk92ufj2f6eh t1607;
                    GM_ST(uint8_t, (uint8_t *)&t1607 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1607 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1607 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1607 + 8, 0);
                    ((uint8_t *)&t1607)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1607 + 16), t1606);
                    memmove(gm_out, ((uint8_t *)&t1607), 112);
                    goto gm_fdone;
                }
gm_lb212:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 378, 5);
            }
        }
gm_lb7:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_coerce_fesnqv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3) {
    struct gm_fr_coerce_fesnqv4 F;
    void * gm_l0; /* m */
    gm_tmn7n0n9raou5 gm_l1; /* kind */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    unsigned __int128 gm_l5; /* bits */
    uint32_t gm_l6; /* i */
    void * gm_l7; /* p */
    gm_tkaapcii86udk gm_l8; /* pl */
    gm_t88rcucg1b7uk gm_l9; /* pointee */
    uint64_t gm_l10; /* n */
    void * gm_l11; /* p */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* from */
    GM_STACK_CHECK(&F, 4828u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_ttk92ufj2f6eh gm_l14;
    gm_tq7s98ihonkju gm_l15;
    gm_tq7s98ihonkju gm_l16;
    gm_tq7s98ihonkju gm_l17;
    gm_t53at80pfdus3 gm_l18;
    gm_t53at80pfdus3 gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_ttk92ufj2f6eh gm_l22;
    gm_tq7s98ihonkju gm_l23;
    {
        void * t33 = gm_l0;
        void * t34 = gm_l2;
        gm_f_eval_2vbtp1v((void *)&gm_l14, (void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (t35) {
        } else {
            gm_t39uai1ajk3qv t36 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l14) + 16));
            gm_ttk92ufj2f6eh t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
            ((uint8_t *)&t37)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t37 + 16), t36);
            memmove(gm_out, ((uint8_t *)&t37), 112);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l14) + 16), 48);
        {
            {
                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t39 = (uint8_t)(t38 == ((uint8_t)0ULL));
                if (!t39) {
                    goto gm_lb2;
                }
                {
                    {
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t41 = (uint8_t)(t40 == ((uint8_t)0ULL));
                        if (!t41) {
                            goto gm_lb4;
                        }
                        unsigned __int128 t42 = GM_LD(unsigned __int128, (((uint8_t *)&F.l4) + 16));
                        gm_l5 = t42;
                        void * t43 = gm_l0;
                        void * t44 = gm_l0;
                        unsigned __int128 t45 = gm_l5;
                        void * t46 = gm_l2;
                        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t46));
                        __int128 t48 = gm_f_int_value_6hmh8tj(t44, t45, GM_CONV(gm_csguankko4lmhd, t47));
                        unsigned __int128 t49;
                        memset(&t49, 0, sizeof t49); memcpy(&t49, ((uint8_t *)&t48), sizeof t49 < 16 ? sizeof t49 : 16);
                        void * t50 = gm_l3;
                        gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t50));
                        unsigned __int128 t52 = gm_f_wrap_bt8r149(t43, t49, GM_CONV(gm_csguankko4lmhd, t51));
                        gm_t6evimt84lndq t53;
                        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t53 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t53 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t53 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t53 + 16), t52);
                        gm_ttk92ufj2f6eh t54;
                        memset(&t54, 0, sizeof t54);
                        ((uint8_t *)&t54)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t54 + 16), t53);
                        memmove(gm_out, ((uint8_t *)&t54), 112);
                        goto gm_fdone;
                    }
gm_lb4:;
                    gm_t6evimt84lndq t55 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
                    gm_ttk92ufj2f6eh t56;
                    memset(&t56, 0, sizeof t56);
                    ((uint8_t *)&t56)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t56 + 16), t55);
                    memmove(gm_out, ((uint8_t *)&t56), 112);
                    goto gm_fdone;
                }
gm_lb3:;
            }
gm_lb2:;
            {
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t58 = (uint8_t)(t57 == ((uint8_t)1ULL));
                if (!t58) {
                    goto gm_lb5;
                }
                gm_t6evimt84lndq t59 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
                gm_ttk92ufj2f6eh t60;
                memset(&t60, 0, sizeof t60);
                ((uint8_t *)&t60)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t60 + 16), t59);
                memmove(gm_out, ((uint8_t *)&t60), 112);
                goto gm_fdone;
            }
gm_lb5:;
            {
                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)2ULL));
                if (!t62) {
                    goto gm_lb6;
                }
                gm_tq7s98ihonkju t63 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kil3h1h1ecr)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t63;
                uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcue4ionbe9lgh, 273, 39);
                void * t65 = GM_LD(void *, (((uint8_t *)&gm_l15) + 8));
                gm_t6evimt84lndq t66;
                memset(&t66, 0, sizeof t66);
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)7ULL));
                GM_ST(void *, ((uint8_t *)&t66 + 16), t65);
                gm_ttk92ufj2f6eh t67;
                memset(&t67, 0, sizeof t67);
                ((uint8_t *)&t67)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t67 + 16), t66);
                memmove(gm_out, ((uint8_t *)&t67), 112);
                goto gm_fdone;
            }
gm_lb6:;
            {
                uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t69 = (uint8_t)(t68 == ((uint8_t)3ULL));
                if (!t69) {
                    goto gm_lb7;
                }
                gm_tq7s98ihonkju t70 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khqbs9bi0vi)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t70;
                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcue4ionbe9lgh, 274, 31);
                void * t72 = GM_LD(void *, (((uint8_t *)&gm_l16) + 8));
                gm_t6evimt84lndq t73;
                memset(&t73, 0, sizeof t73);
                GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)8ULL));
                GM_ST(void *, ((uint8_t *)&t73 + 16), t72);
                gm_ttk92ufj2f6eh t74;
                memset(&t74, 0, sizeof t74);
                ((uint8_t *)&t74)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t74 + 16), t73);
                memmove(gm_out, ((uint8_t *)&t74), 112);
                goto gm_fdone;
            }
gm_lb7:;
            {
                uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t76 = (uint8_t)(t75 == ((uint8_t)4ULL));
                if (!t76) {
                    goto gm_lb8;
                }
                uint32_t t77 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
                gm_l6 = t77;
                {
                    {
                        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t79 = (uint8_t)(t78 == ((uint8_t)9ULL));
                        if (!t79) {
                            goto gm_lb10;
                        }
                        void * t80 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
                        gm_l7 = t80;
                        uint32_t t81 = gm_l6;
                        void * t82 = gm_l7;
                        gm_ttggq7ng4kp3p t83;
                        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t83 + 0), t81);
                        GM_ST(void *, ((uint8_t *)&t83 + 8), t82);
                        gm_t6evimt84lndq t84;
                        GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t84 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t84 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t84 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)10ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t84 + 16), t83);
                        gm_tq7s98ihonkju t85 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t84, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksi8gqve35e)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l17 = t85;
                        uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcue4ionbe9lgh, 276, 36);
                        void * t87 = GM_LD(void *, (((uint8_t *)&gm_l17) + 8));
                        gm_t6evimt84lndq t88;
                        memset(&t88, 0, sizeof t88);
                        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)9ULL));
                        GM_ST(void *, ((uint8_t *)&t88 + 16), t87);
                        gm_ttk92ufj2f6eh t89;
                        memset(&t89, 0, sizeof t89);
                        ((uint8_t *)&t89)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t89 + 16), t88);
                        memmove(gm_out, ((uint8_t *)&t89), 112);
                        goto gm_fdone;
                    }
gm_lb10:;
                    {
                        gm_t6evimt84lndq t90 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
                        gm_ttk92ufj2f6eh t91;
                        memset(&t91, 0, sizeof t91);
                        ((uint8_t *)&t91)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t91 + 16), t90);
                        memmove(gm_out, ((uint8_t *)&t91), 112);
                        goto gm_fdone;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 275, 24);
                }
gm_lb9:;
            }
gm_lb8:;
            {
                uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t93 = (uint8_t)(t92 == ((uint8_t)7ULL));
                if (!t93) {
                    goto gm_lb12;
                }
                {
                    {
                        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t95 = (uint8_t)(t94 == ((uint8_t)4ULL));
                        if (!t95) {
                            goto gm_lb14;
                        }
                        memmove(((uint8_t *)&gm_l8), (((uint8_t *)&F.l4) + 16), 24);
                        {
                            {
                                void * t96 = gm_l0;
                                void * t97 = GM_LD(void *, ((uint8_t *)t96));
                                void * t98 = (void *)(((uint8_t *)t97) + 8);
                                void * t99 = gm_l2;
                                gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t99));
                                gm_f_kind_jcnt4hm((void *)&gm_l18, t98, GM_CONV(gm_csguankko4lmhd, t100));
                                uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                                uint8_t t102 = (uint8_t)(t101 == ((uint8_t)8ULL));
                                if (!t102) {
                                    goto gm_lb16;
                                }
                                gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l18) + 8) + 4));
                                gm_l9 = t103;
                                {
                                    {
                                        void * t104 = gm_l0;
                                        void * t105 = GM_LD(void *, ((uint8_t *)t104));
                                        void * t106 = (void *)(((uint8_t *)t105) + 8);
                                        gm_t88rcucg1b7uk t107 = gm_l9;
                                        gm_f_kind_jcnt4hm((void *)&gm_l19, t106, GM_CONV(gm_csguankko4lmhd, t107));
                                        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                                        uint8_t t109 = (uint8_t)(t108 == ((uint8_t)11ULL));
                                        if (!t109) {
                                            goto gm_lb18;
                                        }
                                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                        gm_l10 = t110;
                                        void * t111 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                                        gm_b16_8 t113 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t112, ((uint64_t)0ULL));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tkaapcii86udk t114;
                                        GM_ST(void *, ((uint8_t *)&t114 + 0), t111);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t113);
                                        uint64_t t115 = gm_l10;
                                        gm_to6glrduves6o t116;
                                        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t116 + 0), t114);
                                        GM_ST(uint64_t, ((uint8_t *)&t116 + 24), t115);
                                        gm_t6evimt84lndq t117;
                                        GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t117 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)5ULL));
                                        GM_ST(gm_to6glrduves6o, ((uint8_t *)&t117 + 16), t116);
                                        gm_ttk92ufj2f6eh t118;
                                        memset(&t118, 0, sizeof t118);
                                        ((uint8_t *)&t118)[0] = 0;
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t118 + 16), t117);
                                        memmove(gm_out, ((uint8_t *)&t118), 112);
                                        goto gm_fdone;
                                    }
gm_lb18:;
                                    void * t119 = gm_l0;
                                    void * t120 = gm_l3;
                                    gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t120) + 72));
                                    gm_b16_8 t122;
                                    GM_ST(void *, (uint8_t *)&t122, (void *)(gm_sqh0568fl8oao));
                                    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(13));
                                    gm_t39uai1ajk3qv t123;
                                    gm_f_unsupported_2seq49l((void *)&t123, t119, GM_CONV(gm_cseahvg8v7p0vs, t121), t122);
                                    gm_ttk92ufj2f6eh t124;
                                    GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t124 + 8, 0);
                                    ((uint8_t *)&t124)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t124 + 16), t123);
                                    memmove(gm_out, ((uint8_t *)&t124), 112);
                                    goto gm_fdone;
                                }
gm_lb17:;
                            }
gm_lb16:;
                            void * t125 = gm_l0;
                            void * t126 = gm_l3;
                            gm_tl874t2atnlh3 t127 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t126) + 72));
                            gm_b16_8 t128;
                            GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sqh0568fl8oao));
                            GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(13));
                            gm_t39uai1ajk3qv t129;
                            gm_f_unsupported_2seq49l((void *)&t129, t125, GM_CONV(gm_cseahvg8v7p0vs, t127), t128);
                            gm_ttk92ufj2f6eh t130;
                            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t130 + 8, 0);
                            ((uint8_t *)&t130)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t130 + 16), t129);
                            memmove(gm_out, ((uint8_t *)&t130), 112);
                            goto gm_fdone;
                        }
gm_lb15:;
                    }
gm_lb14:;
                    void * t131 = gm_l0;
                    void * t132 = gm_l3;
                    gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 72));
                    gm_b16_8 t134;
                    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sqh0568fl8oao));
                    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(13));
                    gm_t39uai1ajk3qv t135;
                    gm_f_unsupported_2seq49l((void *)&t135, t131, GM_CONV(gm_cseahvg8v7p0vs, t133), t134);
                    gm_ttk92ufj2f6eh t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t136 + 8, 0);
                    ((uint8_t *)&t136)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t136 + 16), t135);
                    memmove(gm_out, ((uint8_t *)&t136), 112);
                    goto gm_fdone;
                }
gm_lb13:;
            }
gm_lb12:;
            {
                uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t138 = (uint8_t)(t137 == ((uint8_t)11ULL));
                if (!t138) {
                    goto gm_lb19;
                }
                {
                    {
                        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t140 = (uint8_t)(t139 == ((uint8_t)7ULL));
                        if (!t140) {
                            goto gm_lb21;
                        }
                        void * t141 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
                        gm_l11 = t141;
                        {
                            {
                                void * t142 = gm_l0;
                                void * t143 = GM_LD(void *, ((uint8_t *)t142));
                                void * t144 = (void *)(((uint8_t *)t143) + 8);
                                void * t145 = gm_l3;
                                gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t145));
                                gm_f_kind_jcnt4hm((void *)&gm_l20, t144, GM_CONV(gm_csguankko4lmhd, t146));
                                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                                uint8_t t148 = (uint8_t)(t147 == ((uint8_t)13ULL));
                                if (!t148) {
                                    goto gm_lb23;
                                }
                                gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l20) + 8));
                                gm_l12 = t149;
                                {
                                    {
                                        void * t150 = gm_l0;
                                        void * t151 = GM_LD(void *, ((uint8_t *)t150));
                                        void * t152 = (void *)(((uint8_t *)t151) + 8);
                                        void * t153 = gm_l2;
                                        gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t153));
                                        gm_f_kind_jcnt4hm((void *)&gm_l21, t152, GM_CONV(gm_csguankko4lmhd, t154));
                                        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                        uint8_t t156 = (uint8_t)(t155 == ((uint8_t)13ULL));
                                        if (!t156) {
                                            goto gm_lb25;
                                        }
                                        gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l21) + 8));
                                        gm_l13 = t157;
                                        void * t158 = gm_l0;
                                        void * t159 = gm_l11;
                                        gm_t88rcucg1b7uk t160 = gm_l13;
                                        gm_t88rcucg1b7uk t161 = gm_l12;
                                        gm_t6evimt84lndq t162 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t159));
                                        gm_f_convert_value_v1i8v3i((void *)&gm_l22, (void *)NULL, cx, NULL, t158, &t162, t160, t161);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                        if (t163) {
                                        } else {
                                            gm_t39uai1ajk3qv t164 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l22) + 16));
                                            gm_ttk92ufj2f6eh t165;
                                            GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t165 + 8, 0);
                                            ((uint8_t *)&t165)[0] = 1;
                                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t165 + 16), t164);
                                            memmove(gm_out, ((uint8_t *)&t165), 112);
                                            goto gm_fdone;
                                        }
                                        gm_tq7s98ihonkju t166 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)(((uint8_t *)&gm_l22) + 16)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knm7lgi0bll)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l23 = t166;
                                        uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                        if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcue4ionbe9lgh, 289, 30);
                                        void * t168 = GM_LD(void *, (((uint8_t *)&gm_l23) + 8));
                                        gm_t6evimt84lndq t169;
                                        memset(&t169, 0, sizeof t169);
                                        GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)7ULL));
                                        GM_ST(void *, ((uint8_t *)&t169 + 16), t168);
                                        gm_ttk92ufj2f6eh t170;
                                        memset(&t170, 0, sizeof t170);
                                        ((uint8_t *)&t170)[0] = 0;
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t170 + 16), t169);
                                        memmove(gm_out, ((uint8_t *)&t170), 112);
                                        goto gm_fdone;
                                    }
gm_lb25:;
                                    gm_t6evimt84lndq t171 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
                                    gm_ttk92ufj2f6eh t172;
                                    memset(&t172, 0, sizeof t172);
                                    ((uint8_t *)&t172)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t172 + 16), t171);
                                    memmove(gm_out, ((uint8_t *)&t172), 112);
                                    goto gm_fdone;
                                }
gm_lb24:;
                            }
gm_lb23:;
                            gm_t6evimt84lndq t173 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
                            gm_ttk92ufj2f6eh t174;
                            memset(&t174, 0, sizeof t174);
                            ((uint8_t *)&t174)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t174 + 16), t173);
                            memmove(gm_out, ((uint8_t *)&t174), 112);
                            goto gm_fdone;
                        }
gm_lb22:;
                    }
gm_lb21:;
                    {
                        gm_t6evimt84lndq t175 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
                        gm_ttk92ufj2f6eh t176;
                        memset(&t176, 0, sizeof t176);
                        ((uint8_t *)&t176)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t176 + 16), t175);
                        memmove(gm_out, ((uint8_t *)&t176), 112);
                        goto gm_fdone;
                    }
gm_lb26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 285, 24);
                }
gm_lb20:;
            }
gm_lb19:;
            {
                {
                    {
                        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t178 = (uint8_t)(t177 == ((uint8_t)12ULL));
                        if (!t178) {
                            goto gm_lb29;
                        }
                        goto gm_lb28;
                    }
gm_lb29:;
                    {
                        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t180 = (uint8_t)(t179 == ((uint8_t)5ULL));
                        if (!t180) {
                            goto gm_lb30;
                        }
                        goto gm_lb28;
                    }
gm_lb30:;
                    goto gm_lb27;
                }
gm_lb28:;
                void * t181 = gm_l0;
                void * t182 = gm_l3;
                gm_tl874t2atnlh3 t183 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t182) + 72));
                gm_b16_8 t184;
                GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sqh0568fl8oao));
                GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t185;
                gm_f_unsupported_2seq49l((void *)&t185, t181, GM_CONV(gm_cseahvg8v7p0vs, t183), t184);
                gm_ttk92ufj2f6eh t186;
                GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t186 + 8, 0);
                ((uint8_t *)&t186)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t186 + 16), t185);
                memmove(gm_out, ((uint8_t *)&t186), 112);
                goto gm_fdone;
            }
gm_lb27:;
            {
                gm_t6evimt84lndq t187 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
                gm_ttk92ufj2f6eh t188;
                memset(&t188, 0, sizeof t188);
                ((uint8_t *)&t188)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t188 + 16), t187);
                memmove(gm_out, ((uint8_t *)&t188), 112);
                goto gm_fdone;
            }
gm_lb31:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 267, 5);
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_const_cell_p4adg2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_cell_p4adg2s F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* use_span */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 1285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l6;
    uint32_t gm_l7;
    gm_tabb710d9mq2e gm_l8;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_tl874t2atnlh3 t9 = gm_l1;
    gm_t88rcucg1b7uk t10 = gm_l2;
    gm_t9805qtonbsag t11 = gm_f_const_id_2mlh0og(t8, GM_CONV(gm_cseahvg8v7p0vs, t9), GM_CONV(gm_csguankko4lmhd, t10));
    gm_l6 = t11;
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l6))[4] != 0);
    if (t12) {
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
        gm_l7 = t13;
    } else {
        void * t14 = gm_l0;
        gm_tl874t2atnlh3 t15 = gm_l3;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sdrg69bmlm0tm));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(29));
        gm_t39uai1ajk3qv t17;
        gm_f_unsupported_2seq49l((void *)&t17, t14, GM_CONV(gm_cseahvg8v7p0vs, t15), t16);
        gm_t7g11k265ojef t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
        ((uint8_t *)&t18)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t18 + 16), t17);
        memmove(gm_out, ((uint8_t *)&t18), 112);
        goto gm_fdone;
    }
    uint32_t t19 = gm_l7;
    gm_l4 = t19;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 2088));
    uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 2088) + 8));
    uint32_t t24 = gm_l4;
    uint64_t t25 = (uint64_t)t24;
    if ((uintptr_t)t25 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t25, t23, gm_srcue4ionbe9lgh, 1012, 10);
    memmove(((uint8_t *)&F.l5), (((uint8_t *)t22) + (uintptr_t)t25 * 88), 88);
    {
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)2ULL));
            if (!t27) {
                goto gm_lb2;
            }
            uint8_t t28 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 80)) != 0);
            if (!t28) gm_trap("unwrap of null", gm_srcue4ionbe9lgh, 1014, 25);
            void * t29 = GM_LD(void *, (((uint8_t *)&F.l5) + 80));
            gm_t7g11k265ojef t30;
            memset(&t30, 0, sizeof t30);
            ((uint8_t *)&t30)[0] = 0;
            GM_ST(void *, ((uint8_t *)&t30 + 16), t29);
            memmove(gm_out, ((uint8_t *)&t30), 112);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
            if (!t32) {
                goto gm_lb3;
            }
            void * t33 = gm_l0;
            gm_tl874t2atnlh3 t34 = gm_l3;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
            gm_tnpifv20irnph t36;
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
            gm_f_format_q8f0n70((void *)&gm_l8, (void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcue4ionbe9lgh, 1015, 49);
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_t39uai1ajk3qv t40;
            gm_f_trap_jftbss6((void *)&t40, (void *)NULL, cx, NULL, t33, t34, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t7g11k265ojef t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
            ((uint8_t *)&t41)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t41 + 16), t40);
            memmove(gm_out, ((uint8_t *)&t41), 112);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)0ULL));
            if (!t43) {
                goto gm_lb4;
            }
            void * t44 = gm_l0;
            gm_tl874t2atnlh3 t45 = gm_l3;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sdrg69bmlm0tm));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(29));
            gm_t39uai1ajk3qv t47;
            gm_f_unsupported_2seq49l((void *)&t47, t44, GM_CONV(gm_cseahvg8v7p0vs, t45), t46);
            gm_t7g11k265ojef t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t48 + 8, 0);
            ((uint8_t *)&t48)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t48 + 16), t47);
            memmove(gm_out, ((uint8_t *)&t48), 112);
            goto gm_fdone;
        }
gm_lb4:;
        {
            gm_t39uai1ajk3qv t49;
            memset(&t49, 0, sizeof t49);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)6ULL));
            gm_t7g11k265ojef t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 8, 0);
            ((uint8_t *)&t50)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t50 + 16), t49);
            memmove(gm_out, ((uint8_t *)&t50), 112);
            goto gm_fdone;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1013, 5);
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
inline __attribute__((aligned(4))) void gm_f_convert_value_v1i8v3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_convert_value_v1i8v3i F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* from */
    gm_t88rcucg1b7uk gm_l3; /* to */
    unsigned __int128 gm_l4; /* bits */
    gm_tkaapcii86udk gm_l5; /* pl */
    gm_t88rcucg1b7uk gm_l6; /* pointee */
    uint64_t gm_l7; /* n */
    GM_STACK_CHECK(&F, 1519u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l8;
    gm_t53at80pfdus3 gm_l9;
    gm_t53at80pfdus3 gm_l10;
    gm_t88rcucg1b7uk t10 = gm_l2;
    gm_t88rcucg1b7uk t11 = gm_l3;
    uint8_t t12 = (gm_eq9m6mvtmgde(((uint8_t *)&t10), ((uint8_t *)&t11)));
    if (t12) {
        gm_t6evimt84lndq t13 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
        gm_ttk92ufj2f6eh t14;
        memset(&t14, 0, sizeof t14);
        ((uint8_t *)&t14)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t14 + 16), t13);
        memmove(gm_out, ((uint8_t *)&t14), 112);
        goto gm_fdone;
    }
    {
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
            if (!t16) {
                goto gm_lb2;
            }
            unsigned __int128 t17 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
            gm_l4 = t17;
            void * t18 = gm_l0;
            void * t19 = gm_l0;
            unsigned __int128 t20 = gm_l4;
            gm_t88rcucg1b7uk t21 = gm_l2;
            __int128 t22 = gm_f_int_value_6hmh8tj(t19, t20, GM_CONV(gm_csguankko4lmhd, t21));
            unsigned __int128 t23;
            memset(&t23, 0, sizeof t23); memcpy(&t23, ((uint8_t *)&t22), sizeof t23 < 16 ? sizeof t23 : 16);
            gm_t88rcucg1b7uk t24 = gm_l3;
            unsigned __int128 t25 = gm_f_wrap_bt8r149(t18, t23, GM_CONV(gm_csguankko4lmhd, t24));
            gm_t6evimt84lndq t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t26 + 16), t25);
            gm_ttk92ufj2f6eh t27;
            memset(&t27, 0, sizeof t27);
            ((uint8_t *)&t27)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t27 + 16), t26);
            memmove(gm_out, ((uint8_t *)&t27), 112);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)4ULL));
            if (!t29) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l5), (((uint8_t *)&F.l1) + 16), 24);
            {
                {
                    void * t30 = gm_l0;
                    void * t31 = GM_LD(void *, ((uint8_t *)t30));
                    void * t32 = (void *)(((uint8_t *)t31) + 8);
                    gm_t88rcucg1b7uk t33 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l8, t32, GM_CONV(gm_csguankko4lmhd, t33));
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)8ULL));
                    if (!t35) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l8) + 8) + 4));
                    gm_l6 = t36;
                    {
                        {
                            void * t37 = gm_l0;
                            void * t38 = GM_LD(void *, ((uint8_t *)t37));
                            void * t39 = (void *)(((uint8_t *)t38) + 8);
                            gm_t88rcucg1b7uk t40 = gm_l3;
                            gm_f_kind_jcnt4hm((void *)&gm_l9, t39, GM_CONV(gm_csguankko4lmhd, t40));
                            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)10ULL));
                            if (!t42) {
                                goto gm_lb7;
                            }
                            void * t43 = gm_l0;
                            void * t44 = GM_LD(void *, ((uint8_t *)t43));
                            void * t45 = (void *)(((uint8_t *)t44) + 8);
                            gm_t88rcucg1b7uk t46 = gm_l6;
                            gm_f_kind_jcnt4hm((void *)&gm_l10, t45, GM_CONV(gm_csguankko4lmhd, t46));
                            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)11ULL));
                            if (!t48) {
                                goto gm_lb7;
                            }
                            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            gm_l7 = t49;
                            void * t50 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                            gm_b16_8 t52 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t51, ((uint64_t)0ULL));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tkaapcii86udk t53;
                            GM_ST(void *, ((uint8_t *)&t53 + 0), t50);
                            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 8), t52);
                            uint64_t t54 = gm_l7;
                            gm_to6glrduves6o t55;
                            GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t55 + 0), t53);
                            GM_ST(uint64_t, ((uint8_t *)&t55 + 24), t54);
                            gm_t6evimt84lndq t56;
                            GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t56 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)5ULL));
                            GM_ST(gm_to6glrduves6o, ((uint8_t *)&t56 + 16), t55);
                            gm_ttk92ufj2f6eh t57;
                            memset(&t57, 0, sizeof t57);
                            ((uint8_t *)&t57)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t57 + 16), t56);
                            memmove(gm_out, ((uint8_t *)&t57), 112);
                            goto gm_fdone;
                        }
gm_lb7:;
                    }
gm_lb6:;
                    gm_t6evimt84lndq t58 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
                    gm_ttk92ufj2f6eh t59;
                    memset(&t59, 0, sizeof t59);
                    ((uint8_t *)&t59)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t59 + 16), t58);
                    memmove(gm_out, ((uint8_t *)&t59), 112);
                    goto gm_fdone;
                }
gm_lb5:;
                gm_t6evimt84lndq t60 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
                gm_ttk92ufj2f6eh t61;
                memset(&t61, 0, sizeof t61);
                ((uint8_t *)&t61)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t61 + 16), t60);
                memmove(gm_out, ((uint8_t *)&t61), 112);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
        {
            gm_t6evimt84lndq t62 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
            gm_ttk92ufj2f6eh t63;
            memset(&t63, 0, sizeof t63);
            ((uint8_t *)&t63)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t63 + 16), t62);
            memmove(gm_out, ((uint8_t *)&t63), 112);
            goto gm_fdone;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 301, 5);
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
inline __attribute__((aligned(4))) void gm_f_display_covn4i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_display_covn4i2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* t */
    unsigned __int128 gm_l3; /* bits */
    uint8_t gm_l4; /* b */
    uint32_t gm_l5; /* index */
    uint32_t gm_l6; /* id */
    gm_b16_8 gm_l7; /* variants */
    GM_STACK_CHECK(&F, 642u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l8;
    gm_t53at80pfdus3 gm_l9;
    gm_tabb710d9mq2e gm_l10;
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)0ULL));
            if (!t12) {
                goto gm_lb2;
            }
            unsigned __int128 t13 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
            gm_l3 = t13;
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, ((uint8_t *)t14));
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l2;
            uint8_t t18 = gm_f_is_integer_8d4f9ek(t16, GM_CONV(gm_csguankko4lmhd, t17));
            if (t18) {
                void * t19 = gm_l0;
                unsigned __int128 t20 = gm_l3;
                gm_t88rcucg1b7uk t21 = gm_l2;
                gm_tbci5ospk5nnn t22;
                gm_f_big_of_1cq0qao((void *)&t22, (void *)NULL, cx, NULL, t19, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t23 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t24), t23);
                ((uint8_t *)&t24)[16] = 1;
                memmove(gm_out, ((uint8_t *)&t24), 24);
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
            if (!t26) {
                goto gm_lb3;
            }
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
            gm_l4 = t27;
            uint8_t t28 = gm_l4;
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), t28);
            gm_f_format_9u3jvus((void *)&gm_l8, (void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcue4ionbe9lgh, 993, 28);
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_th2lqbo77pvsc t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t32), 24);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)10ULL));
            if (!t34) {
                goto gm_lb4;
            }
            uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
            gm_l5 = t35;
            uint8_t t36 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8)) != 0);
            if (t36) {
                goto gm_lb4;
            }
            {
                {
                    void * t37 = gm_l0;
                    void * t38 = GM_LD(void *, ((uint8_t *)t37));
                    void * t39 = (void *)(((uint8_t *)t38) + 8);
                    gm_t88rcucg1b7uk t40 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l9, t39, GM_CONV(gm_csguankko4lmhd, t40));
                    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)17ULL));
                    if (!t42) {
                        goto gm_lb6;
                    }
                    uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                    gm_l6 = t43;
                    {
                        {
                            void * t44 = gm_l0;
                            void * t45 = GM_LD(void *, ((uint8_t *)t44));
                            void * t46 = GM_LD(void *, (((uint8_t *)t45) + 1088));
                            uint64_t t47 = GM_LD(uint64_t, ((((uint8_t *)t45) + 1088) + 8));
                            uint32_t t48 = gm_l6;
                            uint64_t t49 = (uint64_t)t48;
                            if ((uintptr_t)t49 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t49, t47, gm_srcue4ionbe9lgh, 996, 36);
                            uint8_t t50 = GM_LD(uint8_t, (((((uint8_t *)t46) + (uintptr_t)t49 * 184) + 24) + 0));
                            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)1ULL));
                            if (!t51) {
                                goto gm_lb8;
                            }
                            gm_b16_8 t52 = GM_LD(gm_b16_8, ((((((uint8_t *)t46) + (uintptr_t)t49 * 184) + 24) + 8) + 8));
                            gm_l7 = t52;
                            void * t53 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint32_t t55 = gm_l5;
                            uint64_t t56 = (uint64_t)t55;
                            if ((uintptr_t)t56 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t56, t54, gm_srcue4ionbe9lgh, 997, 38);
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t53) + (uintptr_t)t56 * 72));
                            gm_tnpifv20irnph t58;
                            GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
                            gm_f_format_g0ibdjv((void *)&gm_l10, (void *)NULL, cx, NULL, t58);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcue4ionbe9lgh, 997, 20);
                            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                            gm_th2lqbo77pvsc t61;
                            GM_ST(uint8_t, (uint8_t *)&t61 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t61 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t61 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t61), t60);
                            ((uint8_t *)&t61)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t61), 24);
                            goto gm_fdone;
                        }
gm_lb8:;
                        gm_th2lqbo77pvsc t62;
                        memset(&t62, 0, sizeof t62);
                        memmove(gm_out, ((uint8_t *)&t62), 24);
                        goto gm_fdone;
                    }
gm_lb7:;
                }
gm_lb6:;
                gm_th2lqbo77pvsc t63;
                memset(&t63, 0, sizeof t63);
                memmove(gm_out, ((uint8_t *)&t63), 24);
                goto gm_fdone;
            }
gm_lb5:;
        }
gm_lb4:;
        {
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 991, 5);
    }
gm_lb1:;
    gm_th2lqbo77pvsc t64;
    memset(&t64, 0, sizeof t64);
    memmove(gm_out, ((uint8_t *)&t64), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_equal_hinsj77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_equal_hinsj77 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    unsigned __int128 gm_l6; /* x */
    unsigned __int128 gm_l7; /* y */
    double gm_l8; /* x */
    double gm_l9; /* y */
    uint8_t gm_l10; /* x */
    uint8_t gm_l11; /* y */
    uint32_t gm_l12; /* f */
    uint32_t gm_l13; /* g */
    void * gm_l14; /* x */
    void * gm_l15; /* y */
    gm_t88rcucg1b7uk gm_l16; /* inner */
    void * gm_l17; /* x */
    void * gm_l18; /* y */
    gm_t88rcucg1b7uk gm_l19; /* ok */
    void * gm_l20; /* x */
    void * gm_l21; /* y */
    gm_t88rcucg1b7uk gm_l22; /* err */
    uint32_t gm_l23; /* index */
    void * gm_l24; /* pa */
    uint32_t gm_l25; /* index_b */
    void * gm_l26; /* pb */
    void * gm_l27; /* x */
    void * gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* pt */
    void * gm_l30; /* x */
    void * gm_l31; /* y */
    gm_b16_8 gm_l32; /* elems */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    gm_to6glrduves6o gm_l36; /* x */
    gm_to6glrduves6o gm_l37; /* y */
    gm_t88rcucg1b7uk gm_l38; /* et */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_tkaapcii86udk gm_l42; /* x */
    gm_tkaapcii86udk gm_l43; /* y */
    GM_STACK_CHECK(&F, 7642u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l44;
    gm_t53at80pfdus3 gm_l45;
    gm_t53at80pfdus3 gm_l46;
    void * gm_l47;
    void * gm_l48;
    gm_tkn42u7na3t81 gm_l49;
    gm_t88rcucg1b7uk gm_l50;
    gm_th2lqbo77pvsc gm_l51;
    gm_b16_8 gm_l52;
    gm_tsg40glpvq01o gm_l53;
    gm_t53at80pfdus3 gm_l54;
    gm_tsg40glpvq01o gm_l55;
    {
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, ((uint8_t *)t56));
        void * t58 = (void *)(((uint8_t *)t57) + 8);
        gm_l5 = t58;
        {
            {
                {
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)0ULL));
                    if (!t60) {
                        goto gm_lb2;
                    }
                    unsigned __int128 t61 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
                    gm_l6 = t61;
                    {
                        {
                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)0ULL));
                            if (!t63) {
                                goto gm_lb4;
                            }
                            unsigned __int128 t64 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
                            gm_l7 = t64;
                            unsigned __int128 t65 = gm_l6;
                            unsigned __int128 t66 = gm_l7;
                            uint8_t t67 = ((t65 == t66));
                            gm_tsg40glpvq01o t68;
                            memset(&t68, 0, sizeof t68);
                            ((uint8_t *)&t68)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t68 + 16), t67);
                            memmove(gm_out, ((uint8_t *)&t68), 112);
                            goto gm_fdone;
                        }
gm_lb4:;
                        gm_tsg40glpvq01o t69;
                        memset(&t69, 0, sizeof t69);
                        ((uint8_t *)&t69)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t69 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t69), 112);
                        goto gm_fdone;
                    }
gm_lb3:;
                }
gm_lb2:;
                {
                    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t71 = (uint8_t)(t70 == ((uint8_t)1ULL));
                    if (!t71) {
                        goto gm_lb5;
                    }
                    double t72 = GM_LD(double, (((uint8_t *)&F.l1) + 16));
                    gm_l8 = t72;
                    {
                        {
                            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)1ULL));
                            if (!t74) {
                                goto gm_lb7;
                            }
                            double t75 = GM_LD(double, (((uint8_t *)&F.l2) + 16));
                            gm_l9 = t75;
                            double t76 = gm_l8;
                            double t77 = gm_l9;
                            uint8_t t78 = ((t76 == t77));
                            gm_tsg40glpvq01o t79;
                            memset(&t79, 0, sizeof t79);
                            ((uint8_t *)&t79)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t79 + 16), t78);
                            memmove(gm_out, ((uint8_t *)&t79), 112);
                            goto gm_fdone;
                        }
gm_lb7:;
                        gm_tsg40glpvq01o t80;
                        memset(&t80, 0, sizeof t80);
                        ((uint8_t *)&t80)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t80 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t80), 112);
                        goto gm_fdone;
                    }
gm_lb6:;
                }
gm_lb5:;
                {
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)2ULL));
                    if (!t82) {
                        goto gm_lb8;
                    }
                    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
                    gm_l10 = t83;
                    {
                        {
                            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t85 = (uint8_t)(t84 == ((uint8_t)2ULL));
                            if (!t85) {
                                goto gm_lb10;
                            }
                            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
                            gm_l11 = t86;
                            uint8_t t87 = gm_l10;
                            uint8_t t88 = gm_l11;
                            uint8_t t89 = ((t87 == t88));
                            gm_tsg40glpvq01o t90;
                            memset(&t90, 0, sizeof t90);
                            ((uint8_t *)&t90)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t90 + 16), t89);
                            memmove(gm_out, ((uint8_t *)&t90), 112);
                            goto gm_fdone;
                        }
gm_lb10:;
                        gm_tsg40glpvq01o t91;
                        memset(&t91, 0, sizeof t91);
                        ((uint8_t *)&t91)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t91 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t91), 112);
                        goto gm_fdone;
                    }
gm_lb9:;
                }
gm_lb8:;
                {
                    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t93 = (uint8_t)(t92 == ((uint8_t)6ULL));
                    if (!t93) {
                        goto gm_lb11;
                    }
                    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t95 = (uint8_t)((t94 == ((uint8_t)6ULL)));
                    gm_tsg40glpvq01o t96;
                    memset(&t96, 0, sizeof t96);
                    ((uint8_t *)&t96)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t96 + 16), t95);
                    gm_t6evimt84lndq t97 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l2));
                    gm_t6evimt84lndq t98;
                    memset(&t98, 0, sizeof t98);
                    GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)6ULL));
                    memmove(gm_out, ((uint8_t *)&t96), 112);
                    goto gm_fdone;
                }
gm_lb11:;
                {
                    uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t100 = (uint8_t)(t99 == ((uint8_t)11ULL));
                    if (!t100) {
                        goto gm_lb12;
                    }
                    uint32_t t101 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
                    gm_l12 = t101;
                    {
                        {
                            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t103 = (uint8_t)(t102 == ((uint8_t)11ULL));
                            if (!t103) {
                                goto gm_lb14;
                            }
                            uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
                            gm_l13 = t104;
                            uint32_t t105 = gm_l12;
                            uint32_t t106 = gm_l13;
                            uint8_t t107 = ((t105 == t106));
                            gm_tsg40glpvq01o t108;
                            memset(&t108, 0, sizeof t108);
                            ((uint8_t *)&t108)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t108 + 16), t107);
                            memmove(gm_out, ((uint8_t *)&t108), 112);
                            goto gm_fdone;
                        }
gm_lb14:;
                        gm_tsg40glpvq01o t109;
                        memset(&t109, 0, sizeof t109);
                        ((uint8_t *)&t109)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t109 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t109), 112);
                        goto gm_fdone;
                    }
gm_lb13:;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)7ULL));
                    if (!t111) {
                        goto gm_lb15;
                    }
                    void * t112 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l14 = t112;
                    {
                        {
                            uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t114 = (uint8_t)(t113 == ((uint8_t)7ULL));
                            if (!t114) {
                                goto gm_lb17;
                            }
                            void * t115 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                            gm_l15 = t115;
                            {
                                {
                                    void * t116 = gm_l5;
                                    gm_t88rcucg1b7uk t117 = gm_l3;
                                    gm_f_kind_jcnt4hm((void *)&gm_l44, t116, GM_CONV(gm_csguankko4lmhd, t117));
                                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)13ULL));
                                    if (!t119) {
                                        goto gm_lb19;
                                    }
                                    gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l44) + 8));
                                    gm_l16 = t120;
                                    void * t121 = gm_l0;
                                    void * t122 = gm_l14;
                                    void * t123 = gm_l15;
                                    gm_t88rcucg1b7uk t124 = gm_l16;
                                    gm_tl874t2atnlh3 t125 = gm_l4;
                                    gm_t6evimt84lndq t126 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t122));
                                    gm_t6evimt84lndq t127 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t123));
                                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t121, &t126, &t127, t124, t125);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
gm_lb19:;
                                void * t128 = gm_l0;
                                gm_tl874t2atnlh3 t129 = gm_l4;
                                gm_b16_8 t130;
                                GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t131;
                                gm_f_unsupported_2seq49l((void *)&t131, t128, GM_CONV(gm_cseahvg8v7p0vs, t129), t130);
                                gm_tsg40glpvq01o t132;
                                GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t132 + 8, 0);
                                ((uint8_t *)&t132)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t132 + 16), t131);
                                memmove(gm_out, ((uint8_t *)&t132), 112);
                                goto gm_fdone;
                            }
gm_lb18:;
                        }
gm_lb17:;
                        gm_tsg40glpvq01o t133;
                        memset(&t133, 0, sizeof t133);
                        ((uint8_t *)&t133)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t133 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t133), 112);
                        goto gm_fdone;
                    }
gm_lb16:;
                }
gm_lb15:;
                {
                    uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t135 = (uint8_t)(t134 == ((uint8_t)8ULL));
                    if (!t135) {
                        goto gm_lb20;
                    }
                    void * t136 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l17 = t136;
                    {
                        {
                            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t138 = (uint8_t)(t137 == ((uint8_t)8ULL));
                            if (!t138) {
                                goto gm_lb22;
                            }
                            void * t139 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                            gm_l18 = t139;
                            {
                                {
                                    void * t140 = gm_l5;
                                    gm_t88rcucg1b7uk t141 = gm_l3;
                                    gm_f_kind_jcnt4hm((void *)&gm_l45, t140, GM_CONV(gm_csguankko4lmhd, t141));
                                    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                                    uint8_t t143 = (uint8_t)(t142 == ((uint8_t)14ULL));
                                    if (!t143) {
                                        goto gm_lb24;
                                    }
                                    gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l45) + 8) + 4));
                                    gm_l19 = t144;
                                    void * t145 = gm_l0;
                                    void * t146 = gm_l17;
                                    void * t147 = gm_l18;
                                    gm_t88rcucg1b7uk t148 = gm_l19;
                                    gm_tl874t2atnlh3 t149 = gm_l4;
                                    gm_t6evimt84lndq t150 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t146));
                                    gm_t6evimt84lndq t151 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t147));
                                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t145, &t150, &t151, t148, t149);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
gm_lb24:;
                                void * t152 = gm_l0;
                                gm_tl874t2atnlh3 t153 = gm_l4;
                                gm_b16_8 t154;
                                GM_ST(void *, (uint8_t *)&t154, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t155;
                                gm_f_unsupported_2seq49l((void *)&t155, t152, GM_CONV(gm_cseahvg8v7p0vs, t153), t154);
                                gm_tsg40glpvq01o t156;
                                GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t156 + 8, 0);
                                ((uint8_t *)&t156)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t156 + 16), t155);
                                memmove(gm_out, ((uint8_t *)&t156), 112);
                                goto gm_fdone;
                            }
gm_lb23:;
                        }
gm_lb22:;
                        gm_tsg40glpvq01o t157;
                        memset(&t157, 0, sizeof t157);
                        ((uint8_t *)&t157)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t157 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t157), 112);
                        goto gm_fdone;
                    }
gm_lb21:;
                }
gm_lb20:;
            }
            {
                {
                    uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t159 = (uint8_t)(t158 == ((uint8_t)9ULL));
                    if (!t159) {
                        goto gm_lb25;
                    }
                    void * t160 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l20 = t160;
                    {
                        {
                            uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t162 = (uint8_t)(t161 == ((uint8_t)9ULL));
                            if (!t162) {
                                goto gm_lb27;
                            }
                            void * t163 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                            gm_l21 = t163;
                            {
                                {
                                    void * t164 = gm_l5;
                                    gm_t88rcucg1b7uk t165 = gm_l3;
                                    gm_f_kind_jcnt4hm((void *)&gm_l46, t164, GM_CONV(gm_csguankko4lmhd, t165));
                                    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                                    uint8_t t167 = (uint8_t)(t166 == ((uint8_t)14ULL));
                                    if (!t167) {
                                        goto gm_lb29;
                                    }
                                    gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l46) + 8));
                                    gm_l22 = t168;
                                    void * t169 = gm_l0;
                                    void * t170 = gm_l20;
                                    void * t171 = gm_l21;
                                    gm_t88rcucg1b7uk t172 = gm_l22;
                                    gm_tl874t2atnlh3 t173 = gm_l4;
                                    gm_t6evimt84lndq t174 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t170));
                                    gm_t6evimt84lndq t175 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t171));
                                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t169, &t174, &t175, t172, t173);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
gm_lb29:;
                                void * t176 = gm_l0;
                                gm_tl874t2atnlh3 t177 = gm_l4;
                                gm_b16_8 t178;
                                GM_ST(void *, (uint8_t *)&t178, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t179;
                                gm_f_unsupported_2seq49l((void *)&t179, t176, GM_CONV(gm_cseahvg8v7p0vs, t177), t178);
                                gm_tsg40glpvq01o t180;
                                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t180 + 8, 0);
                                ((uint8_t *)&t180)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t180 + 16), t179);
                                memmove(gm_out, ((uint8_t *)&t180), 112);
                                goto gm_fdone;
                            }
gm_lb28:;
                        }
gm_lb27:;
                        gm_tsg40glpvq01o t181;
                        memset(&t181, 0, sizeof t181);
                        ((uint8_t *)&t181)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t181 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t181), 112);
                        goto gm_fdone;
                    }
gm_lb26:;
                }
gm_lb25:;
                {
                    uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t183 = (uint8_t)(t182 == ((uint8_t)10ULL));
                    if (!t183) {
                        goto gm_lb30;
                    }
                    uint32_t t184 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
                    gm_l23 = t184;
                    void * t185 = GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8));
                    gm_l24 = t185;
                    {
                        {
                            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t187 = (uint8_t)(t186 == ((uint8_t)10ULL));
                            if (!t187) {
                                goto gm_lb32;
                            }
                            uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
                            gm_l25 = t188;
                            void * t189 = GM_LD(void *, ((((uint8_t *)&F.l2) + 16) + 8));
                            gm_l26 = t189;
                            uint32_t t190 = gm_l23;
                            uint32_t t191 = gm_l25;
                            uint8_t t192 = (!(t190 == t191));
                            if (t192) {
                                gm_tsg40glpvq01o t193;
                                memset(&t193, 0, sizeof t193);
                                ((uint8_t *)&t193)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t193 + 16), ((uint8_t)0));
                                memmove(gm_out, ((uint8_t *)&t193), 112);
                                goto gm_fdone;
                            }
                            uint8_t t194 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l24)) != 0);
                            if (t194) {
                                void * t195 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                gm_l47 = t195;
                            } else {
                                gm_tsg40glpvq01o t196;
                                memset(&t196, 0, sizeof t196);
                                ((uint8_t *)&t196)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t196 + 16), ((uint8_t)1));
                                memmove(gm_out, ((uint8_t *)&t196), 112);
                                goto gm_fdone;
                            }
                            void * t197 = gm_l47;
                            gm_l27 = t197;
                            uint8_t t198 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l26)) != 0);
                            if (t198) {
                                void * t199 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                gm_l48 = t199;
                            } else {
                                gm_tsg40glpvq01o t200;
                                memset(&t200, 0, sizeof t200);
                                ((uint8_t *)&t200)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t200 + 16), ((uint8_t)1));
                                memmove(gm_out, ((uint8_t *)&t200), 112);
                                goto gm_fdone;
                            }
                            void * t201 = gm_l48;
                            gm_l28 = t201;
                            void * t202 = gm_l0;
                            gm_t88rcucg1b7uk t203 = gm_l3;
                            uint32_t t204 = gm_l23;
                            gm_tkn42u7na3t81 t205 = gm_f_payload_ty_mu1ii1k(t202, GM_CONV(gm_csguankko4lmhd, t203), t204);
                            gm_l49 = t205;
                            uint8_t t206 = (uint8_t)((((uint8_t *)&gm_l49))[4] != 0);
                            if (t206) {
                                gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l49));
                                gm_l50 = t207;
                            } else {
                                void * t208 = gm_l0;
                                gm_tl874t2atnlh3 t209 = gm_l4;
                                gm_b16_8 t210;
                                GM_ST(void *, (uint8_t *)&t210, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t211;
                                gm_f_unsupported_2seq49l((void *)&t211, t208, GM_CONV(gm_cseahvg8v7p0vs, t209), t210);
                                gm_tsg40glpvq01o t212;
                                GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t212 + 8, 0);
                                ((uint8_t *)&t212)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t212 + 16), t211);
                                memmove(gm_out, ((uint8_t *)&t212), 112);
                                goto gm_fdone;
                            }
                            gm_t88rcucg1b7uk t213 = gm_l50;
                            gm_l29 = t213;
                            void * t214 = gm_l0;
                            void * t215 = gm_l27;
                            void * t216 = gm_l28;
                            gm_t88rcucg1b7uk t217 = gm_l29;
                            gm_tl874t2atnlh3 t218 = gm_l4;
                            gm_t6evimt84lndq t219 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t215));
                            gm_t6evimt84lndq t220 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t216));
                            gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t214, &t219, &t220, t217, t218);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb32:;
                        gm_tsg40glpvq01o t221;
                        memset(&t221, 0, sizeof t221);
                        ((uint8_t *)&t221)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t221 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t221), 112);
                        goto gm_fdone;
                    }
gm_lb31:;
                }
gm_lb30:;
            }
            {
                {
                    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t223 = (uint8_t)(t222 == ((uint8_t)3ULL));
                    if (!t223) {
                        goto gm_lb33;
                    }
                    void * t224 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l30 = t224;
                    {
                        {
                            uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t226 = (uint8_t)(t225 == ((uint8_t)3ULL));
                            if (!t226) {
                                goto gm_lb35;
                            }
                            void * t227 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                            gm_l31 = t227;
                            void * t228 = gm_l30;
                            uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)t228) + 8));
                            void * t230 = gm_l31;
                            uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)t230) + 8));
                            uint8_t t232 = (!(t229 == t231));
                            if (t232) {
                                gm_tsg40glpvq01o t233;
                                memset(&t233, 0, sizeof t233);
                                ((uint8_t *)&t233)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t233 + 16), ((uint8_t)0));
                                memmove(gm_out, ((uint8_t *)&t233), 112);
                                goto gm_fdone;
                            }
                            void * t234 = gm_l0;
                            gm_t88rcucg1b7uk t235 = gm_l3;
                            void * t236 = gm_l30;
                            uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)t236) + 8));
                            gm_f_field_tys_foan32f((void *)&gm_l51, (void *)NULL, cx, NULL, t234, t235, t237);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t238 = (uint8_t)((((uint8_t *)&gm_l51))[16] != 0);
                            if (t238) {
                                gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l51));
                                gm_l52 = t239;
                            } else {
                                void * t240 = gm_l0;
                                gm_tl874t2atnlh3 t241 = gm_l4;
                                gm_b16_8 t242;
                                GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t243;
                                gm_f_unsupported_2seq49l((void *)&t243, t240, GM_CONV(gm_cseahvg8v7p0vs, t241), t242);
                                gm_tsg40glpvq01o t244;
                                GM_ST(uint8_t, (uint8_t *)&t244 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t244 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t244 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t244 + 8, 0);
                                ((uint8_t *)&t244)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t244 + 16), t243);
                                memmove(gm_out, ((uint8_t *)&t244), 112);
                                goto gm_fdone;
                            }
                            gm_b16_8 t245 = gm_l52;
                            gm_l32 = t245;
                            gm_l34 = ((uint64_t)0ULL);
                            void * t246 = gm_l30;
                            uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)t246) + 8));
                            gm_l33 = t247;
                            {
                                for (;;) {
                                    uint64_t t248 = gm_l34;
                                    uint64_t t249 = gm_l33;
                                    uint8_t t250 = (t248 < t249);
                                    if (!t250) {
                                        goto gm_lb36;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t251 = gm_l34;
                                                gm_l35 = t251;
                                                void * t252 = gm_l0;
                                                void * t253 = gm_l30;
                                                void * t254 = GM_LD(void *, ((uint8_t *)t253));
                                                uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)t253) + 8));
                                                uint64_t t256 = gm_l35;
                                                if ((uintptr_t)t256 >= (uintptr_t)(t255)) gm_trap_index((uintptr_t)t256, t255, gm_srcue4ionbe9lgh, 891, 36);
                                                void * t257 = gm_l31;
                                                void * t258 = GM_LD(void *, ((uint8_t *)t257));
                                                uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)t257) + 8));
                                                uint64_t t260 = gm_l35;
                                                if ((uintptr_t)t260 >= (uintptr_t)(t259)) gm_trap_index((uintptr_t)t260, t259, gm_srcue4ionbe9lgh, 891, 48);
                                                void * t261 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                                uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                uint64_t t263 = gm_l35;
                                                if ((uintptr_t)t263 >= (uintptr_t)(t262)) gm_trap_index((uintptr_t)t263, t262, gm_srcue4ionbe9lgh, 891, 60);
                                                gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t261) + (uintptr_t)t263 * 4));
                                                gm_tl874t2atnlh3 t265 = gm_l4;
                                                gm_t6evimt84lndq t266 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t254) + (uintptr_t)t256 * 48));
                                                gm_t6evimt84lndq t267 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t258) + (uintptr_t)t260 * 48));
                                                gm_f_equal_hinsj77((void *)&gm_l53, (void *)NULL, cx, NULL, t252, &t266, &t267, t264, t265);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t268 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                                if (t268) {
                                                } else {
                                                    gm_t39uai1ajk3qv t269 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l53) + 16));
                                                    gm_tsg40glpvq01o t270;
                                                    GM_ST(uint8_t, (uint8_t *)&t270 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t270 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t270 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t270 + 8, 0);
                                                    ((uint8_t *)&t270)[0] = 1;
                                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t270 + 16), t269);
                                                    memmove(gm_out, ((uint8_t *)&t270), 112);
                                                    goto gm_fdone;
                                                }
                                                uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l53) + 16));
                                                uint8_t t272 = (uint8_t)!t271;
                                                if (t272) {
                                                    gm_tsg40glpvq01o t273;
                                                    memset(&t273, 0, sizeof t273);
                                                    ((uint8_t *)&t273)[0] = 0;
                                                    GM_ST(uint8_t, ((uint8_t *)&t273 + 16), ((uint8_t)0));
                                                    memmove(gm_out, ((uint8_t *)&t273), 112);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb39;
                                            }
gm_lb40:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 890, 13);
                                        }
gm_lb39:;
                                    }
gm_lb38:;
                                    uint64_t t274 = gm_l34;
                                    uint64_t r275; bool o276 = __builtin_add_overflow(t274, ((uint64_t)1ULL), &r275);
                                    uint64_t t277 = r275;
                                    gm_l34 = t277;
                                }
                            }
gm_lb36:;
                            gm_tsg40glpvq01o t278;
                            memset(&t278, 0, sizeof t278);
                            ((uint8_t *)&t278)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t278 + 16), ((uint8_t)1));
                            memmove(gm_out, ((uint8_t *)&t278), 112);
                            goto gm_fdone;
                        }
gm_lb35:;
                        gm_tsg40glpvq01o t279;
                        memset(&t279, 0, sizeof t279);
                        ((uint8_t *)&t279)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t279 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t279), 112);
                        goto gm_fdone;
                    }
gm_lb34:;
                }
gm_lb33:;
            }
            {
                {
                    uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t281 = (uint8_t)(t280 == ((uint8_t)5ULL));
                    if (!t281) {
                        goto gm_lb41;
                    }
                    memmove(((uint8_t *)&gm_l36), (((uint8_t *)&F.l1) + 16), 32);
                    {
                        {
                            uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t283 = (uint8_t)(t282 == ((uint8_t)5ULL));
                            if (!t283) {
                                goto gm_lb43;
                            }
                            memmove(((uint8_t *)&gm_l37), (((uint8_t *)&F.l2) + 16), 32);
                            {
                                {
                                    void * t284 = gm_l5;
                                    gm_t88rcucg1b7uk t285 = gm_l3;
                                    gm_f_kind_jcnt4hm((void *)&gm_l54, t284, GM_CONV(gm_csguankko4lmhd, t285));
                                    uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                                    uint8_t t287 = (uint8_t)(t286 == ((uint8_t)10ULL));
                                    if (!t287) {
                                        goto gm_lb45;
                                    }
                                    gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l54) + 8) + 4));
                                    gm_l38 = t288;
                                    uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                                    uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 24));
                                    uint8_t t291 = (!(t289 == t290));
                                    if (t291) {
                                        gm_tsg40glpvq01o t292;
                                        memset(&t292, 0, sizeof t292);
                                        ((uint8_t *)&t292)[0] = 0;
                                        GM_ST(uint8_t, ((uint8_t *)&t292 + 16), ((uint8_t)0));
                                        memmove(gm_out, ((uint8_t *)&t292), 112);
                                        goto gm_fdone;
                                    }
                                    gm_l40 = ((uint64_t)0ULL);
                                    uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                                    gm_l39 = t293;
                                    {
                                        for (;;) {
                                            uint64_t t294 = gm_l40;
                                            uint64_t t295 = gm_l39;
                                            uint8_t t296 = (t294 < t295);
                                            if (!t296) {
                                                goto gm_lb46;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t297 = gm_l40;
                                                        gm_l41 = t297;
                                                        void * t298 = gm_l0;
                                                        void * t299 = gm_l0;
                                                        uint64_t t300 = gm_l41;
                                                        gm_tkaapcii86udk t301;
                                                        gm_f_offset_nkn711s((void *)&t301, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l36)), t300);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_t6evimt84lndq t302;
                                                        gm_f_load_nf7o5kl((void *)&t302, t299, &t301);
                                                        void * t303 = gm_l0;
                                                        uint64_t t304 = gm_l41;
                                                        gm_tkaapcii86udk t305;
                                                        gm_f_offset_nkn711s((void *)&t305, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l37)), t304);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_t6evimt84lndq t306;
                                                        gm_f_load_nf7o5kl((void *)&t306, t303, &t305);
                                                        gm_t88rcucg1b7uk t307 = gm_l38;
                                                        gm_tl874t2atnlh3 t308 = gm_l4;
                                                        gm_f_equal_hinsj77((void *)&gm_l55, (void *)NULL, cx, NULL, t298, &t302, &t306, t307, t308);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t309 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                                        if (t309) {
                                                        } else {
                                                            gm_t39uai1ajk3qv t310 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l55) + 16));
                                                            gm_tsg40glpvq01o t311;
                                                            GM_ST(uint8_t, (uint8_t *)&t311 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t311 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t311 + 4, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&t311 + 8, 0);
                                                            ((uint8_t *)&t311)[0] = 1;
                                                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t311 + 16), t310);
                                                            memmove(gm_out, ((uint8_t *)&t311), 112);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t312 = GM_LD(uint8_t, (((uint8_t *)&gm_l55) + 16));
                                                        uint8_t t313 = (uint8_t)!t312;
                                                        if (t313) {
                                                            gm_tsg40glpvq01o t314;
                                                            memset(&t314, 0, sizeof t314);
                                                            ((uint8_t *)&t314)[0] = 0;
                                                            GM_ST(uint8_t, ((uint8_t *)&t314 + 16), ((uint8_t)0));
                                                            memmove(gm_out, ((uint8_t *)&t314), 112);
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb49;
                                                    }
gm_lb50:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 899, 13);
                                                }
gm_lb49:;
                                            }
gm_lb48:;
                                            uint64_t t315 = gm_l40;
                                            uint64_t r316; bool o317 = __builtin_add_overflow(t315, ((uint64_t)1ULL), &r316);
                                            uint64_t t318 = r316;
                                            gm_l40 = t318;
                                        }
                                    }
gm_lb46:;
                                    gm_tsg40glpvq01o t319;
                                    memset(&t319, 0, sizeof t319);
                                    ((uint8_t *)&t319)[0] = 0;
                                    GM_ST(uint8_t, ((uint8_t *)&t319 + 16), ((uint8_t)1));
                                    memmove(gm_out, ((uint8_t *)&t319), 112);
                                    goto gm_fdone;
                                }
gm_lb45:;
                                void * t320 = gm_l0;
                                gm_tl874t2atnlh3 t321 = gm_l4;
                                gm_b16_8 t322;
                                GM_ST(void *, (uint8_t *)&t322, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t323;
                                gm_f_unsupported_2seq49l((void *)&t323, t320, GM_CONV(gm_cseahvg8v7p0vs, t321), t322);
                                gm_tsg40glpvq01o t324;
                                GM_ST(uint8_t, (uint8_t *)&t324 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t324 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t324 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t324 + 8, 0);
                                ((uint8_t *)&t324)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t324 + 16), t323);
                                memmove(gm_out, ((uint8_t *)&t324), 112);
                                goto gm_fdone;
                            }
gm_lb44:;
                        }
gm_lb43:;
                        gm_tsg40glpvq01o t325;
                        memset(&t325, 0, sizeof t325);
                        ((uint8_t *)&t325)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t325 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t325), 112);
                        goto gm_fdone;
                    }
gm_lb42:;
                }
gm_lb41:;
            }
            {
                {
                    uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t327 = (uint8_t)(t326 == ((uint8_t)4ULL));
                    if (!t327) {
                        goto gm_lb51;
                    }
                    memmove(((uint8_t *)&gm_l42), (((uint8_t *)&F.l1) + 16), 24);
                    {
                        {
                            uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t329 = (uint8_t)(t328 == ((uint8_t)4ULL));
                            if (!t329) {
                                goto gm_lb53;
                            }
                            memmove(((uint8_t *)&gm_l43), (((uint8_t *)&F.l2) + 16), 24);
                            uint8_t t330 = gm_f_same_place_1r7qrqk(&(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l42)), &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l43)));
                            gm_tsg40glpvq01o t331;
                            memset(&t331, 0, sizeof t331);
                            ((uint8_t *)&t331)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t331 + 16), t330);
                            memmove(gm_out, ((uint8_t *)&t331), 112);
                            goto gm_fdone;
                        }
gm_lb53:;
                        gm_tsg40glpvq01o t332;
                        memset(&t332, 0, sizeof t332);
                        ((uint8_t *)&t332)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t332 + 16), ((uint8_t)0));
                        memmove(gm_out, ((uint8_t *)&t332), 112);
                        goto gm_fdone;
                    }
gm_lb52:;
                }
gm_lb51:;
                {
                    void * t333 = gm_l0;
                    gm_tl874t2atnlh3 t334 = gm_l4;
                    gm_b16_8 t335;
                    GM_ST(void *, (uint8_t *)&t335, (void *)(gm_s8afn1h3asffl));
                    GM_ST(uintptr_t, (uint8_t *)&t335 + 8, (uintptr_t)(31));
                    gm_t39uai1ajk3qv t336;
                    gm_f_unsupported_2seq49l((void *)&t336, t333, GM_CONV(gm_cseahvg8v7p0vs, t334), t335);
                    gm_tsg40glpvq01o t337;
                    GM_ST(uint8_t, (uint8_t *)&t337 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t337 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t337 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t337 + 8, 0);
                    ((uint8_t *)&t337)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t337 + 16), t336);
                    memmove(gm_out, ((uint8_t *)&t337), 112);
                    goto gm_fdone;
                }
gm_lb54:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 845, 5);
            }
        }
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_field_tys_foan32f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2) {
    struct gm_fr_field_tys_foan32f F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* n */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l4; /* elems */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* id */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 688u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = (void *)(((uint8_t *)t20) + 8);
    gm_l3 = t21;
    void * t22 = gm_l3;
    gm_t88rcucg1b7uk t23 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l15, t22, GM_CONV(gm_csguankko4lmhd, t23));
    {
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)12ULL));
            if (!t25) {
                goto gm_lb2;
            }
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
            gm_l4 = t26;
            gm_b16_8 t27 = gm_l4;
            gm_th2lqbo77pvsc t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t28), t27);
            ((uint8_t *)&t28)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t28), 24);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)11ULL));
            if (!t30) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 8));
            gm_l5 = t31;
            gm_trqfvmjtt4sij t32;
            gm_f_list_nof7vfj((void *)&t32, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l6 = t32;
            gm_l8 = ((uint64_t)0ULL);
            uint64_t t33 = gm_l2;
            gm_l7 = t33;
            {
                for (;;) {
                    uint64_t t34 = gm_l8;
                    uint64_t t35 = gm_l7;
                    uint8_t t36 = (t34 < t35);
                    if (!t36) {
                        goto gm_lb4;
                    }
                    {
                        {
                            {
                                void * t37 = (void *)((uint8_t *)&F.l6);
                                gm_t88rcucg1b7uk t38 = gm_l5;
                                gm_tftb1odc739t5 t39 = gm_f_push_k3l9nic(t37, GM_CONV(gm_csguankko4lmhd, t38));
                                gm_l16 = t39;
                                uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcue4ionbe9lgh, 968, 29);
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 968, 13);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t41 = gm_l8;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l8 = t44;
                }
            }
gm_lb4:;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_th2lqbo77pvsc t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t47), t46);
            ((uint8_t *)&t47)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t47), 24);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)17ULL));
            if (!t49) {
                goto gm_lb9;
            }
            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 8));
            gm_l9 = t50;
            {
                {
                    void * t51 = gm_l0;
                    void * t52 = GM_LD(void *, ((uint8_t *)t51));
                    void * t53 = GM_LD(void *, (((uint8_t *)t52) + 1088));
                    uint64_t t54 = GM_LD(uint64_t, ((((uint8_t *)t52) + 1088) + 8));
                    uint32_t t55 = gm_l9;
                    uint64_t t56 = (uint64_t)t55;
                    if ((uintptr_t)t56 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t56, t54, gm_srcue4ionbe9lgh, 972, 29);
                    uint8_t t57 = GM_LD(uint8_t, (((((uint8_t *)t53) + (uintptr_t)t56 * 184) + 24) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
                    if (!t58) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&F.l10), (((((uint8_t *)t53) + (uintptr_t)t56 * 184) + 24) + 8), 48);
                    gm_trqfvmjtt4sij t59;
                    gm_f_list_nof7vfj((void *)&t59, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l11 = t59;
                    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                    gm_l12 = t60;
                    gm_l13 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t61 = gm_l13;
                            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint8_t t63 = (t61 < t62);
                            if (!t63) {
                                goto gm_lb12;
                            }
                            {
                                void * t64 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t66 = gm_l13;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t64) + (uintptr_t)t66 * 40), 40);
                                        void * t67 = (void *)((uint8_t *)&F.l11);
                                        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                                        gm_tftb1odc739t5 t69 = gm_f_push_k3l9nic(t67, GM_CONV(gm_csguankko4lmhd, t68));
                                        gm_l17 = t69;
                                        uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                        if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcue4ionbe9lgh, 974, 35);
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 974, 13);
                                }
gm_lb15:;
                            }
gm_lb14:;
                            uint64_t t71 = gm_l13;
                            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                            uint64_t t74 = r72;
                            gm_l13 = t74;
                        }
                    }
gm_lb12:;
                    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                    gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                    gm_th2lqbo77pvsc t77;
                    GM_ST(uint8_t, (uint8_t *)&t77 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t77 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t77 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t77), t76);
                    ((uint8_t *)&t77)[16] = 1;
                    memmove(gm_out, ((uint8_t *)&t77), 24);
                    goto gm_fdone;
                }
gm_lb11:;
                gm_th2lqbo77pvsc t78;
                memset(&t78, 0, sizeof t78);
                memmove(gm_out, ((uint8_t *)&t78), 24);
                goto gm_fdone;
            }
gm_lb10:;
        }
gm_lb9:;
        {
            gm_th2lqbo77pvsc t79;
            memset(&t79, 0, sizeof t79);
            memmove(gm_out, ((uint8_t *)&t79), 24);
            goto gm_fdone;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 964, 5);
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
inline __attribute__((aligned(4))) void gm_f_fit_8hvi4bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_fit_8hvi4bv F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l2; /* flavor */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* sym */
    gm_tl874t2atnlh3 gm_l5; /* span */
    GM_STACK_CHECK(&F, 901u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tbci5ospk5nnn gm_l7;
    gm_tabb710d9mq2e gm_l8;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l3;
    gm_f_int_range_hue3vms((void *)&F.l6, (void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            int32_t t10 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
            uint8_t t11 = (t10 >= ((int32_t)0LL));
            if (!t11) {
                goto gm_lb2;
            }
            int32_t t12 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l6) + 48)));
            uint8_t t13 = (t12 <= ((int32_t)0LL));
            if (!t13) {
                goto gm_lb2;
            }
            void * t14 = gm_l0;
            unsigned __int128 t15 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
            gm_t88rcucg1b7uk t16 = gm_l3;
            unsigned __int128 t17 = gm_f_wrap_bt8r149(t14, t15, GM_CONV(gm_csguankko4lmhd, t16));
            gm_t8carr7mku484 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t18 + 16), t17);
            memmove(gm_out, ((uint8_t *)&t18), 112);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb4;
            }
            void * t21 = gm_l0;
            unsigned __int128 t22 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
            gm_t88rcucg1b7uk t23 = gm_l3;
            unsigned __int128 t24 = gm_f_wrap_bt8r149(t21, t22, GM_CONV(gm_csguankko4lmhd, t23));
            gm_t8carr7mku484 t25;
            memset(&t25, 0, sizeof t25);
            ((uint8_t *)&t25)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t25 + 16), t24);
            memmove(gm_out, ((uint8_t *)&t25), 112);
            goto gm_fdone;
        }
gm_lb4:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)2ULL));
            if (!t27) {
                goto gm_lb5;
            }
            void * t28 = gm_l0;
            uint8_t t29 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
            if (t29) {
                memmove(((uint8_t *)&gm_l7), ((uint8_t *)&F.l6), 48);
            } else {
                memmove(((uint8_t *)&gm_l7), (((uint8_t *)&F.l6) + 48), 48);
            }
            unsigned __int128 t30 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&gm_l7)));
            gm_t88rcucg1b7uk t31 = gm_l3;
            unsigned __int128 t32 = gm_f_wrap_bt8r149(t28, t30, GM_CONV(gm_csguankko4lmhd, t31));
            gm_t8carr7mku484 t33;
            memset(&t33, 0, sizeof t33);
            ((uint8_t *)&t33)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t33 + 16), t32);
            memmove(gm_out, ((uint8_t *)&t33), 112);
            goto gm_fdone;
        }
gm_lb5:;
        {
            void * t34 = gm_l0;
            gm_tl874t2atnlh3 t35 = gm_l5;
            gm_b16_8 t36 = gm_l4;
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_f_format_eruojoe((void *)&gm_l8, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcue4ionbe9lgh, 53, 35);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_t39uai1ajk3qv t41;
            gm_f_trap_jftbss6((void *)&t41, (void *)NULL, cx, NULL, t34, t35, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t8carr7mku484 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t42 + 8, 0);
            ((uint8_t *)&t42)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t42 + 16), t41);
            memmove(gm_out, ((uint8_t *)&t42), 112);
            goto gm_fdone;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 50, 5);
    }
gm_lb3:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_fit_small_7o94i8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, __int128 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_fit_small_7o94i8v F;
    void * gm_l0; /* m */
    __int128 gm_l1; /* exact */
    gm_t9qkh0rsooc8d gm_l2; /* flavor */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* sym */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_ttovhfee8mf28 gm_l6; /* info */
    __int128 gm_l7; /* lo */
    __int128 gm_l8; /* hi */
    GM_STACK_CHECK(&F, 1267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    __int128 gm_l9;
    __int128 gm_l10;
    __int128 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l3;
    gm_ttovhfee8mf28 t10 = gm_f_int_info_2r9qb5n(t8, GM_CONV(gm_csguankko4lmhd, t9));
    gm_l6 = t10;
    uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
    if (t11) {
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint32_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint32_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 60, 47);
        uint32_t t15 = r13;
        unsigned __int128 n16 = (unsigned __int128)t15;
        if (n16 >= 128) gm_trap_shift((uint64_t)n16, "i128", gm_srcue4ionbe9lgh, 60, 35);
        __int128 r17 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n16);
        bool o18 = (__int128)(r17 >> (int)n16) != ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL));
        if (o18) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 60, 35);
        __int128 t19 = r17;
        if (t19 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 60, 33);
        __int128 t20 = (__int128)((unsigned __int128)0 - (unsigned __int128)t19);
        gm_l9 = t20;
    } else {
        gm_l9 = ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL));
    }
    __int128 t21 = gm_l9;
    gm_l7 = t21;
    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
    if (t22) {
        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint32_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint32_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 46);
        uint32_t t26 = r24;
        unsigned __int128 n27 = (unsigned __int128)t26;
        if (n27 >= 128) gm_trap_shift((uint64_t)n27, "i128", gm_srcue4ionbe9lgh, 61, 34);
        __int128 r28 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n27);
        bool o29 = (__int128)(r28 >> (int)n27) != ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL));
        if (o29) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 61, 34);
        __int128 t30 = r28;
        __int128 r31; bool o32 = __builtin_sub_overflow(t30, ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 33);
        __int128 t33 = r31;
        gm_l10 = t33;
    } else {
        uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        unsigned __int128 n35 = (unsigned __int128)t34;
        if (n35 >= 128) gm_trap_shift((uint64_t)n35, "i128", gm_srcue4ionbe9lgh, 61, 76);
        __int128 r36 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n35);
        bool o37 = (__int128)(r36 >> (int)n35) != ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL));
        if (o37) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 61, 76);
        __int128 t38 = r36;
        __int128 r39; bool o40 = __builtin_sub_overflow(t38, ((__int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r39);
        if (o40) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 75);
        __int128 t41 = r39;
        gm_l10 = t41;
    }
    __int128 t42 = gm_l10;
    gm_l8 = t42;
    {
        {
            __int128 t43 = gm_l1;
            __int128 t44 = gm_l7;
            uint8_t t45 = (t43 >= t44);
            if (!t45) {
                goto gm_lb2;
            }
            __int128 t46 = gm_l1;
            __int128 t47 = gm_l8;
            uint8_t t48 = (t46 <= t47);
            if (!t48) {
                goto gm_lb2;
            }
            void * t49 = gm_l0;
            __int128 t50 = gm_l1;
            unsigned __int128 t51;
            memset(&t51, 0, sizeof t51); memcpy(&t51, ((uint8_t *)&t50), sizeof t51 < 16 ? sizeof t51 : 16);
            gm_t88rcucg1b7uk t52 = gm_l3;
            unsigned __int128 t53 = gm_f_wrap_bt8r149(t49, t51, GM_CONV(gm_csguankko4lmhd, t52));
            gm_t8carr7mku484 t54;
            memset(&t54, 0, sizeof t54);
            ((uint8_t *)&t54)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t54 + 16), t53);
            memmove(gm_out, ((uint8_t *)&t54), 112);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)1ULL));
            if (!t56) {
                goto gm_lb4;
            }
            void * t57 = gm_l0;
            __int128 t58 = gm_l1;
            unsigned __int128 t59;
            memset(&t59, 0, sizeof t59); memcpy(&t59, ((uint8_t *)&t58), sizeof t59 < 16 ? sizeof t59 : 16);
            gm_t88rcucg1b7uk t60 = gm_l3;
            unsigned __int128 t61 = gm_f_wrap_bt8r149(t57, t59, GM_CONV(gm_csguankko4lmhd, t60));
            gm_t8carr7mku484 t62;
            memset(&t62, 0, sizeof t62);
            ((uint8_t *)&t62)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t62 + 16), t61);
            memmove(gm_out, ((uint8_t *)&t62), 112);
            goto gm_fdone;
        }
gm_lb4:;
        {
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)2ULL));
            if (!t64) {
                goto gm_lb5;
            }
            void * t65 = gm_l0;
            __int128 t66 = gm_l1;
            uint8_t t67 = (t66 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
            if (t67) {
                __int128 t68 = gm_l7;
                gm_l11 = t68;
            } else {
                __int128 t69 = gm_l8;
                gm_l11 = t69;
            }
            __int128 t70 = gm_l11;
            unsigned __int128 t71;
            memset(&t71, 0, sizeof t71); memcpy(&t71, ((uint8_t *)&t70), sizeof t71 < 16 ? sizeof t71 : 16);
            gm_t88rcucg1b7uk t72 = gm_l3;
            unsigned __int128 t73 = gm_f_wrap_bt8r149(t65, t71, GM_CONV(gm_csguankko4lmhd, t72));
            gm_t8carr7mku484 t74;
            memset(&t74, 0, sizeof t74);
            ((uint8_t *)&t74)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t74 + 16), t73);
            memmove(gm_out, ((uint8_t *)&t74), 112);
            goto gm_fdone;
        }
gm_lb5:;
        {
            void * t75 = gm_l0;
            gm_tl874t2atnlh3 t76 = gm_l5;
            gm_b16_8 t77 = gm_l4;
            gm_tnpifv20irnph t78;
            GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t77);
            gm_f_format_eruojoe((void *)&gm_l12, (void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcue4ionbe9lgh, 66, 35);
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
            gm_t39uai1ajk3qv t82;
            gm_f_trap_jftbss6((void *)&t82, (void *)NULL, cx, NULL, t75, t76, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t8carr7mku484 t83;
            GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t83 + 8, 0);
            ((uint8_t *)&t83)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t83 + 16), t82);
            memmove(gm_out, ((uint8_t *)&t83), 112);
            goto gm_fdone;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 63, 5);
    }
gm_lb3:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_float_exact_1g17c6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0) {
    struct gm_fr_float_exact_1g17c6c F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* raw */
    uint8_t gm_l2; /* negative */
    int64_t gm_l3; /* exp */
    uint64_t gm_l4; /* mant */
    int64_t gm_l5; /* e */
    GM_STACK_CHECK(&F, 1046u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t2 = gm_l0;
    double t3 = gm_l0;
    double t4 = (t2 - t3);
    uint8_t t5 = (!(t4 == ((double)0e0)));
    if (t5) {
        gm_tvegkb6ouf3m0 t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 104);
        goto gm_fdone;
    }
    double t7 = gm_l0;
    uint64_t t8;
    memset(&t8, 0, sizeof t8); memcpy(&t8, ((uint8_t *)&t7), sizeof t8 < 8 ? sizeof t8 : 8);
    gm_l1 = t8;
    uint64_t t9 = gm_l1;
    unsigned __int128 n10 = (unsigned __int128)((uint64_t)63ULL);
    if (n10 >= 64) gm_trap_shift((uint64_t)n10, "u64", gm_srcue4ionbe9lgh, 316, 17);
    uint64_t t11 = (uint64_t)(t9 >> (int)n10);
    uint8_t t12 = ((t11 == ((uint64_t)1ULL)));
    gm_l2 = t12;
    uint64_t t13 = gm_l1;
    unsigned __int128 n14 = (unsigned __int128)((uint64_t)52ULL);
    if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcue4ionbe9lgh, 317, 14);
    uint64_t t15 = (uint64_t)(t13 >> (int)n14);
    uint64_t t16 = (uint64_t)(t15 & ((uint64_t)2047ULL));
    int64_t t17;
    memset(&t17, 0, sizeof t17); memcpy(&t17, ((uint8_t *)&t16), sizeof t17 < 8 ? sizeof t17 : 8);
    gm_l3 = t17;
    uint64_t t18 = gm_l1;
    uint64_t t19 = (uint64_t)(t18 & ((uint64_t)4503599627370495ULL));
    gm_l4 = t19;
    int64_t t20 = gm_l3;
    uint8_t t21 = ((t20 == ((int64_t)0LL)));
    if (t21) {
        gm_l3 = ((int64_t)1LL);
    } else {
        uint64_t t22 = gm_l4;
        unsigned __int128 n23 = (unsigned __int128)((uint64_t)52ULL);
        if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcue4ionbe9lgh, 322, 17);
        uint64_t r24 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n23);
        bool o25 = (uint64_t)(r24 >> (int)n23) != ((uint64_t)1ULL);
        if (o25) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 322, 17);
        uint64_t t26 = r24;
        uint64_t t27 = (uint64_t)(t22 | t26);
        gm_l4 = t27;
    }
    int64_t t28 = gm_l3;
    int64_t r29; bool o30 = __builtin_sub_overflow(t28, ((int64_t)1075LL), &r29);
    if (o30) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 324, 10);
    int64_t t31 = r29;
    gm_l5 = t31;
    uint64_t t32 = gm_l4;
    gm_f_from_u64_ud13q5v((void *)&F.l6, (void *)NULL, cx, NULL, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t33 = gm_l2;
    if (t33) {
        gm_tbci5ospk5nnn t34 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l6));
        gm_f_neg_agd5c5k((void *)&F.l6, (void *)NULL, cx, NULL, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    int64_t t35 = gm_l5;
    uint8_t t36 = (t35 >= ((int64_t)0LL));
    if (t36) {
        int64_t t37 = gm_l5;
        gm_tbci5ospk5nnn t38 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l6));
        uint64_t t39;
        memset(&t39, 0, sizeof t39); memcpy(&t39, ((uint8_t *)&t37), sizeof t39 < 8 ? sizeof t39 : 8);
        gm_tbci5ospk5nnn t40;
        gm_f_shl_qqecl81((void *)&t40, (void *)NULL, cx, NULL, &t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t41;
        gm_f_from_int_ofjuoqh((void *)&t41, (void *)NULL, cx, NULL, &t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 97, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 98, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 100, 0);
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t42), t41);
        ((uint8_t *)&t42)[96] = 1;
        memmove(gm_out, ((uint8_t *)&t42), 104);
        goto gm_fdone;
    }
    gm_tbci5ospk5nnn t43 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l6));
    gm_tbci5ospk5nnn t44;
    gm_f_one_08qg1op((void *)&t44, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int64_t t45 = gm_l5;
    if (t45 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 328, 45);
    int64_t t46 = (int64_t)((uint64_t)0 - (uint64_t)t45);
    uint64_t t47;
    memset(&t47, 0, sizeof t47); memcpy(&t47, ((uint8_t *)&t46), sizeof t47 < 8 ? sizeof t47 : 8);
    gm_tbci5ospk5nnn t48;
    gm_f_shl_qqecl81((void *)&t48, (void *)NULL, cx, NULL, &t44, t47);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t49;
    gm_f_new_2jv6hag((void *)&t49, (void *)NULL, cx, NULL, &t43, &t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvegkb6ouf3m0 t50;
    GM_ST(uint8_t, (uint8_t *)&t50 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t50 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t50 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t50), t49);
    ((uint8_t *)&t50)[96] = 1;
    memmove(gm_out, ((uint8_t *)&t50), 104);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) double gm_f_float_rem_24loign(void *env, gm_node *cx, gm_node *inst, double a0, double a1) {
    struct gm_fr_float_rem_24loign F;
    double gm_l0; /* x */
    double gm_l1; /* y */
    GM_STACK_CHECK(&F, 1276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_tvegkb6ouf3m0 gm_l9;
    gm_tngtuiuskn938 gm_l10;
    gm_tvegkb6ouf3m0 gm_l11;
    gm_tngtuiuskn938 gm_l12;
    double gm_l13;
    double t2 = gm_l0;
    double t3 = gm_l0;
    double t4 = (t2 - t3);
    uint8_t t5 = (!(t4 == ((double)0e0)));
    if (t5) {
        gm_l8 = ((uint8_t)1);
    } else {
        double t6 = gm_l1;
        uint8_t t7 = ((t6 == ((double)0e0)));
        gm_l8 = t7;
    }
    uint8_t t8 = gm_l8;
    if (t8) {
        gm_l7 = ((uint8_t)1);
    } else {
        double t9 = gm_l1;
        double t10 = gm_l1;
        uint8_t t11 = (!(t9 == t10));
        gm_l7 = t11;
    }
    uint8_t t12 = gm_l7;
    if (t12) {
        double t13 = (((double)0e0) / ((double)0e0));
        F.ret = t13;
        goto gm_fret;
    }
    double t14 = gm_l1;
    double t15 = gm_l1;
    double t16 = (t14 - t15);
    uint8_t t17 = (!(t16 == ((double)0e0)));
    if (t17) {
        double t18 = gm_l0;
        F.ret = t18;
        goto gm_fret;
    }
    double t19 = gm_l0;
    gm_f_float_exact_1g17c6c((void *)&gm_l9, (void *)NULL, cx, NULL, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l9))[96] != 0);
    if (t20) {
        memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l9), 96);
    } else {
        double t21 = (((double)0e0) / ((double)0e0));
        F.ret = t21;
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l10), 96);
    double t22 = gm_l1;
    gm_f_float_exact_1g17c6c((void *)&gm_l11, (void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l11))[96] != 0);
    if (t23) {
        memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l11), 96);
    } else {
        double t24 = (((double)0e0) / ((double)0e0));
        F.ret = t24;
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l12), 96);
    gm_tngtuiuskn938 t25 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l2));
    gm_tngtuiuskn938 t26 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l3));
    gm_f_div_d8sd4qc((void *)&F.l4, (void *)NULL, cx, NULL, &t25, &t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t27 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l4));
    gm_tbci5ospk5nnn t28 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l4) + 48));
    gm_f_div_f0qrfbr((void *)&F.l5, (void *)NULL, cx, NULL, &t27, &t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t29 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l2));
    gm_tngtuiuskn938 t30 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l3));
    gm_tngtuiuskn938 t31;
    gm_f_from_int_ofjuoqh((void *)&t31, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t32;
    gm_f_mul_od1e7nd((void *)&t32, (void *)NULL, cx, NULL, &t30, &t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_sub_mler6q8((void *)&F.l6, (void *)NULL, cx, NULL, &t29, &t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t33 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
    if (t33) {
        double t34 = gm_l0;
        uint8_t t35 = (t34 < ((double)0e0));
        if (t35) {
            double t36 = -((double)0e0);
            gm_l13 = t36;
        } else {
            gm_l13 = ((double)0e0);
        }
        double t37 = gm_l13;
        F.ret = t37;
        goto gm_fret;
    }
    double t38 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_float_to_int_sf4dap7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3) {
    struct gm_fr_float_to_int_sf4dap7 F;
    void * gm_l0; /* m */
    double gm_l1; /* x */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tvdg94bk7aa22 gm_l3; /* mode */
    uint8_t gm_l8; /* exact */
    int32_t gm_l10; /* c */
    GM_STACK_CHECK(&F, 1192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tvegkb6ouf3m0 gm_l12;
    gm_tngtuiuskn938 gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    double t9 = gm_l1;
    gm_f_float_exact_1g17c6c((void *)&gm_l12, (void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l12))[96] != 0);
    if (t10) {
        memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l12), 96);
    } else {
        gm_thrdpodrs8pgt t11;
        memset(&t11, 0, sizeof t11);
        memmove(gm_out, ((uint8_t *)&t11), 56);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l13), 96);
    gm_tbci5ospk5nnn t12 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l4));
    gm_tbci5ospk5nnn t13 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l4) + 48));
    gm_f_div_floor_u3mfihk((void *)&F.l5, (void *)NULL, cx, NULL, &t12, &t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t14 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l4));
    gm_tngtuiuskn938 t15;
    gm_f_from_int_ofjuoqh((void *)&t15, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_sub_mler6q8((void *)&F.l6, (void *)NULL, cx, NULL, &t14, &t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    memmove(((uint8_t *)&F.l7), ((uint8_t *)&F.l5), 48);
    uint8_t t16 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
    gm_l8 = t16;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
            if (!t18) {
                goto gm_lb2;
            }
            {
                {
                    uint8_t t19 = gm_l8;
                    uint8_t t20 = (uint8_t)!t19;
                    if (!t20) {
                        goto gm_lb4;
                    }
                    uint8_t t21 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
                    if (!t21) {
                        goto gm_lb4;
                    }
                    gm_tbci5ospk5nnn t22 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l5));
                    gm_tbci5ospk5nnn t23;
                    gm_f_one_08qg1op((void *)&t23, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_add_420lj6o((void *)&F.l7, (void *)NULL, cx, NULL, &t22, &t23);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)4ULL));
            if (!t25) {
                goto gm_lb5;
            }
            uint8_t t26 = gm_l8;
            uint8_t t27 = (uint8_t)!t26;
            if (t27) {
                gm_tbci5ospk5nnn t28 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l5));
                gm_tbci5ospk5nnn t29;
                gm_f_one_08qg1op((void *)&t29, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_add_420lj6o((void *)&F.l7, (void *)NULL, cx, NULL, &t28, &t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
            if (!t31) {
                goto gm_lb6;
            }
            uint8_t t32 = gm_l8;
            uint8_t t33 = (uint8_t)!t32;
            if (t33) {
                gm_tbci5ospk5nnn t34;
                gm_f_one_08qg1op((void *)&t34, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t35;
                gm_f_from_u64_ud13q5v((void *)&t35, (void *)NULL, cx, NULL, ((uint64_t)2ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_new_2jv6hag((void *)&F.l9, (void *)NULL, cx, NULL, &t34, &t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                int32_t t36 = gm_f_cmp_9aseneh((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t36;
                int32_t t37 = gm_l10;
                uint8_t t38 = (t37 > ((int32_t)0LL));
                if (t38) {
                    gm_l14 = ((uint8_t)1);
                } else {
                    int32_t t39 = gm_l10;
                    uint8_t t40 = ((t39 == ((int32_t)0LL)));
                    if (t40) {
                        uint8_t t41 = gm_f_is_even_3kgcpjs(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
                        uint8_t t42 = (uint8_t)!t41;
                        gm_l15 = t42;
                    } else {
                        gm_l15 = ((uint8_t)0);
                    }
                    uint8_t t43 = gm_l15;
                    gm_l14 = t43;
                }
                uint8_t t44 = gm_l14;
                if (t44) {
                    gm_tbci5ospk5nnn t45 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l5));
                    gm_tbci5ospk5nnn t46;
                    gm_f_one_08qg1op((void *)&t46, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_add_420lj6o((void *)&F.l7, (void *)NULL, cx, NULL, &t45, &t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 340, 5);
    }
gm_lb1:;
    void * t47 = gm_l0;
    gm_t88rcucg1b7uk t48 = gm_l2;
    gm_f_int_range_hue3vms((void *)&F.l11, (void *)NULL, cx, NULL, t47, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int32_t t49 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
    uint8_t t50 = (t49 < ((int32_t)0LL));
    if (t50) {
        gm_l16 = ((uint8_t)1);
    } else {
        int32_t t51 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l11) + 48)));
        uint8_t t52 = (t51 > ((int32_t)0LL));
        gm_l16 = t52;
    }
    uint8_t t53 = gm_l16;
    if (t53) {
        gm_thrdpodrs8pgt t54;
        memset(&t54, 0, sizeof t54);
        memmove(gm_out, ((uint8_t *)&t54), 56);
        goto gm_fdone;
    }
    gm_tbci5ospk5nnn t55 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l7));
    gm_thrdpodrs8pgt t56;
    GM_ST(uint8_t, (uint8_t *)&t56 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t56 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t56 + 52, 0);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t56), t55);
    ((uint8_t *)&t56)[48] = 1;
    memmove(gm_out, ((uint8_t *)&t56), 56);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_fn_0957mgc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
extern __attribute__((aligned(4))) void gm_f_fn_0l5daab(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_get_bytes_ql9o4k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3) {
    struct gm_fr_get_bytes_ql9o4k0 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* at */
    void * gm_l4; /* s */
    uint64_t gm_l5; /* size */
    unsigned __int128 gm_l6; /* v */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint16_t gm_l10; /* w */
    uint64_t gm_l11; /* n */
    uint64_t gm_l12; /* raw */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t6j6smj7qoctu gm_l16; /* o */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* et */
    GM_STACK_CHECK(&F, 1942u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l22;
    gm_tbccvikg6b96e gm_l23;
    uint64_t gm_l24;
    uint64_t gm_l25;
    double gm_l26;
    gm_ttu09lij36f8c gm_l27;
    gm_t6j6smj7qoctu gm_l28;
    gm_tlcm4lvu3g1a8 gm_l29;
    gm_tjoi8o798ne9k gm_l30;
    gm_t6evimt84lndq gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tq7s98ihonkju gm_l33;
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, ((uint8_t *)t25));
    gm_l4 = t26;
    void * t27 = gm_l4;
    void * t28 = (void *)(((uint8_t *)t27) + 8);
    gm_t88rcucg1b7uk t29 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l22, t28, GM_CONV(gm_csguankko4lmhd, t29));
    {
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)0ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)1ULL));
                    if (!t33) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)2ULL));
                    if (!t35) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t36 = gm_l4;
            gm_t88rcucg1b7uk t37 = gm_l1;
            gm_f_layout_a5ovnrm((void *)&gm_l23, (void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l23));
            gm_l5 = t38;
            gm_l6 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
            gm_l8 = ((uint64_t)0ULL);
            uint64_t t39 = gm_l5;
            gm_l7 = t39;
            {
                for (;;) {
                    uint64_t t40 = gm_l8;
                    uint64_t t41 = gm_l7;
                    uint8_t t42 = (t40 < t41);
                    if (!t42) {
                        goto gm_lb7;
                    }
                    {
                        {
                            {
                                uint64_t t43 = gm_l8;
                                gm_l9 = t43;
                                unsigned __int128 t44 = gm_l6;
                                void * t45 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t47 = gm_l3;
                                void * t48 = gm_l0;
                                uint64_t t49 = gm_l9;
                                uint64_t t50 = gm_l5;
                                uint64_t t51 = gm_f_byte_at_m94bj3e(t48, t49, t50);
                                uint64_t r52; bool o53 = __builtin_add_overflow(t47, t51, &r52);
                                if (o53) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 1112, 45);
                                uint64_t t54 = r52;
                                uint64_t t55 = (uint64_t)t54;
                                if ((uintptr_t)t55 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t55, t46, gm_srcue4ionbe9lgh, 1112, 42);
                                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)t45) + (uintptr_t)t55 * 1));
                                unsigned __int128 t57 = (unsigned __int128)t56;
                                uint64_t t58 = gm_l9;
                                uint64_t r59; bool o60 = __builtin_mul_overflow(t58, ((uint64_t)8ULL), &r59);
                                if (o60) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 1112, 93);
                                uint64_t t61 = r59;
                                unsigned __int128 t62 = (unsigned __int128)t61;
                                unsigned __int128 n63 = (unsigned __int128)t62;
                                if (n63 >= 128) gm_trap_shift((uint64_t)n63, "u128", gm_srcue4ionbe9lgh, 1112, 37);
                                unsigned __int128 r64 = (unsigned __int128)((unsigned __int128)t57 << (int)n63);
                                bool o65 = (unsigned __int128)(r64 >> (int)n63) != t57;
                                if (o65) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 1112, 37);
                                unsigned __int128 t66 = r64;
                                unsigned __int128 t67 = (unsigned __int128)(t44 | t66);
                                gm_l6 = t67;
                                goto gm_lb10;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1112, 13);
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint64_t t68 = gm_l8;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l8 = t71;
                }
            }
gm_lb7:;
            void * t72 = gm_l0;
            unsigned __int128 t73 = gm_l6;
            gm_t88rcucg1b7uk t74 = gm_l1;
            unsigned __int128 t75 = gm_f_wrap_bt8r149(t72, t73, GM_CONV(gm_csguankko4lmhd, t74));
            gm_t6evimt84lndq t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t76 + 16), t75);
            gm_tjoi8o798ne9k t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 56, 0);
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t77), t76);
            ((uint8_t *)&t77)[48] = 1;
            memmove(gm_out, ((uint8_t *)&t77), 64);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t79 = (uint8_t)(t78 == ((uint8_t)3ULL));
            if (!t79) {
                goto gm_lb12;
            }
            uint16_t t80 = GM_LD(uint16_t, (((uint8_t *)&gm_l22) + 8));
            gm_l10 = t80;
            uint16_t t81 = gm_l10;
            uint8_t t82 = ((t81 == ((uint16_t)64ULL)));
            if (t82) {
                gm_l24 = ((uint64_t)8ULL);
            } else {
                uint16_t t83 = gm_l10;
                uint8_t t84 = ((t83 == ((uint16_t)32ULL)));
                if (t84) {
                    gm_l25 = ((uint64_t)4ULL);
                } else {
                    gm_tjoi8o798ne9k t85;
                    memset(&t85, 0, sizeof t85);
                    memmove(gm_out, ((uint8_t *)&t85), 64);
                    goto gm_fdone;
                    gm_l25 = ((uint64_t)0);
                }
                uint64_t t86 = gm_l25;
                gm_l24 = t86;
            }
            uint64_t t87 = gm_l24;
            gm_l11 = t87;
            gm_l12 = ((uint64_t)0ULL);
            gm_l14 = ((uint64_t)0ULL);
            uint64_t t88 = gm_l11;
            gm_l13 = t88;
            {
                for (;;) {
                    uint64_t t89 = gm_l14;
                    uint64_t t90 = gm_l13;
                    uint8_t t91 = (t89 < t90);
                    if (!t91) {
                        goto gm_lb13;
                    }
                    {
                        {
                            {
                                uint64_t t92 = gm_l14;
                                gm_l15 = t92;
                                uint64_t t93 = gm_l12;
                                void * t94 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t96 = gm_l3;
                                void * t97 = gm_l0;
                                uint64_t t98 = gm_l15;
                                uint64_t t99 = gm_l11;
                                uint64_t t100 = gm_f_byte_at_m94bj3e(t97, t98, t99);
                                uint64_t r101; bool o102 = __builtin_add_overflow(t96, t100, &r101);
                                if (o102) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 1118, 43);
                                uint64_t t103 = r101;
                                uint64_t t104 = (uint64_t)t103;
                                if ((uintptr_t)t104 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t104, t95, gm_srcue4ionbe9lgh, 1118, 40);
                                uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)t94) + (uintptr_t)t104 * 1));
                                uint64_t t106 = (uint64_t)t105;
                                uint64_t t107 = gm_l15;
                                uint64_t r108; bool o109 = __builtin_mul_overflow(t107, ((uint64_t)8ULL), &r108);
                                if (o109) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 1118, 84);
                                uint64_t t110 = r108;
                                unsigned __int128 n111 = (unsigned __int128)t110;
                                if (n111 >= 64) gm_trap_shift((uint64_t)n111, "u64", gm_srcue4ionbe9lgh, 1118, 36);
                                uint64_t r112 = (uint64_t)((uint64_t)t106 << (int)n111);
                                bool o113 = (uint64_t)(r112 >> (int)n111) != t106;
                                if (o113) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 1118, 36);
                                uint64_t t114 = r112;
                                uint64_t t115 = (uint64_t)(t93 | t114);
                                gm_l12 = t115;
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1118, 13);
                        }
gm_lb16:;
                    }
gm_lb15:;
                    uint64_t t116 = gm_l14;
                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                    uint64_t t119 = r117;
                    gm_l14 = t119;
                }
            }
gm_lb13:;
            uint16_t t120 = gm_l10;
            uint8_t t121 = ((t120 == ((uint16_t)64ULL)));
            if (t121) {
                uint64_t t122 = gm_l12;
                double t123;
                memset(&t123, 0, sizeof t123); memcpy(&t123, ((uint8_t *)&t122), sizeof t123 < 8 ? sizeof t123 : 8);
                gm_l26 = t123;
            } else {
                uint64_t t124 = gm_l12;
                uint32_t t125 = (uint32_t)t124;
                float t126;
                memset(&t126, 0, sizeof t126); memcpy(&t126, ((uint8_t *)&t125), sizeof t126 < 4 ? sizeof t126 : 4);
                double t127 = (double)t126;
                gm_l26 = t127;
            }
            double t128 = gm_l26;
            gm_t6evimt84lndq t129;
            memset(&t129, 0, sizeof t129);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)1ULL));
            GM_ST(double, ((uint8_t *)&t129 + 16), t128);
            gm_tjoi8o798ne9k t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 56, 0);
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t130), t129);
            ((uint8_t *)&t130)[48] = 1;
            memmove(gm_out, ((uint8_t *)&t130), 64);
            goto gm_fdone;
        }
gm_lb12:;
        {
            void * t131 = gm_l0;
            gm_t88rcucg1b7uk t132 = gm_l1;
            gm_f_offsets_niooj98((void *)&gm_l27, (void *)NULL, cx, NULL, t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t133 = (uint8_t)((((uint8_t *)&gm_l27))[32] != 0);
            if (t133) {
                memmove(((uint8_t *)&gm_l28), ((uint8_t *)&gm_l27), 32);
            } else {
                gm_tjoi8o798ne9k t134;
                memset(&t134, 0, sizeof t134);
                memmove(gm_out, ((uint8_t *)&t134), 64);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&gm_l16), ((uint8_t *)&gm_l28), 32);
            gm_trqfvmjtt4sij t135;
            gm_f_list_0202sdh_2((void *)&t135, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l17 = t135;
            gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
            gm_l18 = t136;
            gm_l19 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t137 = gm_l19;
                    uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint8_t t139 = (t137 < t138);
                    if (!t139) {
                        goto gm_lb19;
                    }
                    {
                        uint64_t t140 = gm_l19;
                        void * t141 = GM_LD(void *, ((uint8_t *)&gm_l18));
                        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                        uint64_t t143 = gm_l19;
                        gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t141) + (uintptr_t)t143 * 4));
                        gm_tlcm4lvu3g1a8 t145;
                        GM_ST(uint32_t, (uint8_t *)&t145 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t145 + 0), t140);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t145 + 8), t144);
                        gm_l29 = t145;
                        {
                            {
                                uint64_t t146 = GM_LD(uint64_t, ((uint8_t *)&gm_l29));
                                gm_l20 = t146;
                                gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l29) + 8));
                                gm_l21 = t147;
                                void * t148 = (void *)((uint8_t *)&F.l17);
                                void * t149 = gm_l0;
                                gm_t88rcucg1b7uk t150 = gm_l21;
                                gm_b16_8 t151 = gm_l2;
                                uint64_t t152 = gm_l3;
                                void * t153 = GM_LD(void *, (((uint8_t *)&gm_l16) + 16));
                                uint64_t t154 = GM_LD(uint64_t, ((((uint8_t *)&gm_l16) + 16) + 8));
                                uint64_t t155 = gm_l20;
                                if ((uintptr_t)t155 >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)t155, t154, gm_srcue4ionbe9lgh, 1124, 80);
                                uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)t153) + (uintptr_t)t155 * 8));
                                uint64_t r157; bool o158 = __builtin_add_overflow(t152, t156, &r157);
                                if (o158) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 1124, 75);
                                uint64_t t159 = r157;
                                gm_f_get_bytes_ql9o4k0((void *)&gm_l30, (void *)NULL, cx, NULL, t149, t150, t151, t159);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t160 = (uint8_t)((((uint8_t *)&gm_l30))[48] != 0);
                                if (t160) {
                                    memmove(((uint8_t *)&gm_l31), ((uint8_t *)&gm_l30), 48);
                                } else {
                                    gm_tjoi8o798ne9k t161;
                                    memset(&t161, 0, sizeof t161);
                                    memmove(gm_out, ((uint8_t *)&t161), 64);
                                    goto gm_fdone;
                                }
                                gm_tftb1odc739t5 t162 = gm_f_push_0pgacsj_2(t148, &(*(gm_t6evimt84lndq *)((uint8_t *)&gm_l31)));
                                gm_l32 = t162;
                                uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcue4ionbe9lgh, 1124, 45);
                                goto gm_lb22;
                            }
gm_lb23:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1124, 13);
                        }
gm_lb22:;
                    }
gm_lb21:;
                    uint64_t t164 = gm_l19;
                    uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                    uint64_t t167 = r165;
                    gm_l19 = t167;
                }
            }
gm_lb19:;
            gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
            gm_tnpifv20irnph t169;
            GM_ST(gm_b16_8, ((uint8_t *)&t169 + 0), t168);
            gm_tq7s98ihonkju t170 = gm_f_new_lcff781((void *)NULL, cx, NULL, t169, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0cjq15hqfg)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t170;
            uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
            if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcue4ionbe9lgh, 1125, 25);
            void * t172 = GM_LD(void *, (((uint8_t *)&gm_l33) + 8));
            gm_t6evimt84lndq t173;
            memset(&t173, 0, sizeof t173);
            GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t173 + 16), t172);
            gm_tjoi8o798ne9k t174;
            GM_ST(uint8_t, (uint8_t *)&t174 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t174 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t174 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t174 + 56, 0);
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t174), t173);
            ((uint8_t *)&t174)[48] = 1;
            memmove(gm_out, ((uint8_t *)&t174), 64);
            goto gm_fdone;
        }
gm_lb18:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1108, 5);
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
inline __attribute__((aligned(4))) void gm_f_hash_byte_words_gtg9hm1(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_byte_words_gtg9hm1 F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* w */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
                gm_l5 = ((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_sub_overflow(t13, t14, &r15);
                if (o16) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 674, 22);
                uint64_t t17 = r15;
                uint64_t t18 = (t17 < ((uint64_t)8ULL) ? t17 : ((uint64_t)8ULL));
                gm_l4 = t18;
                {
                    for (;;) {
                        uint64_t t19 = gm_l5;
                        uint64_t t20 = gm_l4;
                        uint8_t t21 = (t19 < t20);
                        if (!t21) {
                            goto gm_lb4;
                        }
                        {
                            {
                                {
                                    uint64_t t22 = gm_l5;
                                    gm_l6 = t22;
                                    uint64_t t23 = gm_l3;
                                    void * t24 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t26 = gm_l2;
                                    uint64_t t27 = gm_l6;
                                    uint64_t r28; bool o29 = __builtin_add_overflow(t26, t27, &r28);
                                    if (o29) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 674, 61);
                                    uint64_t t30 = r28;
                                    if ((uintptr_t)t30 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t30, t25, gm_srcue4ionbe9lgh, 674, 55);
                                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t24) + (uintptr_t)t30 * 1));
                                    uint64_t t32 = (uint64_t)t31;
                                    uint64_t t33 = gm_l6;
                                    uint64_t r34; bool o35 = __builtin_mul_overflow(t33, ((uint64_t)8ULL), &r34);
                                    if (o35) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 674, 76);
                                    uint64_t t36 = r34;
                                    uint64_t t37 = (uint64_t)t36;
                                    unsigned __int128 n38 = (unsigned __int128)t37;
                                    if (n38 >= 64) gm_trap_shift((uint64_t)n38, "u64", gm_srcue4ionbe9lgh, 674, 51);
                                    uint64_t r39 = (uint64_t)((uint64_t)t32 << (int)n38);
                                    bool o40 = (uint64_t)(r39 >> (int)n38) != t32;
                                    if (o40) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 674, 51);
                                    uint64_t t41 = r39;
                                    uint64_t t42 = (uint64_t)(t23 | t41);
                                    gm_l3 = t42;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 674, 9);
                            }
gm_lb7:;
                        }
gm_lb6:;
                        uint64_t t43 = gm_l5;
                        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                        uint64_t t46 = r44;
                        gm_l5 = t46;
                    }
                }
gm_lb4:;
                void * t47 = gm_l0;
                uint64_t t48 = gm_l3;
                gm_f_hash_word_r89fvfg(t47, t48);
                uint64_t t49 = gm_l2;
                uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)8ULL), &r50);
                if (o51) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 676, 9);
                uint64_t t52 = r50;
                gm_l2 = t52;
            }
gm_lb3:;
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
__attribute__((aligned(4))) void gm_f_hash_value_j2e3sqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_hash_value_j2e3sqh F;
    gm_t88rcucg1b7uk gm_l2; /* t */
    void * gm_l3; /* h */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    unsigned __int128 gm_l6; /* raw */
    gm_ttovhfee8mf28 gm_l7; /* info */
    unsigned __int128 gm_l8; /* x */
    double gm_l9; /* f */
    double gm_l10; /* x */
    uint8_t gm_l11; /* b */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    void * gm_l13; /* x */
    gm_t88rcucg1b7uk gm_l14; /* err */
    gm_t88rcucg1b7uk gm_l15; /* ok */
    void * gm_l16; /* x */
    void * gm_l17; /* x */
    gm_t88rcucg1b7uk gm_l18; /* et */
    gm_b16_8 gm_l20; /* bytes */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* k */
    gm_t88rcucg1b7uk gm_l24; /* et */
    gm_b16_8 gm_l26; /* bytes */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l30; /* elems */
    void * gm_l31; /* a */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* i */
    uint32_t gm_l36; /* id */
    gm_t88rcucg1b7uk gm_l38; /* bt */
    unsigned __int128 gm_l39; /* raw */
    void * gm_l40; /* a */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    gm_b16_8 gm_l45; /* variants */
    uint32_t gm_l46; /* index */
    void * gm_l47; /* payload */
    gm_t88rcucg1b7uk gm_l48; /* pt */
    void * gm_l49; /* p */
    GM_STACK_CHECK(&F, 9971u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l50;
    double gm_l51;
    uint64_t gm_l52;
    gm_th2lqbo77pvsc gm_l53;
    gm_t9j082e6qvnjj gm_l54;
    gm_th2lqbo77pvsc gm_l55;
    gm_t9j082e6qvnjj gm_l56;
    gm_thf8as07kj3uj gm_l57;
    gm_t9j082e6qvnjj gm_l58;
    gm_t9p38b040grms gm_l59;
    unsigned __int128 gm_l60;
    gm_thf8as07kj3uj gm_l61;
    gm_t9j082e6qvnjj gm_l62;
    void * gm_l63;
    {
        void * t83 = F.l0;
        void * t84 = GM_LD(void *, ((uint8_t *)t83));
        void * t85 = (void *)(((uint8_t *)t84) + 8);
        gm_l5 = t85;
        void * t86 = gm_l5;
        gm_t88rcucg1b7uk t87 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&gm_l50, t86, GM_CONV(gm_csguankko4lmhd, t87));
        {
            {
                {
                    {
                        {
                            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)0ULL));
                            if (!t89) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)1ULL));
                            if (!t91) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)2ULL));
                            if (!t93) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        {
                            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)0ULL));
                            if (!t95) {
                                goto gm_lb8;
                            }
                            unsigned __int128 t96 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
                            gm_l6 = t96;
                            void * t97 = F.l0;
                            gm_t88rcucg1b7uk t98 = gm_l2;
                            gm_ttovhfee8mf28 t99 = gm_f_int_info_2r9qb5n(t97, GM_CONV(gm_csguankko4lmhd, t98));
                            gm_l7 = t99;
                            unsigned __int128 t100 = gm_l6;
                            uint32_t t101 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                            unsigned __int128 t102 = gm_f_mask_gbhl4hp(t101);
                            unsigned __int128 t103 = (unsigned __int128)(t100 & t102);
                            gm_l8 = t103;
                            {
                                {
                                    uint8_t t104 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                                    if (!t104) {
                                        goto gm_lb10;
                                    }
                                    uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                                    uint8_t t106 = (t105 < ((uint32_t)128ULL));
                                    if (!t106) {
                                        goto gm_lb10;
                                    }
                                    unsigned __int128 t107 = gm_l8;
                                    uint32_t t108 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                                    uint32_t r109; bool o110 = __builtin_sub_overflow(t108, ((uint32_t)1ULL), &r109);
                                    if (o110) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 725, 63);
                                    uint32_t t111 = r109;
                                    unsigned __int128 t112 = (unsigned __int128)t111;
                                    unsigned __int128 n113 = (unsigned __int128)t112;
                                    if (n113 >= 128) gm_trap_shift((uint64_t)n113, "u128", gm_srcue4ionbe9lgh, 725, 53);
                                    unsigned __int128 t114 = (unsigned __int128)(t107 >> (int)n113);
                                    unsigned __int128 t115 = (unsigned __int128)(t114 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
                                    uint8_t t116 = ((t115 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
                                    if (!t116) {
                                        goto gm_lb10;
                                    }
                                    unsigned __int128 t117 = gm_l8;
                                    uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                                    unsigned __int128 t119 = gm_f_mask_gbhl4hp(t118);
                                    unsigned __int128 t120 = (unsigned __int128)~t119;
                                    unsigned __int128 t121 = (unsigned __int128)(t117 | t120);
                                    gm_l8 = t121;
                                    goto gm_lb9;
                                }
gm_lb10:;
                            }
gm_lb9:;
                            void * t122 = gm_l3;
                            unsigned __int128 t123 = gm_l8;
                            uint64_t t124 = (uint64_t)t123;
                            gm_f_hash_word_r89fvfg(t122, t124);
                            uint32_t t125 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                            uint8_t t126 = (t125 > ((uint32_t)64ULL));
                            if (t126) {
                                void * t127 = gm_l3;
                                unsigned __int128 t128 = gm_l8;
                                unsigned __int128 n129 = (unsigned __int128)((uint64_t)64ULL);
                                if (n129 >= 128) gm_trap_shift((uint64_t)n129, "u128", gm_srcue4ionbe9lgh, 727, 47);
                                unsigned __int128 t130 = (unsigned __int128)(t128 >> (int)n129);
                                uint64_t t131 = (uint64_t)t130;
                                gm_f_hash_word_r89fvfg(t127, t131);
                            }
                            gm_t9j082e6qvnjj t132;
                            memset(&t132, 0, sizeof t132);
                            ((uint8_t *)&t132)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t132 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t132), 112);
                            goto gm_fdone;
                        }
gm_lb8:;
                        void * t133 = F.l0;
                        gm_tl874t2atnlh3 t134 = gm_l4;
                        gm_b16_8 t135;
                        GM_ST(void *, (uint8_t *)&t135, (void *)(gm_srtm6s969i45s));
                        GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(21));
                        gm_t39uai1ajk3qv t136;
                        gm_f_unsupported_2seq49l((void *)&t136, t133, GM_CONV(gm_cseahvg8v7p0vs, t134), t135);
                        gm_t9j082e6qvnjj t137;
                        GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t137 + 8, 0);
                        ((uint8_t *)&t137)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t137 + 16), t136);
                        memmove(gm_out, ((uint8_t *)&t137), 112);
                        goto gm_fdone;
                    }
gm_lb7:;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)3ULL));
                    if (!t139) {
                        goto gm_lb11;
                    }
                    {
                        {
                            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t141 = (uint8_t)(t140 == ((uint8_t)1ULL));
                            if (!t141) {
                                goto gm_lb13;
                            }
                            double t142 = GM_LD(double, (((uint8_t *)&F.l1) + 16));
                            gm_l9 = t142;
                            double t143 = gm_l9;
                            uint8_t t144 = ((t143 == ((double)0e0)));
                            if (t144) {
                                gm_l51 = ((double)0e0);
                            } else {
                                double t145 = gm_l9;
                                gm_l51 = t145;
                            }
                            double t146 = gm_l51;
                            gm_l10 = t146;
                            void * t147 = gm_l3;
                            double t148 = gm_l10;
                            uint64_t t149;
                            memset(&t149, 0, sizeof t149); memcpy(&t149, ((uint8_t *)&t148), sizeof t149 < 8 ? sizeof t149 : 8);
                            gm_f_hash_word_r89fvfg(t147, t149);
                            gm_t9j082e6qvnjj t150;
                            memset(&t150, 0, sizeof t150);
                            ((uint8_t *)&t150)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t150 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t150), 112);
                            goto gm_fdone;
                        }
gm_lb13:;
                        void * t151 = F.l0;
                        gm_tl874t2atnlh3 t152 = gm_l4;
                        gm_b16_8 t153;
                        GM_ST(void *, (uint8_t *)&t153, (void *)(gm_srtm6s969i45s));
                        GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(21));
                        gm_t39uai1ajk3qv t154;
                        gm_f_unsupported_2seq49l((void *)&t154, t151, GM_CONV(gm_cseahvg8v7p0vs, t152), t153);
                        gm_t9j082e6qvnjj t155;
                        GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t155 + 8, 0);
                        ((uint8_t *)&t155)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t155 + 16), t154);
                        memmove(gm_out, ((uint8_t *)&t155), 112);
                        goto gm_fdone;
                    }
gm_lb12:;
                }
gm_lb11:;
                {
                    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t157 = (uint8_t)(t156 == ((uint8_t)4ULL));
                    if (!t157) {
                        goto gm_lb14;
                    }
                    {
                        {
                            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t159 = (uint8_t)(t158 == ((uint8_t)2ULL));
                            if (!t159) {
                                goto gm_lb16;
                            }
                            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
                            gm_l11 = t160;
                            void * t161 = gm_l3;
                            uint8_t t162 = gm_l11;
                            if (t162) {
                                gm_l52 = ((uint64_t)1ULL);
                            } else {
                                gm_l52 = ((uint64_t)0ULL);
                            }
                            uint64_t t163 = gm_l52;
                            gm_f_hash_word_r89fvfg(t161, t163);
                            gm_t9j082e6qvnjj t164;
                            memset(&t164, 0, sizeof t164);
                            ((uint8_t *)&t164)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t164 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t164), 112);
                            goto gm_fdone;
                        }
gm_lb16:;
                        void * t165 = F.l0;
                        gm_tl874t2atnlh3 t166 = gm_l4;
                        gm_b16_8 t167;
                        GM_ST(void *, (uint8_t *)&t167, (void *)(gm_srtm6s969i45s));
                        GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(21));
                        gm_t39uai1ajk3qv t168;
                        gm_f_unsupported_2seq49l((void *)&t168, t165, GM_CONV(gm_cseahvg8v7p0vs, t166), t167);
                        gm_t9j082e6qvnjj t169;
                        GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t169 + 8, 0);
                        ((uint8_t *)&t169)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t169 + 16), t168);
                        memmove(gm_out, ((uint8_t *)&t169), 112);
                        goto gm_fdone;
                    }
gm_lb15:;
                }
gm_lb14:;
                {
                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t171 = (uint8_t)(t170 == ((uint8_t)5ULL));
                    if (!t171) {
                        goto gm_lb17;
                    }
                    gm_t9j082e6qvnjj t172;
                    memset(&t172, 0, sizeof t172);
                    ((uint8_t *)&t172)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t172 + 16), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t172), 112);
                    goto gm_fdone;
                }
gm_lb17:;
                {
                    uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t174 = (uint8_t)(t173 == ((uint8_t)13ULL));
                    if (!t174) {
                        goto gm_lb18;
                    }
                    gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l50) + 8));
                    gm_l12 = t175;
                    void * t176 = F.l0;
                    void * t177 = GM_LD(void *, ((uint8_t *)t176));
                    gm_t88rcucg1b7uk t178 = gm_l12;
                    uint8_t t179 = gm_f_is_nullable_pointer_nkie06s(t177, GM_CONV(gm_csguankko4lmhd, t178));
                    if (t179) {
                        uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                        uint8_t t181 = (uint8_t)((t180 == ((uint8_t)6ULL)));
                        gm_t6evimt84lndq t182 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
                        gm_t6evimt84lndq t183;
                        memset(&t183, 0, sizeof t183);
                        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)6ULL));
                        if (t181) {
                            void * t184 = gm_l3;
                            gm_f_hash_word_r89fvfg(t184, ((uint64_t)0ULL));
                            gm_t9j082e6qvnjj t185;
                            memset(&t185, 0, sizeof t185);
                            ((uint8_t *)&t185)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t185 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t185), 112);
                            goto gm_fdone;
                        }
                        void * t186 = F.l0;
                        gm_tl874t2atnlh3 t187 = gm_l4;
                        gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)gm_cpr60jghihe));
                        gm_t39uai1ajk3qv t189;
                        gm_f_trap_jftbss6((void *)&t189, (void *)NULL, cx, NULL, t186, t187, t188);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t9j082e6qvnjj t190;
                        GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t190 + 8, 0);
                        ((uint8_t *)&t190)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t190 + 16), t189);
                        memmove(gm_out, ((uint8_t *)&t190), 112);
                        goto gm_fdone;
                    }
                    {
                        {
                            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t192 = (uint8_t)(t191 == ((uint8_t)7ULL));
                            if (!t192) {
                                goto gm_lb20;
                            }
                            void * t193 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                            gm_l13 = t193;
                            void * t194 = gm_l3;
                            gm_f_hash_word_r89fvfg(t194, ((uint64_t)1ULL));
                            void * t195 = F.l0;
                            void * t196 = gm_l13;
                            gm_t88rcucg1b7uk t197 = gm_l12;
                            void * t198 = gm_l3;
                            gm_tl874t2atnlh3 t199 = gm_l4;
                            gm_t6evimt84lndq t200 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t196));
                            gm_f_hash_value_j2e3sqh((void *)&(*(gm_t9j082e6qvnjj *)gm_out), (void *)NULL, cx, NULL, t195, &t200, t197, t198, t199);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb20:;
                        {
                            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t202 = (uint8_t)(t201 == ((uint8_t)6ULL));
                            if (!t202) {
                                goto gm_lb21;
                            }
                            void * t203 = gm_l3;
                            gm_f_hash_word_r89fvfg(t203, ((uint64_t)0ULL));
                            gm_t9j082e6qvnjj t204;
                            memset(&t204, 0, sizeof t204);
                            ((uint8_t *)&t204)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t204 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t204), 112);
                            goto gm_fdone;
                        }
gm_lb21:;
                        {
                            void * t205 = F.l0;
                            gm_tl874t2atnlh3 t206 = gm_l4;
                            gm_b16_8 t207;
                            GM_ST(void *, (uint8_t *)&t207, (void *)(gm_srtm6s969i45s));
                            GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(21));
                            gm_t39uai1ajk3qv t208;
                            gm_f_unsupported_2seq49l((void *)&t208, t205, GM_CONV(gm_cseahvg8v7p0vs, t206), t207);
                            gm_t9j082e6qvnjj t209;
                            GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t209 + 8, 0);
                            ((uint8_t *)&t209)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t209 + 16), t208);
                            memmove(gm_out, ((uint8_t *)&t209), 112);
                            goto gm_fdone;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 750, 13);
                    }
gm_lb19:;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t211 = (uint8_t)(t210 == ((uint8_t)14ULL));
                    if (!t211) {
                        goto gm_lb23;
                    }
                    gm_t88rcucg1b7uk t212 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l50) + 8));
                    gm_l14 = t212;
                    gm_t88rcucg1b7uk t213 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l50) + 8) + 4));
                    gm_l15 = t213;
                    {
                        {
                            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t215 = (uint8_t)(t214 == ((uint8_t)8ULL));
                            if (!t215) {
                                goto gm_lb25;
                            }
                            void * t216 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                            gm_l16 = t216;
                            void * t217 = gm_l3;
                            gm_f_hash_word_r89fvfg(t217, ((uint64_t)0ULL));
                            void * t218 = F.l0;
                            void * t219 = gm_l16;
                            gm_t88rcucg1b7uk t220 = gm_l15;
                            void * t221 = gm_l3;
                            gm_tl874t2atnlh3 t222 = gm_l4;
                            gm_t6evimt84lndq t223 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t219));
                            gm_f_hash_value_j2e3sqh((void *)&(*(gm_t9j082e6qvnjj *)gm_out), (void *)NULL, cx, NULL, t218, &t223, t220, t221, t222);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb25:;
                        {
                            uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t225 = (uint8_t)(t224 == ((uint8_t)9ULL));
                            if (!t225) {
                                goto gm_lb26;
                            }
                            void * t226 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                            gm_l17 = t226;
                            void * t227 = gm_l3;
                            gm_f_hash_word_r89fvfg(t227, ((uint64_t)1ULL));
                            void * t228 = F.l0;
                            void * t229 = gm_l17;
                            gm_t88rcucg1b7uk t230 = gm_l14;
                            void * t231 = gm_l3;
                            gm_tl874t2atnlh3 t232 = gm_l4;
                            gm_t6evimt84lndq t233 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t229));
                            gm_f_hash_value_j2e3sqh((void *)&(*(gm_t9j082e6qvnjj *)gm_out), (void *)NULL, cx, NULL, t228, &t233, t230, t231, t232);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb26:;
                        {
                            void * t234 = F.l0;
                            gm_tl874t2atnlh3 t235 = gm_l4;
                            gm_b16_8 t236;
                            GM_ST(void *, (uint8_t *)&t236, (void *)(gm_srtm6s969i45s));
                            GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(21));
                            gm_t39uai1ajk3qv t237;
                            gm_f_unsupported_2seq49l((void *)&t237, t234, GM_CONV(gm_cseahvg8v7p0vs, t235), t236);
                            gm_t9j082e6qvnjj t238;
                            GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t238 + 8, 0);
                            ((uint8_t *)&t238)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t238 + 16), t237);
                            memmove(gm_out, ((uint8_t *)&t238), 112);
                            goto gm_fdone;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 762, 29);
                    }
gm_lb24:;
                }
gm_lb23:;
                {
                    uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t240 = (uint8_t)(t239 == ((uint8_t)10ULL));
                    if (!t240) {
                        goto gm_lb28;
                    }
                    gm_t88rcucg1b7uk t241 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l50) + 8) + 4));
                    gm_l18 = t241;
                    {
                        {
                            uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t243 = (uint8_t)(t242 == ((uint8_t)5ULL));
                            if (!t243) {
                                goto gm_lb30;
                            }
                            memmove(((uint8_t *)&F.l19), (((uint8_t *)&F.l1) + 16), 32);
                            void * t244 = gm_l3;
                            uint64_t t245 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 24));
                            gm_f_hash_word_r89fvfg(t244, t245);
                            {
                                {
                                    void * t246 = F.l0;
                                    gm_t88rcucg1b7uk t247 = gm_l18;
                                    uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 24));
                                    gm_closure t249 = ((gm_closure){(void *)gm_f_fn_0957mgc, (void *)&F.h, cx});
                                    gm_f_u8_elems_akjlvj2((void *)&gm_l53, (void *)NULL, cx, NULL, t246, t247, t248, t249);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t250 = (uint8_t)((((uint8_t *)&gm_l53))[16] != 0);
                                    if (!t250) {
                                        goto gm_lb32;
                                    }
                                    gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l53));
                                    gm_l20 = t251;
                                    void * t252 = gm_l3;
                                    gm_b16_8 t253 = gm_l20;
                                    gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
                                    gm_f_hash_byte_words_gtg9hm1(t252, t254);
                                    gm_t9j082e6qvnjj t255;
                                    memset(&t255, 0, sizeof t255);
                                    ((uint8_t *)&t255)[0] = 0;
                                    GM_ST(gm_b0_1, ((uint8_t *)&t255 + 16), GM_ZERO(gm_b0_1));
                                    memmove(gm_out, ((uint8_t *)&t255), 112);
                                    goto gm_fdone;
                                }
gm_lb32:;
                            }
gm_lb31:;
                            gm_l22 = ((uint64_t)0ULL);
                            uint64_t t256 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 24));
                            gm_l21 = t256;
                            {
                                for (;;) {
                                    uint64_t t258 = gm_l22;
                                    uint64_t t259 = gm_l21;
                                    uint8_t t260 = (t258 < t259);
                                    if (!t260) {
                                        goto gm_lb33;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t261 = gm_l22;
                                                gm_l23 = t261;
                                                void * t262 = F.l0;
                                                void * t263 = F.l0;
                                                uint64_t t264 = gm_l23;
                                                gm_tkaapcii86udk t265;
                                                gm_f_offset_nkn711s((void *)&t265, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&F.l19)), t264);
                                                if (gm_unw.kind) {
                                                    goto gm_ll257;
                                                }
                                                gm_t6evimt84lndq t266;
                                                gm_f_load_nf7o5kl((void *)&t266, t263, &t265);
                                                gm_t88rcucg1b7uk t267 = gm_l18;
                                                void * t268 = gm_l3;
                                                gm_tl874t2atnlh3 t269 = gm_l4;
                                                gm_f_hash_value_j2e3sqh((void *)&gm_l54, (void *)NULL, cx, NULL, t262, &t266, t267, t268, t269);
                                                if (gm_unw.kind) {
                                                    goto gm_ll257;
                                                }
                                                uint8_t t270 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                                if (t270) {
                                                } else {
                                                    gm_t39uai1ajk3qv t271 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l54) + 16));
                                                    gm_t9j082e6qvnjj t272;
                                                    GM_ST(uint8_t, (uint8_t *)&t272 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t272 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t272 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t272 + 8, 0);
                                                    ((uint8_t *)&t272)[0] = 1;
                                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t272 + 16), t271);
                                                    memmove(gm_out, ((uint8_t *)&t272), 112);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 780, 13);
                                        }
gm_lb36:;
                                    }
gm_lb35:;
                                    uint64_t t273 = gm_l22;
                                    uint64_t r274; bool o275 = __builtin_add_overflow(t273, ((uint64_t)1ULL), &r274);
                                    uint64_t t276 = r274;
                                    gm_l22 = t276;
                                }
                            }
                            goto gm_ls277;
gm_ll257:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb33; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb35; }
                            }
                            goto gm_lad;
gm_ls277:;
gm_lb33:;
                            gm_t9j082e6qvnjj t278;
                            memset(&t278, 0, sizeof t278);
                            ((uint8_t *)&t278)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t278 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t278), 112);
                            goto gm_fdone;
                        }
gm_lb30:;
                        void * t279 = F.l0;
                        gm_tl874t2atnlh3 t280 = gm_l4;
                        gm_b16_8 t281;
                        GM_ST(void *, (uint8_t *)&t281, (void *)(gm_srtm6s969i45s));
                        GM_ST(uintptr_t, (uint8_t *)&t281 + 8, (uintptr_t)(21));
                        gm_t39uai1ajk3qv t282;
                        gm_f_unsupported_2seq49l((void *)&t282, t279, GM_CONV(gm_cseahvg8v7p0vs, t280), t281);
                        gm_t9j082e6qvnjj t283;
                        GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t283 + 8, 0);
                        ((uint8_t *)&t283)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t283 + 16), t282);
                        memmove(gm_out, ((uint8_t *)&t283), 112);
                        goto gm_fdone;
                    }
gm_lb29:;
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t284 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t285 = (uint8_t)(t284 == ((uint8_t)11ULL));
                    if (!t285) {
                        goto gm_lb38;
                    }
                    gm_t88rcucg1b7uk t286 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l50) + 8) + 8));
                    gm_l24 = t286;
                    {
                        {
                            uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t288 = (uint8_t)(t287 == ((uint8_t)3ULL));
                            if (!t288) {
                                goto gm_lb40;
                            }
                            void * t289 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                            F.l25 = t289;
                            {
                                {
                                    void * t290 = F.l0;
                                    gm_t88rcucg1b7uk t291 = gm_l24;
                                    void * t292 = F.l25;
                                    uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)t292) + 8));
                                    uint64_t t294 = (uint64_t)t293;
                                    gm_closure t295 = ((gm_closure){(void *)gm_f_fn_0l5daab, (void *)&F.h, cx});
                                    gm_f_u8_elems_akjlvj2((void *)&gm_l55, (void *)NULL, cx, NULL, t290, t291, t294, t295);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t296 = (uint8_t)((((uint8_t *)&gm_l55))[16] != 0);
                                    if (!t296) {
                                        goto gm_lb42;
                                    }
                                    gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l55));
                                    gm_l26 = t297;
                                    void * t298 = gm_l3;
                                    gm_b16_8 t299 = gm_l26;
                                    gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
                                    gm_f_hash_byte_words_gtg9hm1(t298, t300);
                                    gm_t9j082e6qvnjj t301;
                                    memset(&t301, 0, sizeof t301);
                                    ((uint8_t *)&t301)[0] = 0;
                                    GM_ST(gm_b0_1, ((uint8_t *)&t301 + 16), GM_ZERO(gm_b0_1));
                                    memmove(gm_out, ((uint8_t *)&t301), 112);
                                    goto gm_fdone;
                                }
gm_lb42:;
                            }
gm_lb41:;
                            void * t302 = F.l25;
                            gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)t302));
                            gm_l27 = t303;
                            gm_l28 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t305 = gm_l28;
                                    uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                    uint8_t t307 = (t305 < t306);
                                    if (!t307) {
                                        goto gm_lb43;
                                    }
                                    {
                                        void * t308 = GM_LD(void *, ((uint8_t *)&gm_l27));
                                        uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                        uint64_t t310 = gm_l28;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l29), (((uint8_t *)t308) + (uintptr_t)t310 * 48), 48);
                                                void * t311 = F.l0;
                                                gm_t88rcucg1b7uk t312 = gm_l24;
                                                void * t313 = gm_l3;
                                                gm_tl874t2atnlh3 t314 = gm_l4;
                                                gm_t6evimt84lndq t315 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l29));
                                                gm_f_hash_value_j2e3sqh((void *)&gm_l56, (void *)NULL, cx, NULL, t311, &t315, t312, t313, t314);
                                                if (gm_unw.kind) {
                                                    goto gm_ll304;
                                                }
                                                uint8_t t316 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                if (t316) {
                                                } else {
                                                    gm_t39uai1ajk3qv t317 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l56) + 16));
                                                    gm_t9j082e6qvnjj t318;
                                                    GM_ST(uint8_t, (uint8_t *)&t318 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t318 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t318 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t318 + 8, 0);
                                                    ((uint8_t *)&t318)[0] = 1;
                                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t318 + 16), t317);
                                                    memmove(gm_out, ((uint8_t *)&t318), 112);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb46;
                                            }
gm_lb47:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 789, 13);
                                        }
gm_lb46:;
                                    }
gm_lb45:;
                                    uint64_t t319 = gm_l28;
                                    uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                                    uint64_t t322 = r320;
                                    gm_l28 = t322;
                                }
                            }
                            goto gm_ls323;
gm_ll304:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb43; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb45; }
                            }
                            goto gm_lad;
gm_ls323:;
gm_lb43:;
                            gm_t9j082e6qvnjj t324;
                            memset(&t324, 0, sizeof t324);
                            ((uint8_t *)&t324)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t324 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t324), 112);
                            goto gm_fdone;
                        }
gm_lb40:;
                        void * t325 = F.l0;
                        gm_tl874t2atnlh3 t326 = gm_l4;
                        gm_b16_8 t327;
                        GM_ST(void *, (uint8_t *)&t327, (void *)(gm_srtm6s969i45s));
                        GM_ST(uintptr_t, (uint8_t *)&t327 + 8, (uintptr_t)(21));
                        gm_t39uai1ajk3qv t328;
                        gm_f_unsupported_2seq49l((void *)&t328, t325, GM_CONV(gm_cseahvg8v7p0vs, t326), t327);
                        gm_t9j082e6qvnjj t329;
                        GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t329 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t329 + 8, 0);
                        ((uint8_t *)&t329)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t329 + 16), t328);
                        memmove(gm_out, ((uint8_t *)&t329), 112);
                        goto gm_fdone;
                    }
gm_lb39:;
                }
gm_lb38:;
            }
            {
                {
                    uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t331 = (uint8_t)(t330 == ((uint8_t)12ULL));
                    if (!t331) {
                        goto gm_lb48;
                    }
                    gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                    gm_l30 = t332;
                    {
                        {
                            uint8_t t333 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t334 = (uint8_t)(t333 == ((uint8_t)3ULL));
                            if (!t334) {
                                goto gm_lb50;
                            }
                            void * t335 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                            gm_l31 = t335;
                            void * t336 = gm_l31;
                            gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)t336));
                            gm_b16_8 t338 = GM_LD(gm_b16_8, ((uint8_t *)&t337));
                            gm_l32 = t338;
                            gm_l33 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t340 = gm_l33;
                                    uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                    uint8_t t342 = (t340 < t341);
                                    if (!t342) {
                                        goto gm_lb51;
                                    }
                                    {
                                        uint64_t t343 = gm_l33;
                                        void * t344 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                        uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                        uint64_t t346 = gm_l33;
                                        gm_t6evimt84lndq t347 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t344) + (uintptr_t)t346 * 48));
                                        gm_thf8as07kj3uj t348;
                                        GM_ST(uint64_t, (uint8_t *)&t348 + 8, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t348 + 0), t343);
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t348 + 16), t347);
                                        gm_l57 = t348;
                                        {
                                            {
                                                uint64_t t349 = GM_LD(uint64_t, ((uint8_t *)&gm_l57));
                                                gm_l34 = t349;
                                                memmove(((uint8_t *)&F.l35), (((uint8_t *)&gm_l57) + 16), 48);
                                                void * t350 = F.l0;
                                                void * t351 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                                uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                                uint64_t t353 = gm_l34;
                                                if ((uintptr_t)t353 >= (uintptr_t)(t352)) gm_trap_index((uintptr_t)t353, t352, gm_srcue4ionbe9lgh, 794, 66);
                                                gm_t88rcucg1b7uk t354 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t351) + (uintptr_t)t353 * 4));
                                                void * t355 = gm_l3;
                                                gm_tl874t2atnlh3 t356 = gm_l4;
                                                gm_t6evimt84lndq t357 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l35));
                                                gm_f_hash_value_j2e3sqh((void *)&gm_l58, (void *)NULL, cx, NULL, t350, &t357, t354, t355, t356);
                                                if (gm_unw.kind) {
                                                    goto gm_ll339;
                                                }
                                                uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                                if (t358) {
                                                } else {
                                                    gm_t39uai1ajk3qv t359 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l58) + 16));
                                                    gm_t9j082e6qvnjj t360;
                                                    GM_ST(uint8_t, (uint8_t *)&t360 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t360 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t360 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t360 + 8, 0);
                                                    ((uint8_t *)&t360)[0] = 1;
                                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t360 + 16), t359);
                                                    memmove(gm_out, ((uint8_t *)&t360), 112);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb54;
                                            }
gm_lb55:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 794, 13);
                                        }
gm_lb54:;
                                    }
gm_lb53:;
                                    uint64_t t361 = gm_l33;
                                    uint64_t r362; bool o363 = __builtin_add_overflow(t361, ((uint64_t)1ULL), &r362);
                                    uint64_t t364 = r362;
                                    gm_l33 = t364;
                                }
                            }
                            goto gm_ls365;
gm_ll339:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb51; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb53; }
                            }
                            goto gm_lad;
gm_ls365:;
gm_lb51:;
                            gm_t9j082e6qvnjj t366;
                            memset(&t366, 0, sizeof t366);
                            ((uint8_t *)&t366)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t366 + 16), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t366), 112);
                            goto gm_fdone;
                        }
gm_lb50:;
                        void * t367 = F.l0;
                        gm_tl874t2atnlh3 t368 = gm_l4;
                        gm_b16_8 t369;
                        GM_ST(void *, (uint8_t *)&t369, (void *)(gm_srtm6s969i45s));
                        GM_ST(uintptr_t, (uint8_t *)&t369 + 8, (uintptr_t)(21));
                        gm_t39uai1ajk3qv t370;
                        gm_f_unsupported_2seq49l((void *)&t370, t367, GM_CONV(gm_cseahvg8v7p0vs, t368), t369);
                        gm_t9j082e6qvnjj t371;
                        GM_ST(uint8_t, (uint8_t *)&t371 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t371 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t371 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t371 + 8, 0);
                        ((uint8_t *)&t371)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t371 + 16), t370);
                        memmove(gm_out, ((uint8_t *)&t371), 112);
                        goto gm_fdone;
                    }
gm_lb49:;
                }
gm_lb48:;
            }
            {
                {
                    uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    uint8_t t373 = (uint8_t)(t372 == ((uint8_t)17ULL));
                    if (!t373) {
                        goto gm_lb56;
                    }
                    uint32_t t374 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 8));
                    gm_l36 = t374;
                    void * t375 = F.l0;
                    void * t376 = GM_LD(void *, ((uint8_t *)t375));
                    void * t377 = GM_LD(void *, (((uint8_t *)t376) + 1088));
                    uint64_t t378 = GM_LD(uint64_t, ((((uint8_t *)t376) + 1088) + 8));
                    uint32_t t379 = gm_l36;
                    uint64_t t380 = (uint64_t)t379;
                    if ((uintptr_t)t380 >= (uintptr_t)(t378)) gm_trap_index((uintptr_t)t380, t378, gm_srcue4ionbe9lgh, 797, 30);
                    {
                        {
                            uint8_t t381 = GM_LD(uint8_t, (((((uint8_t *)t377) + (uintptr_t)t380 * 184) + 24) + 0));
                            uint8_t t382 = (uint8_t)(t381 == ((uint8_t)0ULL));
                            if (!t382) {
                                goto gm_lb58;
                            }
                            memmove(((uint8_t *)&F.l37), (((((uint8_t *)t377) + (uintptr_t)t380 * 184) + 24) + 8), 48);
                            {
                                {
                                    uint8_t t383 = (uint8_t)(((((uint8_t *)&F.l37) + 40))[4] != 0);
                                    if (!t383) {
                                        goto gm_lb60;
                                    }
                                    gm_t88rcucg1b7uk t384 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l37) + 40));
                                    gm_l38 = t384;
                                    void * t385 = F.l0;
                                    gm_t88rcucg1b7uk t386 = gm_l2;
                                    gm_t6evimt84lndq t387 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
                                    gm_f_bits_of_hithcdi((void *)&gm_l59, (void *)NULL, cx, NULL, t385, &t387, t386);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t388 = (uint8_t)((((uint8_t *)&gm_l59))[16] != 0);
                                    if (t388) {
                                        unsigned __int128 t389 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l59));
                                        gm_l60 = t389;
                                    } else {
                                        void * t390 = F.l0;
                                        gm_tl874t2atnlh3 t391 = gm_l4;
                                        gm_b16_8 t392;
                                        GM_ST(void *, (uint8_t *)&t392, (void *)(gm_srtm6s969i45s));
                                        GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(21));
                                        gm_t39uai1ajk3qv t393;
                                        gm_f_unsupported_2seq49l((void *)&t393, t390, GM_CONV(gm_cseahvg8v7p0vs, t391), t392);
                                        gm_t9j082e6qvnjj t394;
                                        GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t394 + 8, 0);
                                        ((uint8_t *)&t394)[0] = 1;
                                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t394 + 16), t393);
                                        memmove(gm_out, ((uint8_t *)&t394), 112);
                                        goto gm_fdone;
                                    }
                                    unsigned __int128 t395 = gm_l60;
                                    gm_l39 = t395;
                                    void * t396 = F.l0;
                                    unsigned __int128 t397 = gm_l39;
                                    gm_t88rcucg1b7uk t398 = gm_l38;
                                    void * t399 = gm_l3;
                                    gm_tl874t2atnlh3 t400 = gm_l4;
                                    gm_t6evimt84lndq t401;
                                    GM_ST(uint8_t, (uint8_t *)&t401 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t401 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t401 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t401 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t401 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t401 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t401 + 0), ((uint8_t)0ULL));
                                    GM_ST(unsigned __int128, ((uint8_t *)&t401 + 16), t397);
                                    gm_f_hash_value_j2e3sqh((void *)&(*(gm_t9j082e6qvnjj *)gm_out), (void *)NULL, cx, NULL, t396, &t401, t398, t399, t400);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
gm_lb60:;
                            }
gm_lb59:;
                            {
                                {
                                    uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                                    uint8_t t403 = (uint8_t)(t402 == ((uint8_t)3ULL));
                                    if (!t403) {
                                        goto gm_lb62;
                                    }
                                    void * t404 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                                    gm_l40 = t404;
                                    void * t405 = gm_l40;
                                    gm_b16_8 t406 = GM_LD(gm_b16_8, ((uint8_t *)t405));
                                    gm_b16_8 t407 = GM_LD(gm_b16_8, ((uint8_t *)&t406));
                                    gm_l41 = t407;
                                    gm_l42 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t409 = gm_l42;
                                            uint64_t t410 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                            uint8_t t411 = (t409 < t410);
                                            if (!t411) {
                                                goto gm_lb63;
                                            }
                                            {
                                                uint64_t t412 = gm_l42;
                                                void * t413 = GM_LD(void *, ((uint8_t *)&gm_l41));
                                                uint64_t t414 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                                uint64_t t415 = gm_l42;
                                                gm_t6evimt84lndq t416 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t413) + (uintptr_t)t415 * 48));
                                                gm_thf8as07kj3uj t417;
                                                GM_ST(uint64_t, (uint8_t *)&t417 + 8, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t417 + 0), t412);
                                                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t417 + 16), t416);
                                                gm_l61 = t417;
                                                {
                                                    {
                                                        uint64_t t418 = GM_LD(uint64_t, ((uint8_t *)&gm_l61));
                                                        gm_l43 = t418;
                                                        memmove(((uint8_t *)&F.l44), (((uint8_t *)&gm_l61) + 16), 48);
                                                        void * t419 = F.l0;
                                                        void * t420 = GM_LD(void *, ((uint8_t *)&F.l37));
                                                        uint64_t t421 = GM_LD(uint64_t, (((uint8_t *)&F.l37) + 8));
                                                        uint64_t t422 = gm_l43;
                                                        if ((uintptr_t)t422 >= (uintptr_t)(t421)) gm_trap_index((uintptr_t)t422, t421, gm_srcue4ionbe9lgh, 805, 70);
                                                        gm_t88rcucg1b7uk t423 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t420) + (uintptr_t)t422 * 40) + 16));
                                                        void * t424 = gm_l3;
                                                        gm_tl874t2atnlh3 t425 = gm_l4;
                                                        gm_t6evimt84lndq t426 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l44));
                                                        gm_f_hash_value_j2e3sqh((void *)&gm_l62, (void *)NULL, cx, NULL, t419, &t426, t423, t424, t425);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll408;
                                                        }
                                                        uint8_t t427 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                        if (t427) {
                                                        } else {
                                                            gm_t39uai1ajk3qv t428 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l62) + 16));
                                                            gm_t9j082e6qvnjj t429;
                                                            GM_ST(uint8_t, (uint8_t *)&t429 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t429 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t429 + 4, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&t429 + 8, 0);
                                                            ((uint8_t *)&t429)[0] = 1;
                                                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t429 + 16), t428);
                                                            memmove(gm_out, ((uint8_t *)&t429), 112);
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb66;
                                                    }
gm_lb67:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 805, 17);
                                                }
gm_lb66:;
                                            }
gm_lb65:;
                                            uint64_t t430 = gm_l42;
                                            uint64_t r431; bool o432 = __builtin_add_overflow(t430, ((uint64_t)1ULL), &r431);
                                            uint64_t t433 = r431;
                                            gm_l42 = t433;
                                        }
                                    }
                                    goto gm_ls434;
gm_ll408:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb63; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb65; }
                                    }
                                    goto gm_lad;
gm_ls434:;
gm_lb63:;
                                    gm_t9j082e6qvnjj t435;
                                    memset(&t435, 0, sizeof t435);
                                    ((uint8_t *)&t435)[0] = 0;
                                    GM_ST(gm_b0_1, ((uint8_t *)&t435 + 16), GM_ZERO(gm_b0_1));
                                    memmove(gm_out, ((uint8_t *)&t435), 112);
                                    goto gm_fdone;
                                }
gm_lb62:;
                                void * t436 = F.l0;
                                gm_tl874t2atnlh3 t437 = gm_l4;
                                gm_b16_8 t438;
                                GM_ST(void *, (uint8_t *)&t438, (void *)(gm_srtm6s969i45s));
                                GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(21));
                                gm_t39uai1ajk3qv t439;
                                gm_f_unsupported_2seq49l((void *)&t439, t436, GM_CONV(gm_cseahvg8v7p0vs, t437), t438);
                                gm_t9j082e6qvnjj t440;
                                GM_ST(uint8_t, (uint8_t *)&t440 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t440 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t440 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t440 + 8, 0);
                                ((uint8_t *)&t440)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t440 + 16), t439);
                                memmove(gm_out, ((uint8_t *)&t440), 112);
                                goto gm_fdone;
                            }
gm_lb61:;
                        }
gm_lb58:;
                        {
                            uint8_t t441 = GM_LD(uint8_t, (((((uint8_t *)t377) + (uintptr_t)t380 * 184) + 24) + 0));
                            uint8_t t442 = (uint8_t)(t441 == ((uint8_t)1ULL));
                            if (!t442) {
                                goto gm_lb68;
                            }
                            gm_b16_8 t443 = GM_LD(gm_b16_8, ((((((uint8_t *)t377) + (uintptr_t)t380 * 184) + 24) + 8) + 8));
                            gm_l45 = t443;
                            {
                                {
                                    uint8_t t444 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                                    uint8_t t445 = (uint8_t)(t444 == ((uint8_t)10ULL));
                                    if (!t445) {
                                        goto gm_lb70;
                                    }
                                    uint32_t t446 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
                                    gm_l46 = t446;
                                    void * t447 = GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8));
                                    gm_l47 = t447;
                                    void * t448 = gm_l3;
                                    uint32_t t449 = gm_l46;
                                    uint64_t t450 = (uint64_t)t449;
                                    gm_f_hash_word_r89fvfg(t448, t450);
                                    {
                                        {
                                            void * t451 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                            uint64_t t452 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                            uint32_t t453 = gm_l46;
                                            uint64_t t454 = (uint64_t)t453;
                                            if ((uintptr_t)t454 >= (uintptr_t)(t452)) gm_trap_index((uintptr_t)t454, t452, gm_srcue4ionbe9lgh, 811, 26);
                                            uint8_t t455 = (uint8_t)((((((uint8_t *)t451) + (uintptr_t)t454 * 72) + 16))[4] != 0);
                                            if (!t455) {
                                                goto gm_lb72;
                                            }
                                            gm_t88rcucg1b7uk t456 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t451) + (uintptr_t)t454 * 72) + 16));
                                            gm_l48 = t456;
                                            uint8_t t457 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l47)) != 0);
                                            if (t457) {
                                                void * t458 = GM_LD(void *, ((uint8_t *)&gm_l47));
                                                gm_l63 = t458;
                                            } else {
                                                void * t459 = F.l0;
                                                gm_tl874t2atnlh3 t460 = gm_l4;
                                                gm_b16_8 t461;
                                                GM_ST(void *, (uint8_t *)&t461, (void *)(gm_srtm6s969i45s));
                                                GM_ST(uintptr_t, (uint8_t *)&t461 + 8, (uintptr_t)(21));
                                                gm_t39uai1ajk3qv t462;
                                                gm_f_unsupported_2seq49l((void *)&t462, t459, GM_CONV(gm_cseahvg8v7p0vs, t460), t461);
                                                gm_t9j082e6qvnjj t463;
                                                GM_ST(uint8_t, (uint8_t *)&t463 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t463 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t463 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t463 + 8, 0);
                                                ((uint8_t *)&t463)[0] = 1;
                                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t463 + 16), t462);
                                                memmove(gm_out, ((uint8_t *)&t463), 112);
                                                goto gm_fdone;
                                            }
                                            void * t464 = gm_l63;
                                            gm_l49 = t464;
                                            void * t465 = F.l0;
                                            void * t466 = gm_l49;
                                            gm_t88rcucg1b7uk t467 = gm_l48;
                                            void * t468 = gm_l3;
                                            gm_tl874t2atnlh3 t469 = gm_l4;
                                            gm_t6evimt84lndq t470 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t466));
                                            gm_f_hash_value_j2e3sqh((void *)&(*(gm_t9j082e6qvnjj *)gm_out), (void *)NULL, cx, NULL, t465, &t470, t467, t468, t469);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_fdone;
                                        }
gm_lb72:;
                                    }
gm_lb71:;
                                    gm_t9j082e6qvnjj t471;
                                    memset(&t471, 0, sizeof t471);
                                    ((uint8_t *)&t471)[0] = 0;
                                    GM_ST(gm_b0_1, ((uint8_t *)&t471 + 16), GM_ZERO(gm_b0_1));
                                    memmove(gm_out, ((uint8_t *)&t471), 112);
                                    goto gm_fdone;
                                }
gm_lb70:;
                                void * t472 = F.l0;
                                gm_tl874t2atnlh3 t473 = gm_l4;
                                gm_b16_8 t474;
                                GM_ST(void *, (uint8_t *)&t474, (void *)(gm_srtm6s969i45s));
                                GM_ST(uintptr_t, (uint8_t *)&t474 + 8, (uintptr_t)(21));
                                gm_t39uai1ajk3qv t475;
                                gm_f_unsupported_2seq49l((void *)&t475, t472, GM_CONV(gm_cseahvg8v7p0vs, t473), t474);
                                gm_t9j082e6qvnjj t476;
                                GM_ST(uint8_t, (uint8_t *)&t476 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t476 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t476 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t476 + 8, 0);
                                ((uint8_t *)&t476)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t476 + 16), t475);
                                memmove(gm_out, ((uint8_t *)&t476), 112);
                                goto gm_fdone;
                            }
gm_lb69:;
                        }
gm_lb68:;
                        {
                            void * t477 = F.l0;
                            gm_tl874t2atnlh3 t478 = gm_l4;
                            gm_b16_8 t479;
                            GM_ST(void *, (uint8_t *)&t479, (void *)(gm_srfcp96u5v1ui));
                            GM_ST(uintptr_t, (uint8_t *)&t479 + 8, (uintptr_t)(36));
                            gm_t39uai1ajk3qv t480;
                            gm_f_unsupported_2seq49l((void *)&t480, t477, GM_CONV(gm_cseahvg8v7p0vs, t478), t479);
                            gm_t9j082e6qvnjj t481;
                            GM_ST(uint8_t, (uint8_t *)&t481 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t481 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t481 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t481 + 8, 0);
                            ((uint8_t *)&t481)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t481 + 16), t480);
                            memmove(gm_out, ((uint8_t *)&t481), 112);
                            goto gm_fdone;
                        }
gm_lb73:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 797, 24);
                    }
gm_lb57:;
                }
gm_lb56:;
            }
            {
                {
                    {
                        {
                            uint8_t t482 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t483 = (uint8_t)(t482 == ((uint8_t)8ULL));
                            if (!t483) {
                                goto gm_lb76;
                            }
                            goto gm_lb75;
                        }
gm_lb76:;
                        {
                            uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t485 = (uint8_t)(t484 == ((uint8_t)9ULL));
                            if (!t485) {
                                goto gm_lb77;
                            }
                            goto gm_lb75;
                        }
gm_lb77:;
                        {
                            uint8_t t486 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t487 = (uint8_t)(t486 == ((uint8_t)7ULL));
                            if (!t487) {
                                goto gm_lb78;
                            }
                            goto gm_lb75;
                        }
gm_lb78:;
                        {
                            uint8_t t488 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t489 = (uint8_t)(t488 == ((uint8_t)16ULL));
                            if (!t489) {
                                goto gm_lb79;
                            }
                            goto gm_lb75;
                        }
gm_lb79:;
                        {
                            uint8_t t490 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t491 = (uint8_t)(t490 == ((uint8_t)15ULL));
                            if (!t491) {
                                goto gm_lb80;
                            }
                            goto gm_lb75;
                        }
gm_lb80:;
                        goto gm_lb74;
                    }
gm_lb75:;
                    void * t492 = F.l0;
                    gm_tl874t2atnlh3 t493 = gm_l4;
                    gm_b16_8 t494 = GM_LD(gm_b16_8, ((uint8_t *)gm_cpr60jghihe));
                    gm_t39uai1ajk3qv t495;
                    gm_f_trap_jftbss6((void *)&t495, (void *)NULL, cx, NULL, t492, t493, t494);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t9j082e6qvnjj t496;
                    GM_ST(uint8_t, (uint8_t *)&t496 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t496 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t496 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t496 + 8, 0);
                    ((uint8_t *)&t496)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t496 + 16), t495);
                    memmove(gm_out, ((uint8_t *)&t496), 112);
                    goto gm_fdone;
                }
gm_lb74:;
                {
                    void * t497 = F.l0;
                    gm_tl874t2atnlh3 t498 = gm_l4;
                    gm_b16_8 t499;
                    GM_ST(void *, (uint8_t *)&t499, (void *)(gm_srfcp96u5v1ui));
                    GM_ST(uintptr_t, (uint8_t *)&t499 + 8, (uintptr_t)(36));
                    gm_t39uai1ajk3qv t500;
                    gm_f_unsupported_2seq49l((void *)&t500, t497, GM_CONV(gm_cseahvg8v7p0vs, t498), t499);
                    gm_t9j082e6qvnjj t501;
                    GM_ST(uint8_t, (uint8_t *)&t501 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t501 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t501 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t501 + 8, 0);
                    ((uint8_t *)&t501)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t501 + 16), t500);
                    memmove(gm_out, ((uint8_t *)&t501), 112);
                    goto gm_fdone;
                }
gm_lb81:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 719, 5);
            }
        }
gm_lb1:;
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
extern __attribute__((aligned(4))) void gm_f_hash_word_r89fvfg(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_ttovhfee8mf28 gm_f_int_info_2r9qb5n(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_info_2r9qb5n F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 251u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_ttovhfee8mf28 gm_l4;
    gm_t53at80pfdus3 gm_l5;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t9, GM_CONV(gm_csguankko4lmhd, t10));
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)0ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 8));
            gm_l2 = t13;
            uint16_t t14 = GM_LD(uint16_t, ((((uint8_t *)&gm_l5) + 8) + 2));
            gm_l3 = t14;
            uint8_t t15 = gm_l2;
            uint16_t t16 = gm_l3;
            uint32_t t17 = (uint32_t)t16;
            gm_ttovhfee8mf28 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), t15);
            GM_ST(uint32_t, ((uint8_t *)&t18 + 4), t17);
            gm_l4 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb3;
            }
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, ((uint8_t *)t21));
            void * t23 = GM_LD(void *, ((uint8_t *)t22));
            void * t24 = (void *)(((uint8_t *)t23) + 88);
            uint32_t t25 = gm_f_ptr_bits_glps5i7(t24);
            gm_ttovhfee8mf28 t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t26 + 4), t25);
            gm_l4 = t26;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)2ULL));
            if (!t28) {
                goto gm_lb4;
            }
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, ((uint8_t *)t29));
            void * t31 = GM_LD(void *, ((uint8_t *)t30));
            void * t32 = (void *)(((uint8_t *)t31) + 88);
            uint32_t t33 = gm_f_ptr_bits_glps5i7(t32);
            gm_ttovhfee8mf28 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1));
            GM_ST(uint32_t, ((uint8_t *)&t34 + 4), t33);
            gm_l4 = t34;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_ttovhfee8mf28 t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t35 + 4), ((uint32_t)128ULL));
            gm_l4 = t35;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 6, 74);
    }
gm_lb1:;
    gm_ttovhfee8mf28 t36 = gm_l4;
    F.ret = t36;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_int_op_jnafd2a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, unsigned __int128 a2, unsigned __int128 a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_int_op_jnafd2a F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    unsigned __int128 gm_l2; /* x */
    unsigned __int128 gm_l3; /* y */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_ttovhfee8mf28 gm_l7; /* info */
    gm_b16_8 gm_l8; /* sym */
    __int128 gm_l9; /* amount */
    uint32_t gm_l10; /* n */
    gm_t9qkh0rsooc8d gm_l11; /* flavor */
    uint8_t gm_l12; /* is_div */
    __int128 gm_l13; /* a */
    __int128 gm_l14; /* c */
    __int128 gm_l15; /* exact */
    gm_t9qkh0rsooc8d gm_l19; /* flavor */
    gm_t9qkh0rsooc8d gm_l20; /* flavor */
    gm_t9qkh0rsooc8d gm_l21; /* flavor */
    __int128 gm_l22; /* a */
    __int128 gm_l23; /* c */
    __int128 gm_l24; /* exact */
    GM_STACK_CHECK(&F, 3966u);
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
    uint8_t gm_l28;
    gm_tabb710d9mq2e gm_l29;
    __int128 gm_l30;
    gm_tngtuiuskn938 gm_l31;
    gm_tbci5ospk5nnn gm_l32;
    __int128 gm_l33;
    gm_tbci5ospk5nnn gm_l34;
    {
        void * t29 = gm_l0;
        gm_t88rcucg1b7uk t30 = gm_l4;
        gm_ttovhfee8mf28 t31 = gm_f_int_info_2r9qb5n(t29, GM_CONV(gm_csguankko4lmhd, t30));
        gm_l7 = t31;
        gm_ts6oqppjh0ba3 t32 = gm_l1;
        gm_b16_8 t33 = gm_f_binop_symbol_9ijp0eo(t32);
        gm_l8 = t33;
        {
            {
                {
                    {
                        {
                            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)3ULL));
                            if (!t35) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)6ULL));
                            if (!t37) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t38 = gm_l0;
                    unsigned __int128 t39 = gm_l3;
                    gm_t88rcucg1b7uk t40 = gm_l5;
                    __int128 t41 = gm_f_int_value_6hmh8tj(t38, t39, GM_CONV(gm_csguankko4lmhd, t40));
                    gm_l9 = t41;
                    __int128 t42 = gm_l9;
                    uint8_t t43 = (t42 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    if (t43) {
                        gm_l28 = ((uint8_t)1);
                    } else {
                        __int128 t44 = gm_l9;
                        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                        __int128 t46 = (__int128)t45;
                        uint8_t t47 = (t44 >= t46);
                        gm_l28 = t47;
                    }
                    uint8_t t48 = gm_l28;
                    if (t48) {
                        void * t49 = gm_l0;
                        gm_tl874t2atnlh3 t50 = gm_l6;
                        __int128 t51 = gm_l9;
                        void * t52 = gm_l0;
                        void * t53 = GM_LD(void *, ((uint8_t *)t52));
                        gm_t88rcucg1b7uk t54 = gm_l4;
                        gm_b16_8 t55 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t53, t54);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t7a4aqr549lb5 t56;
                        GM_ST(__int128, ((uint8_t *)&t56 + 0), t51);
                        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 16), t55);
                        gm_f_format_0fuesvo((void *)&gm_l29, (void *)NULL, cx, NULL, &t56);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcue4ionbe9lgh, 213, 79);
                        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                        gm_t39uai1ajk3qv t60;
                        gm_f_trap_jftbss6((void *)&t60, (void *)NULL, cx, NULL, t49, t50, t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t8carr7mku484 t61;
                        GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t61 + 8, 0);
                        ((uint8_t *)&t61)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t61 + 16), t60);
                        memmove(gm_out, ((uint8_t *)&t61), 112);
                        goto gm_fdone;
                    }
                    __int128 t62 = gm_l9;
                    uint32_t t63 = (uint32_t)t62;
                    gm_l10 = t63;
                    {
                        {
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)3ULL));
                            if (!t65) {
                                goto gm_lb7;
                            }
                            gm_t9qkh0rsooc8d t66 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                            gm_l11 = t66;
                            uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                            uint8_t t68 = (t67 <= ((uint32_t)63ULL));
                            if (t68) {
                                void * t69 = gm_l0;
                                void * t70 = gm_l0;
                                unsigned __int128 t71 = gm_l2;
                                gm_t88rcucg1b7uk t72 = gm_l4;
                                __int128 t73 = gm_f_int_value_6hmh8tj(t70, t71, GM_CONV(gm_csguankko4lmhd, t72));
                                uint32_t t74 = gm_l10;
                                unsigned __int128 n75 = (unsigned __int128)t74;
                                if (n75 >= 128) gm_trap_shift((uint64_t)n75, "i128", gm_srcue4ionbe9lgh, 216, 57);
                                __int128 r76 = (__int128)((unsigned __int128)t73 << (int)n75);
                                bool o77 = (__int128)(r76 >> (int)n75) != t73;
                                if (o77) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 216, 57);
                                __int128 t78 = r76;
                                gm_t9qkh0rsooc8d t79 = gm_l11;
                                gm_t88rcucg1b7uk t80 = gm_l4;
                                gm_b16_8 t81 = gm_l8;
                                gm_tl874t2atnlh3 t82 = gm_l6;
                                gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t69, t78, t79, t80, t81, t82);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
                            void * t83 = gm_l0;
                            void * t84 = gm_l0;
                            unsigned __int128 t85 = gm_l2;
                            gm_t88rcucg1b7uk t86 = gm_l4;
                            gm_tbci5ospk5nnn t87;
                            gm_f_big_of_1cq0qao((void *)&t87, (void *)NULL, cx, NULL, t84, t85, t86);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t88 = gm_l10;
                            uint64_t t89 = (uint64_t)t88;
                            gm_tbci5ospk5nnn t90;
                            gm_f_shl_qqecl81((void *)&t90, (void *)NULL, cx, NULL, &t87, t89);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t9qkh0rsooc8d t91 = gm_l11;
                            gm_t88rcucg1b7uk t92 = gm_l4;
                            gm_b16_8 t93 = gm_l8;
                            gm_tl874t2atnlh3 t94 = gm_l6;
                            gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t83, &t90, t91, t92, t93, t94);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint8_t t95 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    if (t95) {
                        void * t96 = gm_l0;
                        void * t97 = gm_l0;
                        unsigned __int128 t98 = gm_l2;
                        gm_t88rcucg1b7uk t99 = gm_l4;
                        __int128 t100 = gm_f_int_value_6hmh8tj(t97, t98, GM_CONV(gm_csguankko4lmhd, t99));
                        uint32_t t101 = gm_l10;
                        unsigned __int128 n102 = (unsigned __int128)t101;
                        if (n102 >= 128) gm_trap_shift((uint64_t)n102, "i128", gm_srcue4ionbe9lgh, 219, 45);
                        __int128 t103 = (__int128)(t100 >> (int)n102);
                        unsigned __int128 t104;
                        memset(&t104, 0, sizeof t104); memcpy(&t104, ((uint8_t *)&t103), sizeof t104 < 16 ? sizeof t104 : 16);
                        gm_t88rcucg1b7uk t105 = gm_l4;
                        unsigned __int128 t106 = gm_f_wrap_bt8r149(t96, t104, GM_CONV(gm_csguankko4lmhd, t105));
                        gm_t8carr7mku484 t107;
                        memset(&t107, 0, sizeof t107);
                        ((uint8_t *)&t107)[0] = 0;
                        GM_ST(unsigned __int128, ((uint8_t *)&t107 + 16), t106);
                        memmove(gm_out, ((uint8_t *)&t107), 112);
                        goto gm_fdone;
                    }
                    unsigned __int128 t108 = gm_l2;
                    uint32_t t109 = gm_l10;
                    unsigned __int128 t110 = (unsigned __int128)t109;
                    unsigned __int128 n111 = (unsigned __int128)t110;
                    if (n111 >= 128) gm_trap_shift((uint64_t)n111, "u128", gm_srcue4ionbe9lgh, 220, 20);
                    unsigned __int128 t112 = (unsigned __int128)(t108 >> (int)n111);
                    gm_t8carr7mku484 t113;
                    memset(&t113, 0, sizeof t113);
                    ((uint8_t *)&t113)[0] = 0;
                    GM_ST(unsigned __int128, ((uint8_t *)&t113 + 16), t112);
                    memmove(gm_out, ((uint8_t *)&t113), 112);
                    goto gm_fdone;
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)4ULL));
                            if (!t115) {
                                goto gm_lb10;
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t117 = (uint8_t)(t116 == ((uint8_t)5ULL));
                            if (!t117) {
                                goto gm_lb11;
                            }
                            goto gm_lb9;
                        }
gm_lb11:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    unsigned __int128 t118 = gm_l3;
                    uint8_t t119 = ((t118 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                    if (t119) {
                        void * t120 = gm_l0;
                        gm_tl874t2atnlh3 t121 = gm_l6;
                        gm_b16_8 t122;
                        GM_ST(void *, (uint8_t *)&t122, (void *)(gm_slf2kl43ogurh));
                        GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(16));
                        gm_t39uai1ajk3qv t123;
                        gm_f_trap_jftbss6((void *)&t123, (void *)NULL, cx, NULL, t120, t121, t122);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t8carr7mku484 t124;
                        GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t124 + 8, 0);
                        ((uint8_t *)&t124)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t124 + 16), t123);
                        memmove(gm_out, ((uint8_t *)&t124), 112);
                        goto gm_fdone;
                    }
                    gm_ts6oqppjh0ba3 t125 = gm_l1;
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&t125) + 0));
                    uint8_t t127 = (uint8_t)((t126 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t128;
                    GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)4ULL));
                    gm_l12 = t127;
                    uint32_t t129 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                    uint8_t t130 = (t129 <= ((uint32_t)63ULL));
                    if (t130) {
                        void * t131 = gm_l0;
                        unsigned __int128 t132 = gm_l2;
                        gm_t88rcucg1b7uk t133 = gm_l4;
                        __int128 t134 = gm_f_int_value_6hmh8tj(t131, t132, GM_CONV(gm_csguankko4lmhd, t133));
                        gm_l13 = t134;
                        void * t135 = gm_l0;
                        unsigned __int128 t136 = gm_l3;
                        gm_t88rcucg1b7uk t137 = gm_l5;
                        __int128 t138 = gm_f_int_value_6hmh8tj(t135, t136, GM_CONV(gm_csguankko4lmhd, t137));
                        gm_l14 = t138;
                        uint8_t t139 = gm_l12;
                        if (t139) {
                            __int128 t140 = gm_l13;
                            __int128 t141 = gm_l14;
                            if (t141 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 228, 38);
                            if (t140 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t141 == (__int128)-1) gm_trap("integer overflow in `/`", gm_srcue4ionbe9lgh, 228, 38);
                            __int128 t142 = (__int128)(t140 / t141);
                            gm_l30 = t142;
                        } else {
                            __int128 t143 = gm_l13;
                            __int128 t144 = gm_l14;
                            if (t144 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 228, 53);
                            if (t143 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t144 == (__int128)-1) gm_trap("integer overflow in `%`", gm_srcue4ionbe9lgh, 228, 53);
                            __int128 t145 = (__int128)(t143 % t144);
                            gm_l30 = t145;
                        }
                        __int128 t146 = gm_l30;
                        gm_l15 = t146;
                        void * t147 = gm_l0;
                        __int128 t148 = gm_l15;
                        gm_t88rcucg1b7uk t149 = gm_l4;
                        gm_b16_8 t150 = gm_l8;
                        gm_tl874t2atnlh3 t151 = gm_l6;
                        gm_t9qkh0rsooc8d t152;
                        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)0ULL));
                        gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t147, t148, t152, t149, t150, t151);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    void * t153 = gm_l0;
                    unsigned __int128 t154 = gm_l2;
                    gm_t88rcucg1b7uk t155 = gm_l4;
                    gm_f_big_of_1cq0qao((void *)&F.l16, (void *)NULL, cx, NULL, t153, t154, t155);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t156 = gm_l0;
                    unsigned __int128 t157 = gm_l3;
                    gm_t88rcucg1b7uk t158 = gm_l5;
                    gm_f_big_of_1cq0qao((void *)&F.l17, (void *)NULL, cx, NULL, t156, t157, t158);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t159 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                            if (!t159) {
                                goto gm_lb13;
                            }
                            uint8_t t160 = gm_l12;
                            uint8_t t161 = (uint8_t)!t160;
                            if (!t161) {
                                goto gm_lb13;
                            }
                            gm_tbci5ospk5nnn t162 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l17));
                            gm_tbci5ospk5nnn t163;
                            gm_f_one_08qg1op((void *)&t163, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tbci5ospk5nnn t164;
                            gm_f_neg_agd5c5k((void *)&t164, (void *)NULL, cx, NULL, &t163);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t165 = gm_f_eq_60fjn21(&t162, &t164);
                            if (!t165) {
                                goto gm_lb13;
                            }
                            void * t166 = gm_l0;
                            gm_t88rcucg1b7uk t167 = gm_l4;
                            gm_tbci5ospk5nnn t168 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l16));
                            gm_f_int_range_hue3vms((void *)&gm_l31, (void *)NULL, cx, NULL, t166, t167);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t169 = gm_f_eq_60fjn21(&t168, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&gm_l31)));
                            if (!t169) {
                                goto gm_lb13;
                            }
                            void * t170 = gm_l0;
                            gm_tl874t2atnlh3 t171 = gm_l6;
                            gm_b16_8 t172;
                            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_s3385032lmmqq));
                            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(23));
                            gm_t39uai1ajk3qv t173;
                            gm_f_trap_jftbss6((void *)&t173, (void *)NULL, cx, NULL, t170, t171, t172);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t8carr7mku484 t174;
                            GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t174 + 8, 0);
                            ((uint8_t *)&t174)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t174 + 16), t173);
                            memmove(gm_out, ((uint8_t *)&t174), 112);
                            goto gm_fdone;
                        }
gm_lb13:;
                    }
gm_lb12:;
                    uint8_t t175 = gm_l12;
                    if (t175) {
                        gm_tbci5ospk5nnn t176 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l16));
                        gm_tbci5ospk5nnn t177 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l17));
                        gm_f_div_f0qrfbr((void *)&gm_l32, (void *)NULL, cx, NULL, &t176, &t177);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    } else {
                        gm_tbci5ospk5nnn t178 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l16));
                        gm_tbci5ospk5nnn t179 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l17));
                        gm_f_rem_ib4l1vl((void *)&gm_l32, (void *)NULL, cx, NULL, &t178, &t179);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    memmove(((uint8_t *)&F.l18), ((uint8_t *)&gm_l32), 48);
                    void * t180 = gm_l0;
                    gm_t88rcucg1b7uk t181 = gm_l4;
                    gm_b16_8 t182 = gm_l8;
                    gm_tl874t2atnlh3 t183 = gm_l6;
                    gm_tbci5ospk5nnn t184 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l18));
                    gm_t9qkh0rsooc8d t185;
                    GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)0ULL));
                    gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t180, &t184, t185, t181, t182, t183);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb8:;
            }
            {
                {
                    {
                        {
                            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t187 = (uint8_t)(t186 == ((uint8_t)0ULL));
                            if (!t187) {
                                goto gm_lb16;
                            }
                            gm_t9qkh0rsooc8d t188 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                            gm_l19 = t188;
                            goto gm_lb15;
                        }
gm_lb16:;
                        {
                            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t190 = (uint8_t)(t189 == ((uint8_t)1ULL));
                            if (!t190) {
                                goto gm_lb17;
                            }
                            gm_t9qkh0rsooc8d t191 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                            gm_l19 = t191;
                            goto gm_lb15;
                        }
gm_lb17:;
                        {
                            uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t193 = (uint8_t)(t192 == ((uint8_t)2ULL));
                            if (!t193) {
                                goto gm_lb18;
                            }
                            gm_t9qkh0rsooc8d t194 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                            gm_l19 = t194;
                            goto gm_lb15;
                        }
gm_lb18:;
                        goto gm_lb14;
                    }
gm_lb15:;
                    uint32_t t195 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                    uint8_t t196 = (t195 <= ((uint32_t)63ULL));
                    if (t196) {
                        void * t197 = gm_l0;
                        unsigned __int128 t198 = gm_l2;
                        gm_t88rcucg1b7uk t199 = gm_l4;
                        __int128 t200 = gm_f_int_value_6hmh8tj(t197, t198, GM_CONV(gm_csguankko4lmhd, t199));
                        gm_l22 = t200;
                        void * t201 = gm_l0;
                        unsigned __int128 t202 = gm_l3;
                        gm_t88rcucg1b7uk t203 = gm_l5;
                        __int128 t204 = gm_f_int_value_6hmh8tj(t201, t202, GM_CONV(gm_csguankko4lmhd, t203));
                        gm_l23 = t204;
                        {
                            {
                                uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                uint8_t t206 = (uint8_t)(t205 == ((uint8_t)0ULL));
                                if (!t206) {
                                    goto gm_lb20;
                                }
                                __int128 t207 = gm_l22;
                                __int128 t208 = gm_l23;
                                __int128 r209; bool o210 = __builtin_add_overflow(t207, t208, &r209);
                                if (o210) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 242, 32);
                                __int128 t211 = r209;
                                gm_l33 = t211;
                                goto gm_lb19;
                            }
gm_lb20:;
                            {
                                uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                uint8_t t213 = (uint8_t)(t212 == ((uint8_t)1ULL));
                                if (!t213) {
                                    goto gm_lb21;
                                }
                                __int128 t214 = gm_l22;
                                __int128 t215 = gm_l23;
                                __int128 r216; bool o217 = __builtin_sub_overflow(t214, t215, &r216);
                                if (o217) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 243, 32);
                                __int128 t218 = r216;
                                gm_l33 = t218;
                                goto gm_lb19;
                            }
gm_lb21:;
                            {
                                __int128 t219 = gm_l22;
                                __int128 t220 = gm_l23;
                                __int128 r221; bool o222 = __builtin_mul_overflow(t219, t220, &r221);
                                if (o222) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 244, 26);
                                __int128 t223 = r221;
                                gm_l33 = t223;
                                goto gm_lb19;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 241, 26);
                        }
gm_lb19:;
                        __int128 t224 = gm_l33;
                        gm_l24 = t224;
                        void * t225 = gm_l0;
                        __int128 t226 = gm_l24;
                        gm_t9qkh0rsooc8d t227 = gm_l19;
                        gm_t88rcucg1b7uk t228 = gm_l4;
                        gm_b16_8 t229 = gm_l8;
                        gm_tl874t2atnlh3 t230 = gm_l6;
                        gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t225, t226, t227, t228, t229, t230);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    void * t231 = gm_l0;
                    unsigned __int128 t232 = gm_l2;
                    gm_t88rcucg1b7uk t233 = gm_l4;
                    gm_f_big_of_1cq0qao((void *)&F.l25, (void *)NULL, cx, NULL, t231, t232, t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t234 = gm_l0;
                    unsigned __int128 t235 = gm_l3;
                    gm_t88rcucg1b7uk t236 = gm_l5;
                    gm_f_big_of_1cq0qao((void *)&F.l26, (void *)NULL, cx, NULL, t234, t235, t236);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t238 = (uint8_t)(t237 == ((uint8_t)0ULL));
                            if (!t238) {
                                goto gm_lb24;
                            }
                            gm_tbci5ospk5nnn t239 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l25));
                            gm_tbci5ospk5nnn t240 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l26));
                            gm_f_add_420lj6o((void *)&gm_l34, (void *)NULL, cx, NULL, &t239, &t240);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb23;
                        }
gm_lb24:;
                        {
                            uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t242 = (uint8_t)(t241 == ((uint8_t)1ULL));
                            if (!t242) {
                                goto gm_lb25;
                            }
                            gm_tbci5ospk5nnn t243 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l25));
                            gm_tbci5ospk5nnn t244 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l26));
                            gm_f_sub_pktm5cr((void *)&gm_l34, (void *)NULL, cx, NULL, &t243, &t244);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb23;
                        }
gm_lb25:;
                        {
                            gm_tbci5ospk5nnn t245 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l25));
                            gm_tbci5ospk5nnn t246 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l26));
                            gm_f_mul_nfj5rjv((void *)&gm_l34, (void *)NULL, cx, NULL, &t245, &t246);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb23;
                        }
gm_lb26:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 250, 22);
                    }
gm_lb23:;
                    memmove(((uint8_t *)&F.l27), ((uint8_t *)&gm_l34), 48);
                    void * t247 = gm_l0;
                    gm_t9qkh0rsooc8d t248 = gm_l19;
                    gm_t88rcucg1b7uk t249 = gm_l4;
                    gm_b16_8 t250 = gm_l8;
                    gm_tl874t2atnlh3 t251 = gm_l6;
                    gm_tbci5ospk5nnn t252 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l27));
                    gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t247, &t252, t248, t249, t250, t251);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb14:;
            }
            {
                {
                    void * t253 = gm_l0;
                    gm_tl874t2atnlh3 t254 = gm_l6;
                    gm_b16_8 t255;
                    GM_ST(void *, (uint8_t *)&t255, (void *)(gm_sqe4aodms9tfs));
                    GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(13));
                    gm_t39uai1ajk3qv t256;
                    gm_f_unsupported_2seq49l((void *)&t256, t253, GM_CONV(gm_cseahvg8v7p0vs, t254), t255);
                    gm_t8carr7mku484 t257;
                    GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t257 + 8, 0);
                    ((uint8_t *)&t257)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t257 + 16), t256);
                    memmove(gm_out, ((uint8_t *)&t257), 112);
                    goto gm_fdone;
                }
gm_lb27:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 210, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_int_range_hue3vms(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_int_range_hue3vms F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_ttovhfee8mf28 gm_l2; /* info */
    GM_STACK_CHECK(&F, 741u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
    gm_l2 = t4;
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
    if (t5) {
        gm_tbci5ospk5nnn t6;
        gm_f_one_08qg1op((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint32_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint32_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 39, 38);
        uint32_t t10 = r8;
        uint64_t t11 = (uint64_t)t10;
        gm_f_shl_qqecl81((void *)&F.l3, (void *)NULL, cx, NULL, &t6, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t12;
        gm_f_neg_agd5c5k((void *)&t12, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t13 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3));
        gm_tbci5ospk5nnn t14;
        gm_f_one_08qg1op((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t15;
        gm_f_sub_pktm5cr((void *)&t15, (void *)NULL, cx, NULL, &t13, &t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t16;
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t16 + 0), t12);
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t16 + 48), t15);
        memmove(gm_out, ((uint8_t *)&t16), 96);
        goto gm_fdone;
    }
    gm_tbci5ospk5nnn t17;
    gm_f_zero_s25l990((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t18;
    gm_f_one_08qg1op((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
    uint64_t t20 = (uint64_t)t19;
    gm_tbci5ospk5nnn t21;
    gm_f_shl_qqecl81((void *)&t21, (void *)NULL, cx, NULL, &t18, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t22;
    gm_f_one_08qg1op((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t23;
    gm_f_sub_pktm5cr((void *)&t23, (void *)NULL, cx, NULL, &t21, &t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t24;
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t24 + 0), t17);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t24 + 48), t23);
    memmove(gm_out, ((uint8_t *)&t24), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) __int128 gm_f_int_value_6hmh8tj(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_value_6hmh8tj F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_ttovhfee8mf28 gm_l3; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_ttovhfee8mf28 t6 = gm_f_int_info_2r9qb5n(t4, GM_CONV(gm_csguankko4lmhd, t5));
    gm_l3 = t6;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint8_t t9 = (t8 < ((uint32_t)128ULL));
            if (!t9) {
                goto gm_lb2;
            }
            unsigned __int128 t10 = gm_l1;
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint32_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint32_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 23, 58);
            uint32_t t14 = r12;
            unsigned __int128 t15 = (unsigned __int128)t14;
            unsigned __int128 n16 = (unsigned __int128)t15;
            if (n16 >= 128) gm_trap_shift((uint64_t)n16, "u128", gm_srcue4ionbe9lgh, 23, 45);
            unsigned __int128 t17 = (unsigned __int128)(t10 >> (int)n16);
            unsigned __int128 t18 = (unsigned __int128)(t17 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
            uint8_t t19 = ((t18 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
            if (!t19) {
                goto gm_lb2;
            }
            unsigned __int128 t20 = gm_l1;
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            unsigned __int128 t22 = gm_f_mask_gbhl4hp(t21);
            unsigned __int128 t23 = (unsigned __int128)~t22;
            unsigned __int128 t24 = (unsigned __int128)(t20 | t23);
            __int128 t25;
            memset(&t25, 0, sizeof t25); memcpy(&t25, ((uint8_t *)&t24), sizeof t25 < 16 ? sizeof t25 : 16);
            F.ret = t25;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    unsigned __int128 t26 = gm_l1;
    __int128 t27;
    memset(&t27, 0, sizeof t27); memcpy(&t27, ((uint8_t *)&t26), sizeof t27 < 16 ? sizeof t27 : 16);
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_less_d82rkss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_less_d82rkss F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* r */
    uint8_t gm_l7; /* x */
    uint8_t gm_l8; /* y */
    uint32_t gm_l9; /* i */
    uint32_t gm_l10; /* j */
    void * gm_l13; /* x */
    void * gm_l14; /* y */
    gm_b16_8 gm_l15; /* elems */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_to6glrduves6o gm_l19; /* x */
    gm_to6glrduves6o gm_l20; /* y */
    gm_t88rcucg1b7uk gm_l21; /* et */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    GM_STACK_CHECK(&F, 5765u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_ttk92ufj2f6eh gm_l27;
    gm_t6evimt84lndq gm_l28;
    uint8_t gm_l29;
    gm_thrdpodrs8pgt gm_l30;
    gm_tbci5ospk5nnn gm_l31;
    gm_thrdpodrs8pgt gm_l32;
    gm_tbci5ospk5nnn gm_l33;
    gm_th2lqbo77pvsc gm_l34;
    gm_b16_8 gm_l35;
    gm_tsg40glpvq01o gm_l36;
    gm_t53at80pfdus3 gm_l37;
    gm_tsg40glpvq01o gm_l38;
    {
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, ((uint8_t *)t34));
        void * t36 = (void *)(((uint8_t *)t35) + 8);
        gm_l5 = t36;
        {
            {
                {
                    {
                        {
                            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)0ULL));
                            if (!t38) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)1ULL));
                            if (!t40) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        {
                            void * t41 = gm_l0;
                            gm_t88rcucg1b7uk t42 = gm_l3;
                            gm_t88rcucg1b7uk t43 = gm_l3;
                            gm_tl874t2atnlh3 t44 = gm_l4;
                            gm_ts6oqppjh0ba3 t45;
                            GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)10ULL));
                            gm_t6evimt84lndq t46 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
                            gm_t6evimt84lndq t47 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l2));
                            gm_f_scalar_binary_bvc65kt((void *)&gm_l27, (void *)NULL, cx, NULL, t41, t45, &t46, &t47, t42, t43, t44);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                            if (t48) {
                            } else {
                                gm_t39uai1ajk3qv t49 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l27) + 16));
                                gm_tsg40glpvq01o t50;
                                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t50 + 8, 0);
                                ((uint8_t *)&t50)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t50 + 16), t49);
                                memmove(gm_out, ((uint8_t *)&t50), 112);
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&gm_l28), (((uint8_t *)&gm_l27) + 16), 48);
                            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)2ULL));
                            if (!t52) {
                                goto gm_lb7;
                            }
                            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 16));
                            gm_l6 = t53;
                            uint8_t t54 = gm_l6;
                            gm_tsg40glpvq01o t55;
                            memset(&t55, 0, sizeof t55);
                            ((uint8_t *)&t55)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t55 + 16), t54);
                            memmove(gm_out, ((uint8_t *)&t55), 112);
                            goto gm_fdone;
                        }
gm_lb7:;
                        void * t56 = gm_l0;
                        gm_tl874t2atnlh3 t57 = gm_l4;
                        gm_b16_8 t58;
                        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s9ofbo8c28rrv));
                        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(15));
                        gm_t39uai1ajk3qv t59;
                        gm_f_unsupported_2seq49l((void *)&t59, t56, GM_CONV(gm_cseahvg8v7p0vs, t57), t58);
                        gm_tsg40glpvq01o t60;
                        GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t60 + 8, 0);
                        ((uint8_t *)&t60)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t60 + 16), t59);
                        memmove(gm_out, ((uint8_t *)&t60), 112);
                        goto gm_fdone;
                    }
gm_lb6:;
                }
gm_lb2:;
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)2ULL));
                    if (!t62) {
                        goto gm_lb8;
                    }
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
                    gm_l7 = t63;
                    {
                        {
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
                            if (!t65) {
                                goto gm_lb10;
                            }
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
                            gm_l8 = t66;
                            uint8_t t67 = gm_l7;
                            uint8_t t68 = (uint8_t)!t67;
                            if (t68) {
                                uint8_t t69 = gm_l8;
                                gm_l29 = t69;
                            } else {
                                gm_l29 = ((uint8_t)0);
                            }
                            uint8_t t70 = gm_l29;
                            gm_tsg40glpvq01o t71;
                            memset(&t71, 0, sizeof t71);
                            ((uint8_t *)&t71)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t71 + 16), t70);
                            memmove(gm_out, ((uint8_t *)&t71), 112);
                            goto gm_fdone;
                        }
gm_lb10:;
                        void * t72 = gm_l0;
                        gm_tl874t2atnlh3 t73 = gm_l4;
                        gm_b16_8 t74;
                        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s9ofbo8c28rrv));
                        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(15));
                        gm_t39uai1ajk3qv t75;
                        gm_f_unsupported_2seq49l((void *)&t75, t72, GM_CONV(gm_cseahvg8v7p0vs, t73), t74);
                        gm_tsg40glpvq01o t76;
                        GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t76 + 8, 0);
                        ((uint8_t *)&t76)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t76 + 16), t75);
                        memmove(gm_out, ((uint8_t *)&t76), 112);
                        goto gm_fdone;
                    }
gm_lb9:;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)10ULL));
                    if (!t78) {
                        goto gm_lb11;
                    }
                    uint32_t t79 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
                    gm_l9 = t79;
                    {
                        {
                            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t81 = (uint8_t)(t80 == ((uint8_t)10ULL));
                            if (!t81) {
                                goto gm_lb13;
                            }
                            uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
                            gm_l10 = t82;
                            void * t83 = gm_l0;
                            gm_t88rcucg1b7uk t84 = gm_l3;
                            uint32_t t85 = gm_l9;
                            gm_f_variant_tag_4pbamin((void *)&gm_l30, t83, GM_CONV(gm_csguankko4lmhd, t84), t85);
                            uint8_t t86 = (uint8_t)((((uint8_t *)&gm_l30))[48] != 0);
                            if (t86) {
                                memmove(((uint8_t *)&gm_l31), ((uint8_t *)&gm_l30), 48);
                            } else {
                                void * t87 = gm_l0;
                                gm_tl874t2atnlh3 t88 = gm_l4;
                                gm_b16_8 t89;
                                GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t90;
                                gm_f_unsupported_2seq49l((void *)&t90, t87, GM_CONV(gm_cseahvg8v7p0vs, t88), t89);
                                gm_tsg40glpvq01o t91;
                                GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t91 + 8, 0);
                                ((uint8_t *)&t91)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t91 + 16), t90);
                                memmove(gm_out, ((uint8_t *)&t91), 112);
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&F.l11), ((uint8_t *)&gm_l31), 48);
                            void * t92 = gm_l0;
                            gm_t88rcucg1b7uk t93 = gm_l3;
                            uint32_t t94 = gm_l10;
                            gm_f_variant_tag_4pbamin((void *)&gm_l32, t92, GM_CONV(gm_csguankko4lmhd, t93), t94);
                            uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l32))[48] != 0);
                            if (t95) {
                                memmove(((uint8_t *)&gm_l33), ((uint8_t *)&gm_l32), 48);
                            } else {
                                void * t96 = gm_l0;
                                gm_tl874t2atnlh3 t97 = gm_l4;
                                gm_b16_8 t98;
                                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t99;
                                gm_f_unsupported_2seq49l((void *)&t99, t96, GM_CONV(gm_cseahvg8v7p0vs, t97), t98);
                                gm_tsg40glpvq01o t100;
                                GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t100 + 8, 0);
                                ((uint8_t *)&t100)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t100 + 16), t99);
                                memmove(gm_out, ((uint8_t *)&t100), 112);
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&F.l12), ((uint8_t *)&gm_l33), 48);
                            int32_t t101 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l12)));
                            uint8_t t102 = (t101 < ((int32_t)0LL));
                            gm_tsg40glpvq01o t103;
                            memset(&t103, 0, sizeof t103);
                            ((uint8_t *)&t103)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t103 + 16), t102);
                            memmove(gm_out, ((uint8_t *)&t103), 112);
                            goto gm_fdone;
                        }
gm_lb13:;
                        void * t104 = gm_l0;
                        gm_tl874t2atnlh3 t105 = gm_l4;
                        gm_b16_8 t106;
                        GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s9ofbo8c28rrv));
                        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(15));
                        gm_t39uai1ajk3qv t107;
                        gm_f_unsupported_2seq49l((void *)&t107, t104, GM_CONV(gm_cseahvg8v7p0vs, t105), t106);
                        gm_tsg40glpvq01o t108;
                        GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t108 + 8, 0);
                        ((uint8_t *)&t108)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t108 + 16), t107);
                        memmove(gm_out, ((uint8_t *)&t108), 112);
                        goto gm_fdone;
                    }
gm_lb12:;
                }
gm_lb11:;
                {
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t110 = (uint8_t)(t109 == ((uint8_t)3ULL));
                    if (!t110) {
                        goto gm_lb14;
                    }
                    void * t111 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l13 = t111;
                    {
                        {
                            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t113 = (uint8_t)(t112 == ((uint8_t)3ULL));
                            if (!t113) {
                                goto gm_lb16;
                            }
                            void * t114 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                            gm_l14 = t114;
                            void * t115 = gm_l0;
                            gm_t88rcucg1b7uk t116 = gm_l3;
                            void * t117 = gm_l13;
                            uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)t117) + 8));
                            gm_f_field_tys_foan32f((void *)&gm_l34, (void *)NULL, cx, NULL, t115, t116, t118);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t119 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
                            if (t119) {
                                gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
                                gm_l35 = t120;
                            } else {
                                void * t121 = gm_l0;
                                gm_tl874t2atnlh3 t122 = gm_l4;
                                gm_b16_8 t123;
                                GM_ST(void *, (uint8_t *)&t123, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t124;
                                gm_f_unsupported_2seq49l((void *)&t124, t121, GM_CONV(gm_cseahvg8v7p0vs, t122), t123);
                                gm_tsg40glpvq01o t125;
                                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t125 + 8, 0);
                                ((uint8_t *)&t125)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t125 + 16), t124);
                                memmove(gm_out, ((uint8_t *)&t125), 112);
                                goto gm_fdone;
                            }
                            gm_b16_8 t126 = gm_l35;
                            gm_l15 = t126;
                            gm_l17 = ((uint64_t)0ULL);
                            void * t127 = gm_l13;
                            uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)t127) + 8));
                            void * t129 = gm_l14;
                            uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)t129) + 8));
                            uint64_t t131 = (t128 < t130 ? t128 : t130);
                            gm_l16 = t131;
                            {
                                for (;;) {
                                    uint64_t t132 = gm_l17;
                                    uint64_t t133 = gm_l16;
                                    uint8_t t134 = (t132 < t133);
                                    if (!t134) {
                                        goto gm_lb17;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t135 = gm_l17;
                                                gm_l18 = t135;
                                                void * t136 = gm_l0;
                                                void * t137 = gm_l13;
                                                void * t138 = GM_LD(void *, ((uint8_t *)t137));
                                                uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)t137) + 8));
                                                uint64_t t140 = gm_l18;
                                                if ((uintptr_t)t140 >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)t140, t139, gm_srcue4ionbe9lgh, 935, 36);
                                                void * t141 = gm_l14;
                                                void * t142 = GM_LD(void *, ((uint8_t *)t141));
                                                uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)t141) + 8));
                                                uint64_t t144 = gm_l18;
                                                if ((uintptr_t)t144 >= (uintptr_t)(t143)) gm_trap_index((uintptr_t)t144, t143, gm_srcue4ionbe9lgh, 935, 48);
                                                void * t145 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                uint64_t t147 = gm_l18;
                                                if ((uintptr_t)t147 >= (uintptr_t)(t146)) gm_trap_index((uintptr_t)t147, t146, gm_srcue4ionbe9lgh, 935, 60);
                                                gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t145) + (uintptr_t)t147 * 4));
                                                gm_tl874t2atnlh3 t149 = gm_l4;
                                                gm_t6evimt84lndq t150 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t138) + (uintptr_t)t140 * 48));
                                                gm_t6evimt84lndq t151 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t142) + (uintptr_t)t144 * 48));
                                                gm_f_equal_hinsj77((void *)&gm_l36, (void *)NULL, cx, NULL, t136, &t150, &t151, t148, t149);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                if (t152) {
                                                } else {
                                                    gm_t39uai1ajk3qv t153 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l36) + 16));
                                                    gm_tsg40glpvq01o t154;
                                                    GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t154 + 8, 0);
                                                    ((uint8_t *)&t154)[0] = 1;
                                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t154 + 16), t153);
                                                    memmove(gm_out, ((uint8_t *)&t154), 112);
                                                    goto gm_fdone;
                                                }
                                                uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 16));
                                                uint8_t t156 = (uint8_t)!t155;
                                                if (t156) {
                                                    void * t157 = gm_l0;
                                                    void * t158 = gm_l13;
                                                    void * t159 = GM_LD(void *, ((uint8_t *)t158));
                                                    uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)t158) + 8));
                                                    uint64_t t161 = gm_l18;
                                                    if ((uintptr_t)t161 >= (uintptr_t)(t160)) gm_trap_index((uintptr_t)t161, t160, gm_srcue4ionbe9lgh, 935, 92);
                                                    void * t162 = gm_l14;
                                                    void * t163 = GM_LD(void *, ((uint8_t *)t162));
                                                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)t162) + 8));
                                                    uint64_t t165 = gm_l18;
                                                    if ((uintptr_t)t165 >= (uintptr_t)(t164)) gm_trap_index((uintptr_t)t165, t164, gm_srcue4ionbe9lgh, 935, 104);
                                                    void * t166 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                    uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                    uint64_t t168 = gm_l18;
                                                    if ((uintptr_t)t168 >= (uintptr_t)(t167)) gm_trap_index((uintptr_t)t168, t167, gm_srcue4ionbe9lgh, 935, 116);
                                                    gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t166) + (uintptr_t)t168 * 4));
                                                    gm_tl874t2atnlh3 t170 = gm_l4;
                                                    gm_t6evimt84lndq t171 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t159) + (uintptr_t)t161 * 48));
                                                    gm_t6evimt84lndq t172 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t163) + (uintptr_t)t165 * 48));
                                                    gm_f_less_d82rkss((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t157, &t171, &t172, t169, t170);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb20;
                                            }
gm_lb21:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 934, 13);
                                        }
gm_lb20:;
                                    }
gm_lb19:;
                                    uint64_t t173 = gm_l17;
                                    uint64_t r174; bool o175 = __builtin_add_overflow(t173, ((uint64_t)1ULL), &r174);
                                    uint64_t t176 = r174;
                                    gm_l17 = t176;
                                }
                            }
gm_lb17:;
                            void * t177 = gm_l13;
                            uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)t177) + 8));
                            void * t179 = gm_l14;
                            uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)t179) + 8));
                            uint8_t t181 = (t178 < t180);
                            gm_tsg40glpvq01o t182;
                            memset(&t182, 0, sizeof t182);
                            ((uint8_t *)&t182)[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&t182 + 16), t181);
                            memmove(gm_out, ((uint8_t *)&t182), 112);
                            goto gm_fdone;
                        }
gm_lb16:;
                        void * t183 = gm_l0;
                        gm_tl874t2atnlh3 t184 = gm_l4;
                        gm_b16_8 t185;
                        GM_ST(void *, (uint8_t *)&t185, (void *)(gm_s9ofbo8c28rrv));
                        GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(15));
                        gm_t39uai1ajk3qv t186;
                        gm_f_unsupported_2seq49l((void *)&t186, t183, GM_CONV(gm_cseahvg8v7p0vs, t184), t185);
                        gm_tsg40glpvq01o t187;
                        GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t187 + 8, 0);
                        ((uint8_t *)&t187)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t187 + 16), t186);
                        memmove(gm_out, ((uint8_t *)&t187), 112);
                        goto gm_fdone;
                    }
gm_lb15:;
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t189 = (uint8_t)(t188 == ((uint8_t)5ULL));
                    if (!t189) {
                        goto gm_lb22;
                    }
                    memmove(((uint8_t *)&gm_l19), (((uint8_t *)&F.l1) + 16), 32);
                    {
                        {
                            uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t191 = (uint8_t)(t190 == ((uint8_t)5ULL));
                            if (!t191) {
                                goto gm_lb24;
                            }
                            memmove(((uint8_t *)&gm_l20), (((uint8_t *)&F.l2) + 16), 32);
                            {
                                {
                                    void * t192 = gm_l5;
                                    gm_t88rcucg1b7uk t193 = gm_l3;
                                    gm_f_kind_jcnt4hm((void *)&gm_l37, t192, GM_CONV(gm_csguankko4lmhd, t193));
                                    uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                                    uint8_t t195 = (uint8_t)(t194 == ((uint8_t)10ULL));
                                    if (!t195) {
                                        goto gm_lb26;
                                    }
                                    gm_t88rcucg1b7uk t196 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 4));
                                    gm_l21 = t196;
                                    gm_l23 = ((uint64_t)0ULL);
                                    uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 24));
                                    uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 24));
                                    uint64_t t199 = (t197 < t198 ? t197 : t198);
                                    gm_l22 = t199;
                                    {
                                        for (;;) {
                                            uint64_t t200 = gm_l23;
                                            uint64_t t201 = gm_l22;
                                            uint8_t t202 = (t200 < t201);
                                            if (!t202) {
                                                goto gm_lb27;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t203 = gm_l23;
                                                        gm_l24 = t203;
                                                        void * t204 = gm_l0;
                                                        uint64_t t205 = gm_l24;
                                                        gm_tkaapcii86udk t206;
                                                        gm_f_offset_nkn711s((void *)&t206, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l19)), t205);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_f_load_nf7o5kl((void *)&F.l25, t204, &t206);
                                                        void * t207 = gm_l0;
                                                        uint64_t t208 = gm_l24;
                                                        gm_tkaapcii86udk t209;
                                                        gm_f_offset_nkn711s((void *)&t209, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l20)), t208);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_f_load_nf7o5kl((void *)&F.l26, t207, &t209);
                                                        void * t210 = gm_l0;
                                                        gm_t88rcucg1b7uk t211 = gm_l21;
                                                        gm_tl874t2atnlh3 t212 = gm_l4;
                                                        gm_t6evimt84lndq t213 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l25));
                                                        gm_t6evimt84lndq t214 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l26));
                                                        gm_f_equal_hinsj77((void *)&gm_l38, (void *)NULL, cx, NULL, t210, &t213, &t214, t211, t212);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                        if (t215) {
                                                        } else {
                                                            gm_t39uai1ajk3qv t216 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l38) + 16));
                                                            gm_tsg40glpvq01o t217;
                                                            GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&t217 + 8, 0);
                                                            ((uint8_t *)&t217)[0] = 1;
                                                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t217 + 16), t216);
                                                            memmove(gm_out, ((uint8_t *)&t217), 112);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 16));
                                                        uint8_t t219 = (uint8_t)!t218;
                                                        if (t219) {
                                                            void * t220 = gm_l0;
                                                            gm_t88rcucg1b7uk t221 = gm_l21;
                                                            gm_tl874t2atnlh3 t222 = gm_l4;
                                                            gm_t6evimt84lndq t223 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l25));
                                                            gm_t6evimt84lndq t224 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l26));
                                                            gm_f_less_d82rkss((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t220, &t223, &t224, t221, t222);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb30;
                                                    }
gm_lb31:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 942, 13);
                                                }
gm_lb30:;
                                            }
gm_lb29:;
                                            uint64_t t225 = gm_l23;
                                            uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                                            uint64_t t228 = r226;
                                            gm_l23 = t228;
                                        }
                                    }
gm_lb27:;
                                    uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 24));
                                    uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 24));
                                    uint8_t t231 = (t229 < t230);
                                    gm_tsg40glpvq01o t232;
                                    memset(&t232, 0, sizeof t232);
                                    ((uint8_t *)&t232)[0] = 0;
                                    GM_ST(uint8_t, ((uint8_t *)&t232 + 16), t231);
                                    memmove(gm_out, ((uint8_t *)&t232), 112);
                                    goto gm_fdone;
                                }
gm_lb26:;
                                void * t233 = gm_l0;
                                gm_tl874t2atnlh3 t234 = gm_l4;
                                gm_b16_8 t235;
                                GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s9ofbo8c28rrv));
                                GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(15));
                                gm_t39uai1ajk3qv t236;
                                gm_f_unsupported_2seq49l((void *)&t236, t233, GM_CONV(gm_cseahvg8v7p0vs, t234), t235);
                                gm_tsg40glpvq01o t237;
                                GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t237 + 8, 0);
                                ((uint8_t *)&t237)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t237 + 16), t236);
                                memmove(gm_out, ((uint8_t *)&t237), 112);
                                goto gm_fdone;
                            }
gm_lb25:;
                        }
gm_lb24:;
                        void * t238 = gm_l0;
                        gm_tl874t2atnlh3 t239 = gm_l4;
                        gm_b16_8 t240;
                        GM_ST(void *, (uint8_t *)&t240, (void *)(gm_s9ofbo8c28rrv));
                        GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(15));
                        gm_t39uai1ajk3qv t241;
                        gm_f_unsupported_2seq49l((void *)&t241, t238, GM_CONV(gm_cseahvg8v7p0vs, t239), t240);
                        gm_tsg40glpvq01o t242;
                        GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t242 + 8, 0);
                        ((uint8_t *)&t242)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t242 + 16), t241);
                        memmove(gm_out, ((uint8_t *)&t242), 112);
                        goto gm_fdone;
                    }
gm_lb23:;
                }
gm_lb22:;
            }
            {
                {
                    void * t243 = gm_l0;
                    gm_tl874t2atnlh3 t244 = gm_l4;
                    gm_b16_8 t245;
                    GM_ST(void *, (uint8_t *)&t245, (void *)(gm_s8afn1h3asffl));
                    GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(31));
                    gm_t39uai1ajk3qv t246;
                    gm_f_unsupported_2seq49l((void *)&t246, t243, GM_CONV(gm_cseahvg8v7p0vs, t244), t245);
                    gm_tsg40glpvq01o t247;
                    GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t247 + 8, 0);
                    ((uint8_t *)&t247)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t247 + 16), t246);
                    memmove(gm_out, ((uint8_t *)&t247), 112);
                    goto gm_fdone;
                }
gm_lb32:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 916, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mask_gbhl4hp F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 gm_l1;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint32_t)128ULL));
    if (t3) {
        unsigned __int128 t4 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
        gm_l1 = t4;
    } else {
        uint32_t t5 = gm_l0;
        unsigned __int128 t6 = (unsigned __int128)t5;
        unsigned __int128 n7 = (unsigned __int128)t6;
        if (n7 >= 128) gm_trap_shift((uint64_t)n7, "u128", gm_srcue4ionbe9lgh, 14, 70);
        unsigned __int128 r8 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n7);
        bool o9 = (unsigned __int128)(r8 >> (int)n7) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
        if (o9) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 14, 70);
        unsigned __int128 t10 = r8;
        unsigned __int128 r11; bool o12 = __builtin_sub_overflow(t10, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 14, 69);
        unsigned __int128 t13 = r11;
        gm_l1 = t13;
    }
    unsigned __int128 t14 = gm_l1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_offsets_niooj98(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_niooj98 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* s */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    uint64_t gm_l5; /* size */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* elems */
    uint32_t gm_l12; /* id */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    GM_STACK_CHECK(&F, 1212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l18;
    gm_tbccvikg6b96e gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tbccvikg6b96e gm_l22;
    uint8_t gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tbccvikg6b96e gm_l25;
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    gm_l2 = t23;
    void * t24 = gm_l2;
    void * t25 = (void *)(((uint8_t *)t24) + 8);
    gm_t88rcucg1b7uk t26 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l18, t25, GM_CONV(gm_csguankko4lmhd, t26));
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)11ULL));
            if (!t28) {
                goto gm_lb2;
            }
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            gm_l3 = t29;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l18) + 8) + 8));
            gm_l4 = t30;
            void * t31 = gm_l2;
            gm_t88rcucg1b7uk t32 = gm_l4;
            gm_f_layout_a5ovnrm((void *)&gm_l19, (void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
            gm_l5 = t33;
            gm_trqfvmjtt4sij t34;
            gm_f_list_nof7vfj((void *)&t34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l6 = t34;
            gm_trqfvmjtt4sij t35;
            gm_f_list_kl0t9m0((void *)&t35, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l7 = t35;
            gm_l9 = ((uint64_t)0ULL);
            uint64_t t36 = gm_l3;
            gm_l8 = t36;
            {
                for (;;) {
                    uint64_t t37 = gm_l9;
                    uint64_t t38 = gm_l8;
                    uint8_t t39 = (t37 < t38);
                    if (!t39) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t40 = gm_l9;
                                gm_l10 = t40;
                                void * t41 = (void *)((uint8_t *)&F.l6);
                                gm_t88rcucg1b7uk t42 = gm_l4;
                                gm_tftb1odc739t5 t43 = gm_f_push_k3l9nic(t41, GM_CONV(gm_csguankko4lmhd, t42));
                                gm_l20 = t43;
                                uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcue4ionbe9lgh, 1045, 17);
                                void * t45 = (void *)((uint8_t *)&F.l7);
                                uint64_t t46 = gm_l10;
                                uint64_t t47 = gm_l5;
                                uint64_t r48; bool o49 = __builtin_mul_overflow(t46, t47, &r48);
                                if (o49) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 1046, 27);
                                uint64_t t50 = r48;
                                gm_tftb1odc739t5 t51 = gm_f_push_7o6oubd(t45, t50);
                                gm_l21 = t51;
                                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcue4ionbe9lgh, 1046, 17);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1044, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t53 = gm_l9;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l9 = t56;
                }
            }
gm_lb3:;
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            gm_t6j6smj7qoctu t61;
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t58);
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 16), t60);
            gm_ttu09lij36f8c t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t62), t61);
            ((uint8_t *)&t62)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t62), 40);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)12ULL));
            if (!t64) {
                goto gm_lb8;
            }
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_l11 = t65;
            gm_b16_8 t66 = gm_l11;
            void * t67 = gm_l2;
            gm_t88rcucg1b7uk t68 = gm_l1;
            gm_f_layout_a5ovnrm((void *)&gm_l22, (void *)NULL, cx, NULL, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 16));
            gm_t6j6smj7qoctu t70;
            GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t66);
            GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
            gm_ttu09lij36f8c t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t71 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t71), t70);
            ((uint8_t *)&t71)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t71), 40);
            goto gm_fdone;
        }
gm_lb8:;
        {
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t73 = (uint8_t)(t72 == ((uint8_t)17ULL));
            if (!t73) {
                goto gm_lb9;
            }
            uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 8));
            gm_l12 = t74;
            {
                {
                    void * t75 = gm_l2;
                    void * t76 = GM_LD(void *, (((uint8_t *)t75) + 1088));
                    uint64_t t77 = GM_LD(uint64_t, ((((uint8_t *)t75) + 1088) + 8));
                    uint32_t t78 = gm_l12;
                    uint64_t t79 = (uint64_t)t78;
                    if ((uintptr_t)t79 >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)t79, t77, gm_srcue4ionbe9lgh, 1052, 29);
                    uint8_t t80 = GM_LD(uint8_t, (((((uint8_t *)t76) + (uintptr_t)t79 * 184) + 24) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)0ULL));
                    if (!t81) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&F.l13), (((((uint8_t *)t76) + (uintptr_t)t79 * 184) + 24) + 8), 48);
                    {
                        {
                            uint8_t t82 = (uint8_t)(((((uint8_t *)&F.l13) + 40))[4] != 0);
                            if (t82) {
                                goto gm_lb13;
                            }
                            gm_l23 = ((uint8_t)0);
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            gm_l23 = ((uint8_t)1);
                            goto gm_lb12;
                        }
gm_lb14:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1053, 16);
                    }
gm_lb12:;
                    uint8_t t83 = gm_l23;
                    if (t83) {
                        gm_ttu09lij36f8c t84;
                        memset(&t84, 0, sizeof t84);
                        memmove(gm_out, ((uint8_t *)&t84), 40);
                        goto gm_fdone;
                    }
                    gm_trqfvmjtt4sij t85;
                    gm_f_list_nof7vfj((void *)&t85, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l14 = t85;
                    gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                    gm_l15 = t86;
                    gm_l16 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t87 = gm_l16;
                            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint8_t t89 = (t87 < t88);
                            if (!t89) {
                                goto gm_lb15;
                            }
                            {
                                void * t90 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t92 = gm_l16;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l17), (((uint8_t *)t90) + (uintptr_t)t92 * 40), 40);
                                        void * t93 = (void *)((uint8_t *)&F.l14);
                                        gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                                        gm_tftb1odc739t5 t95 = gm_f_push_k3l9nic(t93, GM_CONV(gm_csguankko4lmhd, t94));
                                        gm_l24 = t95;
                                        uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                        if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcue4ionbe9lgh, 1055, 35);
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1055, 13);
                                }
gm_lb18:;
                            }
gm_lb17:;
                            uint64_t t97 = gm_l16;
                            uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                            uint64_t t100 = r98;
                            gm_l16 = t100;
                        }
                    }
gm_lb15:;
                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                    void * t103 = gm_l2;
                    gm_t88rcucg1b7uk t104 = gm_l1;
                    gm_f_layout_a5ovnrm((void *)&gm_l25, (void *)NULL, cx, NULL, t103, t104);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 16));
                    gm_t6j6smj7qoctu t106;
                    GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t102);
                    GM_ST(gm_b16_8, ((uint8_t *)&t106 + 16), t105);
                    gm_ttu09lij36f8c t107;
                    GM_ST(uint8_t, (uint8_t *)&t107 + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&t107 + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&t107 + 36, 0);
                    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t107), t106);
                    ((uint8_t *)&t107)[32] = 1;
                    memmove(gm_out, ((uint8_t *)&t107), 40);
                    goto gm_fdone;
                }
gm_lb11:;
                gm_ttu09lij36f8c t108;
                memset(&t108, 0, sizeof t108);
                memmove(gm_out, ((uint8_t *)&t108), 40);
                goto gm_fdone;
            }
gm_lb10:;
        }
gm_lb9:;
        {
            gm_ttu09lij36f8c t109;
            memset(&t109, 0, sizeof t109);
            memmove(gm_out, ((uint8_t *)&t109), 40);
            goto gm_fdone;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1039, 5);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_mu1ii1k(void * a0, gm_csguankko4lmhd a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_payload_ty_mu1ii1k F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l5;
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, ((uint8_t *)t6));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)17ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            gm_l3 = t12;
            {
                {
                    void * t13 = gm_l0;
                    void * t14 = GM_LD(void *, ((uint8_t *)t13));
                    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 1088));
                    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 1088) + 8));
                    uint32_t t17 = gm_l3;
                    uint64_t t18 = (uint64_t)t17;
                    if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srcue4ionbe9lgh, 984, 28);
                    uint8_t t19 = GM_LD(uint8_t, (((((uint8_t *)t15) + (uintptr_t)t18 * 184) + 24) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t21 = GM_LD(gm_b16_8, ((((((uint8_t *)t15) + (uintptr_t)t18 * 184) + 24) + 8) + 8));
                    gm_l4 = t21;
                    uint32_t t22 = gm_l2;
                    uint64_t t23 = (uint64_t)t22;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t25 = (t23 >= t24);
                    if (t25) {
                        gm_tkn42u7na3t81 t26;
                        memset(&t26, 0, sizeof t26);
                        F.ret = t26;
                        goto gm_fret;
                    }
                    void * t27 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint32_t t29 = gm_l2;
                    uint64_t t30 = (uint64_t)t29;
                    if ((uintptr_t)t30 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t30, t28, gm_srcue4ionbe9lgh, 986, 12);
                    gm_tkn42u7na3t81 t31 = GM_LD(gm_tkn42u7na3t81, ((((uint8_t *)t27) + (uintptr_t)t30 * 72) + 16));
                    F.ret = t31;
                    goto gm_fret;
                }
gm_lb4:;
                gm_tkn42u7na3t81 t32;
                memset(&t32, 0, sizeof t32);
                F.ret = t32;
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_tkn42u7na3t81 t33;
        memset(&t33, 0, sizeof t33);
        F.ret = t33;
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_put_bytes_1ltl1lv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_put_bytes_1ltl1lv F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* out */
    uint64_t gm_l4; /* at */
    unsigned __int128 gm_l5; /* bits */
    uint64_t gm_l6; /* size */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* b */
    double gm_l11; /* x */
    uint16_t gm_l12; /* w */
    uint64_t gm_l13; /* raw */
    uint64_t gm_l14; /* n */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* a */
    gm_t6j6smj7qoctu gm_l19; /* o */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    GM_STACK_CHECK(&F, 1295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tbccvikg6b96e gm_l24;
    uint8_t gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_ttu09lij36f8c gm_l27;
    gm_t6j6smj7qoctu gm_l28;
    gm_thf8as07kj3uj gm_l29;
    {
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)0ULL));
            if (!t30) {
                goto gm_lb2;
            }
            unsigned __int128 t31 = GM_LD(unsigned __int128, (((uint8_t *)a1) + 16));
            gm_l5 = t31;
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, ((uint8_t *)t32));
            gm_t88rcucg1b7uk t34 = gm_l2;
            gm_f_layout_a5ovnrm((void *)&gm_l24, (void *)NULL, cx, NULL, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
            gm_l6 = t35;
            gm_l8 = ((uint64_t)0ULL);
            uint64_t t36 = gm_l6;
            gm_l7 = t36;
            {
                for (;;) {
                    uint64_t t37 = gm_l8;
                    uint64_t t38 = gm_l7;
                    uint8_t t39 = (t37 < t38);
                    if (!t39) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t40 = gm_l8;
                                gm_l9 = t40;
                                void * t41 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t43 = gm_l4;
                                void * t44 = gm_l0;
                                uint64_t t45 = gm_l9;
                                uint64_t t46 = gm_l6;
                                uint64_t t47 = gm_f_byte_at_m94bj3e(t44, t45, t46);
                                uint64_t r48; bool o49 = __builtin_add_overflow(t43, t47, &r48);
                                if (o49) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 1072, 37);
                                uint64_t t50 = r48;
                                uint64_t t51 = (uint64_t)t50;
                                if ((uintptr_t)t51 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t51, t42, gm_srcue4ionbe9lgh, 1072, 32);
                                unsigned __int128 t52 = gm_l5;
                                uint64_t t53 = gm_l9;
                                uint64_t r54; bool o55 = __builtin_mul_overflow(t53, ((uint64_t)8ULL), &r54);
                                if (o55) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 1072, 92);
                                uint64_t t56 = r54;
                                unsigned __int128 t57 = (unsigned __int128)t56;
                                unsigned __int128 n58 = (unsigned __int128)t57;
                                if (n58 >= 128) gm_trap_shift((uint64_t)n58, "u128", gm_srcue4ionbe9lgh, 1072, 79);
                                unsigned __int128 t59 = (unsigned __int128)(t52 >> (int)n58);
                                uint8_t t60 = (uint8_t)t59;
                                GM_ST(uint8_t, (((uint8_t *)t41) + (uintptr_t)t51 * 1), t60);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1072, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t61 = gm_l8;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l8 = t64;
                }
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)2ULL));
            if (!t66) {
                goto gm_lb8;
            }
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)a1) + 16));
            gm_l10 = t67;
            void * t68 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t70 = gm_l4;
            uint64_t t71 = (uint64_t)t70;
            if ((uintptr_t)t71 >= (uintptr_t)(t69)) gm_trap_index((uintptr_t)t71, t69, gm_srcue4ionbe9lgh, 1076, 13);
            uint8_t t72 = gm_l10;
            if (t72) {
                gm_l25 = ((uint8_t)1ULL);
            } else {
                gm_l25 = ((uint8_t)0ULL);
            }
            uint8_t t73 = gm_l25;
            GM_ST(uint8_t, (((uint8_t *)t68) + (uintptr_t)t71 * 1), t73);
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)1ULL));
            if (!t75) {
                goto gm_lb9;
            }
            double t76 = GM_LD(double, (((uint8_t *)a1) + 16));
            gm_l11 = t76;
            {
                {
                    void * t77 = gm_l0;
                    void * t78 = GM_LD(void *, ((uint8_t *)t77));
                    void * t79 = (void *)(((uint8_t *)t78) + 8);
                    gm_t88rcucg1b7uk t80 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l26, t79, GM_CONV(gm_csguankko4lmhd, t80));
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)3ULL));
                    if (!t82) {
                        goto gm_lb11;
                    }
                    uint16_t t83 = GM_LD(uint16_t, (((uint8_t *)&gm_l26) + 8));
                    gm_l12 = t83;
                    gm_l13 = ((uint64_t)0ULL);
                    gm_l14 = ((uint64_t)8ULL);
                    {
                        {
                            uint16_t t84 = gm_l12;
                            uint8_t t85 = ((t84 == ((uint16_t)64ULL)));
                            if (!t85) {
                                goto gm_lb13;
                            }
                            double t86 = gm_l11;
                            uint64_t t87;
                            memset(&t87, 0, sizeof t87); memcpy(&t87, ((uint8_t *)&t86), sizeof t87 < 8 ? sizeof t87 : 8);
                            gm_l13 = t87;
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            uint16_t t88 = gm_l12;
                            uint8_t t89 = ((t88 == ((uint16_t)32ULL)));
                            if (!t89) {
                                goto gm_lb14;
                            }
                            double t90 = gm_l11;
                            float t91 = (float)((double)t90);
                            uint32_t t92;
                            memset(&t92, 0, sizeof t92); memcpy(&t92, ((uint8_t *)&t91), sizeof t92 < 4 ? sizeof t92 : 4);
                            uint64_t t93 = (uint64_t)t92;
                            gm_l13 = t93;
                            gm_l14 = ((uint64_t)4ULL);
                            goto gm_lb12;
                        }
gm_lb14:;
                        {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1083, 13);
                    }
gm_lb12:;
                    gm_l16 = ((uint64_t)0ULL);
                    uint64_t t94 = gm_l14;
                    gm_l15 = t94;
                    {
                        for (;;) {
                            uint64_t t95 = gm_l16;
                            uint64_t t96 = gm_l15;
                            uint8_t t97 = (t95 < t96);
                            if (!t97) {
                                goto gm_lb16;
                            }
                            {
                                {
                                    {
                                        uint64_t t98 = gm_l16;
                                        gm_l17 = t98;
                                        void * t99 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t101 = gm_l4;
                                        void * t102 = gm_l0;
                                        uint64_t t103 = gm_l17;
                                        uint64_t t104 = gm_l14;
                                        uint64_t t105 = gm_f_byte_at_m94bj3e(t102, t103, t104);
                                        uint64_t r106; bool o107 = __builtin_add_overflow(t101, t105, &r106);
                                        if (o107) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 1091, 34);
                                        uint64_t t108 = r106;
                                        uint64_t t109 = (uint64_t)t108;
                                        if ((uintptr_t)t109 >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)t109, t100, gm_srcue4ionbe9lgh, 1091, 29);
                                        uint64_t t110 = gm_l13;
                                        uint64_t t111 = gm_l17;
                                        uint64_t r112; bool o113 = __builtin_mul_overflow(t111, ((uint64_t)8ULL), &r112);
                                        if (o113) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 1091, 81);
                                        uint64_t t114 = r112;
                                        unsigned __int128 n115 = (unsigned __int128)t114;
                                        if (n115 >= 64) gm_trap_shift((uint64_t)n115, "u64", gm_srcue4ionbe9lgh, 1091, 73);
                                        uint64_t t116 = (uint64_t)(t110 >> (int)n115);
                                        uint8_t t117 = (uint8_t)t116;
                                        GM_ST(uint8_t, (((uint8_t *)t99) + (uintptr_t)t109 * 1), t117);
                                        goto gm_lb19;
                                    }
gm_lb20:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1091, 13);
                                }
gm_lb19:;
                            }
gm_lb18:;
                            uint64_t t118 = gm_l16;
                            uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                            uint64_t t121 = r119;
                            gm_l16 = t121;
                        }
                    }
gm_lb16:;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb11:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb10:;
        }
gm_lb9:;
        {
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t123 = (uint8_t)(t122 == ((uint8_t)3ULL));
            if (!t123) {
                goto gm_lb21;
            }
            void * t124 = GM_LD(void *, (((uint8_t *)a1) + 16));
            gm_l18 = t124;
            void * t125 = gm_l0;
            gm_t88rcucg1b7uk t126 = gm_l2;
            gm_f_offsets_niooj98((void *)&gm_l27, (void *)NULL, cx, NULL, t125, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t127 = (uint8_t)((((uint8_t *)&gm_l27))[32] != 0);
            if (t127) {
                memmove(((uint8_t *)&gm_l28), ((uint8_t *)&gm_l27), 32);
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l28), 32);
            void * t128 = gm_l18;
            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)t128));
            gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
            gm_l20 = t130;
            gm_l21 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t131 = gm_l21;
                    uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                    uint8_t t133 = (t131 < t132);
                    if (!t133) {
                        goto gm_lb22;
                    }
                    {
                        uint64_t t134 = gm_l21;
                        void * t135 = GM_LD(void *, ((uint8_t *)&gm_l20));
                        uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        uint64_t t137 = gm_l21;
                        gm_t6evimt84lndq t138 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t135) + (uintptr_t)t137 * 48));
                        gm_thf8as07kj3uj t139;
                        GM_ST(uint64_t, (uint8_t *)&t139 + 8, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t139 + 0), t134);
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t139 + 16), t138);
                        gm_l29 = t139;
                        {
                            {
                                uint64_t t140 = GM_LD(uint64_t, ((uint8_t *)&gm_l29));
                                gm_l22 = t140;
                                memmove(((uint8_t *)&F.l23), (((uint8_t *)&gm_l29) + 16), 48);
                                void * t141 = gm_l0;
                                void * t142 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                uint64_t t144 = gm_l22;
                                if ((uintptr_t)t144 >= (uintptr_t)(t143)) gm_trap_index((uintptr_t)t144, t143, gm_srcue4ionbe9lgh, 1097, 39);
                                gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t142) + (uintptr_t)t144 * 4));
                                gm_b16_8 t146 = gm_l3;
                                uint64_t t147 = gm_l4;
                                void * t148 = GM_LD(void *, (((uint8_t *)&gm_l19) + 16));
                                uint64_t t149 = GM_LD(uint64_t, ((((uint8_t *)&gm_l19) + 16) + 8));
                                uint64_t t150 = gm_l22;
                                if ((uintptr_t)t150 >= (uintptr_t)(t149)) gm_trap_index((uintptr_t)t150, t149, gm_srcue4ionbe9lgh, 1097, 59);
                                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)t148) + (uintptr_t)t150 * 8));
                                uint64_t r152; bool o153 = __builtin_add_overflow(t147, t151, &r152);
                                if (o153) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 1097, 54);
                                uint64_t t154 = r152;
                                uint8_t t155 = gm_f_put_bytes_1ltl1lv((void *)NULL, cx, NULL, t141, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l23)), t145, t146, t154);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t156 = (uint8_t)!t155;
                                if (t156) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb25;
                            }
gm_lb26:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1096, 13);
                        }
gm_lb25:;
                    }
gm_lb24:;
                    uint64_t t157 = gm_l21;
                    uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                    uint64_t t160 = r158;
                    gm_l21 = t160;
                }
            }
gm_lb22:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb21:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb27:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1069, 5);
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
inline __attribute__((aligned(4))) void gm_f_reinterpret_jmr6jts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_reinterpret_jmr6jts F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* from */
    gm_t88rcucg1b7uk gm_l3; /* to */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint64_t gm_l5; /* size */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 1041u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tbccvikg6b96e gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tjoi8o798ne9k gm_l12;
    gm_t6evimt84lndq gm_l13;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_t88rcucg1b7uk t9 = gm_l2;
    gm_f_layout_a5ovnrm((void *)&gm_l10, (void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
    gm_l5 = t10;
    gm_trqfvmjtt4sij t11;
    gm_f_list_uvfu1k1((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t11;
    gm_l8 = ((uint64_t)0ULL);
    uint64_t t12 = gm_l5;
    gm_l7 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l8;
            uint64_t t14 = gm_l7;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t16 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t17 = gm_f_push_t83rg2m(t16, ((uint8_t)0ULL));
                        gm_l11 = t17;
                        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcue4ionbe9lgh, 1030, 24);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 1030, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l8;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l8 = t22;
        }
    }
gm_lb1:;
    void * t23 = gm_l0;
    gm_t88rcucg1b7uk t24 = gm_l2;
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    uint8_t t26 = gm_f_put_bytes_1ltl1lv((void *)NULL, cx, NULL, t23, &(*(gm_t6evimt84lndq *)((uint8_t *)a1)), t24, t25, ((uint64_t)0ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t27 = (uint8_t)!t26;
    if (t27) {
        void * t28 = gm_l0;
        gm_tl874t2atnlh3 t29 = gm_l4;
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s94n39ltlvmhe));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(35));
        gm_t39uai1ajk3qv t31;
        gm_f_unsupported_2seq49l((void *)&t31, t28, GM_CONV(gm_cseahvg8v7p0vs, t29), t30);
        gm_ttk92ufj2f6eh t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 8, 0);
        ((uint8_t *)&t32)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t32 + 16), t31);
        memmove(gm_out, ((uint8_t *)&t32), 112);
        goto gm_fdone;
    }
    void * t33 = gm_l0;
    gm_t88rcucg1b7uk t34 = gm_l3;
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
    gm_f_get_bytes_ql9o4k0((void *)&gm_l12, (void *)NULL, cx, NULL, t33, t34, t36, ((uint64_t)0ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l12))[48] != 0);
    if (t37) {
        memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l12), 48);
    } else {
        void * t38 = gm_l0;
        gm_tl874t2atnlh3 t39 = gm_l4;
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sbjuojbqo51av));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(35));
        gm_t39uai1ajk3qv t41;
        gm_f_unsupported_2seq49l((void *)&t41, t38, GM_CONV(gm_cseahvg8v7p0vs, t39), t40);
        gm_ttk92ufj2f6eh t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t42 + 8, 0);
        ((uint8_t *)&t42)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t42 + 16), t41);
        memmove(gm_out, ((uint8_t *)&t42), 112);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l13), 48);
    gm_t6evimt84lndq t43 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l9));
    gm_ttk92ufj2f6eh t44;
    memset(&t44, 0, sizeof t44);
    ((uint8_t *)&t44)[0] = 0;
    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t44 + 16), t43);
    memmove(gm_out, ((uint8_t *)&t44), 112);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_round_float_saju9fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_round_float_saju9fu F;
    void * gm_l0; /* m */
    double gm_l1; /* x */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK(&F, 1082u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l5;
    gm_tvegkb6ouf3m0 gm_l6;
    gm_tngtuiuskn938 gm_l7;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = (void *)(((uint8_t *)t5) + 8);
            gm_t88rcucg1b7uk t7 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t6, GM_CONV(gm_csguankko4lmhd, t7));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)3ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint16_t t10 = GM_LD(uint16_t, (((uint8_t *)&gm_l5) + 8));
            gm_l3 = t10;
            uint16_t t11 = gm_l3;
            uint8_t t12 = ((t11 == ((uint16_t)32ULL)));
            if (t12) {
                double t13 = gm_l1;
                float t14 = (float)((double)t13);
                double t15 = (double)t14;
                gm_t26jhpkpc0sda t16;
                memset(&t16, 0, sizeof t16);
                ((uint8_t *)&t16)[0] = 0;
                GM_ST(double, ((uint8_t *)&t16 + 16), t15);
                memmove(gm_out, ((uint8_t *)&t16), 112);
                goto gm_fdone;
            }
            uint16_t t17 = gm_l3;
            uint8_t t18 = ((t17 == ((uint16_t)16ULL)));
            if (t18) {
                double t19 = gm_l1;
                double t20 = gm_l1;
                double t21 = (t19 - t20);
                uint8_t t22 = (!(t21 == ((double)0e0)));
                if (t22) {
                    double t23 = gm_l1;
                    gm_t26jhpkpc0sda t24;
                    memset(&t24, 0, sizeof t24);
                    ((uint8_t *)&t24)[0] = 0;
                    GM_ST(double, ((uint8_t *)&t24 + 16), t23);
                    memmove(gm_out, ((uint8_t *)&t24), 112);
                    goto gm_fdone;
                }
                double t25 = gm_l1;
                gm_f_float_exact_1g17c6c((void *)&gm_l6, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l6))[96] != 0);
                if (t26) {
                    memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l6), 96);
                } else {
                    double t27 = gm_l1;
                    gm_t26jhpkpc0sda t28;
                    memset(&t28, 0, sizeof t28);
                    ((uint8_t *)&t28)[0] = 0;
                    GM_ST(double, ((uint8_t *)&t28 + 16), t27);
                    memmove(gm_out, ((uint8_t *)&t28), 112);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l7), 96);
                double t29 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t26jhpkpc0sda t30;
                memset(&t30, 0, sizeof t30);
                ((uint8_t *)&t30)[0] = 0;
                GM_ST(double, ((uint8_t *)&t30 + 16), t29);
                memmove(gm_out, ((uint8_t *)&t30), 112);
                goto gm_fdone;
            }
            double t31 = gm_l1;
            gm_t26jhpkpc0sda t32;
            memset(&t32, 0, sizeof t32);
            ((uint8_t *)&t32)[0] = 0;
            GM_ST(double, ((uint8_t *)&t32 + 16), t31);
            memmove(gm_out, ((uint8_t *)&t32), 112);
            goto gm_fdone;
        }
gm_lb2:;
        double t33 = gm_l1;
        gm_t26jhpkpc0sda t34;
        memset(&t34, 0, sizeof t34);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(double, ((uint8_t *)&t34 + 16), t33);
        memmove(gm_out, ((uint8_t *)&t34), 112);
        goto gm_fdone;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_place_1r7qrqk F;
    gm_tkaapcii86udk gm_l0; /* a */
    gm_tkaapcii86udk gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l6;
    gm_t2lqu8jsj511d gm_l7;
    void * t7 = GM_LD(void *, ((uint8_t *)a0));
    void * t8 = GM_LD(void *, ((uint8_t *)a1));
    uint8_t t9 = (!(t7 == t8));
    if (t9) {
        gm_l6 = ((uint8_t)1);
    } else {
        uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
        uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
        uint8_t t12 = (!(t10 == t11));
        gm_l6 = t12;
    }
    uint8_t t13 = gm_l6;
    if (t13) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l2 = t14;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l3;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                uint64_t t18 = gm_l3;
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t21 = gm_l3;
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t19) + (uintptr_t)t21 * 8));
                gm_t2lqu8jsj511d t23;
                GM_ST(uint64_t, ((uint8_t *)&t23 + 0), t18);
                GM_ST(uint64_t, ((uint8_t *)&t23 + 8), t22);
                gm_l7 = t23;
                {
                    {
                        uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l4 = t24;
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        gm_l5 = t25;
                        uint64_t t26 = gm_l5;
                        void * t27 = GM_LD(void *, (((uint8_t *)a1) + 8));
                        uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                        uint64_t t29 = gm_l4;
                        if ((uintptr_t)t29 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t29, t28, gm_srcue4ionbe9lgh, 956, 21);
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t27) + (uintptr_t)t29 * 8));
                        uint8_t t31 = (!(t26 == t30));
                        if (t31) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 955, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t32 = gm_l3;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            uint64_t t35 = r33;
            gm_l3 = t35;
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
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk_c(gm_tkaapcii86udk a0, gm_tkaapcii86udk a1) { return gm_f_same_place_1r7qrqk(&a0, &a1); }
__attribute__((aligned(4))) void gm_f_scalar_binary_bvc65kt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_scalar_binary_bvc65kt F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    unsigned __int128 gm_l7; /* x */
    unsigned __int128 gm_l8; /* y */
    int32_t gm_l9; /* c */
    double gm_l10; /* x */
    double gm_l11; /* y */
    double gm_l12; /* r */
    uint8_t gm_l13; /* x */
    uint8_t gm_l14; /* y */
    GM_STACK_CHECK(&F, 7881u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_tsg40glpvq01o gm_l18;
    uint8_t gm_l19;
    gm_tsg40glpvq01o gm_l20;
    gm_tsg40glpvq01o gm_l21;
    gm_tsg40glpvq01o gm_l22;
    uint8_t gm_l23;
    gm_tsg40glpvq01o gm_l24;
    gm_tsg40glpvq01o gm_l25;
    uint8_t gm_l26;
    gm_t8carr7mku484 gm_l27;
    double gm_l28;
    gm_t26jhpkpc0sda gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    {
        {
            {
                gm_ts6oqppjh0ba3 t58 = gm_l1;
                gm_ts6oqppjh0ba3 t59;
                GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)10ULL));
                gm_ts6oqppjh0ba3 t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)11ULL));
                gm_ts6oqppjh0ba3 t61;
                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)12ULL));
                gm_ts6oqppjh0ba3 t62;
                GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)13ULL));
                gm_t6htphat2tk14 t63;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t63 + 0), t59);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t63 + 2), t60);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t63 + 4), t61);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t63 + 6), t62);
                uint8_t t64 = 0;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t63) + 0, ((uint8_t *)&t58))) t64 = 1;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t63) + 2, ((uint8_t *)&t58))) t64 = 1;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t63) + 4, ((uint8_t *)&t58))) t64 = 1;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t63) + 6, ((uint8_t *)&t58))) t64 = 1;
                uint8_t t65 = (uint8_t)(t64);
                if (!t65) {
                    goto gm_lb2;
                }
                {
                    {
                        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                        uint8_t t67 = (uint8_t)(t66 == ((uint8_t)0ULL));
                        if (!t67) {
                            goto gm_lb4;
                        }
                        gm_l15 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l15 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 124, 53);
                }
gm_lb3:;
                uint8_t t68 = gm_l15;
                uint8_t t69 = (uint8_t)!t68;
                if (!t69) {
                    goto gm_lb2;
                }
                {
                    {
                        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                        uint8_t t71 = (uint8_t)(t70 == ((uint8_t)1ULL));
                        if (!t71) {
                            goto gm_lb7;
                        }
                        gm_l16 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        gm_l16 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 124, 76);
                }
gm_lb6:;
                uint8_t t72 = gm_l16;
                uint8_t t73 = (uint8_t)!t72;
                if (!t73) {
                    goto gm_lb2;
                }
                {
                    {
                        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t75 = (uint8_t)(t74 == ((uint8_t)10ULL));
                        if (!t75) {
                            goto gm_lb10;
                        }
                        void * t76 = gm_l0;
                        gm_t88rcucg1b7uk t77 = gm_l4;
                        gm_tl874t2atnlh3 t78 = gm_l6;
                        gm_t6evimt84lndq t79 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a2));
                        gm_t6evimt84lndq t80 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l3));
                        gm_f_less_d82rkss((void *)&gm_l18, (void *)NULL, cx, NULL, t76, &t79, &t80, t77, t78);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                        if (t81) {
                        } else {
                            gm_t39uai1ajk3qv t82 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l18) + 16));
                            gm_ttk92ufj2f6eh t83;
                            GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t83 + 8, 0);
                            ((uint8_t *)&t83)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t83 + 16), t82);
                            memmove(gm_out, ((uint8_t *)&t83), 112);
                            goto gm_fdone;
                        }
                        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 16));
                        gm_l17 = t84;
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t86 = (uint8_t)(t85 == ((uint8_t)11ULL));
                        if (!t86) {
                            goto gm_lb11;
                        }
                        void * t87 = gm_l0;
                        gm_t88rcucg1b7uk t88 = gm_l4;
                        gm_tl874t2atnlh3 t89 = gm_l6;
                        gm_t6evimt84lndq t90 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a2));
                        gm_t6evimt84lndq t91 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l3));
                        gm_f_less_d82rkss((void *)&gm_l20, (void *)NULL, cx, NULL, t87, &t90, &t91, t88, t89);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (t92) {
                        } else {
                            gm_t39uai1ajk3qv t93 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l20) + 16));
                            gm_ttk92ufj2f6eh t94;
                            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t94 + 8, 0);
                            ((uint8_t *)&t94)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t94 + 16), t93);
                            memmove(gm_out, ((uint8_t *)&t94), 112);
                            goto gm_fdone;
                        }
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 16));
                        if (t95) {
                            gm_l19 = ((uint8_t)1);
                        } else {
                            void * t96 = gm_l0;
                            gm_t88rcucg1b7uk t97 = gm_l4;
                            gm_tl874t2atnlh3 t98 = gm_l6;
                            gm_t6evimt84lndq t99 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a2));
                            gm_t6evimt84lndq t100 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l3));
                            gm_f_equal_hinsj77((void *)&gm_l21, (void *)NULL, cx, NULL, t96, &t99, &t100, t97, t98);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                            if (t101) {
                            } else {
                                gm_t39uai1ajk3qv t102 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l21) + 16));
                                gm_ttk92ufj2f6eh t103;
                                GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t103 + 8, 0);
                                ((uint8_t *)&t103)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t103 + 16), t102);
                                memmove(gm_out, ((uint8_t *)&t103), 112);
                                goto gm_fdone;
                            }
                            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 16));
                            gm_l19 = t104;
                        }
                        uint8_t t105 = gm_l19;
                        gm_l17 = t105;
                        goto gm_lb9;
                    }
gm_lb11:;
                    {
                        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t107 = (uint8_t)(t106 == ((uint8_t)12ULL));
                        if (!t107) {
                            goto gm_lb12;
                        }
                        void * t108 = gm_l0;
                        gm_t88rcucg1b7uk t109 = gm_l4;
                        gm_tl874t2atnlh3 t110 = gm_l6;
                        gm_t6evimt84lndq t111 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l3));
                        gm_t6evimt84lndq t112 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a2));
                        gm_f_less_d82rkss((void *)&gm_l22, (void *)NULL, cx, NULL, t108, &t111, &t112, t109, t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (t113) {
                        } else {
                            gm_t39uai1ajk3qv t114 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l22) + 16));
                            gm_ttk92ufj2f6eh t115;
                            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t115 + 8, 0);
                            ((uint8_t *)&t115)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t115 + 16), t114);
                            memmove(gm_out, ((uint8_t *)&t115), 112);
                            goto gm_fdone;
                        }
                        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 16));
                        gm_l17 = t116;
                        goto gm_lb9;
                    }
gm_lb12:;
                    {
                        void * t117 = gm_l0;
                        gm_t88rcucg1b7uk t118 = gm_l4;
                        gm_tl874t2atnlh3 t119 = gm_l6;
                        gm_t6evimt84lndq t120 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l3));
                        gm_t6evimt84lndq t121 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a2));
                        gm_f_less_d82rkss((void *)&gm_l24, (void *)NULL, cx, NULL, t117, &t120, &t121, t118, t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (t122) {
                        } else {
                            gm_t39uai1ajk3qv t123 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l24) + 16));
                            gm_ttk92ufj2f6eh t124;
                            GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t124 + 8, 0);
                            ((uint8_t *)&t124)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t124 + 16), t123);
                            memmove(gm_out, ((uint8_t *)&t124), 112);
                            goto gm_fdone;
                        }
                        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 16));
                        if (t125) {
                            gm_l23 = ((uint8_t)1);
                        } else {
                            void * t126 = gm_l0;
                            gm_t88rcucg1b7uk t127 = gm_l4;
                            gm_tl874t2atnlh3 t128 = gm_l6;
                            gm_t6evimt84lndq t129 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a2));
                            gm_t6evimt84lndq t130 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l3));
                            gm_f_equal_hinsj77((void *)&gm_l25, (void *)NULL, cx, NULL, t126, &t129, &t130, t127, t128);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                            if (t131) {
                            } else {
                                gm_t39uai1ajk3qv t132 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l25) + 16));
                                gm_ttk92ufj2f6eh t133;
                                GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t133 + 8, 0);
                                ((uint8_t *)&t133)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t133 + 16), t132);
                                memmove(gm_out, ((uint8_t *)&t133), 112);
                                goto gm_fdone;
                            }
                            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 16));
                            gm_l23 = t134;
                        }
                        uint8_t t135 = gm_l23;
                        gm_l17 = t135;
                        goto gm_lb9;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 125, 22);
                }
gm_lb9:;
                uint8_t t136 = gm_l17;
                gm_t6evimt84lndq t137;
                memset(&t137, 0, sizeof t137);
                GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 16), t136);
                gm_ttk92ufj2f6eh t138;
                memset(&t138, 0, sizeof t138);
                ((uint8_t *)&t138)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t138 + 16), t137);
                memmove(gm_out, ((uint8_t *)&t138), 112);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                {
                    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t140 = (uint8_t)(t139 == ((uint8_t)0ULL));
                    if (!t140) {
                        goto gm_lb15;
                    }
                    unsigned __int128 t141 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
                    gm_l7 = t141;
                    {
                        {
                            uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t143 = (uint8_t)(t142 == ((uint8_t)0ULL));
                            if (!t143) {
                                goto gm_lb17;
                            }
                            unsigned __int128 t144 = GM_LD(unsigned __int128, (((uint8_t *)&F.l3) + 16));
                            gm_l8 = t144;
                            {
                                {
                                    {
                                        {
                                            uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t146 = (uint8_t)(t145 == ((uint8_t)10ULL));
                                            if (!t146) {
                                                goto gm_lb21;
                                            }
                                            goto gm_lb20;
                                        }
gm_lb21:;
                                        {
                                            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t148 = (uint8_t)(t147 == ((uint8_t)11ULL));
                                            if (!t148) {
                                                goto gm_lb22;
                                            }
                                            goto gm_lb20;
                                        }
gm_lb22:;
                                        {
                                            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t150 = (uint8_t)(t149 == ((uint8_t)12ULL));
                                            if (!t150) {
                                                goto gm_lb23;
                                            }
                                            goto gm_lb20;
                                        }
gm_lb23:;
                                        {
                                            uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t152 = (uint8_t)(t151 == ((uint8_t)13ULL));
                                            if (!t152) {
                                                goto gm_lb24;
                                            }
                                            goto gm_lb20;
                                        }
gm_lb24:;
                                        goto gm_lb19;
                                    }
gm_lb20:;
                                    void * t153 = gm_l0;
                                    unsigned __int128 t154 = gm_l7;
                                    gm_t88rcucg1b7uk t155 = gm_l4;
                                    gm_tbci5ospk5nnn t156;
                                    gm_f_big_of_1cq0qao((void *)&t156, (void *)NULL, cx, NULL, t153, t154, t155);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t157 = gm_l0;
                                    unsigned __int128 t158 = gm_l8;
                                    gm_t88rcucg1b7uk t159 = gm_l5;
                                    gm_tbci5ospk5nnn t160;
                                    gm_f_big_of_1cq0qao((void *)&t160, (void *)NULL, cx, NULL, t157, t158, t159);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    int32_t t161 = gm_f_cmp_i1knrgu(&t156, &t160);
                                    gm_l9 = t161;
                                    {
                                        {
                                            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t163 = (uint8_t)(t162 == ((uint8_t)10ULL));
                                            if (!t163) {
                                                goto gm_lb26;
                                            }
                                            int32_t t164 = gm_l9;
                                            uint8_t t165 = (t164 < ((int32_t)0LL));
                                            gm_l26 = t165;
                                            goto gm_lb25;
                                        }
gm_lb26:;
                                        {
                                            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t167 = (uint8_t)(t166 == ((uint8_t)11ULL));
                                            if (!t167) {
                                                goto gm_lb27;
                                            }
                                            int32_t t168 = gm_l9;
                                            uint8_t t169 = (t168 <= ((int32_t)0LL));
                                            gm_l26 = t169;
                                            goto gm_lb25;
                                        }
gm_lb27:;
                                        {
                                            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                            uint8_t t171 = (uint8_t)(t170 == ((uint8_t)12ULL));
                                            if (!t171) {
                                                goto gm_lb28;
                                            }
                                            int32_t t172 = gm_l9;
                                            uint8_t t173 = (t172 > ((int32_t)0LL));
                                            gm_l26 = t173;
                                            goto gm_lb25;
                                        }
gm_lb28:;
                                        {
                                            int32_t t174 = gm_l9;
                                            uint8_t t175 = (t174 >= ((int32_t)0LL));
                                            gm_l26 = t175;
                                            goto gm_lb25;
                                        }
gm_lb29:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 138, 34);
                                    }
gm_lb25:;
                                    uint8_t t176 = gm_l26;
                                    gm_t6evimt84lndq t177;
                                    memset(&t177, 0, sizeof t177);
                                    GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t177 + 16), t176);
                                    gm_ttk92ufj2f6eh t178;
                                    memset(&t178, 0, sizeof t178);
                                    ((uint8_t *)&t178)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t178 + 16), t177);
                                    memmove(gm_out, ((uint8_t *)&t178), 112);
                                    goto gm_fdone;
                                }
gm_lb19:;
                                {
                                    uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t180 = (uint8_t)(t179 == ((uint8_t)7ULL));
                                    if (!t180) {
                                        goto gm_lb30;
                                    }
                                    unsigned __int128 t181 = gm_l7;
                                    unsigned __int128 t182 = gm_l8;
                                    unsigned __int128 t183 = (unsigned __int128)(t181 & t182);
                                    gm_t6evimt84lndq t184;
                                    GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t184 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t184 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t184 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)0ULL));
                                    GM_ST(unsigned __int128, ((uint8_t *)&t184 + 16), t183);
                                    gm_ttk92ufj2f6eh t185;
                                    memset(&t185, 0, sizeof t185);
                                    ((uint8_t *)&t185)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t185 + 16), t184);
                                    memmove(gm_out, ((uint8_t *)&t185), 112);
                                    goto gm_fdone;
                                }
gm_lb30:;
                                {
                                    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t187 = (uint8_t)(t186 == ((uint8_t)8ULL));
                                    if (!t187) {
                                        goto gm_lb31;
                                    }
                                    unsigned __int128 t188 = gm_l7;
                                    unsigned __int128 t189 = gm_l8;
                                    unsigned __int128 t190 = (unsigned __int128)(t188 | t189);
                                    gm_t6evimt84lndq t191;
                                    GM_ST(uint8_t, (uint8_t *)&t191 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t191 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t191 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t191 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t191 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)0ULL));
                                    GM_ST(unsigned __int128, ((uint8_t *)&t191 + 16), t190);
                                    gm_ttk92ufj2f6eh t192;
                                    memset(&t192, 0, sizeof t192);
                                    ((uint8_t *)&t192)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t192 + 16), t191);
                                    memmove(gm_out, ((uint8_t *)&t192), 112);
                                    goto gm_fdone;
                                }
gm_lb31:;
                                {
                                    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t194 = (uint8_t)(t193 == ((uint8_t)9ULL));
                                    if (!t194) {
                                        goto gm_lb32;
                                    }
                                    unsigned __int128 t195 = gm_l7;
                                    unsigned __int128 t196 = gm_l8;
                                    unsigned __int128 t197 = (unsigned __int128)(t195 ^ t196);
                                    gm_t6evimt84lndq t198;
                                    GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t198 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t198 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t198 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)0ULL));
                                    GM_ST(unsigned __int128, ((uint8_t *)&t198 + 16), t197);
                                    gm_ttk92ufj2f6eh t199;
                                    memset(&t199, 0, sizeof t199);
                                    ((uint8_t *)&t199)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t199 + 16), t198);
                                    memmove(gm_out, ((uint8_t *)&t199), 112);
                                    goto gm_fdone;
                                }
gm_lb32:;
                                {
                                    void * t200 = gm_l0;
                                    gm_ts6oqppjh0ba3 t201 = gm_l1;
                                    unsigned __int128 t202 = gm_l7;
                                    unsigned __int128 t203 = gm_l8;
                                    gm_t88rcucg1b7uk t204 = gm_l4;
                                    gm_t88rcucg1b7uk t205 = gm_l5;
                                    gm_tl874t2atnlh3 t206 = gm_l6;
                                    gm_f_int_op_jnafd2a((void *)&gm_l27, (void *)NULL, cx, NULL, t200, t201, t202, t203, t204, t205, t206);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                    if (t207) {
                                    } else {
                                        gm_t39uai1ajk3qv t208 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l27) + 16));
                                        gm_ttk92ufj2f6eh t209;
                                        GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t209 + 8, 0);
                                        ((uint8_t *)&t209)[0] = 1;
                                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t209 + 16), t208);
                                        memmove(gm_out, ((uint8_t *)&t209), 112);
                                        goto gm_fdone;
                                    }
                                    unsigned __int128 t210 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l27) + 16));
                                    gm_t6evimt84lndq t211;
                                    GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t211 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t211 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t211 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)0ULL));
                                    GM_ST(unsigned __int128, ((uint8_t *)&t211 + 16), t210);
                                    gm_ttk92ufj2f6eh t212;
                                    memset(&t212, 0, sizeof t212);
                                    ((uint8_t *)&t212)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t212 + 16), t211);
                                    memmove(gm_out, ((uint8_t *)&t212), 112);
                                    goto gm_fdone;
                                }
gm_lb33:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 135, 13);
                            }
gm_lb18:;
                        }
gm_lb17:;
                        void * t213 = gm_l0;
                        gm_tl874t2atnlh3 t214 = gm_l6;
                        gm_b16_8 t215;
                        GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sqe4aodms9tfs));
                        GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(13));
                        gm_t39uai1ajk3qv t216;
                        gm_f_unsupported_2seq49l((void *)&t216, t213, GM_CONV(gm_cseahvg8v7p0vs, t214), t215);
                        gm_ttk92ufj2f6eh t217;
                        GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t217 + 8, 0);
                        ((uint8_t *)&t217)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t217 + 16), t216);
                        memmove(gm_out, ((uint8_t *)&t217), 112);
                        goto gm_fdone;
                    }
gm_lb16:;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t219 = (uint8_t)(t218 == ((uint8_t)1ULL));
                    if (!t219) {
                        goto gm_lb34;
                    }
                    double t220 = GM_LD(double, (((uint8_t *)a2) + 16));
                    gm_l10 = t220;
                    {
                        {
                            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t222 = (uint8_t)(t221 == ((uint8_t)1ULL));
                            if (!t222) {
                                goto gm_lb36;
                            }
                            double t223 = GM_LD(double, (((uint8_t *)&F.l3) + 16));
                            gm_l11 = t223;
                            {
                                {
                                    uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t225 = (uint8_t)(t224 == ((uint8_t)0ULL));
                                    if (!t225) {
                                        goto gm_lb38;
                                    }
                                    double t226 = gm_l10;
                                    double t227 = gm_l11;
                                    double t228 = (t226 + t227);
                                    gm_l28 = t228;
                                    goto gm_lb37;
                                }
gm_lb38:;
                                {
                                    uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t230 = (uint8_t)(t229 == ((uint8_t)1ULL));
                                    if (!t230) {
                                        goto gm_lb39;
                                    }
                                    double t231 = gm_l10;
                                    double t232 = gm_l11;
                                    double t233 = (t231 - t232);
                                    gm_l28 = t233;
                                    goto gm_lb37;
                                }
gm_lb39:;
                                {
                                    uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t235 = (uint8_t)(t234 == ((uint8_t)2ULL));
                                    if (!t235) {
                                        goto gm_lb40;
                                    }
                                    double t236 = gm_l10;
                                    double t237 = gm_l11;
                                    double t238 = (t236 * t237);
                                    gm_l28 = t238;
                                    goto gm_lb37;
                                }
gm_lb40:;
                                {
                                    uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t240 = (uint8_t)(t239 == ((uint8_t)4ULL));
                                    if (!t240) {
                                        goto gm_lb41;
                                    }
                                    double t241 = gm_l10;
                                    double t242 = gm_l11;
                                    double t243 = (t241 / t242);
                                    gm_l28 = t243;
                                    goto gm_lb37;
                                }
gm_lb41:;
                                {
                                    uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t245 = (uint8_t)(t244 == ((uint8_t)5ULL));
                                    if (!t245) {
                                        goto gm_lb42;
                                    }
                                    double t246 = gm_l10;
                                    double t247 = gm_l11;
                                    double t248 = gm_f_float_rem_24loign((void *)NULL, cx, NULL, t246, t247);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l28 = t248;
                                    goto gm_lb37;
                                }
gm_lb42:;
                                {
                                    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t250 = (uint8_t)(t249 == ((uint8_t)10ULL));
                                    if (!t250) {
                                        goto gm_lb43;
                                    }
                                    double t251 = gm_l10;
                                    double t252 = gm_l11;
                                    uint8_t t253 = (t251 < t252);
                                    gm_t6evimt84lndq t254;
                                    memset(&t254, 0, sizeof t254);
                                    GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t254 + 16), t253);
                                    gm_ttk92ufj2f6eh t255;
                                    memset(&t255, 0, sizeof t255);
                                    ((uint8_t *)&t255)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t255 + 16), t254);
                                    memmove(gm_out, ((uint8_t *)&t255), 112);
                                    goto gm_fdone;
                                }
gm_lb43:;
                                {
                                    uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t257 = (uint8_t)(t256 == ((uint8_t)11ULL));
                                    if (!t257) {
                                        goto gm_lb44;
                                    }
                                    double t258 = gm_l10;
                                    double t259 = gm_l11;
                                    uint8_t t260 = (t258 <= t259);
                                    gm_t6evimt84lndq t261;
                                    memset(&t261, 0, sizeof t261);
                                    GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t261 + 16), t260);
                                    gm_ttk92ufj2f6eh t262;
                                    memset(&t262, 0, sizeof t262);
                                    ((uint8_t *)&t262)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t262 + 16), t261);
                                    memmove(gm_out, ((uint8_t *)&t262), 112);
                                    goto gm_fdone;
                                }
gm_lb44:;
                                {
                                    uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t264 = (uint8_t)(t263 == ((uint8_t)12ULL));
                                    if (!t264) {
                                        goto gm_lb45;
                                    }
                                    double t265 = gm_l10;
                                    double t266 = gm_l11;
                                    uint8_t t267 = (t265 > t266);
                                    gm_t6evimt84lndq t268;
                                    memset(&t268, 0, sizeof t268);
                                    GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t268 + 16), t267);
                                    gm_ttk92ufj2f6eh t269;
                                    memset(&t269, 0, sizeof t269);
                                    ((uint8_t *)&t269)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t269 + 16), t268);
                                    memmove(gm_out, ((uint8_t *)&t269), 112);
                                    goto gm_fdone;
                                }
gm_lb45:;
                                {
                                    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t271 = (uint8_t)(t270 == ((uint8_t)13ULL));
                                    if (!t271) {
                                        goto gm_lb46;
                                    }
                                    double t272 = gm_l10;
                                    double t273 = gm_l11;
                                    uint8_t t274 = (t272 >= t273);
                                    gm_t6evimt84lndq t275;
                                    memset(&t275, 0, sizeof t275);
                                    GM_ST(uint8_t, ((uint8_t *)&t275 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t275 + 16), t274);
                                    gm_ttk92ufj2f6eh t276;
                                    memset(&t276, 0, sizeof t276);
                                    ((uint8_t *)&t276)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t276 + 16), t275);
                                    memmove(gm_out, ((uint8_t *)&t276), 112);
                                    goto gm_fdone;
                                }
gm_lb46:;
                                {
                                    void * t277 = gm_l0;
                                    gm_tl874t2atnlh3 t278 = gm_l6;
                                    gm_b16_8 t279;
                                    GM_ST(void *, (uint8_t *)&t279, (void *)(gm_sllgvbfukep8p));
                                    GM_ST(uintptr_t, (uint8_t *)&t279 + 8, (uintptr_t)(23));
                                    gm_t39uai1ajk3qv t280;
                                    gm_f_unsupported_2seq49l((void *)&t280, t277, GM_CONV(gm_cseahvg8v7p0vs, t278), t279);
                                    gm_ttk92ufj2f6eh t281;
                                    GM_ST(uint8_t, (uint8_t *)&t281 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t281 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t281 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t281 + 8, 0);
                                    ((uint8_t *)&t281)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t281 + 16), t280);
                                    memmove(gm_out, ((uint8_t *)&t281), 112);
                                    goto gm_fdone;
                                }
gm_lb47:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 153, 18);
                            }
gm_lb37:;
                            double t282 = gm_l28;
                            gm_l12 = t282;
                            void * t283 = gm_l0;
                            double t284 = gm_l12;
                            gm_t88rcucg1b7uk t285 = gm_l4;
                            gm_f_round_float_saju9fu((void *)&gm_l29, (void *)NULL, cx, NULL, t283, t284, t285);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                            if (t286) {
                            } else {
                                gm_t39uai1ajk3qv t287 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l29) + 16));
                                gm_ttk92ufj2f6eh t288;
                                GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t288 + 8, 0);
                                ((uint8_t *)&t288)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t288 + 16), t287);
                                memmove(gm_out, ((uint8_t *)&t288), 112);
                                goto gm_fdone;
                            }
                            double t289 = GM_LD(double, (((uint8_t *)&gm_l29) + 16));
                            gm_t6evimt84lndq t290;
                            memset(&t290, 0, sizeof t290);
                            GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)1ULL));
                            GM_ST(double, ((uint8_t *)&t290 + 16), t289);
                            gm_ttk92ufj2f6eh t291;
                            memset(&t291, 0, sizeof t291);
                            ((uint8_t *)&t291)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t291 + 16), t290);
                            memmove(gm_out, ((uint8_t *)&t291), 112);
                            goto gm_fdone;
                        }
gm_lb36:;
                        void * t292 = gm_l0;
                        gm_tl874t2atnlh3 t293 = gm_l6;
                        gm_b16_8 t294;
                        GM_ST(void *, (uint8_t *)&t294, (void *)(gm_sqe4aodms9tfs));
                        GM_ST(uintptr_t, (uint8_t *)&t294 + 8, (uintptr_t)(13));
                        gm_t39uai1ajk3qv t295;
                        gm_f_unsupported_2seq49l((void *)&t295, t292, GM_CONV(gm_cseahvg8v7p0vs, t293), t294);
                        gm_ttk92ufj2f6eh t296;
                        GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t296 + 8, 0);
                        ((uint8_t *)&t296)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t296 + 16), t295);
                        memmove(gm_out, ((uint8_t *)&t296), 112);
                        goto gm_fdone;
                    }
gm_lb35:;
                }
gm_lb34:;
            }
            {
                {
                    uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t298 = (uint8_t)(t297 == ((uint8_t)2ULL));
                    if (!t298) {
                        goto gm_lb48;
                    }
                    uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
                    gm_l13 = t299;
                    {
                        {
                            uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t301 = (uint8_t)(t300 == ((uint8_t)2ULL));
                            if (!t301) {
                                goto gm_lb50;
                            }
                            uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
                            gm_l14 = t302;
                            {
                                {
                                    uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t304 = (uint8_t)(t303 == ((uint8_t)7ULL));
                                    if (!t304) {
                                        goto gm_lb52;
                                    }
                                    uint8_t t305 = gm_l13;
                                    if (t305) {
                                        uint8_t t306 = gm_l14;
                                        gm_l30 = t306;
                                    } else {
                                        gm_l30 = ((uint8_t)0);
                                    }
                                    uint8_t t307 = gm_l30;
                                    gm_t6evimt84lndq t308;
                                    memset(&t308, 0, sizeof t308);
                                    GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t308 + 16), t307);
                                    gm_ttk92ufj2f6eh t309;
                                    memset(&t309, 0, sizeof t309);
                                    ((uint8_t *)&t309)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t309 + 16), t308);
                                    memmove(gm_out, ((uint8_t *)&t309), 112);
                                    goto gm_fdone;
                                }
gm_lb52:;
                                {
                                    uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t311 = (uint8_t)(t310 == ((uint8_t)8ULL));
                                    if (!t311) {
                                        goto gm_lb53;
                                    }
                                    uint8_t t312 = gm_l13;
                                    if (t312) {
                                        gm_l31 = ((uint8_t)1);
                                    } else {
                                        uint8_t t313 = gm_l14;
                                        gm_l31 = t313;
                                    }
                                    uint8_t t314 = gm_l31;
                                    gm_t6evimt84lndq t315;
                                    memset(&t315, 0, sizeof t315);
                                    GM_ST(uint8_t, ((uint8_t *)&t315 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t315 + 16), t314);
                                    gm_ttk92ufj2f6eh t316;
                                    memset(&t316, 0, sizeof t316);
                                    ((uint8_t *)&t316)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t316 + 16), t315);
                                    memmove(gm_out, ((uint8_t *)&t316), 112);
                                    goto gm_fdone;
                                }
gm_lb53:;
                                {
                                    uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                                    uint8_t t318 = (uint8_t)(t317 == ((uint8_t)9ULL));
                                    if (!t318) {
                                        goto gm_lb54;
                                    }
                                    uint8_t t319 = gm_l13;
                                    uint8_t t320 = gm_l14;
                                    uint8_t t321 = (!(t319 == t320));
                                    gm_t6evimt84lndq t322;
                                    memset(&t322, 0, sizeof t322);
                                    GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)2ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t322 + 16), t321);
                                    gm_ttk92ufj2f6eh t323;
                                    memset(&t323, 0, sizeof t323);
                                    ((uint8_t *)&t323)[0] = 0;
                                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t323 + 16), t322);
                                    memmove(gm_out, ((uint8_t *)&t323), 112);
                                    goto gm_fdone;
                                }
gm_lb54:;
                                {
                                    void * t324 = gm_l0;
                                    gm_tl874t2atnlh3 t325 = gm_l6;
                                    gm_b16_8 t326;
                                    GM_ST(void *, (uint8_t *)&t326, (void *)(gm_sbsu8rcurukea));
                                    GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(24));
                                    gm_t39uai1ajk3qv t327;
                                    gm_f_unsupported_2seq49l((void *)&t327, t324, GM_CONV(gm_cseahvg8v7p0vs, t325), t326);
                                    gm_ttk92ufj2f6eh t328;
                                    GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t328 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t328 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t328 + 8, 0);
                                    ((uint8_t *)&t328)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t328 + 16), t327);
                                    memmove(gm_out, ((uint8_t *)&t328), 112);
                                    goto gm_fdone;
                                }
gm_lb55:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 169, 13);
                            }
gm_lb51:;
                        }
gm_lb50:;
                        void * t329 = gm_l0;
                        gm_tl874t2atnlh3 t330 = gm_l6;
                        gm_b16_8 t331;
                        GM_ST(void *, (uint8_t *)&t331, (void *)(gm_sqe4aodms9tfs));
                        GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(13));
                        gm_t39uai1ajk3qv t332;
                        gm_f_unsupported_2seq49l((void *)&t332, t329, GM_CONV(gm_cseahvg8v7p0vs, t330), t331);
                        gm_ttk92ufj2f6eh t333;
                        GM_ST(uint8_t, (uint8_t *)&t333 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t333 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t333 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t333 + 8, 0);
                        ((uint8_t *)&t333)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t333 + 16), t332);
                        memmove(gm_out, ((uint8_t *)&t333), 112);
                        goto gm_fdone;
                    }
gm_lb49:;
                }
gm_lb48:;
                {
                    void * t334 = gm_l0;
                    gm_tl874t2atnlh3 t335 = gm_l6;
                    gm_b16_8 t336;
                    GM_ST(void *, (uint8_t *)&t336, (void *)(gm_s0dggtblbl7jq));
                    GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(29));
                    gm_t39uai1ajk3qv t337;
                    gm_f_unsupported_2seq49l((void *)&t337, t334, GM_CONV(gm_cseahvg8v7p0vs, t335), t336);
                    gm_ttk92ufj2f6eh t338;
                    GM_ST(uint8_t, (uint8_t *)&t338 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t338 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t338 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t338 + 8, 0);
                    ((uint8_t *)&t338)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t338 + 16), t337);
                    memmove(gm_out, ((uint8_t *)&t338), 112);
                    goto gm_fdone;
                }
gm_lb56:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 132, 5);
            }
        }
gm_lb14:;
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
inline __attribute__((aligned(4))) void gm_f_u8_elems_akjlvj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_closure a3) {
    struct gm_fr_u8_elems_akjlvj2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* et */
    uint64_t gm_l2; /* n */
    gm_closure gm_l3; /* elem */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    unsigned __int128 gm_l8; /* b */
    GM_STACK_CHECK(&F, 621u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t6evimt84lndq gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_t53at80pfdus3 t13;
    gm_f_kind_jcnt4hm((void *)&t13, t11, GM_CONV(gm_csguankko4lmhd, t12));
    gm_tjcj4j2gbh6c8 t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0));
    GM_ST(uint16_t, ((uint8_t *)&t14 + 2), ((uint16_t)8ULL));
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
    if (t16) {
        gm_tjcj4j2gbh6c8 t17 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t13) + 8));
        t16 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t17), ((uint8_t *)&t14));
    }
    uint8_t t18 = (uint8_t)(t16);
    uint8_t t19 = (uint8_t)!t18;
    gm_t53at80pfdus3 t20;
    memset(&t20, 0, sizeof t20);
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t20 + 8), t14);
    if (t19) {
        gm_th2lqbo77pvsc t21;
        memset(&t21, 0, sizeof t21);
        memmove(gm_out, ((uint8_t *)&t21), 24);
        goto gm_fdone;
    }
    gm_trqfvmjtt4sij t22;
    gm_f_list_uvfu1k1((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t22;
    gm_l6 = ((uint64_t)0ULL);
    uint64_t t23 = gm_l2;
    gm_l5 = t23;
    {
        for (;;) {
            uint64_t t24 = gm_l6;
            uint64_t t25 = gm_l5;
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t27 = gm_l6;
                        gm_l7 = t27;
                        {
                            {
                                gm_closure t28 = gm_l3;
                                uint64_t t29 = gm_l7;
                                gm_closure t30 = t28;
                                gm_node *ccx31 = t30.cx;
                                void * t32 = t30.code;
                                gm_t6evimt84lndq t33;
                                if (GM_PTR_TAGGED(t32)) {
                                    t33 = ((gm_t6evimt84lndq (*)(uint64_t))GM_PTR_UNTAG(t32))(t29);
                                } else {
                                    ((void (*)(void *, void *, gm_node *, gm_node *, uint64_t))t32)((void *)&t33, t30.env, ccx31, NULL, t29);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l9 = t33;
                                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                                uint8_t t35 = (uint8_t)(t34 == ((uint8_t)0ULL));
                                if (!t35) {
                                    goto gm_lb7;
                                }
                                unsigned __int128 t36 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l9) + 16));
                                gm_l8 = t36;
                                void * t37 = (void *)((uint8_t *)&F.l4);
                                unsigned __int128 t38 = gm_l8;
                                uint8_t t39 = (uint8_t)t38;
                                gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t37, t39);
                                gm_l10 = t40;
                                uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcue4ionbe9lgh, 687, 9);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_th2lqbo77pvsc t42;
                            memset(&t42, 0, sizeof t42);
                            memmove(gm_out, ((uint8_t *)&t42), 24);
                            goto gm_fdone;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 685, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t43 = gm_l6;
            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
            uint64_t t46 = r44;
            gm_l6 = t46;
        }
    }
gm_lb1:;
    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_th2lqbo77pvsc t48;
    GM_ST(uint8_t, (uint8_t *)&t48 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t48 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t48 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t48), t47);
    ((uint8_t *)&t48)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t48), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_unary_19d8qg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3) {
    struct gm_fr_unary_19d8qg7 F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    void * gm_l5; /* a */
    gm_t88rcucg1b7uk gm_l6; /* lane */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 1403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_ttk92ufj2f6eh gm_l11;
    gm_tb7u19loo3j87 gm_l12;
    gm_tlcm4lvu3g1a8 gm_l13;
    gm_ttk92ufj2f6eh gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tq7s98ihonkju gm_l16;
    void * t9 = gm_l0;
    void * t10 = gm_l2;
    gm_f_eval_2vbtp1v((void *)&gm_l11, (void *)NULL, cx, NULL, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t11) {
    } else {
        gm_t39uai1ajk3qv t12 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l11) + 16));
        gm_ttk92ufj2f6eh t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 8, 0);
        ((uint8_t *)&t13)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t13 + 16), t12);
        memmove(gm_out, ((uint8_t *)&t13), 112);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l11) + 16), 48);
    {
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)3ULL));
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
            gm_l5 = t16;
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, ((uint8_t *)t17));
            void * t19 = (void *)(((uint8_t *)t18) + 8);
            void * t20 = gm_l3;
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t20));
            gm_f_vector_parts_crrfopm((void *)&gm_l12, t19, GM_CONV(gm_csguankko4lmhd, t21));
            uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
            if (!t22) gm_trap("unwrap of null", gm_srcue4ionbe9lgh, 74, 17);
            gm_tlcm4lvu3g1a8 t23 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l12));
            gm_l13 = t23;
            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
            gm_l6 = t24;
            gm_trqfvmjtt4sij t25;
            gm_f_list_0202sdh_2((void *)&t25, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l7 = t25;
            void * t26 = gm_l5;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)t26));
            gm_l8 = t27;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t28 = gm_l9;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t30 = (t28 < t29);
                    if (!t30) {
                        goto gm_lb3;
                    }
                    {
                        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t33 = gm_l9;
                        {
                            {
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t31) + (uintptr_t)t33 * 48), 48);
                                void * t34 = (void *)((uint8_t *)&F.l7);
                                void * t35 = gm_l0;
                                gm_t9qkh0rsooc8d t36 = gm_l1;
                                gm_t88rcucg1b7uk t37 = gm_l6;
                                void * t38 = gm_l2;
                                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
                                void * t40 = gm_l3;
                                gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                                gm_t6evimt84lndq t42 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l10));
                                gm_f_unary_value_vi7cs6u((void *)&gm_l14, (void *)NULL, cx, NULL, t35, t36, &t42, t37, t39, t41);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                if (t43) {
                                } else {
                                    gm_t39uai1ajk3qv t44 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l14) + 16));
                                    gm_ttk92ufj2f6eh t45;
                                    GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t45 + 8, 0);
                                    ((uint8_t *)&t45)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t45 + 16), t44);
                                    memmove(gm_out, ((uint8_t *)&t45), 112);
                                    goto gm_fdone;
                                }
                                gm_tftb1odc739t5 t46 = gm_f_push_0pgacsj_2(t34, &(*(gm_t6evimt84lndq *)(((uint8_t *)&gm_l14) + 16)));
                                gm_l15 = t46;
                                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcue4ionbe9lgh, 76, 29);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 76, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t48 = gm_l9;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l9 = t51;
                }
            }
gm_lb3:;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_tnpifv20irnph t53;
            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
            gm_tq7s98ihonkju t54 = gm_f_new_lcff781((void *)NULL, cx, NULL, t53, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktbnd3fuoao)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t54;
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcue4ionbe9lgh, 77, 21);
            void * t56 = GM_LD(void *, (((uint8_t *)&gm_l16) + 8));
            gm_t6evimt84lndq t57;
            memset(&t57, 0, sizeof t57);
            GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t57 + 16), t56);
            gm_ttk92ufj2f6eh t58;
            memset(&t58, 0, sizeof t58);
            ((uint8_t *)&t58)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t58 + 16), t57);
            memmove(gm_out, ((uint8_t *)&t58), 112);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t59 = gm_l0;
    gm_t9qkh0rsooc8d t60 = gm_l1;
    void * t61 = gm_l2;
    gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t61));
    void * t63 = gm_l2;
    gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
    void * t65 = gm_l3;
    gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
    gm_t6evimt84lndq t67 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l4));
    gm_f_unary_value_vi7cs6u((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t59, t60, &t67, t62, t64, t66);
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
__attribute__((aligned(4))) void gm_f_unary_value_vi7cs6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_unary_value_vi7cs6u F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l3; /* xt */
    gm_tl874t2atnlh3 gm_l4; /* xspan */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* x */
    void * gm_l7; /* e */
    uint8_t gm_l8; /* b */
    unsigned __int128 gm_l9; /* bits */
    double gm_l10; /* f */
    unsigned __int128 gm_l11; /* bits */
    GM_STACK_CHECK(&F, 3238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tq7s98ihonkju gm_l12;
    gm_tq7s98ihonkju gm_l13;
    gm_ttovhfee8mf28 gm_l14;
    gm_t8carr7mku484 gm_l15;
    gm_t8carr7mku484 gm_l16;
    gm_t88rcucg1b7uk t14 = gm_l3;
    gm_tl874t2atnlh3 t15 = gm_l4;
    gm_t2b6tm8c0vgo1 t16;
    memset(&t16, 0, sizeof t16);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)7ULL));
    gm_tcgrjq9toscke t17;
    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 84, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t17 + 0), t14);
    GM_ST(gm_t2b6tm8c0vgo1, ((uint8_t *)&t17 + 8), t16);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t17 + 72), t15);
    gm_tq7s98ihonkju t18 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t17, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kinrtga7ov4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcue4ionbe9lgh, 84, 10);
    void * t20 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
    gm_l6 = t20;
    gm_t88rcucg1b7uk t21 = gm_l3;
    gm_tl874t2atnlh3 t22 = gm_l5;
    gm_t2b6tm8c0vgo1 t23;
    memset(&t23, 0, sizeof t23);
    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)7ULL));
    gm_tcgrjq9toscke t24;
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 84, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t24 + 0), t21);
    GM_ST(gm_t2b6tm8c0vgo1, ((uint8_t *)&t24 + 8), t23);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t24 + 72), t22);
    gm_tq7s98ihonkju t25 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t24, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksrlu8s6v53)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t25;
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcue4ionbe9lgh, 85, 10);
    void * t27 = GM_LD(void *, (((uint8_t *)&gm_l13) + 8));
    gm_l7 = t27;
    {
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)2ULL));
            if (!t29) {
                goto gm_lb2;
            }
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
                    gm_l8 = t32;
                    uint8_t t33 = gm_l8;
                    uint8_t t34 = (uint8_t)!t33;
                    gm_t6evimt84lndq t35;
                    memset(&t35, 0, sizeof t35);
                    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)2ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t35 + 16), t34);
                    gm_ttk92ufj2f6eh t36;
                    memset(&t36, 0, sizeof t36);
                    ((uint8_t *)&t36)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t36 + 16), t35);
                    memmove(gm_out, ((uint8_t *)&t36), 112);
                    goto gm_fdone;
                }
gm_lb4:;
                void * t37 = gm_l0;
                void * t38 = gm_l7;
                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t41;
                gm_f_unsupported_2seq49l((void *)&t41, t37, GM_CONV(gm_cseahvg8v7p0vs, t39), t40);
                gm_ttk92ufj2f6eh t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t42 + 8, 0);
                ((uint8_t *)&t42)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t42 + 16), t41);
                memmove(gm_out, ((uint8_t *)&t42), 112);
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        {
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)1ULL));
            if (!t44) {
                goto gm_lb5;
            }
            {
                {
                    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)0ULL));
                    if (!t46) {
                        goto gm_lb7;
                    }
                    unsigned __int128 t47 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
                    gm_l9 = t47;
                    void * t48 = gm_l0;
                    unsigned __int128 t49 = gm_l9;
                    unsigned __int128 t50 = (unsigned __int128)~t49;
                    void * t51 = gm_l7;
                    gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t51));
                    unsigned __int128 t53 = gm_f_wrap_bt8r149(t48, t50, GM_CONV(gm_csguankko4lmhd, t52));
                    gm_t6evimt84lndq t54;
                    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t54 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t54 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t54 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t54 + 16), t53);
                    gm_ttk92ufj2f6eh t55;
                    memset(&t55, 0, sizeof t55);
                    ((uint8_t *)&t55)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t55 + 16), t54);
                    memmove(gm_out, ((uint8_t *)&t55), 112);
                    goto gm_fdone;
                }
gm_lb7:;
                void * t56 = gm_l0;
                void * t57 = gm_l7;
                gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 72));
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t60;
                gm_f_unsupported_2seq49l((void *)&t60, t56, GM_CONV(gm_cseahvg8v7p0vs, t58), t59);
                gm_ttk92ufj2f6eh t61;
                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t61 + 8, 0);
                ((uint8_t *)&t61)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t61 + 16), t60);
                memmove(gm_out, ((uint8_t *)&t61), 112);
                goto gm_fdone;
            }
gm_lb6:;
        }
gm_lb5:;
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)0ULL));
            if (!t63) {
                goto gm_lb8;
            }
            {
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)1ULL));
                    if (!t65) {
                        goto gm_lb10;
                    }
                    double t66 = GM_LD(double, (((uint8_t *)a2) + 16));
                    gm_l10 = t66;
                    double t67 = gm_l10;
                    double t68 = -t67;
                    gm_t6evimt84lndq t69;
                    memset(&t69, 0, sizeof t69);
                    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
                    GM_ST(double, ((uint8_t *)&t69 + 16), t68);
                    gm_ttk92ufj2f6eh t70;
                    memset(&t70, 0, sizeof t70);
                    ((uint8_t *)&t70)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t70 + 16), t69);
                    memmove(gm_out, ((uint8_t *)&t70), 112);
                    goto gm_fdone;
                }
gm_lb10:;
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)0ULL));
                    if (!t72) {
                        goto gm_lb11;
                    }
                    unsigned __int128 t73 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
                    gm_l11 = t73;
                    void * t74 = gm_l0;
                    void * t75 = gm_l7;
                    gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t75));
                    gm_ttovhfee8mf28 t77 = gm_f_int_info_2r9qb5n(t74, GM_CONV(gm_csguankko4lmhd, t76));
                    gm_l14 = t77;
                    uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 4));
                    uint8_t t79 = (t78 <= ((uint32_t)63ULL));
                    if (t79) {
                        void * t80 = gm_l0;
                        void * t81 = gm_l0;
                        unsigned __int128 t82 = gm_l11;
                        void * t83 = gm_l6;
                        gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t83));
                        __int128 t85 = gm_f_int_value_6hmh8tj(t81, t82, GM_CONV(gm_csguankko4lmhd, t84));
                        if (t85 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 98, 78);
                        __int128 t86 = (__int128)((unsigned __int128)0 - (unsigned __int128)t85);
                        void * t87 = gm_l7;
                        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t87));
                        gm_b16_8 t89;
                        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_skolmnfc0ccbd));
                        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(1));
                        void * t90 = gm_l7;
                        gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 72));
                        gm_t9qkh0rsooc8d t92;
                        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
                        gm_f_fit_small_7o94i8v((void *)&gm_l15, (void *)NULL, cx, NULL, t80, t86, t92, t88, t89, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (t93) {
                        } else {
                            gm_t39uai1ajk3qv t94 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l15) + 16));
                            gm_ttk92ufj2f6eh t95;
                            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t95 + 8, 0);
                            ((uint8_t *)&t95)[0] = 1;
                            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t95 + 16), t94);
                            memmove(gm_out, ((uint8_t *)&t95), 112);
                            goto gm_fdone;
                        }
                        unsigned __int128 t96 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l15) + 16));
                        gm_t6evimt84lndq t97;
                        GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t97 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t97 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t97 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t97 + 16), t96);
                        gm_ttk92ufj2f6eh t98;
                        memset(&t98, 0, sizeof t98);
                        ((uint8_t *)&t98)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t98 + 16), t97);
                        memmove(gm_out, ((uint8_t *)&t98), 112);
                        goto gm_fdone;
                    }
                    void * t99 = gm_l0;
                    void * t100 = gm_l0;
                    unsigned __int128 t101 = gm_l11;
                    void * t102 = gm_l6;
                    gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t102));
                    gm_tbci5ospk5nnn t104;
                    gm_f_big_of_1cq0qao((void *)&t104, (void *)NULL, cx, NULL, t100, t101, t103);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t105;
                    gm_f_neg_agd5c5k((void *)&t105, (void *)NULL, cx, NULL, &t104);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t106 = gm_l7;
                    gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t106));
                    gm_b16_8 t108;
                    GM_ST(void *, (uint8_t *)&t108, (void *)(gm_skolmnfc0ccbd));
                    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(1));
                    void * t109 = gm_l7;
                    gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 72));
                    gm_t9qkh0rsooc8d t111;
                    GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)0ULL));
                    gm_f_fit_8hvi4bv((void *)&gm_l16, (void *)NULL, cx, NULL, t99, &t105, t111, t107, t108, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                    if (t112) {
                    } else {
                        gm_t39uai1ajk3qv t113 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&gm_l16) + 16));
                        gm_ttk92ufj2f6eh t114;
                        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t114 + 8, 0);
                        ((uint8_t *)&t114)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t114 + 16), t113);
                        memmove(gm_out, ((uint8_t *)&t114), 112);
                        goto gm_fdone;
                    }
                    unsigned __int128 t115 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l16) + 16));
                    gm_t6evimt84lndq t116;
                    GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t116 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t116 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t116 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t116 + 16), t115);
                    gm_ttk92ufj2f6eh t117;
                    memset(&t117, 0, sizeof t117);
                    ((uint8_t *)&t117)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t117 + 16), t116);
                    memmove(gm_out, ((uint8_t *)&t117), 112);
                    goto gm_fdone;
                }
gm_lb11:;
                {
                    void * t118 = gm_l0;
                    void * t119 = gm_l7;
                    gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t119) + 72));
                    gm_b16_8 t121;
                    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sqe4aodms9tfs));
                    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(13));
                    gm_t39uai1ajk3qv t122;
                    gm_f_unsupported_2seq49l((void *)&t122, t118, GM_CONV(gm_cseahvg8v7p0vs, t120), t121);
                    gm_ttk92ufj2f6eh t123;
                    GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t123 + 8, 0);
                    ((uint8_t *)&t123)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t123 + 16), t122);
                    memmove(gm_out, ((uint8_t *)&t123), 112);
                    goto gm_fdone;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 95, 17);
            }
gm_lb9:;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 86, 5);
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
inline __attribute__((aligned(4))) void gm_f_variant_tag_4pbamin(void *gm_out, void * a0, gm_csguankko4lmhd a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_variant_tag_4pbamin F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l5;
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, ((uint8_t *)t6));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)17ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            gm_l3 = t12;
            {
                {
                    void * t13 = gm_l0;
                    void * t14 = GM_LD(void *, ((uint8_t *)t13));
                    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 1088));
                    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 1088) + 8));
                    uint32_t t17 = gm_l3;
                    uint64_t t18 = (uint64_t)t17;
                    if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srcue4ionbe9lgh, 833, 28);
                    uint8_t t19 = GM_LD(uint8_t, (((((uint8_t *)t15) + (uintptr_t)t18 * 184) + 24) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t21 = GM_LD(gm_b16_8, ((((((uint8_t *)t15) + (uintptr_t)t18 * 184) + 24) + 8) + 8));
                    gm_l4 = t21;
                    uint32_t t22 = gm_l2;
                    uint64_t t23 = (uint64_t)t22;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t25 = (t23 >= t24);
                    if (t25) {
                        gm_thrdpodrs8pgt t26;
                        memset(&t26, 0, sizeof t26);
                        memmove(gm_out, ((uint8_t *)&t26), 56);
                        goto gm_fdone;
                    }
                    void * t27 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint32_t t29 = gm_l2;
                    uint64_t t30 = (uint64_t)t29;
                    if ((uintptr_t)t30 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t30, t28, gm_srcue4ionbe9lgh, 835, 12);
                    gm_tbci5ospk5nnn t31 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t27) + (uintptr_t)t30 * 72) + 24));
                    gm_thrdpodrs8pgt t32;
                    GM_ST(uint8_t, (uint8_t *)&t32 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t32 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t32 + 52, 0);
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t32), t31);
                    ((uint8_t *)&t32)[48] = 1;
                    memmove(gm_out, ((uint8_t *)&t32), 56);
                    goto gm_fdone;
                }
gm_lb4:;
                gm_thrdpodrs8pgt t33;
                memset(&t33, 0, sizeof t33);
                memmove(gm_out, ((uint8_t *)&t33), 56);
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_thrdpodrs8pgt t34;
        memset(&t34, 0, sizeof t34);
        memmove(gm_out, ((uint8_t *)&t34), 56);
        goto gm_fdone;
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_thrdpodrs8pgt gm_f_variant_tag_4pbamin_c(void * a0, gm_csguankko4lmhd a1, uint32_t a2) { gm_thrdpodrs8pgt gm_r; gm_f_variant_tag_4pbamin(&gm_r, a0, a1, a2); return gm_r; }
extern __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
