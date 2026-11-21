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
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { uint64_t f0; uint64_t f1; gm_t2lqu8jsj511d f2; uint64_t f3; uint64_t f4; } gm_t1hfnlc9mhrjq;
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint64_t f0; uint64_t f1; gm_t9qkh0rsooc8d f2; uint8_t f3; uint8_t pe[6]; } gm_tilak255t5m34;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
_Static_assert(sizeof(gm_t1hfnlc9mhrjq) == 48 && _Alignof(gm_t1hfnlc9mhrjq) == 8 && offsetof(gm_t1hfnlc9mhrjq, f0) == 0 && offsetof(gm_t1hfnlc9mhrjq, f1) == 8 && offsetof(gm_t1hfnlc9mhrjq, f2) == 16 && offsetof(gm_t1hfnlc9mhrjq, f3) == 32 && offsetof(gm_t1hfnlc9mhrjq, f4) == 40, "layout of `AllocTag`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tilak255t5m34) == 24 && _Alignof(gm_tilak255t5m34) == 8 && offsetof(gm_tilak255t5m34, f0) == 0 && offsetof(gm_tilak255t5m34, f1) == 8 && offsetof(gm_tilak255t5m34, f2) == 16 && offsetof(gm_tilak255t5m34, f3) == 17, "layout of `(base: u64, size: u64, kind: AllocKind, live: bool)`");
typedef struct { gm_t1hfnlc9mhrjq f0; uint8_t f1; uint8_t pe[7]; } gm_tm7c53s68971m;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
typedef struct { gm_tilak255t5m34 f0; uint8_t f1; uint8_t pe[7]; } gm_tqvpud5umqqp1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; uint64_t v1; } f1; } gm_t3j6egd6cfnk5;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tm7c53s68971m) == 56 && _Alignof(gm_tm7c53s68971m) == 8 && offsetof(gm_tm7c53s68971m, f0) == 0 && offsetof(gm_tm7c53s68971m, f1) == 48, "layout of `?AllocTag`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqvpud5umqqp1) == 32 && _Alignof(gm_tqvpud5umqqp1) == 8 && offsetof(gm_tqvpud5umqqp1, f0) == 0 && offsetof(gm_tqvpud5umqqp1, f1) == 24, "layout of `?(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_t3j6egd6cfnk5) == 48 && _Alignof(gm_t3j6egd6cfnk5) == 8 && offsetof(gm_t3j6egd6cfnk5, f0) == 0 && offsetof(gm_t3j6egd6cfnk5, f1) == 8, "layout of `MemError!u64`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_37g0gks {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bv13gdl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e7344u2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jc3ftg7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p0ej7hu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u3ioj90 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_unbcnc5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_memory_builtin_9s47dk8 {
    gm_frame h;
    gm_t1hfnlc9mhrjq l22; /* tag */
    gm_tpekhfs32op0f l39; /* e */
    gm_tpekhfs32op0f l42; /* e */
    gm_tpekhfs32op0f l45; /* e */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_tag_site_msl07u1 {
    gm_frame h;
    gm_tpekhfs32op0f l5; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_e7344u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fja4j2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_p0ej7hu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q33vns6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_u3ioj90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_unbcnc5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_get_hd4d6j0(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_64ej4om(void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_remove_gjirbtf(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_at_ktabiji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_tag_site_msl07u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t2lqu8jsj511d a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_ckh5e429gka[8] __attribute__((aligned(8)));
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1gjt02j7um1i[37];
extern const uint8_t gm_s2qp8mqi0c2le[48];
extern const uint8_t gm_s76i8mdsri6em[70];
extern const uint8_t gm_s7j165k7b6shl[32];
extern const uint8_t gm_s8pst1ovjhtbk[71];
extern const uint8_t gm_sdv3b0kjlrdl2[46];
extern const uint8_t gm_sgooul7ab8sa1[65];
extern const uint8_t gm_sm884rqp4fp7u[68];
extern const uint8_t gm_so94vinsu4uib[50];
extern const uint8_t gm_sp0ag7curvl4o[42];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srccp2mbkjs1vn4[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const gm_tmpl gm_tpl4bpbmadpmu1i;
extern const gm_tmpl gm_tpl5prdhjdrg194;
extern const gm_tmpl gm_tplcein5d5pq2cs;
extern const gm_tmpl gm_tple7ukme0qchh6;
extern const gm_tmpl gm_tplf3e41f3siie3;
extern const gm_tmpl gm_tpllhk8pmjfso1j;
extern const gm_tmpl gm_tplrgo7romk0g20;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint8_t t3 = (t2 <= ((uint64_t)1ULL));
    if (t3) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l1;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
    uint64_t t8 = r6;
    uint64_t t9 = (uint64_t)(t4 & t8);
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        uint64_t t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)(t11 & t15);
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l0;
    uint64_t t19 = gm_l1;
    if (t19 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
    uint64_t t20 = (uint64_t)(t18 % t19);
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t2) + 88));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t5 = (uint8_t)((t4 == ((uint8_t)3ULL)));
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)3ULL));
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l0));
    gm_l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = gm_l3;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l4;
                        gm_l5 = t12;
                        void * t13 = gm_l2;
                        uint64_t t14 = gm_l5;
                        uint8_t t15 = gm_l1;
                        GM_ST(uint8_t, (((uint8_t *)t13) + (uintptr_t)t14 * 1), t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l4;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l4 = t19;
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
inline __attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_37g0gks F;
    gm_tgbgti82mf879 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so94vinsu4uib));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(49));
    gm_tgbgti82mf879 t3 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
    gm_f_format_fja4j2u((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllhk8pmjfso1j), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bv13gdl F;
    gm_tgbgti82mf879 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukbpodfceoe1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tgbgti82mf879 t3 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
    gm_f_format_fja4j2u((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4bpbmadpmu1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_e7344u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_e7344u2 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s76i8mdsri6em));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5prdhjdrg194), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_jc3ftg7 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdv3b0kjlrdl2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrgo7romk0g20), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p0ej7hu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p0ej7hu F;
    gm_tkpb4kplftgaa gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tkpb4kplftgaa gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgooul7ab8sa1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_tkpb4kplftgaa t3 = GM_LD(gm_tkpb4kplftgaa, ((uint8_t *)a0));
    gm_f_format_q33vns6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplf3e41f3siie3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_u3ioj90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_u3ioj90 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7j165k7b6shl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple7ukme0qchh6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_unbcnc5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_unbcnc5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm884rqp4fp7u));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcein5d5pq2cs), t2);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 104, 68);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t2, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 106, 67);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_l2;
    gm_f_put_u64_irp77r2(t2, t11, t12);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_l2;
        gm_f_wr_word_npte9fs(t4, t5, t6);
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        uint64_t t9 = gm_l2;
        unsigned __int128 t10 = (unsigned __int128)t9;
        gm_f_put_uint_cddin99(t7, t8, t10);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
            ((uint8_t *)&t11)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
            memmove(gm_out, ((uint8_t *)&t11), 48);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t12 = gm_l1;
    uint64_t t13 = gm_l3;
    uint8_t t14 = gm_f_aligned_q6sor4h(t12, t13);
    if (t14) {
        {
            {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l1;
                uint64_t t17 = gm_l2;
                void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
                gm_l5 = t18;
                uint8_t t19 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t19) {
                    goto gm_lb4;
                }
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t20;
                void * t21 = gm_l4;
                uint64_t t22 = gm_l2;
                uint64_t t23 = gm_f_us_cuv01fp(t22);
                uint64_t t24 = ((uintptr_t)t23) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
                gm_t934k1j7u70ct t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 40, 0);
                ((uint8_t *)&t26)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
                memmove(gm_out, ((uint8_t *)&t26), 48);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    void * t27 = gm_l0;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_l2;
    uint64_t t30 = gm_l3;
    gm_tpekhfs32op0f t31;
    gm_f_fault_u31b92l((void *)&t31, t27, t28, t29, t30, ((uint8_t)0));
    gm_t934k1j7u70ct t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
    ((uint8_t *)&t32)[0] = 1;
    GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t32 + 8), t31);
    memmove(gm_out, ((uint8_t *)&t32), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 360);
    gm_l2 = t3;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    void * t7 = gm_l2;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 40));
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srcpit8mdn0781q, 387, 14);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)t8 * 16));
    gm_l3 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t11 = (uintptr_t)t10;
    gm_l4 = t11;
    uint64_t t12 = gm_l4;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
    uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t20 = r18;
    uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
    uint64_t t22 = (uint64_t)~t21;
    uint64_t t23 = (uint64_t)(t20 & t22);
    uint64_t t24 = gm_l4;
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 391, 12);
    uint64_t t27 = r25;
    gm_l5 = t27;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_f_us_cuv01fp(t28);
    gm_l6 = t29;
    uint64_t t30 = gm_l1;
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l6;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 18);
        uint64_t t36 = r34;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t38 = (t36 > t37);
        gm_l8 = t38;
    }
    uint8_t t39 = gm_l8;
    if (t39) {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t42;
        goto gm_fret;
    }
    void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t45 = gm_l5;
    uint64_t t46 = gm_l5;
    uint64_t t47 = gm_l6;
    uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 21);
    uint64_t t50 = r48;
    uint64_t t51 = (uintptr_t)t45;
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t44) gm_trap_slice(t51, t52, t44, gm_srcpit8mdn0781q, 394, 10);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t45;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t43 + (uintptr_t)t45 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l7 = t54;
    void * t55 = gm_l2;
    uint64_t t56 = gm_l5;
    uint64_t t57 = gm_l6;
    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
    if (o59) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 395, 14);
    uint64_t t60 = r58;
    GM_ST(uint64_t, (((uint8_t *)t55) + 48), t60);
    gm_b16_8 t61 = gm_l7;
    gm_f_fill_abmf5g5(t61, ((uint8_t)0ULL));
    gm_b16_8 t62 = gm_l7;
    F.ret = t62;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 538u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    uint64_t t7 = r5;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n9 = (unsigned __int128)t8;
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
    uint64_t t10 = (uint64_t)(t7 >> (int)n9);
    gm_l4 = t10;
    uint64_t t11 = gm_l4;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    uint64_t t14 = (uint64_t)t13;
    uint8_t t15 = (t11 >= t14);
    if (t15) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = gm_l4;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 32);
    gm_l5 = t20;
    void * t21 = gm_l5;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)t21)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)t21));
        gm_l11 = t23;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t24 = gm_l11;
    gm_l6 = t24;
    uint64_t t25 = gm_l1;
    void * t26 = gm_l5;
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 8));
    uint64_t r28; bool o29 = __builtin_sub_overflow(t25, t27, &r28);
    if (o29) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
    uint64_t t30 = r28;
    gm_l7 = t30;
    uint64_t t31 = gm_l7;
    void * t32 = gm_l5;
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 16));
    unsigned __int128 n34 = (unsigned __int128)t33;
    if (n34 >= 64) gm_trap_shift((uint64_t)n34, "u64", gm_srcq7ohh8h77bn7, 211, 10);
    uint64_t t35 = (uint64_t)(t31 >> (int)n34);
    gm_l8 = t35;
    void * t36 = gm_l6;
    uint64_t t37 = gm_l8;
    uint64_t t38 = gm_f_us_cuv01fp(t37);
    void * t39 = (void *)(((uint8_t *)t36) + (uintptr_t)t38 * 24);
    gm_l9 = t39;
    uint64_t t40 = gm_l7;
    uint64_t t41 = gm_l8;
    void * t42 = gm_l5;
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 16));
    unsigned __int128 n44 = (unsigned __int128)t43;
    if (n44 >= 64) gm_trap_shift((uint64_t)n44, "u64", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t r45 = (uint64_t)((uint64_t)t41 << (int)n44);
    bool o46 = (uint64_t)(r45 >> (int)n44) != t41;
    if (o46) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t t47 = r45;
    uint64_t r48; bool o49 = __builtin_sub_overflow(t40, t47, &r48);
    if (o49) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
    uint64_t t50 = r48;
    gm_l10 = t50;
    void * t51 = gm_l9;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)t51) + 17));
    uint8_t t53 = (uint8_t)!t52;
    if (t53) {
        gm_l14 = ((uint8_t)1);
    } else {
        uint64_t t54 = gm_l2;
        void * t55 = gm_l9;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 8));
        uint8_t t57 = (t54 > t56);
        gm_l14 = t57;
    }
    uint8_t t58 = gm_l14;
    if (t58) {
        gm_l13 = ((uint8_t)1);
    } else {
        uint64_t t59 = gm_l10;
        void * t60 = gm_l9;
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 8));
        uint64_t t62 = gm_l2;
        uint64_t r63; bool o64 = __builtin_sub_overflow(t61, t62, &r63);
        if (o64) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
        uint64_t t65 = r63;
        uint8_t t66 = (t59 > t65);
        gm_l13 = t66;
    }
    uint8_t t67 = gm_l13;
    if (t67) {
        gm_l12 = ((uint8_t)1);
    } else {
        uint8_t t68 = gm_l3;
        if (t68) {
            void * t69 = gm_l9;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t69) + 18));
            uint8_t t71 = (uint8_t)!t70;
            gm_l15 = t71;
        } else {
            gm_l15 = ((uint8_t)0);
        }
        uint8_t t72 = gm_l15;
        gm_l12 = t72;
    }
    uint8_t t73 = gm_l12;
    if (t73) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t74 = gm_l9;
    void * t75 = GM_LD(void *, ((uint8_t *)t74));
    uint64_t t76 = gm_l10;
    uint64_t t77 = gm_f_us_cuv01fp(t76);
    void * t78 = (void *)(((uint8_t *)t75) + (uintptr_t)t77 * 1);
    void * t79 = t78;
    F.ret = t79;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 329, 13);
            uint64_t t8 = gm_l2;
            uint8_t t9 = (uint8_t)t8;
            GM_ST(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1), t9);
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 330, 13);
            uint64_t t12 = gm_l2;
            unsigned __int128 n13 = (unsigned __int128)((uint64_t)8ULL);
            if (n13 >= 64) gm_trap_shift((uint64_t)n13, "u64", gm_srcnqtpfma6qe4a, 330, 21);
            uint64_t t14 = (uint64_t)(t12 >> (int)n13);
            uint8_t t15 = (uint8_t)t14;
            GM_ST(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1), t15);
            void * t16 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t17, gm_srcnqtpfma6qe4a, 331, 13);
            uint64_t t18 = gm_l2;
            unsigned __int128 n19 = (unsigned __int128)((uint64_t)16ULL);
            if (n19 >= 64) gm_trap_shift((uint64_t)n19, "u64", gm_srcnqtpfma6qe4a, 331, 21);
            uint64_t t20 = (uint64_t)(t18 >> (int)n19);
            uint8_t t21 = (uint8_t)t20;
            GM_ST(uint8_t, (((uint8_t *)t16) + (uintptr_t)((uint64_t)2ULL) * 1), t21);
            void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t23, gm_srcnqtpfma6qe4a, 332, 13);
            uint64_t t24 = gm_l2;
            unsigned __int128 n25 = (unsigned __int128)((uint64_t)24ULL);
            if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_srcnqtpfma6qe4a, 332, 21);
            uint64_t t26 = (uint64_t)(t24 >> (int)n25);
            uint8_t t27 = (uint8_t)t26;
            GM_ST(uint8_t, (((uint8_t *)t22) + (uintptr_t)((uint64_t)3ULL) * 1), t27);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t29, gm_srcnqtpfma6qe4a, 333, 13);
            uint64_t t30 = gm_l2;
            unsigned __int128 n31 = (unsigned __int128)((uint64_t)32ULL);
            if (n31 >= 64) gm_trap_shift((uint64_t)n31, "u64", gm_srcnqtpfma6qe4a, 333, 21);
            uint64_t t32 = (uint64_t)(t30 >> (int)n31);
            uint8_t t33 = (uint8_t)t32;
            GM_ST(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)4ULL) * 1), t33);
            void * t34 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t35, gm_srcnqtpfma6qe4a, 334, 13);
            uint64_t t36 = gm_l2;
            unsigned __int128 n37 = (unsigned __int128)((uint64_t)40ULL);
            if (n37 >= 64) gm_trap_shift((uint64_t)n37, "u64", gm_srcnqtpfma6qe4a, 334, 21);
            uint64_t t38 = (uint64_t)(t36 >> (int)n37);
            uint8_t t39 = (uint8_t)t38;
            GM_ST(uint8_t, (((uint8_t *)t34) + (uintptr_t)((uint64_t)5ULL) * 1), t39);
            void * t40 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t41, gm_srcnqtpfma6qe4a, 335, 13);
            uint64_t t42 = gm_l2;
            unsigned __int128 n43 = (unsigned __int128)((uint64_t)48ULL);
            if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srcnqtpfma6qe4a, 335, 21);
            uint64_t t44 = (uint64_t)(t42 >> (int)n43);
            uint8_t t45 = (uint8_t)t44;
            GM_ST(uint8_t, (((uint8_t *)t40) + (uintptr_t)((uint64_t)6ULL) * 1), t45);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t47, gm_srcnqtpfma6qe4a, 336, 13);
            uint64_t t48 = gm_l2;
            unsigned __int128 n49 = (unsigned __int128)((uint64_t)56ULL);
            if (n49 >= 64) gm_trap_shift((uint64_t)n49, "u64", gm_srcnqtpfma6qe4a, 336, 21);
            uint64_t t50 = (uint64_t)(t48 >> (int)n49);
            uint8_t t51 = (uint8_t)t50;
            GM_ST(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)7ULL) * 1), t51);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t53 = ((t52 == ((uint64_t)1ULL)));
        if (t53) {
            void * t54 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t55, gm_srcnqtpfma6qe4a, 340, 13);
            uint64_t t56 = gm_l2;
            uint8_t t57 = (uint8_t)t56;
            GM_ST(uint8_t, (((uint8_t *)t54) + (uintptr_t)((uint64_t)0ULL) * 1), t57);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t59 = ((t58 == ((uint64_t)4ULL)));
        if (t59) {
            void * t60 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t61, gm_srcnqtpfma6qe4a, 344, 13);
            uint64_t t62 = gm_l2;
            uint8_t t63 = (uint8_t)t62;
            GM_ST(uint8_t, (((uint8_t *)t60) + (uintptr_t)((uint64_t)0ULL) * 1), t63);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t65, gm_srcnqtpfma6qe4a, 345, 13);
            uint64_t t66 = gm_l2;
            unsigned __int128 n67 = (unsigned __int128)((uint64_t)8ULL);
            if (n67 >= 64) gm_trap_shift((uint64_t)n67, "u64", gm_srcnqtpfma6qe4a, 345, 21);
            uint64_t t68 = (uint64_t)(t66 >> (int)n67);
            uint8_t t69 = (uint8_t)t68;
            GM_ST(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)1ULL) * 1), t69);
            void * t70 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t71, gm_srcnqtpfma6qe4a, 346, 13);
            uint64_t t72 = gm_l2;
            unsigned __int128 n73 = (unsigned __int128)((uint64_t)16ULL);
            if (n73 >= 64) gm_trap_shift((uint64_t)n73, "u64", gm_srcnqtpfma6qe4a, 346, 21);
            uint64_t t74 = (uint64_t)(t72 >> (int)n73);
            uint8_t t75 = (uint8_t)t74;
            GM_ST(uint8_t, (((uint8_t *)t70) + (uintptr_t)((uint64_t)2ULL) * 1), t75);
            void * t76 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t77, gm_srcnqtpfma6qe4a, 347, 13);
            uint64_t t78 = gm_l2;
            unsigned __int128 n79 = (unsigned __int128)((uint64_t)24ULL);
            if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 347, 21);
            uint64_t t80 = (uint64_t)(t78 >> (int)n79);
            uint8_t t81 = (uint8_t)t80;
            GM_ST(uint8_t, (((uint8_t *)t76) + (uintptr_t)((uint64_t)3ULL) * 1), t81);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    void * t82 = gm_l0;
    gm_b16_8 t83 = gm_l1;
    uint64_t t84 = gm_l2;
    unsigned __int128 t85 = (unsigned __int128)t84;
    gm_f_put_uint_cddin99(t82, t83, t85);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
            ((uint8_t *)&t11)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
            memmove(gm_out, ((uint8_t *)&t11), 48);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t12 = gm_l1;
    uint64_t t13 = gm_l3;
    uint8_t t14 = gm_f_aligned_q6sor4h(t12, t13);
    if (t14) {
        {
            {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l1;
                uint64_t t17 = gm_l2;
                void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)1));
                gm_l5 = t18;
                uint8_t t19 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t19) {
                    goto gm_lb4;
                }
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t20;
                void * t21 = gm_l4;
                uint64_t t22 = gm_l2;
                uint64_t t23 = gm_f_us_cuv01fp(t22);
                uint64_t t24 = ((uintptr_t)t23) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
                gm_t934k1j7u70ct t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 40, 0);
                ((uint8_t *)&t26)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
                memmove(gm_out, ((uint8_t *)&t26), 48);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    void * t27 = gm_l0;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_l2;
    uint64_t t30 = gm_l3;
    gm_tpekhfs32op0f t31;
    gm_f_fault_u31b92l((void *)&t31, t27, t28, t29, t30, ((uint8_t)1));
    gm_t934k1j7u70ct t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
    ((uint8_t *)&t32)[0] = 1;
    GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t32 + 8), t31);
    memmove(gm_out, ((uint8_t *)&t32), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6) {
    struct gm_fr_memory_builtin_9s47dk8 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* dst */
    uint64_t gm_l7; /* size */
    uint64_t gm_l8; /* al */
    uint64_t gm_l9; /* addr */
    gm_b16_8 gm_l10; /* d */
    gm_t2doi72kmmlil gm_l11; /* pb */
    gm_b16_8 gm_l12; /* p */
    uint64_t gm_l13; /* p */
    uint64_t gm_l14; /* size */
    gm_tilak255t5m34 gm_l15; /* d */
    uint64_t gm_l16; /* p */
    uint64_t gm_l17; /* owner */
    uint64_t gm_l18; /* vt */
    gm_t2lqu8jsj511d gm_l19; /* file */
    uint64_t gm_l20; /* line */
    uint64_t gm_l21; /* column */
    gm_b16_8 gm_l23; /* at */
    gm_b16_8 gm_l24; /* here */
    uint64_t gm_l25; /* addr */
    gm_t88rcucg1b7uk gm_l26; /* ty */
    uint64_t gm_l27; /* al */
    gm_b16_8 gm_l28; /* old */
    gm_t9qkh0rsooc8d gm_l29; /* rmw */
    unsigned __int128 gm_l30; /* x */
    unsigned __int128 gm_l31; /* y */
    uint8_t gm_l32; /* signed */
    unsigned __int128 gm_l33; /* r */
    gm_b16_8 gm_l34; /* nb */
    gm_t88rcucg1b7uk gm_l35; /* elem */
    gm_t2lqu8jsj511d gm_l36; /* d */
    gm_t2lqu8jsj511d gm_l37; /* s */
    uint64_t gm_l38; /* n */
    gm_b16_8 gm_l40; /* src */
    gm_b16_8 gm_l41; /* tmp */
    uint64_t gm_l43; /* p */
    uint64_t gm_l44; /* n */
    gm_b16_8 gm_l46; /* b */
    gm_t88rcucg1b7uk gm_l47; /* vt */
    gm_t2lqu8jsj511d gm_l48; /* sp */
    uint64_t gm_l49; /* n */
    gm_t88rcucg1b7uk gm_l50; /* lane */
    gm_b16_8 gm_l51; /* what */
    uint64_t gm_l52; /* al */
    GM_STACK_CHECK(&F, 8806u);
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
    gm_tabb710d9mq2e gm_l53;
    gm_t3j6egd6cfnk5 gm_l54;
    uint64_t gm_l55;
    gm_t934k1j7u70ct gm_l56;
    gm_b16_8 gm_l57;
    gm_tqvpud5umqqp1 gm_l58;
    gm_tilak255t5m34 gm_l59;
    gm_tabb710d9mq2e gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_tabb710d9mq2e gm_l62;
    gm_tabb710d9mq2e gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tm7c53s68971m gm_l65;
    gm_t1hfnlc9mhrjq gm_l66;
    uint8_t gm_l67;
    gm_td0ept12e7q8b gm_l68;
    gm_td0ept12e7q8b gm_l69;
    gm_tabb710d9mq2e gm_l70;
    gm_t53at80pfdus3 gm_l71;
    gm_tcfa6l7reoosn gm_l72;
    gm_tcfa6l7reoosn gm_l73;
    gm_tcfa6l7reoosn gm_l74;
    gm_t1ec8br9ddkav gm_l75;
    gm_tcfa6l7reoosn gm_l76;
    gm_tg5lql6itufk7 gm_l77;
    gm_ttovhfee8mf28 gm_l78;
    gm_ttovhfee8mf28 gm_l79;
    unsigned __int128 gm_l80;
    unsigned __int128 gm_l81;
    uint8_t gm_l82;
    uint8_t gm_l83;
    uint8_t gm_l84;
    unsigned __int128 gm_l85;
    uint8_t gm_l86;
    uint8_t gm_l87;
    uint8_t gm_l88;
    gm_tcfa6l7reoosn gm_l89;
    gm_t53at80pfdus3 gm_l90;
    gm_t934k1j7u70ct gm_l91;
    gm_b16_8 gm_l92;
    gm_tgfdnufetdnfc gm_l93;
    gm_t934k1j7u70ct gm_l94;
    gm_b16_8 gm_l95;
    gm_t88rcucg1b7uk gm_l96;
    gm_b16_8 gm_l97;
    gm_t53at80pfdus3 gm_l98;
    gm_b16_8 gm_l99;
    gm_tabb710d9mq2e gm_l100;
    gm_tcfa6l7reoosn gm_l101;
    gm_tcfa6l7reoosn gm_l102;
    {
        {
            {
                {
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)74ULL));
                    if (!t67) {
                        goto gm_lb2;
                    }
                    void * t68 = gm_l0;
                    void * t69 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t70, gm_srccp2mbkjs1vn4, 12, 31);
                    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                    uint64_t t73 = gm_f_get_u64_tgpb2vh(t68, t72);
                    gm_l7 = t73;
                    void * t74 = gm_l0;
                    void * t75 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t76, gm_srccp2mbkjs1vn4, 13, 29);
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t75) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                    uint64_t t79 = gm_f_get_u64_tgpb2vh(t74, t78);
                    uint64_t t80 = (t79 > ((uint64_t)1ULL) ? t79 : ((uint64_t)1ULL));
                    gm_l8 = t80;
                    uint64_t t81 = gm_l8;
                    uint64_t t82 = gm_l8;
                    uint64_t r83; bool o84 = __builtin_sub_overflow(t82, ((uint64_t)1ULL), &r83);
                    if (o84) gm_trap("integer overflow in `-`", gm_srccp2mbkjs1vn4, 14, 22);
                    uint64_t t85 = r83;
                    uint64_t t86 = (uint64_t)(t81 & t85);
                    uint8_t t87 = (!(t86 == ((uint64_t)0ULL)));
                    if (t87) {
                        void * t88 = gm_l0;
                        gm_tl874t2atnlh3 t89 = gm_l1;
                        uint64_t t90 = gm_l8;
                        gm_t4ce0jd3tdro4 t91;
                        GM_ST(uint64_t, ((uint8_t *)&t91 + 0), t90);
                        gm_f_format_jc3ftg7((void *)&gm_l53, (void *)NULL, cx, NULL, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                        if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_srccp2mbkjs1vn4, 14, 62);
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                        gm_t76123gfet73g t95;
                        gm_f_trap_qh84l9d((void *)&t95, (void *)NULL, cx, NULL, t88, t89, t94);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t96;
                        GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                        ((uint8_t *)&t96)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t96 + 8), t95);
                        memmove(gm_out, ((uint8_t *)&t96), 32);
                        goto gm_fdone;
                    }
                    uint64_t t97 = gm_l8;
                    uint64_t t98 = GM_LD(uint64_t, ((uint8_t *)gm_ckh5e429gka));
                    uint8_t t99 = (t97 > t98);
                    if (t99) {
                        gm_b16_8 t100 = gm_l6;
                        gm_f_fill_abmf5g5(t100, ((uint8_t)0ULL));
                        gm_tcfa6l7reoosn t101;
                        GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t101 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t101 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t101 + 24, 0);
                        ((uint8_t *)&t101)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t101 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t101), 32);
                        goto gm_fdone;
                    }
                    void * t102 = gm_l0;
                    void * t103 = (void *)(((uint8_t *)t102) + 184);
                    uint64_t t104 = gm_l7;
                    uint64_t t105 = gm_l8;
                    gm_t9qkh0rsooc8d t106;
                    GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)2ULL));
                    gm_f_alloc_bfo8187((void *)&gm_l54, (void *)NULL, cx, NULL, t103, t104, t105, t106, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (t107) {
                        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l54) + 8));
                        gm_l55 = t108;
                    } else {
                        gm_b16_8 t109 = gm_l6;
                        gm_f_fill_abmf5g5(t109, ((uint8_t)0ULL));
                        gm_tcfa6l7reoosn t110;
                        GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t110 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t110 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t110 + 24, 0);
                        ((uint8_t *)&t110)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t110 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t110), 32);
                        goto gm_fdone;
                    }
                    uint64_t t111 = gm_l55;
                    gm_l9 = t111;
                    {
                        {
                            void * t112 = gm_l0;
                            gm_t9qkh0rsooc8d t113 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t112) + 88));
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&t113) + 0));
                            uint8_t t115 = (uint8_t)((t114 == ((uint8_t)0ULL)));
                            if (!t115) {
                                goto gm_lb4;
                            }
                            uint64_t t116 = gm_l7;
                            uint8_t t117 = (t116 > ((uint64_t)0ULL));
                            if (!t117) {
                                goto gm_lb4;
                            }
                            void * t118 = gm_l0;
                            void * t119 = (void *)(((uint8_t *)t118) + 184);
                            uint64_t t120 = gm_l9;
                            uint64_t t121 = gm_l7;
                            gm_t9qkh0rsooc8d t122;
                            GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)0ULL));
                            gm_f_write_view_pebit6h((void *)&gm_l56, t119, t120, t121, ((uint64_t)1ULL));
                            uint8_t t123 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                            if (t123) {
                                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                                gm_l57 = t124;
                            } else {
                                gm_tcfa6l7reoosn t125;
                                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t125 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t125 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
                                ((uint8_t *)&t125)[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&t125 + 8), GM_ZERO(gm_b0_1));
                                memmove(gm_out, ((uint8_t *)&t125), 32);
                                goto gm_fdone;
                            }
                            gm_b16_8 t126 = gm_l57;
                            gm_l10 = t126;
                            gm_b16_8 t127 = gm_l10;
                            gm_f_fill_abmf5g5(t127, ((uint8_t)170ULL));
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    gm_t2doi72kmmlil t128;
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 1), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 2), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 3), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 4), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 5), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 6), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t128 + 7), ((uint8_t)0ULL));
                    gm_l11 = t128;
                    void * t129 = (void *)((uint8_t *)&gm_l11);
                    void * t130 = gm_l0;
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)t130) + 176));
                    uint64_t t132 = gm_f_us_cuv01fp(t131);
                    uint64_t t133 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t134 = (uintptr_t)t132;
                    if (t133 > t134 || t134 > (uintptr_t)((uint64_t)8ULL)) gm_trap_slice(t133, t134, ((uint64_t)8ULL), gm_srccp2mbkjs1vn4, 32, 18);
                    uint64_t t135 = ((uintptr_t)t132) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t136;
                    GM_ST(void *, (uint8_t *)&t136, (void *)(((uint8_t *)t129 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(t135));
                    gm_l12 = t136;
                    void * t137 = gm_l0;
                    gm_b16_8 t138 = gm_l12;
                    uint64_t t139 = gm_l9;
                    gm_f_put_ptr_188p3mm(t137, t138, t139);
                    void * t140 = gm_l0;
                    gm_t88rcucg1b7uk t141 = gm_l5;
                    gm_b16_8 t142 = gm_l6;
                    gm_b16_8 t143 = gm_l12;
                    gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                    gm_b0_1 t145 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t140, t141, t142, t144);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t147 = (uint8_t)(t146 == ((uint8_t)75ULL));
                    if (!t147) {
                        goto gm_lb5;
                    }
                    void * t148 = gm_l0;
                    void * t149 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t150)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t150, gm_srccp2mbkjs1vn4, 37, 27);
                    gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)t149) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                    uint64_t t153 = gm_f_ptr_of_b77b4c9(t148, t152);
                    gm_l13 = t153;
                    void * t154 = gm_l0;
                    void * t155 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t156)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t156, gm_srccp2mbkjs1vn4, 38, 31);
                    gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)t155) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                    uint64_t t159 = gm_f_get_u64_tgpb2vh(t154, t158);
                    gm_l14 = t159;
                    void * t160 = gm_l0;
                    void * t161 = (void *)(((uint8_t *)t160) + 184);
                    uint64_t t162 = gm_l13;
                    gm_f_describe_t62jhnt((void *)&gm_l58, t161, t162);
                    uint8_t t163 = (uint8_t)((((uint8_t *)&gm_l58))[24] != 0);
                    if (t163) {
                        memmove(((uint8_t *)&gm_l59), ((uint8_t *)&gm_l58), 24);
                    } else {
                        void * t164 = gm_l0;
                        gm_tl874t2atnlh3 t165 = gm_l1;
                        uint64_t t166 = gm_l13;
                        uint64_t t167 = gm_l14;
                        gm_t2lqu8jsj511d t168;
                        GM_ST(uint64_t, ((uint8_t *)&t168 + 0), t166);
                        GM_ST(uint64_t, ((uint8_t *)&t168 + 8), t167);
                        gm_f_format_e7344u2((void *)&gm_l60, (void *)NULL, cx, NULL, t168);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t169 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                        if (!t169) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_srccp2mbkjs1vn4, 39, 62);
                        gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                        gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
                        gm_t76123gfet73g t172;
                        gm_f_trap_qh84l9d((void *)&t172, (void *)NULL, cx, NULL, t164, t165, t171);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t173;
                        GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
                        ((uint8_t *)&t173)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t173 + 8), t172);
                        memmove(gm_out, ((uint8_t *)&t173), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l15), ((uint8_t *)&gm_l59), 24);
                    {
                        {
                            gm_t9qkh0rsooc8d t174 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l15) + 16));
                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&t174) + 0));
                            uint8_t t176 = (uint8_t)((t175 == ((uint8_t)2ULL)));
                            if (!t176) {
                                goto gm_lb7;
                            }
                            uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                            if (!t177) {
                                goto gm_lb7;
                            }
                            uint64_t t178 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            uint64_t t179 = gm_l13;
                            uint8_t t180 = ((t178 == t179));
                            if (!t180) {
                                goto gm_lb7;
                            }
                            uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t182 = gm_l14;
                            uint8_t t183 = ((t181 == t182));
                            if (!t183) {
                                goto gm_lb7;
                            }
                            void * t184 = gm_l0;
                            void * t185 = (void *)(((uint8_t *)t184) + 184);
                            uint64_t t186 = gm_l13;
                            gm_t9qkh0rsooc8d t187;
                            GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)2ULL));
                            uint8_t t188 = gm_f_kill_3kcavfd((void *)NULL, cx, NULL, t185, t186);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tcfa6l7reoosn t189;
                            GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t189 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t189 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t189 + 24, 0);
                            ((uint8_t *)&t189)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t189 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t189), 32);
                            goto gm_fdone;
                        }
gm_lb7:;
                    }
gm_lb6:;
                    {
                        {
                            gm_t9qkh0rsooc8d t190 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l15) + 16));
                            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&t190) + 0));
                            uint8_t t192 = (uint8_t)((t191 == ((uint8_t)2ULL)));
                            if (!t192) {
                                goto gm_lb9;
                            }
                            uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                            uint8_t t194 = (uint8_t)!t193;
                            if (!t194) {
                                goto gm_lb9;
                            }
                            void * t195 = gm_l0;
                            gm_tl874t2atnlh3 t196 = gm_l1;
                            uint64_t t197 = gm_l13;
                            gm_t4ce0jd3tdro4 t198;
                            GM_ST(uint64_t, ((uint8_t *)&t198 + 0), t197);
                            gm_t9qkh0rsooc8d t199;
                            GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)2ULL));
                            gm_f_format_u3ioj90((void *)&gm_l61, (void *)NULL, cx, NULL, t198);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                            if (!t200) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_srccp2mbkjs1vn4, 44, 70);
                            gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                            gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                            gm_t76123gfet73g t203;
                            gm_f_trap_qh84l9d((void *)&t203, (void *)NULL, cx, NULL, t195, t196, t202);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tcfa6l7reoosn t204;
                            GM_ST(uint8_t, (uint8_t *)&t204 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
                            ((uint8_t *)&t204)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t204 + 8), t203);
                            memmove(gm_out, ((uint8_t *)&t204), 32);
                            goto gm_fdone;
                        }
gm_lb9:;
                    }
gm_lb8:;
                    {
                        {
                            gm_t9qkh0rsooc8d t205 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l15) + 16));
                            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&t205) + 0));
                            uint8_t t207 = (uint8_t)((t206 == ((uint8_t)2ULL)));
                            if (!t207) {
                                goto gm_lb11;
                            }
                            uint64_t t208 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            uint64_t t209 = gm_l13;
                            uint8_t t210 = ((t208 == t209));
                            if (!t210) {
                                goto gm_lb11;
                            }
                            void * t211 = gm_l0;
                            gm_tl874t2atnlh3 t212 = gm_l1;
                            uint64_t t213 = gm_l13;
                            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t215 = gm_l14;
                            gm_tkpb4kplftgaa t216;
                            GM_ST(uint64_t, ((uint8_t *)&t216 + 0), t213);
                            GM_ST(uint64_t, ((uint8_t *)&t216 + 8), t214);
                            GM_ST(uint64_t, ((uint8_t *)&t216 + 16), t215);
                            gm_t9qkh0rsooc8d t217;
                            GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)2ULL));
                            gm_f_format_p0ej7hu((void *)&gm_l62, (void *)NULL, cx, NULL, &t216);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t218 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                            if (!t218) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_srccp2mbkjs1vn4, 46, 71);
                            gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                            gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                            gm_t76123gfet73g t221;
                            gm_f_trap_qh84l9d((void *)&t221, (void *)NULL, cx, NULL, t211, t212, t220);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tcfa6l7reoosn t222;
                            GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                            ((uint8_t *)&t222)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t222 + 8), t221);
                            memmove(gm_out, ((uint8_t *)&t222), 32);
                            goto gm_fdone;
                        }
gm_lb11:;
                    }
gm_lb10:;
                    void * t223 = gm_l0;
                    gm_tl874t2atnlh3 t224 = gm_l1;
                    uint64_t t225 = gm_l13;
                    uint64_t t226 = gm_l14;
                    gm_t2lqu8jsj511d t227;
                    GM_ST(uint64_t, ((uint8_t *)&t227 + 0), t225);
                    GM_ST(uint64_t, ((uint8_t *)&t227 + 8), t226);
                    gm_f_format_e7344u2((void *)&gm_l63, (void *)NULL, cx, NULL, t227);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                    if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_srccp2mbkjs1vn4, 47, 34);
                    gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                    gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                    gm_t76123gfet73g t231;
                    gm_f_trap_qh84l9d((void *)&t231, (void *)NULL, cx, NULL, t223, t224, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t232;
                    GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
                    ((uint8_t *)&t232)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t232 + 8), t231);
                    memmove(gm_out, ((uint8_t *)&t232), 32);
                    goto gm_fdone;
                }
gm_lb5:;
            }
            {
                {
                    {
                        {
                            uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t234 = (uint8_t)(t233 == ((uint8_t)76ULL));
                            if (!t234) {
                                goto gm_lb14;
                            }
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t236 = (uint8_t)(t235 == ((uint8_t)77ULL));
                            if (!t236) {
                                goto gm_lb15;
                            }
                            goto gm_lb13;
                        }
gm_lb15:;
                        goto gm_lb12;
                    }
gm_lb13:;
                    void * t237 = gm_l0;
                    gm_t9qkh0rsooc8d t238 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t237) + 88));
                    uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&t238) + 0));
                    uint8_t t240 = (uint8_t)(!(t239 == ((uint8_t)0ULL)));
                    gm_t9qkh0rsooc8d t241;
                    GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)0ULL));
                    if (t240) {
                        gm_tcfa6l7reoosn t242;
                        GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t242 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t242 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t242 + 24, 0);
                        ((uint8_t *)&t242)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t242 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t242), 32);
                        goto gm_fdone;
                    }
                    void * t243 = gm_l0;
                    void * t244 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t245 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t245)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t245, gm_srccp2mbkjs1vn4, 51, 27);
                    gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)t244) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
                    uint64_t t248 = gm_f_ptr_of_b77b4c9(t243, t247);
                    gm_l16 = t248;
                    void * t249 = gm_l0;
                    void * t250 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t251)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t251, gm_srccp2mbkjs1vn4, 52, 31);
                    gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)t250) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
                    uint64_t t254 = gm_f_ptr_of_b77b4c9(t249, t253);
                    gm_l17 = t254;
                    void * t255 = gm_l0;
                    void * t256 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t257)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t257, gm_srccp2mbkjs1vn4, 53, 28);
                    gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)t256) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
                    uint64_t t260 = gm_f_ptr_of_b77b4c9(t255, t259);
                    gm_l18 = t260;
                    void * t261 = gm_l0;
                    void * t262 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t263)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t263, gm_srccp2mbkjs1vn4, 54, 35);
                    gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)t262) + (uintptr_t)((uint64_t)3ULL) * 16));
                    gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                    gm_t2lqu8jsj511d t266 = gm_f_slice_parts_1se5hu4(t261, t265);
                    gm_l19 = t266;
                    void * t267 = gm_l0;
                    void * t268 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t269)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t269, gm_srccp2mbkjs1vn4, 55, 31);
                    gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)t268) + (uintptr_t)((uint64_t)4ULL) * 16));
                    gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
                    uint64_t t272 = gm_f_get_u64_tgpb2vh(t267, t271);
                    gm_l20 = t272;
                    void * t273 = gm_l0;
                    void * t274 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t275 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t275)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t275, gm_srccp2mbkjs1vn4, 56, 33);
                    gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)t274) + (uintptr_t)((uint64_t)5ULL) * 16));
                    gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                    uint64_t t278 = gm_f_get_u64_tgpb2vh(t273, t277);
                    gm_l21 = t278;
                    gm_tvdg94bk7aa22 t279 = gm_l2;
                    uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&t279) + 0));
                    uint8_t t281 = (uint8_t)((t280 == ((uint8_t)76ULL)));
                    gm_tvdg94bk7aa22 t282;
                    GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t282 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)76ULL));
                    if (t281) {
                        void * t283 = gm_l0;
                        void * t284 = (void *)(((uint8_t *)t283) + 1272);
                        uint64_t t285 = gm_l16;
                        uint64_t t286 = gm_l17;
                        uint64_t t287 = gm_l18;
                        uint64_t t288 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                        uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                        gm_t2lqu8jsj511d t290;
                        GM_ST(uint64_t, ((uint8_t *)&t290 + 0), t288);
                        GM_ST(uint64_t, ((uint8_t *)&t290 + 8), t289);
                        uint64_t t291 = gm_l20;
                        uint64_t t292 = gm_l21;
                        gm_t1hfnlc9mhrjq t293;
                        GM_ST(uint64_t, ((uint8_t *)&t293 + 0), t286);
                        GM_ST(uint64_t, ((uint8_t *)&t293 + 8), t287);
                        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t293 + 16), t290);
                        GM_ST(uint64_t, ((uint8_t *)&t293 + 32), t291);
                        GM_ST(uint64_t, ((uint8_t *)&t293 + 40), t292);
                        gm_tftb1odc739t5 t294 = gm_f_put_64ej4om(t284, t285, &t293);
                        gm_l64 = t294;
                        uint8_t t295 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                        if (!t295) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srccp2mbkjs1vn4, 58, 17);
                        gm_tcfa6l7reoosn t296;
                        GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t296 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t296 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t296 + 24, 0);
                        ((uint8_t *)&t296)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t296 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t296), 32);
                        goto gm_fdone;
                    }
                    void * t297 = gm_l0;
                    void * t298 = (void *)(((uint8_t *)t297) + 1272);
                    uint64_t t299 = gm_l16;
                    gm_f_get_hd4d6j0((void *)&gm_l65, t298, t299);
                    uint8_t t300 = (uint8_t)((((uint8_t *)&gm_l65))[48] != 0);
                    if (t300) {
                        memmove(((uint8_t *)&gm_l66), ((uint8_t *)&gm_l65), 48);
                    } else {
                        gm_tcfa6l7reoosn t301;
                        GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t301 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t301 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t301 + 24, 0);
                        ((uint8_t *)&t301)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t301 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t301), 32);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l66), 48);
                    void * t302 = gm_l0;
                    void * t303 = (void *)(((uint8_t *)t302) + 1272);
                    uint64_t t304 = gm_l16;
                    gm_tm7c53s68971m t305;
                    gm_f_remove_gjirbtf((void *)&t305, t303, t304);
                    uint64_t t306 = GM_LD(uint64_t, ((uint8_t *)&F.l22));
                    uint64_t t307 = gm_l17;
                    uint8_t t308 = (!(t306 == t307));
                    if (t308) {
                        gm_l67 = ((uint8_t)1);
                    } else {
                        uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                        uint64_t t310 = gm_l18;
                        uint8_t t311 = (!(t309 == t310));
                        gm_l67 = t311;
                    }
                    uint8_t t312 = gm_l67;
                    if (t312) {
                        void * t313 = gm_l0;
                        gm_tl874t2atnlh3 t314 = gm_l1;
                        gm_t2lqu8jsj511d t315 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&F.l22) + 16));
                        uint64_t t316 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 32));
                        uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 40));
                        gm_f_tag_site_msl07u1((void *)&gm_l68, (void *)NULL, cx, NULL, t313, t314, t315, t316, t317);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t318 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                        if (t318) {
                        } else {
                            gm_t76123gfet73g t319 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l68) + 8));
                            gm_tcfa6l7reoosn t320;
                            GM_ST(uint8_t, (uint8_t *)&t320 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t320 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t320 + 4, 0);
                            ((uint8_t *)&t320)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t320 + 8), t319);
                            memmove(gm_out, ((uint8_t *)&t320), 32);
                            goto gm_fdone;
                        }
                        gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                        gm_l23 = t321;
                        void * t322 = gm_l0;
                        gm_tl874t2atnlh3 t323 = gm_l1;
                        uint64_t t324 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                        uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                        gm_t2lqu8jsj511d t326;
                        GM_ST(uint64_t, ((uint8_t *)&t326 + 0), t324);
                        GM_ST(uint64_t, ((uint8_t *)&t326 + 8), t325);
                        uint64_t t327 = gm_l20;
                        uint64_t t328 = gm_l21;
                        gm_f_tag_site_msl07u1((void *)&gm_l69, (void *)NULL, cx, NULL, t322, t323, t326, t327, t328);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t329 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                        if (t329) {
                        } else {
                            gm_t76123gfet73g t330 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l69) + 8));
                            gm_tcfa6l7reoosn t331;
                            GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                            ((uint8_t *)&t331)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t331 + 8), t330);
                            memmove(gm_out, ((uint8_t *)&t331), 32);
                            goto gm_fdone;
                        }
                        gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                        gm_l24 = t332;
                        void * t333 = gm_l0;
                        gm_tl874t2atnlh3 t334 = gm_l1;
                        gm_b16_8 t335 = gm_l23;
                        gm_b16_8 t336 = gm_l24;
                        gm_t6j6smj7qoctu t337;
                        GM_ST(gm_b16_8, ((uint8_t *)&t337 + 0), t335);
                        GM_ST(gm_b16_8, ((uint8_t *)&t337 + 16), t336);
                        gm_f_format_unbcnc5((void *)&gm_l70, (void *)NULL, cx, NULL, &t337);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t338 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                        if (!t338) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 8, gm_srccp2mbkjs1vn4, 66, 38);
                        gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                        gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
                        gm_t76123gfet73g t341;
                        gm_f_trap_qh84l9d((void *)&t341, (void *)NULL, cx, NULL, t333, t334, t340);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t342;
                        GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                        ((uint8_t *)&t342)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t342 + 8), t341);
                        memmove(gm_out, ((uint8_t *)&t342), 32);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    {
                        {
                            {
                                uint8_t t343 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t344 = (uint8_t)(t343 == ((uint8_t)64ULL));
                                if (!t344) {
                                    goto gm_lb18;
                                }
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t346 = (uint8_t)(t345 == ((uint8_t)65ULL));
                                if (!t346) {
                                    goto gm_lb19;
                                }
                                goto gm_lb17;
                            }
gm_lb19:;
                            {
                                uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t348 = (uint8_t)(t347 == ((uint8_t)66ULL));
                                if (!t348) {
                                    goto gm_lb20;
                                }
                                goto gm_lb17;
                            }
gm_lb20:;
                            {
                                uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t350 = (uint8_t)(t349 == ((uint8_t)67ULL));
                                if (!t350) {
                                    goto gm_lb21;
                                }
                                goto gm_lb17;
                            }
gm_lb21:;
                            {
                                uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t352 = (uint8_t)(t351 == ((uint8_t)68ULL));
                                if (!t352) {
                                    goto gm_lb22;
                                }
                                goto gm_lb17;
                            }
gm_lb22:;
                            {
                                uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t354 = (uint8_t)(t353 == ((uint8_t)70ULL));
                                if (!t354) {
                                    goto gm_lb23;
                                }
                                goto gm_lb17;
                            }
gm_lb23:;
                            {
                                uint8_t t355 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t356 = (uint8_t)(t355 == ((uint8_t)71ULL));
                                if (!t356) {
                                    goto gm_lb24;
                                }
                                goto gm_lb17;
                            }
gm_lb24:;
                            goto gm_lb16;
                        }
gm_lb17:;
                        void * t357 = gm_l0;
                        void * t358 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t359 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t359)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t359, gm_srccp2mbkjs1vn4, 70, 30);
                        gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)t358) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
                        uint64_t t362 = gm_f_ptr_of_b77b4c9(t357, t361);
                        gm_l25 = t362;
                        {
                            {
                                {
                                    {
                                        void * t363 = gm_l0;
                                        void * t364 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t365 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t365)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t365, gm_srccp2mbkjs1vn4, 71, 35);
                                        void * t366 = GM_LD(void *, (((uint8_t *)t364) + (uintptr_t)((uint64_t)0ULL) * 8));
                                        gm_t88rcucg1b7uk t367 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t366) + 56));
                                        gm_f_kind_htv6093((void *)&gm_l71, t363, GM_CONV(gm_csguankko4lmhd, t367));
                                        uint8_t t368 = GM_LD(uint8_t, (((uint8_t *)&gm_l71) + 0));
                                        uint8_t t369 = (uint8_t)(t368 == ((uint8_t)8ULL));
                                        if (!t369) {
                                            goto gm_lb26;
                                        }
                                        gm_t88rcucg1b7uk t370 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l71) + 8) + 4));
                                        gm_l26 = t370;
                                        void * t371 = gm_l0;
                                        gm_t88rcucg1b7uk t372 = gm_l26;
                                        uint64_t t373 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t371, t372);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l27 = t373;
                                        void * t374 = gm_l0;
                                        void * t375 = gm_l0;
                                        gm_t88rcucg1b7uk t376 = gm_l26;
                                        uint64_t t377 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t375, t376);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t378 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t374, t377);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l28 = t378;
                                        void * t379 = gm_l0;
                                        gm_tl874t2atnlh3 t380 = gm_l1;
                                        uint64_t t381 = gm_l25;
                                        uint64_t t382 = gm_l27;
                                        gm_b16_8 t383 = gm_l28;
                                        gm_f_load_at_6u81jcc((void *)&gm_l72, (void *)NULL, cx, NULL, t379, t380, t381, t382, t383);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t384 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                        if (t384) {
                                        } else {
                                            gm_t76123gfet73g t385 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l72) + 8));
                                            gm_tcfa6l7reoosn t386;
                                            GM_ST(uint8_t, (uint8_t *)&t386 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t386 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t386 + 4, 0);
                                            ((uint8_t *)&t386)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t386 + 8), t385);
                                            memmove(gm_out, ((uint8_t *)&t386), 32);
                                            goto gm_fdone;
                                        }
                                        {
                                            {
                                                {
                                                    {
                                                        {
                                                            uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t388 = (uint8_t)(t387 == ((uint8_t)64ULL));
                                                            if (!t388) {
                                                                goto gm_lb30;
                                                            }
                                                            goto gm_lb29;
                                                        }
gm_lb30:;
                                                        {
                                                            uint8_t t389 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t390 = (uint8_t)(t389 == ((uint8_t)70ULL));
                                                            if (!t390) {
                                                                goto gm_lb31;
                                                            }
                                                            goto gm_lb29;
                                                        }
gm_lb31:;
                                                        goto gm_lb28;
                                                    }
gm_lb29:;
                                                    gm_b16_8 t391 = gm_l6;
                                                    gm_b16_8 t392 = gm_l28;
                                                    gm_b16_8 t393 = GM_LD(gm_b16_8, ((uint8_t *)&t392));
                                                    gm_f_copy_from_f7j6p05(t391, t393);
                                                    goto gm_lb27;
                                                }
gm_lb28:;
                                                {
                                                    {
                                                        {
                                                            uint8_t t394 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t395 = (uint8_t)(t394 == ((uint8_t)65ULL));
                                                            if (!t395) {
                                                                goto gm_lb34;
                                                            }
                                                            goto gm_lb33;
                                                        }
gm_lb34:;
                                                        {
                                                            uint8_t t396 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t397 = (uint8_t)(t396 == ((uint8_t)71ULL));
                                                            if (!t397) {
                                                                goto gm_lb35;
                                                            }
                                                            goto gm_lb33;
                                                        }
gm_lb35:;
                                                        goto gm_lb32;
                                                    }
gm_lb33:;
                                                    void * t398 = gm_l0;
                                                    gm_tl874t2atnlh3 t399 = gm_l1;
                                                    uint64_t t400 = gm_l25;
                                                    uint64_t t401 = gm_l27;
                                                    void * t402 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t403 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t403)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t403, gm_srccp2mbkjs1vn4, 77, 80);
                                                    gm_b16_8 t404 = GM_LD(gm_b16_8, (((uint8_t *)t402) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_b16_8 t405 = GM_LD(gm_b16_8, ((uint8_t *)&t404));
                                                    gm_f_store_at_ktabiji((void *)&gm_l73, (void *)NULL, cx, NULL, t398, t399, t400, t401, t405);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t406 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                                                    if (t406) {
                                                    } else {
                                                        gm_t76123gfet73g t407 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l73) + 8));
                                                        gm_tcfa6l7reoosn t408;
                                                        GM_ST(uint8_t, (uint8_t *)&t408 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t408 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t408 + 4, 0);
                                                        ((uint8_t *)&t408)[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t408 + 8), t407);
                                                        memmove(gm_out, ((uint8_t *)&t408), 32);
                                                        goto gm_fdone;
                                                    }
                                                    goto gm_lb27;
                                                }
gm_lb32:;
                                                {
                                                    uint8_t t409 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t410 = (uint8_t)(t409 == ((uint8_t)66ULL));
                                                    if (!t410) {
                                                        goto gm_lb36;
                                                    }
                                                    void * t411 = gm_l0;
                                                    gm_tl874t2atnlh3 t412 = gm_l1;
                                                    uint64_t t413 = gm_l25;
                                                    uint64_t t414 = gm_l27;
                                                    void * t415 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t416 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t416)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t416, gm_srccp2mbkjs1vn4, 79, 52);
                                                    gm_b16_8 t417 = GM_LD(gm_b16_8, (((uint8_t *)t415) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_b16_8 t418 = GM_LD(gm_b16_8, ((uint8_t *)&t417));
                                                    gm_f_store_at_ktabiji((void *)&gm_l74, (void *)NULL, cx, NULL, t411, t412, t413, t414, t418);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t419 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                                    if (t419) {
                                                    } else {
                                                        gm_t76123gfet73g t420 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l74) + 8));
                                                        gm_tcfa6l7reoosn t421;
                                                        GM_ST(uint8_t, (uint8_t *)&t421 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t421 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t421 + 4, 0);
                                                        ((uint8_t *)&t421)[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t421 + 8), t420);
                                                        memmove(gm_out, ((uint8_t *)&t421), 32);
                                                        goto gm_fdone;
                                                    }
                                                    gm_b16_8 t422 = gm_l6;
                                                    gm_b16_8 t423 = gm_l28;
                                                    gm_b16_8 t424 = GM_LD(gm_b16_8, ((uint8_t *)&t423));
                                                    gm_f_copy_from_f7j6p05(t422, t424);
                                                    goto gm_lb27;
                                                }
gm_lb36:;
                                            }
                                            {
                                                {
                                                    uint8_t t425 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)67ULL));
                                                    if (!t426) {
                                                        goto gm_lb37;
                                                    }
                                                    void * t427 = gm_l0;
                                                    gm_tl874t2atnlh3 t428 = gm_l1;
                                                    gm_t88rcucg1b7uk t429 = gm_l26;
                                                    gm_b16_8 t430 = gm_l28;
                                                    void * t431 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t432 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t432)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t432, gm_srccp2mbkjs1vn4, 83, 51);
                                                    gm_b16_8 t433 = GM_LD(gm_b16_8, (((uint8_t *)t431) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_f_equal_e5vf82h((void *)&gm_l75, (void *)NULL, cx, NULL, t427, t428, t429, t430, t433);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t434 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                                    if (t434) {
                                                    } else {
                                                        gm_t76123gfet73g t435 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l75) + 8));
                                                        gm_tcfa6l7reoosn t436;
                                                        GM_ST(uint8_t, (uint8_t *)&t436 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t436 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t436 + 4, 0);
                                                        ((uint8_t *)&t436)[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t436 + 8), t435);
                                                        memmove(gm_out, ((uint8_t *)&t436), 32);
                                                        goto gm_fdone;
                                                    }
                                                    uint8_t t437 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 8));
                                                    if (t437) {
                                                        void * t438 = gm_l0;
                                                        gm_tl874t2atnlh3 t439 = gm_l1;
                                                        uint64_t t440 = gm_l25;
                                                        uint64_t t441 = gm_l27;
                                                        void * t442 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                        uint64_t t443 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                        if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t443)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t443, gm_srccp2mbkjs1vn4, 84, 56);
                                                        gm_b16_8 t444 = GM_LD(gm_b16_8, (((uint8_t *)t442) + (uintptr_t)((uint64_t)2ULL) * 16));
                                                        gm_b16_8 t445 = GM_LD(gm_b16_8, ((uint8_t *)&t444));
                                                        gm_f_store_at_ktabiji((void *)&gm_l76, (void *)NULL, cx, NULL, t438, t439, t440, t441, t445);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t446 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                                        if (t446) {
                                                        } else {
                                                            gm_t76123gfet73g t447 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l76) + 8));
                                                            gm_tcfa6l7reoosn t448;
                                                            GM_ST(uint8_t, (uint8_t *)&t448 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t448 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t448 + 4, 0);
                                                            ((uint8_t *)&t448)[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t448 + 8), t447);
                                                            memmove(gm_out, ((uint8_t *)&t448), 32);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t449 = gm_l6;
                                                        gm_f_fill_abmf5g5(t449, ((uint8_t)0ULL));
                                                    } else {
                                                        void * t450 = gm_l0;
                                                        gm_t88rcucg1b7uk t451 = gm_l5;
                                                        gm_b16_8 t452 = gm_l6;
                                                        gm_b16_8 t453 = gm_l28;
                                                        gm_b16_8 t454 = GM_LD(gm_b16_8, ((uint8_t *)&t453));
                                                        gm_b0_1 t455 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t450, t451, t452, t454);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    goto gm_lb27;
                                                }
gm_lb37:;
                                                {
                                                    uint8_t t456 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t457 = (uint8_t)(t456 == ((uint8_t)68ULL));
                                                    if (!t457) {
                                                        goto gm_lb38;
                                                    }
                                                    gm_t9qkh0rsooc8d t458 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
                                                    gm_l29 = t458;
                                                    void * t459 = gm_l0;
                                                    gm_t88rcucg1b7uk t460 = gm_l26;
                                                    gm_b16_8 t461 = gm_l28;
                                                    gm_b16_8 t462 = GM_LD(gm_b16_8, ((uint8_t *)&t461));
                                                    unsigned __int128 t463 = gm_f_get_int_pg7p4gc(t459, GM_CONV(gm_csguankko4lmhd, t460), t462);
                                                    gm_l30 = t463;
                                                    void * t464 = gm_l0;
                                                    gm_t88rcucg1b7uk t465 = gm_l26;
                                                    void * t466 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t467 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t467)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t467, gm_srccp2mbkjs1vn4, 92, 40);
                                                    gm_b16_8 t468 = GM_LD(gm_b16_8, (((uint8_t *)t466) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_b16_8 t469 = GM_LD(gm_b16_8, ((uint8_t *)&t468));
                                                    unsigned __int128 t470 = gm_f_get_int_pg7p4gc(t464, GM_CONV(gm_csguankko4lmhd, t465), t469);
                                                    gm_l31 = t470;
                                                    void * t471 = gm_l0;
                                                    gm_t88rcucg1b7uk t472 = gm_l26;
                                                    gm_tg5lql6itufk7 t473 = gm_f_int_info_ulcevdv(t471, GM_CONV(gm_csguankko4lmhd, t472));
                                                    gm_l77 = t473;
                                                    uint8_t t474 = (uint8_t)((((uint8_t *)&gm_l77))[8] != 0);
                                                    if (t474) {
                                                        gm_ttovhfee8mf28 t475 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l77));
                                                        gm_l78 = t475;
                                                    } else {
                                                        gm_ttovhfee8mf28 t476;
                                                        GM_ST(uint8_t, (uint8_t *)&t476 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t476 + 2, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t476 + 0), ((uint8_t)0));
                                                        GM_ST(uint32_t, ((uint8_t *)&t476 + 4), ((uint32_t)0ULL));
                                                        gm_l78 = t476;
                                                    }
                                                    gm_ttovhfee8mf28 t477 = gm_l78;
                                                    gm_l79 = t477;
                                                    uint8_t t478 = GM_LD(uint8_t, ((uint8_t *)&gm_l79));
                                                    gm_l32 = t478;
                                                    {
                                                        {
                                                            uint8_t t479 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t480 = (uint8_t)(t479 == ((uint8_t)0ULL));
                                                            if (!t480) {
                                                                goto gm_lb40;
                                                            }
                                                            unsigned __int128 t481 = gm_l30;
                                                            unsigned __int128 t482 = gm_l31;
                                                            unsigned __int128 r483; bool o484 = __builtin_add_overflow(t481, t482, &r483);
                                                            unsigned __int128 t485 = r483;
                                                            gm_l80 = t485;
                                                            goto gm_lb39;
                                                        }
gm_lb40:;
                                                        {
                                                            uint8_t t486 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t487 = (uint8_t)(t486 == ((uint8_t)1ULL));
                                                            if (!t487) {
                                                                goto gm_lb41;
                                                            }
                                                            unsigned __int128 t488 = gm_l30;
                                                            unsigned __int128 t489 = gm_l31;
                                                            unsigned __int128 r490; bool o491 = __builtin_sub_overflow(t488, t489, &r490);
                                                            unsigned __int128 t492 = r490;
                                                            gm_l80 = t492;
                                                            goto gm_lb39;
                                                        }
gm_lb41:;
                                                        {
                                                            uint8_t t493 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t494 = (uint8_t)(t493 == ((uint8_t)2ULL));
                                                            if (!t494) {
                                                                goto gm_lb42;
                                                            }
                                                            unsigned __int128 t495 = gm_l30;
                                                            unsigned __int128 t496 = gm_l31;
                                                            unsigned __int128 t497 = (unsigned __int128)(t495 & t496);
                                                            gm_l80 = t497;
                                                            goto gm_lb39;
                                                        }
gm_lb42:;
                                                        {
                                                            uint8_t t498 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t499 = (uint8_t)(t498 == ((uint8_t)3ULL));
                                                            if (!t499) {
                                                                goto gm_lb43;
                                                            }
                                                            unsigned __int128 t500 = gm_l30;
                                                            unsigned __int128 t501 = gm_l31;
                                                            unsigned __int128 t502 = (unsigned __int128)(t500 | t501);
                                                            gm_l80 = t502;
                                                            goto gm_lb39;
                                                        }
gm_lb43:;
                                                        {
                                                            uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t504 = (uint8_t)(t503 == ((uint8_t)4ULL));
                                                            if (!t504) {
                                                                goto gm_lb44;
                                                            }
                                                            unsigned __int128 t505 = gm_l30;
                                                            unsigned __int128 t506 = gm_l31;
                                                            unsigned __int128 t507 = (unsigned __int128)(t505 ^ t506);
                                                            gm_l80 = t507;
                                                            goto gm_lb39;
                                                        }
gm_lb44:;
                                                        {
                                                            uint8_t t508 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t509 = (uint8_t)(t508 == ((uint8_t)5ULL));
                                                            if (!t509) {
                                                                goto gm_lb45;
                                                            }
                                                            uint8_t t510 = gm_l32;
                                                            if (t510) {
                                                                unsigned __int128 t511 = gm_l31;
                                                                __int128 t512;
                                                                memset(&t512, 0, sizeof t512); memcpy(&t512, ((uint8_t *)&t511), sizeof t512 < 16 ? sizeof t512 : 16);
                                                                unsigned __int128 t513 = gm_l30;
                                                                __int128 t514;
                                                                memset(&t514, 0, sizeof t514); memcpy(&t514, ((uint8_t *)&t513), sizeof t514 < 16 ? sizeof t514 : 16);
                                                                uint8_t t515 = (t512 > t514);
                                                                gm_l83 = t515;
                                                            } else {
                                                                gm_l83 = ((uint8_t)0);
                                                            }
                                                            uint8_t t516 = gm_l83;
                                                            if (t516) {
                                                                gm_l82 = ((uint8_t)1);
                                                            } else {
                                                                uint8_t t517 = gm_l32;
                                                                uint8_t t518 = (uint8_t)!t517;
                                                                if (t518) {
                                                                    unsigned __int128 t519 = gm_l31;
                                                                    unsigned __int128 t520 = gm_l30;
                                                                    uint8_t t521 = (t519 > t520);
                                                                    gm_l84 = t521;
                                                                } else {
                                                                    gm_l84 = ((uint8_t)0);
                                                                }
                                                                uint8_t t522 = gm_l84;
                                                                gm_l82 = t522;
                                                            }
                                                            uint8_t t523 = gm_l82;
                                                            if (t523) {
                                                                unsigned __int128 t524 = gm_l31;
                                                                gm_l81 = t524;
                                                            } else {
                                                                unsigned __int128 t525 = gm_l30;
                                                                gm_l81 = t525;
                                                            }
                                                            unsigned __int128 t526 = gm_l81;
                                                            gm_l80 = t526;
                                                            goto gm_lb39;
                                                        }
gm_lb45:;
                                                        {
                                                            uint8_t t527 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t528 = (uint8_t)(t527 == ((uint8_t)6ULL));
                                                            if (!t528) {
                                                                goto gm_lb46;
                                                            }
                                                            uint8_t t529 = gm_l32;
                                                            if (t529) {
                                                                unsigned __int128 t530 = gm_l31;
                                                                __int128 t531;
                                                                memset(&t531, 0, sizeof t531); memcpy(&t531, ((uint8_t *)&t530), sizeof t531 < 16 ? sizeof t531 : 16);
                                                                unsigned __int128 t532 = gm_l30;
                                                                __int128 t533;
                                                                memset(&t533, 0, sizeof t533); memcpy(&t533, ((uint8_t *)&t532), sizeof t533 < 16 ? sizeof t533 : 16);
                                                                uint8_t t534 = (t531 < t533);
                                                                gm_l87 = t534;
                                                            } else {
                                                                gm_l87 = ((uint8_t)0);
                                                            }
                                                            uint8_t t535 = gm_l87;
                                                            if (t535) {
                                                                gm_l86 = ((uint8_t)1);
                                                            } else {
                                                                uint8_t t536 = gm_l32;
                                                                uint8_t t537 = (uint8_t)!t536;
                                                                if (t537) {
                                                                    unsigned __int128 t538 = gm_l31;
                                                                    unsigned __int128 t539 = gm_l30;
                                                                    uint8_t t540 = (t538 < t539);
                                                                    gm_l88 = t540;
                                                                } else {
                                                                    gm_l88 = ((uint8_t)0);
                                                                }
                                                                uint8_t t541 = gm_l88;
                                                                gm_l86 = t541;
                                                            }
                                                            uint8_t t542 = gm_l86;
                                                            if (t542) {
                                                                unsigned __int128 t543 = gm_l31;
                                                                gm_l85 = t543;
                                                            } else {
                                                                unsigned __int128 t544 = gm_l30;
                                                                gm_l85 = t544;
                                                            }
                                                            unsigned __int128 t545 = gm_l85;
                                                            gm_l80 = t545;
                                                            goto gm_lb39;
                                                        }
gm_lb46:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 94, 31);
                                                    }
gm_lb39:;
                                                    unsigned __int128 t546 = gm_l80;
                                                    gm_l33 = t546;
                                                    void * t547 = gm_l0;
                                                    void * t548 = gm_l0;
                                                    gm_t88rcucg1b7uk t549 = gm_l26;
                                                    uint64_t t550 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t548, t549);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b16_8 t551 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t547, t550);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l34 = t551;
                                                    void * t552 = gm_l0;
                                                    gm_b16_8 t553 = gm_l34;
                                                    unsigned __int128 t554 = gm_l33;
                                                    gm_f_put_uint_cddin99(t552, t553, t554);
                                                    void * t555 = gm_l0;
                                                    gm_tl874t2atnlh3 t556 = gm_l1;
                                                    uint64_t t557 = gm_l25;
                                                    uint64_t t558 = gm_l27;
                                                    gm_b16_8 t559 = gm_l34;
                                                    gm_b16_8 t560 = GM_LD(gm_b16_8, ((uint8_t *)&t559));
                                                    gm_f_store_at_ktabiji((void *)&gm_l89, (void *)NULL, cx, NULL, t555, t556, t557, t558, t560);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t561 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                                                    if (t561) {
                                                    } else {
                                                        gm_t76123gfet73g t562 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l89) + 8));
                                                        gm_tcfa6l7reoosn t563;
                                                        GM_ST(uint8_t, (uint8_t *)&t563 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t563 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t563 + 4, 0);
                                                        ((uint8_t *)&t563)[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t563 + 8), t562);
                                                        memmove(gm_out, ((uint8_t *)&t563), 32);
                                                        goto gm_fdone;
                                                    }
                                                    gm_b16_8 t564 = gm_l6;
                                                    gm_b16_8 t565 = gm_l28;
                                                    gm_b16_8 t566 = GM_LD(gm_b16_8, ((uint8_t *)&t565));
                                                    gm_f_copy_from_f7j6p05(t564, t566);
                                                    goto gm_lb27;
                                                }
gm_lb38:;
                                            }
                                            {
                                                {
                                                    goto gm_lb27;
                                                }
gm_lb47:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 75, 13);
                                            }
                                        }
gm_lb27:;
                                    }
                                    {
                                        goto gm_lb25;
                                    }
                                }
gm_lb26:;
                            }
                            {
                                void * t567 = gm_l0;
                                gm_tl874t2atnlh3 t568 = gm_l1;
                                gm_b16_8 t569;
                                GM_ST(void *, (uint8_t *)&t569, (void *)(gm_s8pst1ovjhtbk));
                                GM_ST(uintptr_t, (uint8_t *)&t569 + 8, (uintptr_t)(70));
                                gm_t76123gfet73g t570;
                                gm_f_trap_qh84l9d((void *)&t570, (void *)NULL, cx, NULL, t567, t568, t569);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tcfa6l7reoosn t571;
                                GM_ST(uint8_t, (uint8_t *)&t571 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t571 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t571 + 4, 0);
                                ((uint8_t *)&t571)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t571 + 8), t570);
                                memmove(gm_out, ((uint8_t *)&t571), 32);
                                goto gm_fdone;
                            }
                        }
gm_lb25:;
                    }
                    {
                        goto gm_lb1;
                    }
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t572 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t573 = (uint8_t)(t572 == ((uint8_t)69ULL));
                    if (!t573) {
                        goto gm_lb48;
                    }
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    uint8_t t574 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t575 = (uint8_t)(t574 == ((uint8_t)78ULL));
                    if (!t575) {
                        goto gm_lb49;
                    }
                    {
                        {
                            void * t576 = gm_l0;
                            void * t577 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t578 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t578)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t578, gm_srccp2mbkjs1vn4, 113, 39);
                            void * t579 = GM_LD(void *, (((uint8_t *)t577) + (uintptr_t)((uint64_t)0ULL) * 8));
                            gm_t88rcucg1b7uk t580 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t579) + 56));
                            gm_f_kind_htv6093((void *)&gm_l90, t576, GM_CONV(gm_csguankko4lmhd, t580));
                            uint8_t t581 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                            uint8_t t582 = (uint8_t)(t581 == ((uint8_t)10ULL));
                            if (!t582) {
                                goto gm_lb51;
                            }
                            gm_t88rcucg1b7uk t583 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l90) + 8) + 4));
                            gm_l35 = t583;
                            void * t584 = gm_l0;
                            void * t585 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t586 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t586)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t586, gm_srccp2mbkjs1vn4, 114, 32);
                            gm_b16_8 t587 = GM_LD(gm_b16_8, (((uint8_t *)t585) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t588 = GM_LD(gm_b16_8, ((uint8_t *)&t587));
                            gm_t2lqu8jsj511d t589 = gm_f_slice_parts_1se5hu4(t584, t588);
                            gm_l36 = t589;
                            void * t590 = gm_l0;
                            void * t591 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t592 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t592)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t592, gm_srccp2mbkjs1vn4, 115, 32);
                            gm_b16_8 t593 = GM_LD(gm_b16_8, (((uint8_t *)t591) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t594 = GM_LD(gm_b16_8, ((uint8_t *)&t593));
                            gm_t2lqu8jsj511d t595 = gm_f_slice_parts_1se5hu4(t590, t594);
                            gm_l37 = t595;
                            uint64_t t596 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                            void * t597 = gm_l0;
                            gm_t88rcucg1b7uk t598 = gm_l35;
                            uint64_t t599 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t597, t598);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t r600; bool o601 = __builtin_mul_overflow(t596, t599, &r600);
                            if (o601) gm_trap("integer overflow in `*`", gm_srccp2mbkjs1vn4, 116, 18);
                            uint64_t t602 = r600;
                            gm_l38 = t602;
                            uint64_t t603 = gm_l38;
                            uint8_t t604 = ((t603 == ((uint64_t)0ULL)));
                            if (t604) {
                                gm_tcfa6l7reoosn t605;
                                GM_ST(uint8_t, (uint8_t *)&t605 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t605 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t605 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t605 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t605 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t605 + 24, 0);
                                ((uint8_t *)&t605)[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&t605 + 8), GM_ZERO(gm_b0_1));
                                memmove(gm_out, ((uint8_t *)&t605), 32);
                                goto gm_fdone;
                            }
                            void * t606 = gm_l0;
                            void * t607 = (void *)(((uint8_t *)t606) + 184);
                            uint64_t t608 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
                            uint64_t t609 = gm_l38;
                            gm_f_read_u6r4vns((void *)&gm_l91, t607, t608, t609, ((uint64_t)1ULL));
                            uint8_t t610 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                            if (t610) {
                                gm_b16_8 t611 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                                gm_l92 = t611;
                            } else {
                                memmove(((uint8_t *)&F.l39), (((uint8_t *)&gm_l91) + 8), 40);
                                void * t612 = gm_l0;
                                gm_tl874t2atnlh3 t613 = gm_l1;
                                gm_t76123gfet73g t614;
                                gm_f_mem_trap_ecogko0((void *)&t614, (void *)NULL, cx, NULL, t612, t613, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l39)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tcfa6l7reoosn t615;
                                GM_ST(uint8_t, (uint8_t *)&t615 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t615 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t615 + 4, 0);
                                ((uint8_t *)&t615)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t615 + 8), t614);
                                memmove(gm_out, ((uint8_t *)&t615), 32);
                                goto gm_fdone;
                            }
                            gm_b16_8 t616 = gm_l92;
                            gm_l40 = t616;
                            void * t617 = gm_l0;
                            uint64_t t618 = gm_l38;
                            gm_b16_8 t619 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t617, t618);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l41 = t619;
                            gm_b16_8 t620 = gm_l41;
                            gm_b16_8 t621 = gm_l40;
                            gm_b16_8 t622 = GM_LD(gm_b16_8, ((uint8_t *)&t621));
                            gm_f_copy_from_f7j6p05(t620, t622);
                            void * t623 = gm_l0;
                            void * t624 = (void *)(((uint8_t *)t623) + 184);
                            uint64_t t625 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                            gm_b16_8 t626 = gm_l41;
                            gm_b16_8 t627 = GM_LD(gm_b16_8, ((uint8_t *)&t626));
                            gm_f_write_5gub0ul((void *)&gm_l93, t624, t625, t627, ((uint64_t)1ULL));
                            uint8_t t628 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                            if (t628) {
                            } else {
                                memmove(((uint8_t *)&F.l42), (((uint8_t *)&gm_l93) + 8), 40);
                                void * t629 = gm_l0;
                                gm_tl874t2atnlh3 t630 = gm_l1;
                                gm_t76123gfet73g t631;
                                gm_f_mem_trap_ecogko0((void *)&t631, (void *)NULL, cx, NULL, t629, t630, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l42)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tcfa6l7reoosn t632;
                                GM_ST(uint8_t, (uint8_t *)&t632 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t632 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t632 + 4, 0);
                                ((uint8_t *)&t632)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t632 + 8), t631);
                                memmove(gm_out, ((uint8_t *)&t632), 32);
                                goto gm_fdone;
                            }
                            goto gm_lb50;
                        }
gm_lb51:;
                        void * t633 = gm_l0;
                        gm_tl874t2atnlh3 t634 = gm_l1;
                        gm_b16_8 t635;
                        GM_ST(void *, (uint8_t *)&t635, (void *)(gm_sp0ag7curvl4o));
                        GM_ST(uintptr_t, (uint8_t *)&t635 + 8, (uintptr_t)(41));
                        gm_t76123gfet73g t636;
                        gm_f_trap_qh84l9d((void *)&t636, (void *)NULL, cx, NULL, t633, t634, t635);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t637;
                        GM_ST(uint8_t, (uint8_t *)&t637 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t637 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t637 + 4, 0);
                        ((uint8_t *)&t637)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t637 + 8), t636);
                        memmove(gm_out, ((uint8_t *)&t637), 32);
                        goto gm_fdone;
                    }
gm_lb50:;
                    goto gm_lb1;
                }
gm_lb49:;
            }
            {
                {
                    uint8_t t638 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t639 = (uint8_t)(t638 == ((uint8_t)60ULL));
                    if (!t639) {
                        goto gm_lb52;
                    }
                    void * t640 = gm_l0;
                    void * t641 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t642 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t642)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t642, gm_srccp2mbkjs1vn4, 125, 27);
                    gm_b16_8 t643 = GM_LD(gm_b16_8, (((uint8_t *)t641) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t644 = GM_LD(gm_b16_8, ((uint8_t *)&t643));
                    uint64_t t645 = gm_f_ptr_of_b77b4c9(t640, t644);
                    gm_l43 = t645;
                    gm_l44 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            if (!((uint8_t)1)) {
                                goto gm_lb53;
                            }
                            {
                                void * t646 = gm_l0;
                                void * t647 = (void *)(((uint8_t *)t646) + 184);
                                uint64_t t648 = gm_l43;
                                uint64_t t649 = gm_l44;
                                uint64_t r650; bool o651 = __builtin_add_overflow(t648, t649, &r650);
                                if (o651) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 128, 33);
                                uint64_t t652 = r650;
                                gm_f_read_u6r4vns((void *)&gm_l94, t647, t652, ((uint64_t)1ULL), ((uint64_t)1ULL));
                                uint8_t t653 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                                if (t653) {
                                    gm_b16_8 t654 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                                    gm_l95 = t654;
                                } else {
                                    memmove(((uint8_t *)&F.l45), (((uint8_t *)&gm_l94) + 8), 40);
                                    void * t655 = gm_l0;
                                    gm_tl874t2atnlh3 t656 = gm_l1;
                                    gm_t76123gfet73g t657;
                                    gm_f_mem_trap_ecogko0((void *)&t657, (void *)NULL, cx, NULL, t655, t656, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l45)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tcfa6l7reoosn t658;
                                    GM_ST(uint8_t, (uint8_t *)&t658 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t658 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t658 + 4, 0);
                                    ((uint8_t *)&t658)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t658 + 8), t657);
                                    memmove(gm_out, ((uint8_t *)&t658), 32);
                                    goto gm_fdone;
                                }
                                gm_b16_8 t659 = gm_l95;
                                gm_l46 = t659;
                                void * t660 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                uint64_t t661 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t661)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t661, gm_srccp2mbkjs1vn4, 129, 20);
                                uint8_t t662 = GM_LD(uint8_t, (((uint8_t *)t660) + (uintptr_t)((uint64_t)0ULL) * 1));
                                uint8_t t663 = ((t662 == ((uint8_t)0ULL)));
                                if (t663) {
                                    goto gm_lb53;
                                }
                                uint64_t t664 = gm_l44;
                                uint64_t r665; bool o666 = __builtin_add_overflow(t664, ((uint64_t)1ULL), &r665);
                                if (o666) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 130, 17);
                                uint64_t t667 = r665;
                                gm_l44 = t667;
                            }
gm_lb55:;
                        }
                    }
gm_lb53:;
                    void * t668 = gm_l0;
                    gm_b16_8 t669 = gm_l6;
                    uint64_t t670 = gm_l44;
                    gm_f_put_u64_irp77r2(t668, t669, t670);
                    goto gm_lb1;
                }
gm_lb52:;
                {
                    {
                        {
                            uint8_t t671 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t672 = (uint8_t)(t671 == ((uint8_t)58ULL));
                            if (!t672) {
                                goto gm_lb58;
                            }
                            goto gm_lb57;
                        }
gm_lb58:;
                        {
                            uint8_t t673 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t674 = (uint8_t)(t673 == ((uint8_t)59ULL));
                            if (!t674) {
                                goto gm_lb59;
                            }
                            goto gm_lb57;
                        }
gm_lb59:;
                        goto gm_lb56;
                    }
gm_lb57:;
                    gm_tvdg94bk7aa22 t675 = gm_l2;
                    uint8_t t676 = GM_LD(uint8_t, (((uint8_t *)&t675) + 0));
                    uint8_t t677 = (uint8_t)((t676 == ((uint8_t)58ULL)));
                    gm_tvdg94bk7aa22 t678;
                    GM_ST(uint8_t, (uint8_t *)&t678 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t678 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t678 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t678 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t678 + 0), ((uint8_t)58ULL));
                    if (t677) {
                        gm_t88rcucg1b7uk t679 = gm_l5;
                        gm_l96 = t679;
                    } else {
                        void * t680 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t681 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t681)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t681, gm_srccp2mbkjs1vn4, 135, 55);
                        void * t682 = GM_LD(void *, (((uint8_t *)t680) + (uintptr_t)((uint64_t)0ULL) * 8));
                        gm_t88rcucg1b7uk t683 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t682) + 56));
                        gm_l96 = t683;
                    }
                    gm_t88rcucg1b7uk t684 = gm_l96;
                    gm_l47 = t684;
                    void * t685 = gm_l0;
                    gm_tvdg94bk7aa22 t686 = gm_l2;
                    uint8_t t687 = GM_LD(uint8_t, (((uint8_t *)&t686) + 0));
                    uint8_t t688 = (uint8_t)((t687 == ((uint8_t)58ULL)));
                    gm_tvdg94bk7aa22 t689;
                    GM_ST(uint8_t, (uint8_t *)&t689 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t689 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t689 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t689 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t689 + 0), ((uint8_t)58ULL));
                    if (t688) {
                        void * t690 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t691 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t691)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t691, gm_srccp2mbkjs1vn4, 136, 53);
                        gm_b16_8 t692 = GM_LD(gm_b16_8, (((uint8_t *)t690) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t693 = GM_LD(gm_b16_8, ((uint8_t *)&t692));
                        gm_l97 = t693;
                    } else {
                        void * t694 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t695 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t695)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t695, gm_srccp2mbkjs1vn4, 136, 67);
                        gm_b16_8 t696 = GM_LD(gm_b16_8, (((uint8_t *)t694) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t697 = GM_LD(gm_b16_8, ((uint8_t *)&t696));
                        gm_l97 = t697;
                    }
                    gm_b16_8 t698 = gm_l97;
                    gm_t2lqu8jsj511d t699 = gm_f_slice_parts_1se5hu4(t685, t698);
                    gm_l48 = t699;
                    {
                        {
                            void * t700 = gm_l0;
                            gm_t88rcucg1b7uk t701 = gm_l47;
                            gm_f_kind_htv6093((void *)&gm_l98, t700, GM_CONV(gm_csguankko4lmhd, t701));
                            uint8_t t702 = GM_LD(uint8_t, (((uint8_t *)&gm_l98) + 0));
                            uint8_t t703 = (uint8_t)(t702 == ((uint8_t)11ULL));
                            if (!t703) {
                                goto gm_lb61;
                            }
                            uint64_t t704 = GM_LD(uint64_t, (((uint8_t *)&gm_l98) + 8));
                            gm_l49 = t704;
                            gm_t88rcucg1b7uk t705 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l98) + 8) + 8));
                            gm_l50 = t705;
                            {
                                {
                                    uint64_t t706 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                    uint64_t t707 = gm_l49;
                                    uint8_t t708 = (t706 < t707);
                                    if (!t708) {
                                        goto gm_lb63;
                                    }
                                    void * t709 = gm_l0;
                                    uint8_t t710 = gm_f_fast_23em7sv(t709);
                                    uint8_t t711 = (uint8_t)!t710;
                                    if (!t711) {
                                        goto gm_lb63;
                                    }
                                    gm_tvdg94bk7aa22 t712 = gm_l2;
                                    uint8_t t713 = GM_LD(uint8_t, (((uint8_t *)&t712) + 0));
                                    uint8_t t714 = (uint8_t)((t713 == ((uint8_t)58ULL)));
                                    gm_tvdg94bk7aa22 t715;
                                    GM_ST(uint8_t, (uint8_t *)&t715 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t715 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t715 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t715 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t715 + 0), ((uint8_t)58ULL));
                                    if (t714) {
                                        gm_b16_8 t716;
                                        GM_ST(void *, (uint8_t *)&t716, (void *)(gm_sqfrv3g5v9m2n));
                                        GM_ST(uintptr_t, (uint8_t *)&t716 + 8, (uintptr_t)(4));
                                        gm_l99 = t716;
                                    } else {
                                        gm_b16_8 t717;
                                        GM_ST(void *, (uint8_t *)&t717, (void *)(gm_s0ugop4l95o1j));
                                        GM_ST(uintptr_t, (uint8_t *)&t717 + 8, (uintptr_t)(5));
                                        gm_l99 = t717;
                                    }
                                    gm_b16_8 t718 = gm_l99;
                                    gm_l51 = t718;
                                    void * t719 = gm_l0;
                                    gm_tl874t2atnlh3 t720 = gm_l1;
                                    gm_b16_8 t721 = gm_l51;
                                    uint64_t t722 = gm_l49;
                                    uint64_t t723 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                    gm_tgbgti82mf879 t724;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t724 + 0), t721);
                                    GM_ST(uint64_t, ((uint8_t *)&t724 + 16), t722);
                                    GM_ST(uint64_t, ((uint8_t *)&t724 + 24), t723);
                                    gm_f_format_37g0gks((void *)&gm_l100, (void *)NULL, cx, NULL, &t724);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t725 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                                    if (!t725) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 8, gm_srccp2mbkjs1vn4, 140, 38);
                                    gm_b16_8 t726 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l100) + 8));
                                    gm_b16_8 t727 = GM_LD(gm_b16_8, ((uint8_t *)&t726));
                                    gm_t76123gfet73g t728;
                                    gm_f_trap_qh84l9d((void *)&t728, (void *)NULL, cx, NULL, t719, t720, t727);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tcfa6l7reoosn t729;
                                    GM_ST(uint8_t, (uint8_t *)&t729 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t729 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t729 + 4, 0);
                                    ((uint8_t *)&t729)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t729 + 8), t728);
                                    memmove(gm_out, ((uint8_t *)&t729), 32);
                                    goto gm_fdone;
                                }
gm_lb63:;
                            }
gm_lb62:;
                            void * t730 = gm_l0;
                            gm_t88rcucg1b7uk t731 = gm_l50;
                            uint64_t t732 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t730, t731);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l52 = t732;
                            gm_tvdg94bk7aa22 t733 = gm_l2;
                            uint8_t t734 = GM_LD(uint8_t, (((uint8_t *)&t733) + 0));
                            uint8_t t735 = (uint8_t)((t734 == ((uint8_t)58ULL)));
                            gm_tvdg94bk7aa22 t736;
                            GM_ST(uint8_t, (uint8_t *)&t736 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t736 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t736 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t736 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t736 + 0), ((uint8_t)58ULL));
                            if (t735) {
                                void * t737 = gm_l0;
                                gm_tl874t2atnlh3 t738 = gm_l1;
                                uint64_t t739 = GM_LD(uint64_t, ((uint8_t *)&gm_l48));
                                uint64_t t740 = gm_l52;
                                gm_b16_8 t741 = gm_l6;
                                gm_f_load_at_6u81jcc((void *)&gm_l101, (void *)NULL, cx, NULL, t737, t738, t739, t740, t741);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t742 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                                if (t742) {
                                } else {
                                    gm_t76123gfet73g t743 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l101) + 8));
                                    gm_tcfa6l7reoosn t744;
                                    GM_ST(uint8_t, (uint8_t *)&t744 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t744 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t744 + 4, 0);
                                    ((uint8_t *)&t744)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t744 + 8), t743);
                                    memmove(gm_out, ((uint8_t *)&t744), 32);
                                    goto gm_fdone;
                                }
                            } else {
                                void * t745 = gm_l0;
                                gm_tl874t2atnlh3 t746 = gm_l1;
                                uint64_t t747 = GM_LD(uint64_t, ((uint8_t *)&gm_l48));
                                uint64_t t748 = gm_l52;
                                void * t749 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t750 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t750)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t750, gm_srccp2mbkjs1vn4, 143, 112);
                                gm_b16_8 t751 = GM_LD(gm_b16_8, (((uint8_t *)t749) + (uintptr_t)((uint64_t)0ULL) * 16));
                                gm_b16_8 t752 = GM_LD(gm_b16_8, ((uint8_t *)&t751));
                                gm_f_store_at_ktabiji((void *)&gm_l102, (void *)NULL, cx, NULL, t745, t746, t747, t748, t752);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t753 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                                if (t753) {
                                } else {
                                    gm_t76123gfet73g t754 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l102) + 8));
                                    gm_tcfa6l7reoosn t755;
                                    GM_ST(uint8_t, (uint8_t *)&t755 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t755 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t755 + 4, 0);
                                    ((uint8_t *)&t755)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t755 + 8), t754);
                                    memmove(gm_out, ((uint8_t *)&t755), 32);
                                    goto gm_fdone;
                                }
                            }
                            goto gm_lb60;
                        }
gm_lb61:;
                        void * t756 = gm_l0;
                        gm_tl874t2atnlh3 t757 = gm_l1;
                        gm_b16_8 t758;
                        GM_ST(void *, (uint8_t *)&t758, (void *)(gm_s2qp8mqi0c2le));
                        GM_ST(uintptr_t, (uint8_t *)&t758 + 8, (uintptr_t)(47));
                        gm_t76123gfet73g t759;
                        gm_f_trap_qh84l9d((void *)&t759, (void *)NULL, cx, NULL, t756, t757, t758);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t760;
                        GM_ST(uint8_t, (uint8_t *)&t760 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t760 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t760 + 4, 0);
                        ((uint8_t *)&t760)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t760 + 8), t759);
                        memmove(gm_out, ((uint8_t *)&t760), 32);
                        goto gm_fdone;
                    }
gm_lb60:;
                    goto gm_lb1;
                }
gm_lb56:;
            }
            {
                {
                    void * t761 = gm_l0;
                    gm_tl874t2atnlh3 t762 = gm_l1;
                    gm_b16_8 t763;
                    GM_ST(void *, (uint8_t *)&t763, (void *)(gm_s1gjt02j7um1i));
                    GM_ST(uintptr_t, (uint8_t *)&t763 + 8, (uintptr_t)(36));
                    gm_t76123gfet73g t764;
                    gm_f_trap_qh84l9d((void *)&t764, (void *)NULL, cx, NULL, t761, t762, t763);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t765;
                    GM_ST(uint8_t, (uint8_t *)&t765 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t765 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t765 + 4, 0);
                    ((uint8_t *)&t765)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t765 + 8), t764);
                    memmove(gm_out, ((uint8_t *)&t765), 32);
                    goto gm_fdone;
                }
gm_lb64:;
                gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 10, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tcfa6l7reoosn t766;
        GM_ST(uint8_t, (uint8_t *)&t766 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t766 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t766 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t766 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t766 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t766 + 24, 0);
        ((uint8_t *)&t766)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t766 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t766), 32);
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
inline __attribute__((aligned(4))) void gm_f_tag_site_msl07u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t2lqu8jsj511d a2, uint64_t a3, uint64_t a4) {
    struct gm_fr_tag_site_msl07u1 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t2lqu8jsj511d gm_l2; /* file */
    uint64_t gm_l3; /* line */
    uint64_t gm_l4; /* column */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 499u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_b16_8 gm_l7;
    gm_t934k1j7u70ct gm_l8;
    gm_b16_8 gm_l9;
    gm_tabb710d9mq2e gm_l10;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(0));
        gm_l7 = t4;
    } else {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_f_read_u6r4vns((void *)&gm_l8, t6, t7, t8, ((uint64_t)1ULL));
        uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (t9) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_l9 = t10;
        } else {
            memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l8) + 8), 40);
            void * t11 = gm_l0;
            gm_tl874t2atnlh3 t12 = gm_l1;
            gm_t76123gfet73g t13;
            gm_f_mem_trap_ecogko0((void *)&t13, (void *)NULL, cx, NULL, t11, t12, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), t13);
            memmove(gm_out, ((uint8_t *)&t14), 32);
            goto gm_fdone;
        }
        gm_b16_8 t15 = gm_l9;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_l7 = t16;
    }
    gm_b16_8 t17 = gm_l7;
    gm_l6 = t17;
    gm_b16_8 t18 = gm_l6;
    uint64_t t19 = gm_l3;
    uint64_t t20 = gm_l4;
    gm_tgbgti82mf879 t21;
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t18);
    GM_ST(uint64_t, ((uint8_t *)&t21 + 16), t19);
    GM_ST(uint64_t, ((uint8_t *)&t21 + 24), t20);
    gm_f_format_bv13gdl((void *)&gm_l10, (void *)NULL, cx, NULL, &t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srccp2mbkjs1vn4, 152, 12);
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
    gm_td0ept12e7q8b t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
    ((uint8_t *)&t25)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 8), t24);
    memmove(gm_out, ((uint8_t *)&t25), 32);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
